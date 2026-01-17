#!/usr/bin/env python3
"""
FC PICO ROM APU パッチスクリプト (FC_COM_BUF 方式)

【概要】
FC ROM にパッチを当てて、Pico から FC_COM_BUF (ゼロページ $40-$4F) に
送信された APU データを読み取り、NES APU レジスタに書き込む機能を追加する。
これにより WASM-4 ゲームや NSF プレイヤーのサウンドを NES APU で再生可能になる。

【背景: なぜ FC_COM_BUF を使うのか】
FC PICO では、Pico から FC へのデータ転送に PPU データストリームを使用している。
毎フレーム、Pico は VRAM データの末尾に 16 バイトのコマンドバッファ (FC_COM_BUF) を
付加して送信する。FC 側ではこのバッファをゼロページ $40-$4F にマッピングし、
毎フレーム読み取って処理する。

【APU コマンドプロトコル】
通常コマンド（パレット更新など）と APU コマンドを区別するため、
マジックバイト ($40) と検証バイト ($41) の 2 バイトで判定する。

■ Full APU Update (0xAx) - 全チャンネル一括更新
  $40 = 0xA0 | writeMask
        上位ニブル 0xA: Full APU コマンド識別子
        下位ニブル: 位相リセットレジスタの書き込みマスク
          bit 0: $4003 (Pulse1 Freq Hi/Length) を書き込む
          bit 1: $4007 (Pulse2 Freq Hi/Length) を書き込む
          bit 2: $400B (Triangle Freq Hi/Length) を書き込む
          bit 3: $400F (Noise Length) を書き込む

  $41 = 0x40 | ($400C & 0x3F)
        上位2ビット = 01: APU コマンド検証用
        下位6ビット = Noise Volume ($400C の値)

        【なぜ $400C を $41 に配置するのか】
        NES APU の $400C レジスタは上位2ビットが常に 0 である（NES 仕様）。
        そのため、0x40 (01xxxxxx) を OR しても元の値と衝突しない。
        通常コマンドでは $41 = 0xFC (PF_MAGIC_NO = 11111100) なので、
        上位2ビットが 01 の APU コマンドと確実に区別できる。

  $42-$4F = APU レジスタ値（固定順序）

  判定条件: ($40 & 0xF0) == 0xA0 && ($41 & 0xC0) == 0x40

■ Per-Channel Update (0xBx) - 単一チャンネル更新（将来の拡張用）
  $40 = 0xB0 | channel (0=Pulse1, 1=Pulse2, 2=Triangle, 3=Noise)
  $41 = 0x50 | flags
  判定条件: ($40 & 0xF0) == 0xB0 && ($41 & 0xF0) == 0x50

■ Quick Silence (0xC0) - 全チャンネル即時消音
  $40 = 0xC0
  $41 = 0x60
  判定条件: $40 == 0xC0 && $41 == 0x60

【位相リセット問題と解決策】
NES APU の $4003, $4007, $400B, $400F レジスタに書き込むと、
対応するチャンネルの位相がリセットされる。これを毎フレーム行うと
「プチプチ」というクリックノイズが発生する。

解決策:
1. Pico 側の queueApuWrite() で、これらのレジスタへの書き込み時に
   「値が前回と変化したか」をチェック
2. 変化した場合のみ writeMask の対応ビットをセット
3. FC 側は writeMask に従って条件付き書き込みを行う

これにより、FC 側での前回値トラッキングが不要になり、コードが簡潔になった。

【パッチの仕組み】
1. Reset ベクタ ($FFFC-$FFFD) を新しいハンドラにリダイレクト
   - NES APU を安全な状態に初期化
   - 元の Reset ハンドラにジャンプ

2. JSR $9536 (毎フレーム呼ばれる PPU 転送ルーチン) をフック
   - 元のルーチンを呼び出した後、FC_COM_BUF をチェック
   - APU コマンドであれば対応する APU レジスタに書き込み

【APU 対応 ROM の通知】
古い FC ROM では FC_COM_BUF ($40-$4F) がワーク RAM として使用されている
可能性があり、APU データを書き込むとクラッシュする恐れがある。
そのため、パッチ済み ROM は起動時に Pico へ「APU 対応」を通知する。
Pico は通知を受け取るまで APU コマンドを送信しない。
"""

