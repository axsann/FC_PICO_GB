#!/usr/bin/env python3
"""
FC PICO ROM APU パッチスクリプト (FC_COM_BUF 方式)

【概要】
FC ROM にパッチを当てて、Pico から FC_COM_BUF (ゼロページ $40-$4F) に
送信された APU データを読み取り、NES APU レジスタに書き込む機能を追加する。
これにより GB エミュレータのサウンドを NES APU で再生可能になる。

【問題と解決策】
- APU 非対応の古い FC ROM では、FC_COM_BUF ($40-$4F) がワーク RAM として
  使用されている場合があり、APU コマンドを書き込むとクラッシュする
- 解決策: FC ROM 起動時に Pico へ「APU 対応通知」を送信し、
  Pico 側で m_apuSupported フラグを設定。非対応 ROM では APU コマンドを送信しない

【APU 対応通知の仕組み】
- JSR フック内で $2007 (PPUDATA) に 0x05 を書き込む
- この書き込みは Pico の SM_RECV (PIO) で検出される
- $2007 への書き込みは PPU の VRAM を変更するため、毎フレーム行うと画面が壊れる
- そのため、ゼロページ $3F をフラグとして使用し、初回フレームのみ送信する
- 初回フレームで 1 バイトだけ VRAM が変更されるが、実用上問題なし

【データフロー】
1. Pico: GB APU の状態を NES APU レジスタ値に変換
2. Pico: FC_COM_BUF ($40-$4F) に APU データを書き込み
3. FC: 毎フレーム FC_COM_BUF から 16 バイト読み出し
4. FC: マジックバイト (0xA5) を確認後、APU レジスタに書き込み

【APU データフォーマット】(FC_COM_BUF $40-$4F、16バイト、固定順序)
  $40 = 0xA5 (マジックバイト)
  $41 = $400C 値 (Noise Volume) ※ 上位2ビットが常に0なので 0xFC と衝突しない
  $42 = $400E 値 (Noise Mode/Period)
  $43 = $400F 値 (Noise Length)
  $44 = $4000 値 (Pulse1 Duty/Volume)
  $45 = $4001 値 (Pulse1 Sweep)
  $46 = $4002 値 (Pulse1 Freq Lo)
  $47 = $4003 値 (Pulse1 Freq Hi)
  $48 = $4004 値 (Pulse2 Duty/Volume)
  $49 = $4005 値 (Pulse2 Sweep)
  $4A = $4006 値 (Pulse2 Freq Lo)
  $4B = $4007 値 (Pulse2 Freq Hi)
  $4C = $4008 値 (Triangle Linear)
  $4D = $400A 値 (Triangle Freq Lo)
  $4E = $400B 値 (Triangle Freq Hi)
  $4F = $4015 値 (APU Status/Enable) ※ 最後に書き込むことで確実にチャンネル有効/無効を制御

【通常コマンドとの区別】
  通常コマンド (パレット更新など) では $41 = 0xFC (PF_MAGIC_NO)
  APU コマンドでは $41 = $400C 値 (上位2ビットが常に0なので 0xFC にはならない)
  → $40 == 0xA5 かつ $41 != 0xFC で APU コマンドと判定

【パッチ内容】
1. Reset ベクタを新しいハンドラにリダイレクト
   - NES APU を初期化 ($4015, $4017, $4010)
   - 元の Reset ハンドラにジャンプ

2. JSR $9536 (毎フレーム呼ばれる) をフック
   - FC_COM_BUF $40 をチェック
   - 0xA5 なら APU データとして処理
   - 各値を対応する APU レジスタに書き込み
"""

import sys
import os

def rom_to_file(addr):
    """ROM アドレス ($8000-$FFFF) をファイルオフセットに変換

    FC PICO の ROM は 32KB (NROM-256 相当) で、
    $8000-$FFFF の範囲がファイルの先頭からマッピングされる。
    iNES ヘッダー (16 バイト) を考慮する必要がある。
    """
    return (addr - 0x8000) + 16  # +16 for iNES header

# パッチを適用する位置 (空き領域)
# $9EC8-$9FFF は元の ROM で未使用領域
APU_CODE_ADDR = 0x9EC8

# フックする JSR 命令のアドレス
# $EDD9 に JSR $9536 がある (PPU データ転送ルーチン呼び出し)
JSR_HOOK_ADDR = 0xEDD9

# 元の JSR ターゲット
ORIGINAL_JSR_TARGET = 0x9536

