# CLAUDE.md

FC PICO GB プロジェクトの開発ガイドラインです。

## プロジェクト概要

FC PICO 上で動作する Game Boy エミュレーターです。Raspberry Pi Pico 2 をファミコンのカートリッジスロットに接続し、ファミコンの PPU を利用して Game Boy ゲームを表示します。

## ビルド・実行

```bash
# Arduino IDE を使用
# 1. fc_pico_v100/fc_pico_v100.ino を開く
# 2. ボード設定: Raspberry Pi Pico 2
# 3. Upload ボタン (→) または Sketch → Upload
# 4. FC PICO をファミコンに挿入して電源 ON
```

## ファイル構成

```
fc_pico_v100/
├── fc_pico_v100.ino    # メインスケッチ、GB_EMU_MODE フラグ
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
python3 tools/rom2c.py your_game.gb res/gbrom.c
```

## デバッグ

```cpp
Serial.begin(115200);
Serial.printf("Debug: %d\n", value);
```