import sys
import os


def rom_to_file(addr):
    """ROM アドレス ($8000-$FFFF) をファイルオフセットに変換

    FC PICO の ROM は 32KB (NROM-256 相当)。
    $8000-$FFFF がファイルの先頭からマッピングされる。
    iNES ヘッダー (16 バイト) を考慮してオフセットを計算。
    """
    return (addr - 0x8000) + 16  # +16 for iNES header


# =============================================================================
# パッチ設定
# =============================================================================

# パッチコードを配置する ROM アドレス
# $9EC8-$9FFF は元の ROM で未使用の空き領域
APU_CODE_ADDR = 0x9EC8

# フックする JSR 命令のアドレス
# $EDD9: JSR $9536 (PPU データ転送ルーチン呼び出し)
# このルーチンは毎フレーム呼ばれるため、ここをフックして APU 処理を挿入
JSR_HOOK_ADDR = 0xEDD9

# フック対象の元の JSR ターゲット
ORIGINAL_JSR_TARGET = 0x9536

# 元の Reset ハンドラアドレス
# パッチ後は新しい Reset ハンドラから元のハンドラにジャンプする
ORIGINAL_RESET = 0xF000

# FC_COM_BUF のゼロページベースアドレス
# Pico からのコマンドバッファは $40-$4F (16 バイト) にマッピングされる
FC_COM_BUF_ZP = 0x40

# =============================================================================
# APU コマンド定数
# =============================================================================

# マジックバイト ($40 の上位ニブルでコマンドタイプを識別)
APU_MAGIC_FULL = 0xA0       # Full APU Update: 0xA0 | writeMask
APU_MAGIC_PERCHAN = 0xB0    # Per-Channel Update: 0xB0 | channel (将来用)
APU_MAGIC_SILENCE = 0xC0    # Quick Silence: 固定値

# 検証バイト ($41 の上位ビットで APU コマンドを検証)
# 通常コマンドの $41 = 0xFC (11xxxxxx) と重複しない値を使用
APU_CHECK_FULL = 0x40       # Full: 上位2ビット = 01 → 0x40-0x7F の範囲
APU_CHECK_PERCHAN = 0x50    # PerCh: 上位4ビット = 0101 → 0x50-0x5F の範囲
APU_CHECK_SILENCE = 0x60    # Silence: 固定値 0x60

# APU 対応通知コマンド
# この値を $2007 (PPUDATA) に書き込んで Pico に通知
# Pico 側では SM_RECV で検出し、m_apuSupported フラグをセット
APU_NOTIFY_CMD = 0x05

# APU 対応通知フラグのゼロページアドレス
# 初回フレームで通知を送信後、このフラグをインクリメントして2回目以降をスキップ
# (毎フレーム $2007 に書き込むと VRAM が壊れるため)
APU_NOTIFY_FLAG_ZP = 0x3F