# 元の Reset ハンドラアドレス
ORIGINAL_RESET = 0xF000

# FC_COM_BUF のゼロページベースアドレス
FC_COM_BUF_ZP = 0x40

# APU マジックバイト
APU_MAGIC_BYTE = 0xA5

# 通常コマンドのマジックバイト (PF_MAGIC_NO)
# FC_COM_BUF[1] がこの値なら通常コマンド（パレット、属性など）
# APU コマンドではこの位置に $4000 レジスタ値が入るため、0xFC にはならない
PF_MAGIC_NO = 0xFC

# APU 対応通知コマンド
# この値を $2007 に書き込むことで、Pico に「この ROM は APU 対応」と通知
# Pico 側では rp_system.cpp の jobRcvCom() で case 0x05 として処理
APU_NOTIFY_CMD = 0x05

# APU 対応通知フラグのゼロページアドレス
# 初回送信後にインクリメントし、2回目以降の送信をスキップ
APU_NOTIFY_FLAG_ZP = 0x3F


def create_apu_code():
    """APU 制御用の 6502 マシンコードを生成

    Returns:
        tuple: (バイト列, new_reset_offset, jsr_hook_offset)
    """
    code = []

    # =============================================
    # 新しい Reset ハンドラ (NES APU 初期化)
    # =============================================
    new_reset_offset = len(code)

    # $4015 (APU Status) = 0x00: 全チャンネル無効化
    code += bytes([0xA9, 0x00])         # LDA #$00
    code += bytes([0x8D, 0x15, 0x40])   # STA $4015

    # 各チャンネルの音量を 0 に設定 (起動時ノイズ防止)
    # $4000 = 0x30: Pulse1 音量=0, 定数モード (LC halt)
    code += bytes([0xA9, 0x30])         # LDA #$30
    code += bytes([0x8D, 0x00, 0x40])   # STA $4000
    # $4004 = 0x30: Pulse2 音量=0, 定数モード (LC halt)
    code += bytes([0x8D, 0x04, 0x40])   # STA $4004
    # $400C = 0x30: Noise 音量=0, 定数モード (LC halt)
    code += bytes([0x8D, 0x0C, 0x40])   # STA $400C
    # $4008 = 0x00: Triangle リニアカウンタ=0 (無音)
    code += bytes([0xA9, 0x00])         # LDA #$00
    code += bytes([0x8D, 0x08, 0x40])   # STA $4008

    # $4017 (Frame Counter) = 0x40: 4ステップモード、IRQ 無効
    code += bytes([0xA9, 0x40])         # LDA #$40
    code += bytes([0x8D, 0x17, 0x40])   # STA $4017

    # $4010 (DMC) = 0x00: DMC チャンネル無効化
    code += bytes([0xA9, 0x00])         # LDA #$00
    code += bytes([0x8D, 0x10, 0x40])   # STA $4010

    # $4015 = 0x0F: Pulse1, Pulse2, Triangle, Noise チャンネル有効化
    code += bytes([0xA9, 0x0F])         # LDA #$0F
    code += bytes([0x8D, 0x15, 0x40])   # STA $4015

    # 元の Reset ハンドラにジャンプ
    code += bytes([0x4C, ORIGINAL_RESET & 0xFF,
                   (ORIGINAL_RESET >> 8) & 0xFF])  # JMP $F000

    # =============================================
    # JSR フック (APU コマンド処理)
    # =============================================
    jsr_hook_offset = len(code)

    # 元の PPU データ転送ルーチンを呼び出す
    code += bytes([0x20, ORIGINAL_JSR_TARGET & 0xFF,
                   (ORIGINAL_JSR_TARGET >> 8) & 0xFF])  # JSR $9536

    # レジスタを保存 (A, X, Y)
    code += bytes([0x48])                    # PHA
    code += bytes([0x8A])                    # TXA
    code += bytes([0x48])                    # PHA
    code += bytes([0x98])                    # TYA
    code += bytes([0x48])                    # PHA

    # -----------------------------------------
    # APU 対応通知 (初回フレームのみ)
    # -----------------------------------------
    # $3F をフラグとして使用。0 なら未送信、非 0 なら送信済み。
    #
    # $2007 (PPUDATA) への書き込みは Pico の SM_RECV で検出される。
    # ただし、$2007 への書き込みは VRAM を変更するため、
    # 毎フレーム行うと画面が壊れる。そのため初回のみ送信する。

    code += bytes([0xA5, APU_NOTIFY_FLAG_ZP])  # LDA $3F (フラグをチェック)
    code += bytes([0xD0])                       # BNE skip_notify (非0ならスキップ)
    bne_skip_notify = len(code)
    code += bytes([0x00])                       # 分岐オフセット (後で設定)

    # フラグが 0 の場合: APU 対応通知を送信
    code += bytes([0xA9, APU_NOTIFY_CMD])       # LDA #$05
    code += bytes([0x8D, 0x07, 0x20])           # STA $2007 (Pico に通知)
    code += bytes([0xE6, APU_NOTIFY_FLAG_ZP])   # INC $3F (フラグをセット)

    # skip_notify: 分岐先
    skip_notify_offset = len(code)
    code[bne_skip_notify] = skip_notify_offset - bne_skip_notify - 1

    # -----------------------------------------
    # FC_COM_BUF からAPU データを読み出し
    # -----------------------------------------
    # $40 (マジックバイト) をチェック
    code += bytes([0xA5, FC_COM_BUF_ZP])     # LDA $40
    code += bytes([0xC9, APU_MAGIC_BYTE])    # CMP #$A5
    code += bytes([0xD0])                    # BNE skip_apu
    bne_skip1 = len(code)
    code += bytes([0x00])                    # (オフセット後で設定)

    # $41 != 0xFC をチェック (通常コマンドとの区別)
    # 通常コマンドでは FC_COM_BUF[1] = PF_MAGIC_NO (0xFC)
    # APU コマンドでは FC_COM_BUF[1] = $4000 レジスタ値
    code += bytes([0xA5, FC_COM_BUF_ZP + 1])  # LDA $41
    code += bytes([0xC9, PF_MAGIC_NO])        # CMP #$FC
    code += bytes([0xF0])                     # BEQ skip_apu (0xFC なら通常コマンド)
    bne_skip2 = len(code)
    code += bytes([0x00])                     # (オフセット後で設定)

    # マジックバイトが一致: APU データを処理
    # 注: $41 には $400C が入っている (上位2ビットが常に0なので 0xFC と衝突しない)
    #     $4F には $4015 が入っている (最後に書き込むことで確実にチャンネル有効/無効を制御)

    # $41 → $400C (Noise Volume)
    code += bytes([0xA5, FC_COM_BUF_ZP + 1]) # LDA $41
    code += bytes([0x8D, 0x0C, 0x40])        # STA $400C

    # $42 → $400E (Noise Mode/Period)
    code += bytes([0xA5, FC_COM_BUF_ZP + 2]) # LDA $42
    code += bytes([0x8D, 0x0E, 0x40])        # STA $400E

    # $43 → $400F (Noise Length)
    code += bytes([0xA5, FC_COM_BUF_ZP + 3]) # LDA $43
    code += bytes([0x8D, 0x0F, 0x40])        # STA $400F

    # $44 → $4000 (Pulse1 Duty/Volume)
    code += bytes([0xA5, FC_COM_BUF_ZP + 4]) # LDA $44
    code += bytes([0x8D, 0x00, 0x40])        # STA $4000

    # $45 → $4001 (Pulse1 Sweep)
    code += bytes([0xA5, FC_COM_BUF_ZP + 5]) # LDA $45
    code += bytes([0x8D, 0x01, 0x40])        # STA $4001

    # $46 → $4002 (Pulse1 Freq Lo)
    code += bytes([0xA5, FC_COM_BUF_ZP + 6]) # LDA $46
    code += bytes([0x8D, 0x02, 0x40])        # STA $4002

    # $47 → $4003 (Pulse1 Freq Hi)
    code += bytes([0xA5, FC_COM_BUF_ZP + 7]) # LDA $47
    code += bytes([0x8D, 0x03, 0x40])        # STA $4003

    # $48 → $4004 (Pulse2 Duty/Volume)
    code += bytes([0xA5, FC_COM_BUF_ZP + 8]) # LDA $48
    code += bytes([0x8D, 0x04, 0x40])        # STA $4004

    # $49 → $4005 (Pulse2 Sweep)
    code += bytes([0xA5, FC_COM_BUF_ZP + 9]) # LDA $49
    code += bytes([0x8D, 0x05, 0x40])        # STA $4005

    # $4A → $4006 (Pulse2 Freq Lo)
    code += bytes([0xA5, FC_COM_BUF_ZP + 10]) # LDA $4A
    code += bytes([0x8D, 0x06, 0x40])         # STA $4006

    # $4B → $4007 (Pulse2 Freq Hi)
    code += bytes([0xA5, FC_COM_BUF_ZP + 11]) # LDA $4B
    code += bytes([0x8D, 0x07, 0x40])         # STA $4007

    # $4C → $4008 (Triangle Linear)
    code += bytes([0xA5, FC_COM_BUF_ZP + 12]) # LDA $4C
    code += bytes([0x8D, 0x08, 0x40])         # STA $4008

    # $4D → $400A (Triangle Freq Lo)
    code += bytes([0xA5, FC_COM_BUF_ZP + 13]) # LDA $4D
    code += bytes([0x8D, 0x0A, 0x40])         # STA $400A

    # $4E → $400B (Triangle Freq Hi)
    code += bytes([0xA5, FC_COM_BUF_ZP + 14]) # LDA $4E
    code += bytes([0x8D, 0x0B, 0x40])         # STA $400B

    # $4F → $4015 (APU Status/Enable) - 最後に書き込むことで確実にチャンネル有効/無効を制御
    code += bytes([0xA5, FC_COM_BUF_ZP + 15]) # LDA $4F
    code += bytes([0x8D, 0x15, 0x40])         # STA $4015

    # マジックバイトをクリア (処理済みマーク)
    code += bytes([0xA9, 0x00])              # LDA #$00
    code += bytes([0x85, FC_COM_BUF_ZP])     # STA $40

    # skip_apu: 分岐先 (両方の BNE 命令のオフセットを設定)
    skip_apu_offset = len(code)
    code[bne_skip1] = skip_apu_offset - bne_skip1 - 1
    code[bne_skip2] = skip_apu_offset - bne_skip2 - 1

    # -----------------------------------------
    # 処理完了: レジスタ復元
    # -----------------------------------------
    code += bytes([0x68])                    # PLA
    code += bytes([0xA8])                    # TAY
    code += bytes([0x68])                    # PLA
    code += bytes([0xAA])                    # TAX
    code += bytes([0x68])                    # PLA

    # 呼び出し元に戻る
    code += bytes([0x60])                    # RTS

    return bytes(code), new_reset_offset, jsr_hook_offset


