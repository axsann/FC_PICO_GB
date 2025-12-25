# FC PICO GB

FC PICO 上で動作する Game Boy エミュレーターです。ファミコンの PPU を利用して Game Boy のゲームを表示します。

## アーキテクチャ

### システム構成

```
┌─────────────────────┐     GPIO/PIO     ┌─────────────────────┐
│   ファミコン本体     │ ◄──────────────► │  Raspberry Pi Pico   │
│   (PPU表示)         │    データ通信     │  (GB エミュレーション) │
└─────────────────────┘                   └─────────────────────┘
```

### ファイル構成

| ファイル | 役割 |
|----------|------|
| `fc_pico_v100.ino` | メインスケッチ、`GB_EMU_MODE` フラグ定義 |
| `ap_core0.h` | Core0 の setup/loop、GB エミュレータ初期化 |
| `rp_gbemu.cpp/h` | Peanut-GB ラッパークラス |
| `ap_gb.cpp/h` | GB 画面ハンドラ（FC への描画処理） |
| `ap_main.cpp/h` | アプリケーション状態管理（`ST_GB` ステート追加） |
| `rp_system.cpp/h` | FC との通信、パレット/属性テーブル管理 |
| `res/gbrom.c` | 埋め込み ROM データ |

### データフロー

**起動時:**
```
fc_pico_v100.ino → ap_core0.h::setup()
    → initGBEmulator()    ... gbrom.c から ROM 読み込み
    → gbemu.init()        ... Peanut-GB 初期化
    → ap.setStep(ST_GB)   ... GB モードに遷移
```

**メインループ (60fps):**
```
ap_core0.h::loop()
    → ap_main::main()     ... ST_GB ステート
    → ap_gb::main()
        ├── gbemu.setJoypad()   ... FC コントローラ入力を GB 形式に変換
        ├── gbemu.runFrame()    ... 1 フレーム分の GB エミュレーション実行
        └── renderToFC()        ... GB 画面を FC 画面に転送
```

### 画面変換

GB 画面 (160x144) を FC 画面 (256x240) の中央に配置します。

```
FC 画面 (256 x 240)
┌────────────────────────────────────┐
│          黒ボーダー (48px)          │
│  ┌────────────────────────────┐   │
│  │                            │   │
│  │     GB 画面 (160 x 144)    │   │
│  │                            │   │
│  └────────────────────────────┘   │
│          黒ボーダー (48px)          │
└────────────────────────────────────┘
```

- **オフセット**: X=48, Y=48
- **パレット**: GB 4階調 → FC パレット (0x0F, 0x00, 0x10, 0x30)

### コントローラ

FC コントローラをそのまま GB に対応させています。

| FC | GB |
|----|----|
| A | A |
| B | B |
| SELECT | SELECT |
| START | START |
| 十字キー | 十字キー |

### 制限事項

- **サウンド非対応**: 現在、音は出ません

## ビルド・実行方法

### 1. Arduino IDE で書き込み

1. Arduino IDE で `fc_pico_v100/fc_pico_v100.ino` を開く
2. ボード設定: Raspberry Pi Pico 2
3. 画面左上の Upload ボタン (→) または「Sketch」→「Upload」

### 2. FC PICO での実行

1. FC PICO をファミコン本体のカートリッジスロットに挿入
2. ファミコンの電源を入れる
3. Game Boy ゲームが FC の画面に表示される

## ROM について

### 同梱ゲーム

デフォルトで **Tobu Tobu Girl** が `res/gbrom.c` に埋め込まれています。

Tobu Tobu Girl はオープンソースの Game Boy ゲームで、再配布が許可されています。

- **公式サイト**: https://tangramgames.dk/tobutobugirl/
- **GitHub**: https://github.com/SimonLarsen/tobutobugirl
- **著作権者**: Simon Larsen, Lukas Hansen
- **ライセンス**: MIT (ソースコード) / CC-BY 4.0 (アセット)

### 他の ROM を使用する場合

```bash
python3 tools/rom2c.py your_game.gb res/gbrom.c
```

※ 市販ゲームの ROM は各自で用意してください。

## ライセンス

本プロジェクトは **GPL v3** ライセンスの下で配布されます。

### 使用ライブラリ

| ライブラリ | ライセンス | 著作権者 |
|-----------|-----------|---------|
| rp_fcemu (FabGL由来) | GPL v3 | Fabrizio Di Vittorio |
| ArduinoGL | MIT | Fabio de Albuquerque Dela Antonio |
| Peanut-GB | MIT | Mahyar Koshkouei |
| Tobu Tobu Girl | MIT / CC-BY 4.0 | Simon Larsen, Lukas Hansen |

### GPL v3 について

`rp_fcemu.cpp` が GPL v3 でライセンスされているため、本プロジェクト全体が GPL v3 の条件に従います。

- ソースコードの公開義務があります
- 派生物も GPL v3 でライセンスする必要があります
- 商用ライセンスが必要な場合は FabGL の作者にお問い合わせください

詳細は LICENSE ファイルを参照してください。