def create_apu_code():
    """APU 制御用の 6502 マシンコードを生成

    生成されるコードの構成:
    1. Reset ハンドラ: APU 初期化 → 元の Reset へジャンプ
    2. JSR フック: 元のルーチン呼び出し → APU コマンド処理 → 復帰

    Returns:
        tuple: (バイト列, new_reset_offset, jsr_hook_offset)
               new_reset_offset: 新 Reset ハンドラのオフセット
               jsr_hook_offset: JSR フックハンドラのオフセット
    """
    code = []

    # =========================================================================
    # Reset ハンドラ: NES APU を安全な状態に初期化
    # =========================================================================
    #
    # 【目的】
    # 起動時に APU を無音状態に設定し、ノイズを防ぐ。
    # APU レジスタは電源投入時に不定値が入っている可能性があるため、
    # 明示的に初期化する必要がある。
    #
    new_reset_offset = len(code)

    # --- $4015 = 0x00: 全チャンネル無効化 ---
    # まず全チャンネルを無効にしてから各レジスタを設定
    # (有効状態で不定値のレジスタを読むとノイズが出る可能性があるため)
    code += bytes([0xA9, 0x00])         # LDA #$00
    code += bytes([0x8D, 0x15, 0x40])   # STA $4015

    # --- 各チャンネルの音量を 0 に設定 ---
    # $4000/$4004/$400C: 0x30 = 定数音量モード (bit 4) + 音量 0 (bit 0-3)
    # bit 5 (Length Counter halt) もセットされるため、Length Counter が止まる
    code += bytes([0xA9, 0x30])         # LDA #$30
    code += bytes([0x8D, 0x00, 0x40])   # STA $4000 (Pulse1)
    code += bytes([0x8D, 0x04, 0x40])   # STA $4004 (Pulse2)
    code += bytes([0x8D, 0x0C, 0x40])   # STA $400C (Noise)

    # $4008: Triangle のリニアカウンタを 0 に設定
    # bit 7 (control flag) = 0, bit 0-6 (counter reload) = 0
    # これにより Triangle チャンネルは無音になる
    code += bytes([0xA9, 0x00])         # LDA #$00
    code += bytes([0x8D, 0x08, 0x40])   # STA $4008

    # --- $4017 = 0x40: Frame Counter 設定 ---
    # bit 7 = 0: 4 ステップシーケンスモード
    # bit 6 = 1: Frame IRQ 無効化
    # IRQ を無効化しないと、意図しない割り込みが発生する可能性がある
    code += bytes([0xA9, 0x40])         # LDA #$40
    code += bytes([0x8D, 0x17, 0x40])   # STA $4017

    # --- $4010 = 0x00: DMC チャンネル無効化 ---
    # DMC (Delta Modulation Channel) は使用しないので無効化
    code += bytes([0xA9, 0x00])         # LDA #$00
    code += bytes([0x8D, 0x10, 0x40])   # STA $4010

    # --- $4015 = 0x0F: 4 チャンネル有効化 ---
    # bit 0-3 = 1: Pulse1, Pulse2, Triangle, Noise を有効化
    # bit 4 = 0: DMC は無効のまま
    # 各チャンネルの音量は既に 0 に設定済みなので、有効化しても無音
    code += bytes([0xA9, 0x0F])         # LDA #$0F
    code += bytes([0x8D, 0x15, 0x40])   # STA $4015

    # --- 元の Reset ハンドラにジャンプ ---
    # 初期化完了後、通常のゲーム起動処理を続行
    code += bytes([0x4C, ORIGINAL_RESET & 0xFF,
                   (ORIGINAL_RESET >> 8) & 0xFF])  # JMP $F000

    # =========================================================================
    # JSR フック: APU コマンド処理
    # =========================================================================
    #
    # 【フックの仕組み】
    # 元のコード: JSR $9536 (PPU データ転送)
    # パッチ後:   JSR (このフック) → JSR $9536 → APU 処理 → RTS
    #
    # 毎フレーム呼ばれる PPU 転送ルーチンをフックすることで、
    # フレームごとに APU コマンドを処理できる。
    #
    jsr_hook_offset = len(code)

    # --- 元の PPU データ転送ルーチンを呼び出す ---
    # これにより、Pico から送信された VRAM データが FC_COM_BUF に書き込まれる
    code += bytes([0x20, ORIGINAL_JSR_TARGET & 0xFF,
                   (ORIGINAL_JSR_TARGET >> 8) & 0xFF])  # JSR $9536

    # --- レジスタを保存 ---
    # APU 処理で A, X, Y を使用するため、呼び出し元の値を保存
    code += bytes([0x48])                    # PHA (A をスタックに保存)
    code += bytes([0x8A])                    # TXA
    code += bytes([0x48])                    # PHA (X をスタックに保存)
    code += bytes([0x98])                    # TYA
    code += bytes([0x48])                    # PHA (Y をスタックに保存)

    # =========================================================================
    # APU 対応通知 (初回フレームのみ)
    # =========================================================================
    #
    # 【目的】
    # この ROM が APU 対応であることを Pico に通知する。
    # Pico は通知を受け取るまで APU コマンドを送信しない。
    #
    # 【実装】
    # $3F をフラグとして使用。初期値 0 なら未送信。
    # $2007 (PPUDATA) への書き込みは Pico の SM_RECV で検出される。
    #
    # 【注意】
    # $2007 への書き込みは実際の VRAM を変更するため、
    # 毎フレーム行うと画面が壊れる。そのため初回のみ送信する。
    # 1 バイトだけ VRAM が変更されるが、実用上問題なし。
    #
    code += bytes([0xA5, APU_NOTIFY_FLAG_ZP])  # LDA $3F (フラグをチェック)
    code += bytes([0xD0])                       # BNE skip_notify (非0ならスキップ)
    bne_skip_notify = len(code)
    code += bytes([0x00])                       # (分岐オフセット、後で設定)

    # フラグが 0 の場合: 通知を送信
    code += bytes([0xA9, APU_NOTIFY_CMD])       # LDA #$05
    code += bytes([0x8D, 0x07, 0x20])           # STA $2007 (Pico に通知)
    code += bytes([0xE6, APU_NOTIFY_FLAG_ZP])   # INC $3F (フラグを 1 に設定)

    # skip_notify: 通知スキップ時の合流点
    skip_notify_offset = len(code)
    code[bne_skip_notify] = skip_notify_offset - bne_skip_notify - 1

    # =========================================================================
    # APU コマンド判定
    # =========================================================================
    #
    # $40 (マジックバイト) と $41 (検証バイト) を使って
    # 3 種類の APU コマンドを判定する。
    #
    # 判定順序:
    # 1. Quick Silence (0xC0): 完全一致チェック
    # 2. Full APU Update (0xAx): 上位ニブル + 検証バイト
    # 3. Per-Channel (0xBx): 上位ニブル + 検証バイト (将来用)
    # 4. いずれでもなければスキップ
    #

    # $40 を読み込み、後で使うために X レジスタに保存
    # X には writeMask も含まれているため、後の条件分岐で使用
    code += bytes([0xA5, FC_COM_BUF_ZP])     # LDA $40
    code += bytes([0xAA])                    # TAX (マジックバイトを X に保存)

    # =========================================================================
    # Quick Silence (0xC0) チェック
    # =========================================================================
    #
    # 【目的】
    # 全チャンネルを即座に消音する。
    # 曲の終了時やシーン切り替え時に使用。
    #
    # 【判定】
    # $40 == 0xC0 && $41 == 0x60
    #
    code += bytes([0xC9, APU_MAGIC_SILENCE]) # CMP #$C0
    code += bytes([0xD0])                    # BNE check_full (不一致なら次へ)
    bne_check_full = len(code)
    code += bytes([0x00])                    # (分岐オフセット、後で設定)

    # $40 == 0xC0 の場合: $41 == 0x60 をチェック
    code += bytes([0xA5, FC_COM_BUF_ZP + 1]) # LDA $41
    code += bytes([0xC9, APU_CHECK_SILENCE]) # CMP #$60
    code += bytes([0xD0, 0x03])              # BNE +3 (不一致なら JMP skip_apu へ)
    code += bytes([0x4C])                    # JMP silence_handler
    jmp_silence_lo = len(code)
    code += bytes([0x00, 0x00])              # (アドレス、後で設定)
    code += bytes([0x4C])                    # JMP skip_apu (検証失敗)
    jmp_skip_silence_lo = len(code)
    code += bytes([0x00, 0x00])              # (アドレス、後で設定)

    # =========================================================================
    # Full APU Update (0xAx) チェック
    # =========================================================================
    #
    # 【目的】
    # 全 4 チャンネルの APU レジスタを一括更新する。
    # 通常の音声再生で毎フレーム使用。
    #
    # 【判定】
    # ($40 & 0xF0) == 0xA0 && ($41 & 0xC0) == 0x40
    #
    # 【writeMask の仕組み】
    # $40 の下位 4 ビットは writeMask として機能する。
    # bit が 1 のレジスタのみ実際に APU に書き込む。
    # これにより位相リセットによるクリックノイズを防ぐ。
    #
    check_full_offset = len(code)
    code[bne_check_full] = check_full_offset - bne_check_full - 1

    # $40 の上位ニブルが 0xA かチェック
    code += bytes([0x8A])                    # TXA (マジックバイトを A に復元)
    code += bytes([0x29, 0xF0])              # AND #$F0 (上位ニブルを取得)
    code += bytes([0xC9, APU_MAGIC_FULL])    # CMP #$A0
    code += bytes([0xD0])                    # BNE check_perchan (不一致なら次へ)
    bne_check_perchan = len(code)
    code += bytes([0x00])                    # (分岐オフセット、後で設定)

    # ($40 & 0xF0) == 0xA0 の場合: ($41 & 0xC0) == 0x40 をチェック
    code += bytes([0xA5, FC_COM_BUF_ZP + 1]) # LDA $41
    code += bytes([0x29, 0xC0])              # AND #$C0 (上位2ビットを取得)
    code += bytes([0xC9, APU_CHECK_FULL])    # CMP #$40
    code += bytes([0xF0, 0x03])              # BEQ +3 (一致なら JMP を飛び越える)
    code += bytes([0x4C])                    # JMP skip_apu (検証失敗)
    jmp_skip_full_lo = len(code)
    code += bytes([0x00, 0x00])              # (アドレス、後で設定)

    # 検証成功: Full APU 処理へジャンプ
    code += bytes([0x4C])                    # JMP full_apu_handler
    jmp_full_lo = len(code)
    code += bytes([0x00, 0x00])              # (アドレス、後で設定)

    # =========================================================================
    # Per-Channel Update (0xBx) チェック - 将来の拡張用
    # =========================================================================
    #
    # 現在は未実装。Full APU Update で十分な性能が得られるため、
    # 単一チャンネル更新は将来の最適化用に予約。
    #
    check_perchan_offset = len(code)
    code[bne_check_perchan] = check_perchan_offset - bne_check_perchan - 1

    # Per-Channel は未実装なので skip_apu へジャンプ
    code += bytes([0x4C])                    # JMP skip_apu
    jmp_skip_perchan_lo = len(code)
    code += bytes([0x00, 0x00])              # (アドレス、後で設定)

    # =========================================================================
    # Silence Handler: 全チャンネル消音
    # =========================================================================
    #
    # $4015 = 0x00 で全チャンネルを無効化し、即座に消音する。
    #
    silence_handler_offset = len(code)
    silence_handler_addr = APU_CODE_ADDR + silence_handler_offset
    code[jmp_silence_lo] = silence_handler_addr & 0xFF
    code[jmp_silence_lo + 1] = (silence_handler_addr >> 8) & 0xFF

    code += bytes([0xA9, 0x00])              # LDA #$00
    code += bytes([0x8D, 0x15, 0x40])        # STA $4015 (全チャンネル無効化)
    code += bytes([0x85, FC_COM_BUF_ZP])     # STA $40 (マジックバイトをクリア)
    code += bytes([0x4C])                    # JMP skip_apu
    jmp_skip_from_silence_lo = len(code)
    code += bytes([0x00, 0x00])              # (アドレス、後で設定)

    # =========================================================================
    # Full APU Update Handler
    # =========================================================================
    #
    # FC_COM_BUF から APU レジスタ値を読み取り、NES APU に書き込む。
    #
    # 【レジスタ配置】(FC_COM_BUF $40-$4F)
    # $40: マジック (0xA0 | writeMask)
    # $41: 検証 (0x40) | $400C 下位6ビット (Noise Volume)
    # $42: $400E (Noise Mode/Period)
    # $43: $400F (Noise Length) - writeMask bit3 で制御
    # $44: $4000 (Pulse1 Duty/Volume)
    # $45: $4001 (Pulse1 Sweep)
    # $46: $4002 (Pulse1 Freq Lo)
    # $47: $4003 (Pulse1 Freq Hi) - writeMask bit0 で制御
    # $48: $4004 (Pulse2 Duty/Volume)
    # $49: $4005 (Pulse2 Sweep)
    # $4A: $4006 (Pulse2 Freq Lo)
    # $4B: $4007 (Pulse2 Freq Hi) - writeMask bit1 で制御
    # $4C: $4008 (Triangle Linear Counter)
    # $4D: $400A (Triangle Freq Lo)
    # $4E: $400B (Triangle Freq Hi) - writeMask bit2 で制御
    # $4F: $4015 (APU Status/Enable)
    #
    # 【writeMask による条件付き書き込み】
    # $4003, $4007, $400B, $400F への書き込みは位相リセットを引き起こす。
    # これらは Pico 側で値が変化した場合のみ writeMask にフラグがセットされ、
    # FC 側はフラグに従って書き込むかどうかを決定する。
    #
    full_apu_handler_offset = len(code)
    full_apu_handler_addr = APU_CODE_ADDR + full_apu_handler_offset
    code[jmp_full_lo] = full_apu_handler_addr & 0xFF
    code[jmp_full_lo + 1] = (full_apu_handler_addr >> 8) & 0xFF

    # この時点で X レジスタにマジックバイト (0xA0 | writeMask) が保存されている

    # --- Noise チャンネル ($400C-$400F) ---

    # $41 → $400C (Noise Volume)
    # 下位6ビットのみ使用 (上位2ビットは検証用)
    code += bytes([0xA5, FC_COM_BUF_ZP + 1]) # LDA $41
    code += bytes([0x29, 0x3F])              # AND #$3F (検証ビットを除去)
    code += bytes([0x8D, 0x0C, 0x40])        # STA $400C

    # $42 → $400E (Noise Mode/Period)
    code += bytes([0xA5, FC_COM_BUF_ZP + 2]) # LDA $42
    code += bytes([0x8D, 0x0E, 0x40])        # STA $400E

    # $43 → $400F (Noise Length) - writeMask bit3 で制御
    # 【位相リセット回避】
    # $400F への書き込みは Noise チャンネルの Length Counter をリロードする。
    # 値が変化していない場合は書き込みをスキップしてクリックノイズを防ぐ。
    code += bytes([0x8A])                    # TXA (writeMask を A に取得)
    code += bytes([0x29, 0x08])              # AND #$08 (bit3 をチェック)
    code += bytes([0xF0, 0x05])              # BEQ +5 (bit3 = 0 ならスキップ)
    code += bytes([0xA5, FC_COM_BUF_ZP + 3]) # LDA $43
    code += bytes([0x8D, 0x0F, 0x40])        # STA $400F

    # --- Pulse1 チャンネル ($4000-$4003) ---

    # $44 → $4000 (Pulse1 Duty/Volume)
    code += bytes([0xA5, FC_COM_BUF_ZP + 4]) # LDA $44
    code += bytes([0x8D, 0x00, 0x40])        # STA $4000

    # $45 → $4001 (Pulse1 Sweep)
    code += bytes([0xA5, FC_COM_BUF_ZP + 5]) # LDA $45
    code += bytes([0x8D, 0x01, 0x40])        # STA $4001

    # $46 → $4002 (Pulse1 Freq Lo)
    code += bytes([0xA5, FC_COM_BUF_ZP + 6]) # LDA $46
    code += bytes([0x8D, 0x02, 0x40])        # STA $4002

    # $47 → $4003 (Pulse1 Freq Hi) - writeMask bit0 で制御
    # 【位相リセット回避】
    # $4003 への書き込みは Pulse1 の位相をリセットする。
    # 同じ周波数で継続して発音する場合、書き込みをスキップしてクリックノイズを防ぐ。
    code += bytes([0x8A])                    # TXA (writeMask を A に取得)
    code += bytes([0x29, 0x01])              # AND #$01 (bit0 をチェック)
    code += bytes([0xF0, 0x05])              # BEQ +5 (bit0 = 0 ならスキップ)
    code += bytes([0xA5, FC_COM_BUF_ZP + 7]) # LDA $47
    code += bytes([0x8D, 0x03, 0x40])        # STA $4003

    # --- Pulse2 チャンネル ($4004-$4007) ---

    # $48 → $4004 (Pulse2 Duty/Volume)
    code += bytes([0xA5, FC_COM_BUF_ZP + 8]) # LDA $48
    code += bytes([0x8D, 0x04, 0x40])        # STA $4004

    # $49 → $4005 (Pulse2 Sweep)
    code += bytes([0xA5, FC_COM_BUF_ZP + 9]) # LDA $49
    code += bytes([0x8D, 0x05, 0x40])        # STA $4005

    # $4A → $4006 (Pulse2 Freq Lo)
    code += bytes([0xA5, FC_COM_BUF_ZP + 10]) # LDA $4A
    code += bytes([0x8D, 0x06, 0x40])         # STA $4006

    # $4B → $4007 (Pulse2 Freq Hi) - writeMask bit1 で制御
    code += bytes([0x8A])                     # TXA (writeMask を A に取得)
    code += bytes([0x29, 0x02])               # AND #$02 (bit1 をチェック)
    code += bytes([0xF0, 0x05])               # BEQ +5 (bit1 = 0 ならスキップ)
    code += bytes([0xA5, FC_COM_BUF_ZP + 11]) # LDA $4B
    code += bytes([0x8D, 0x07, 0x40])         # STA $4007

    # --- Triangle チャンネル ($4008-$400B) ---

    # $4C → $4008 (Triangle Linear Counter)
    code += bytes([0xA5, FC_COM_BUF_ZP + 12]) # LDA $4C
    code += bytes([0x8D, 0x08, 0x40])         # STA $4008

    # $4D → $400A (Triangle Freq Lo)
    code += bytes([0xA5, FC_COM_BUF_ZP + 13]) # LDA $4D
    code += bytes([0x8D, 0x0A, 0x40])         # STA $400A

    # $4E → $400B (Triangle Freq Hi) - writeMask bit2 で制御
    # 【位相リセット回避】
    # Triangle チャンネルは位相リセットの影響を受けにくいが、
    # Length Counter のリロードを防ぐために条件付き書き込みを使用。
    code += bytes([0x8A])                     # TXA (writeMask を A に取得)
    code += bytes([0x29, 0x04])               # AND #$04 (bit2 をチェック)
    code += bytes([0xF0, 0x05])               # BEQ +5 (bit2 = 0 ならスキップ)
    code += bytes([0xA5, FC_COM_BUF_ZP + 14]) # LDA $4E
    code += bytes([0x8D, 0x0B, 0x40])         # STA $400B

    # --- APU Status ---

    # $4F → $4015 (APU Status/Enable)
    # 最後に書き込むことで、チャンネルの有効/無効を確実に制御
    code += bytes([0xA5, FC_COM_BUF_ZP + 15]) # LDA $4F
    code += bytes([0x8D, 0x15, 0x40])         # STA $4015

    # --- マジックバイトをクリア ---
    # 処理済みマークとして $40 を 0 に設定
    # (同じコマンドが再処理されるのを防ぐ)
    code += bytes([0xA9, 0x00])              # LDA #$00
    code += bytes([0x85, FC_COM_BUF_ZP])     # STA $40

    # =========================================================================
    # skip_apu: 処理完了、レジスタ復元
    # =========================================================================
    #
    # APU 処理をスキップした場合、またはAPU 処理完了後にここに到達。
    # 保存しておいたレジスタを復元して呼び出し元に戻る。
    #
    skip_apu_offset = len(code)
    skip_apu_addr = APU_CODE_ADDR + skip_apu_offset

    # 各 JMP skip_apu 命令のアドレスを設定
    code[jmp_skip_silence_lo] = skip_apu_addr & 0xFF
    code[jmp_skip_silence_lo + 1] = (skip_apu_addr >> 8) & 0xFF
    code[jmp_skip_full_lo] = skip_apu_addr & 0xFF
    code[jmp_skip_full_lo + 1] = (skip_apu_addr >> 8) & 0xFF
    code[jmp_skip_perchan_lo] = skip_apu_addr & 0xFF
    code[jmp_skip_perchan_lo + 1] = (skip_apu_addr >> 8) & 0xFF
    code[jmp_skip_from_silence_lo] = skip_apu_addr & 0xFF
    code[jmp_skip_from_silence_lo + 1] = (skip_apu_addr >> 8) & 0xFF

    # レジスタを復元 (保存時と逆順)
    code += bytes([0x68])                    # PLA
    code += bytes([0xA8])                    # TAY (Y を復元)
    code += bytes([0x68])                    # PLA
    code += bytes([0xAA])                    # TAX (X を復元)
    code += bytes([0x68])                    # PLA (A を復元)
    code += bytes([0x60])                    # RTS (呼び出し元に戻る)

    return bytes(code), new_reset_offset, jsr_hook_offset