def patch_rom(input_path, output_path):
    """ROM ファイルにパッチを適用

    Args:
        input_path: 入力 ROM ファイルパス (rom_original.nes)
        output_path: 出力 ROM ファイルパス (rom_patched.nes)

    Returns:
        bool: 成功時 True
    """
    with open(input_path, 'rb') as f:
        rom_data = bytearray(f.read())

    print(f"入力 ROM: {input_path}")
    print(f"ROM サイズ: {len(rom_data)} bytes")

    # APU 制御コードを生成
    apu_code, new_reset_offset, jsr_hook_offset = create_apu_code()

    print(f"APU コードサイズ: {len(apu_code)} bytes")
    print(f"APU コード配置: ${APU_CODE_ADDR:04X}")
    print(f"新 Reset: ${APU_CODE_ADDR + new_reset_offset:04X}")
    print(f"JSR フック: ${APU_CODE_ADDR + jsr_hook_offset:04X}")
    print(f"FC_COM_BUF: ${FC_COM_BUF_ZP:02X}-${FC_COM_BUF_ZP + 15:02X}")

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

    # JSR $9536 を JSR (新しいフックハンドラ) に変更
    jsr_offset = rom_to_file(JSR_HOOK_ADDR)
    old_jsr = rom_data[jsr_offset:jsr_offset + 3]
    print(f"元のコード @ ${JSR_HOOK_ADDR:04X}: {old_jsr[0]:02X}{old_jsr[1]:02X}{old_jsr[2]:02X} (20 36 95 であるべき)")

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


if __name__ == '__main__':
    # デフォルトのファイルパス
    script_dir = os.path.dirname(os.path.abspath(__file__))
    input_rom = os.path.join(script_dir, 'rom_original.nes')
    output_rom = os.path.join(script_dir, 'rom_patched.nes')

    if len(sys.argv) >= 2:
        input_rom = sys.argv[1]
    if len(sys.argv) >= 3:
        output_rom = sys.argv[2]

    if not os.path.exists(input_rom):
        print(f"エラー: 入力 ROM が見つかりません: {input_rom}")
        sys.exit(1)

    patch_rom(input_rom, output_rom)
