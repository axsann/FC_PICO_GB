# CLAUDE.md

FC PICO GB プロジェクトの開発ガイドラインです。

## ⚠️ 重要: コミット禁止ファイル

**`fc_pico_gb/res/gbrom.c` は絶対に git add やコミットしてはいけません。**

現在リポジトリにはフリーソフトの ROM が含まれていますが、開発中にユーザーが別のゲームに差し替える可能性があります。誤って著作権で保護されたゲーム ROM をコミットしないよう、このファイルは常にコミット対象から除外してください。

## プロジェクト概要

FC PICO 上で動作する Game Boy エミュレーターです。Raspberry Pi Pico 2 をファミコンのカートリッジスロットに接続し、ファミコンの PPU を利用して Game Boy ゲームを表示します。

## ビルド・実行

```bash
# Arduino IDE を使用
# 1. fc_pico_gb/fc_pico_gb.ino を開く
# 2. ボード設定: Raspberry Pi Pico 2
# 3. Upload ボタン (→) または Sketch → Upload
# 4. FC PICO をファミコンに挿入して電源 ON
```

## ファイル構成

```
fc_pico_gb/
├── fc_pico_gb.ino    # メインスケッチ、GB_EMU_MODE フラグ
├── ap_core0.h          # Core0 setup/loop、GB エミュレータ初期化
├── rp_system.*         # FC 通信（PIO/DMA）、パレット/属性管理
├── rp_gbemu.*          # Peanut-GB ラッパー
├── ap_main.*           # アプリケーション状態管理
├── ap_gb.*             # GB 画面ハンドラ
├── Canvas.*            # 描画キャンバス (256x240)
├── peanut-gb/          # Peanut-GB ライブラリ
└── res/gbrom.c         # 埋め込み ROM データ
```

## 状態管理

```cpp
// ap_main.h
enum {
    ST_INIT,    // 初期化
    ST_TITLE,   // タイトル画面
    ST_GAME,    // オリジナルゲーム
    ST_GB,      // GB エミュレーション ← 現在のメイン
    ST_WAIT = 255,
};
```

## データフロー

**起動時:**
```
setup() → initGBEmulator() → gbemu.init() → ap.setStep(ST_GB)
```

**メインループ (60fps):**
```
loop() → ap_main::main() → ap_gb::main()
    ├── gbemu.setJoypad()   // FC → GB キー変換
    ├── gbemu.runFrame()    // 1 フレーム実行
    └── renderToFC()        // GB → FC 画面転送
```

## 重要な実装パターン

### 画面初期化

```cpp
void ap_xxx::init() {
    sys.setPalData(pal_data);
    sys.forcePalUpdate();
    sys.clearAtrData();
    sys.forceAtrUpdate();
    sys.startDataMode();
}
```

### ジョイパッド変換

```cpp
// FC キー → GB ジョイパッド
// Peanut-GB は active-low を期待するため反転必須
gb_pad ^= 0xFF;
gb.direct.joypad = gb_pad;
```

### 画面サイズ

- GB: 160x144
- FC: 256x240
- オフセット: X=48, Y=48 (中央配置)
- パレット: GB 4階調 → FC (0x0F, 0x00, 0x10, 0x30)

## ROM 変更

```bash
python3 fc_pico_gb/tools/rom2c.py your_game.gb fc_pico_gb/res/gbrom.c
```

## サウンド機能 (FC_COM_BUF 方式)

GB APU の音声を NES APU にマッピングして出力します。FC ROM にパッチを当てる必要があります。

### パッチ手順

```bash
cd fc_rom

# 1. ROM にパッチを適用
python3 patch_apu.py
# 出力: rom_patched.nes

# 2. パッチ済み ROM を C ファイルに変換
python3 rom_to_c.py rom_patched.nes ../fc_pico_gb/res/rom.c

# 3. Pico を再ビルド
```

### 関連ファイル

- `fc_rom/patch_apu.py` - APU コマンド処理を FC ROM に追加
- `fc_rom/rom_to_c.py` - NES ROM を C 配列に変換
- `fc_pico_gb/rp_gbapu.*` - GB APU → NES APU マッピング
- `fc_pico_gb/rp_system.*` - FC_COM_BUF 経由で APU データ送信
- `fc_pico_gb/res/rom.c` - パッチ済み FC ROM データ

### 技術詳細

**データフロー:**
1. `rp_gbapu`: GB APU レジスタ書き込みをフック、NES APU 形式に変換
2. `rp_system::queueApuWrite()`: APU レジスタ値を `m_apuRegLatest[]` に保存
3. `rp_system::sendApuCommands()`: FC_COM_BUF ($40-$4F) に APU データを書き込み
4. FC ROM: 毎フレーム FC_COM_BUF をチェック、マジックバイト確認後 APU レジスタに書き込み

**APU データフォーマット (FC_COM_BUF $40-$4F、16 バイト固定順序):**
```
$40: 0xA5 (マジックバイト)
$41: $400C 値 (Noise Volume) ※ 上位2ビットが常に0なので 0xFC と衝突しない
$42: $400E 値 (Noise Mode/Period)
$43: $400F 値 (Noise Length)
$44: $4000 値 (Pulse1 Duty/Volume)
$45: $4001 値 (Pulse1 Sweep)
$46: $4002 値 (Pulse1 Freq Lo)
$47: $4003 値 (Pulse1 Freq Hi)
$48: $4004 値 (Pulse2 Duty/Volume)
$49: $4005 値 (Pulse2 Sweep)
$4A: $4006 値 (Pulse2 Freq Lo)
$4B: $4007 値 (Pulse2 Freq Hi)
$4C: $4008 値 (Triangle Linear)
$4D: $400A 値 (Triangle Freq Lo)
$4E: $400B 値 (Triangle Freq Hi)
$4F: $4015 値 (APU Status/Enable) ※ 最後に書き込むことでチャンネル有効/無効を確実に制御
```

**利点:**
- 全 15 APU レジスタを毎フレーム送信可能
- PPU ($2006/$2007) を使用しないため画面に影響なし
- ゼロページアクセスで高速

## デバッグ

```cpp
Serial.begin(115200);
Serial.printf("Debug: %d\n", value);
```