def patch_rom(input_path, output_path):
    """ROM ファイルにパッチを適用

    Args:
        input_path: 入力 ROM ファイルパス (iNES 形式)
        output_path: 出力 ROM ファイルパス

    Returns:
        bool: 成功時 True
    """
    # ROM ファイルを読み込み
    with open(input_path, 'rb') as f:
        rom_data = bytearray(f.read())

    print(f"入力 ROM: {input_path}")
    print(f"ROM サイズ: {len(rom_data)} bytes")

    # APU 制御コードを生成
    apu_code, new_reset_offset, jsr_hook_offset = create_apu_code()

    print(f"APU コードサイズ: {len(apu_code)} bytes")
    print(f"APU コード配置: ${APU_CODE_ADDR:04X}")
    print(f"新 Reset ハンドラ: ${APU_CODE_ADDR + new_reset_offset:04X}")
    print(f"JSR フック: ${APU_CODE_ADDR + jsr_hook_offset:04X}")

    # APU コードを ROM の空き領域に書き込む
    apu_code_offset = rom_to_file(APU_CODE_ADDR)
    rom_data[apu_code_offset:apu_code_offset + len(apu_code)] = apu_code

    # Reset ベクタ ($FFFC-$FFFD) を新しいハンドラに変更
    new_reset_addr = APU_CODE_ADDR + new_reset_offset
    reset_vector_offset = rom_to_file(0xFFFC)
    old_reset = rom_data[reset_vector_offset] | (rom_data[reset_vector_offset + 1] << 8)
    rom_data[reset_vector_offset] = new_reset_addr & 0xFF
    rom_data[reset_vector_offset + 1] = (new_reset_addr >> 8) & 0xFF
    print(f"Reset ベクタ: ${old_reset:04X} → ${new_reset_addr:04X}")

    # JSR 命令をフックに変更
    jsr_offset = rom_to_file(JSR_HOOK_ADDR)
    new_jsr_addr = APU_CODE_ADDR + jsr_hook_offset
    rom_data[jsr_offset] = 0x20  # JSR opcode
    rom_data[jsr_offset + 1] = new_jsr_addr & 0xFF
    rom_data[jsr_offset + 2] = (new_jsr_addr >> 8) & 0xFF
    print(f"JSR @ ${JSR_HOOK_ADDR:04X}: JSR ${ORIGINAL_JSR_TARGET:04X} → JSR ${new_jsr_addr:04X}")

    # パッチ済み ROM を保存
    with open(output_path, 'wb') as f:
        f.write(rom_data)

    print(f"出力 ROM: {output_path}")
    print("パッチ完了!")

    # デバッグ: パッチ部分をダンプ
    print("\n--- パッチ部分のダンプ ---")
    for i in range(0, len(apu_code), 16):
        addr = APU_CODE_ADDR + i
        chunk = apu_code[i:i+16]
        hex_str = ' '.join(f'{b:02X}' for b in chunk)
        print(f"${addr:04X}: {hex_str}")

    return True


def main():
    """メイン関数"""
    # デフォルトのファイルパス
    script_dir = os.path.dirname(os.path.abspath(__file__))
    input_path = os.path.join(script_dir, "rom.nes")
    output_path = os.path.join(script_dir, "rom_apu.nes")

    # コマンドライン引数があれば使用
    if len(sys.argv) >= 2:
        input_path = sys.argv[1]
    if len(sys.argv) >= 3:
        output_path = sys.argv[2]

    # 入力ファイルの存在確認
    if not os.path.exists(input_path):
        print(f"エラー: 入力ファイルが見つかりません: {input_path}")
        return 1

    # パッチを適用
    if patch_rom(input_path, output_path):
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
