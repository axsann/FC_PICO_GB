#!/usr/bin/env python3
"""
NES ROM を C 配列に変換するスクリプト

使用法:
  python3 rom_to_c.py [input.nes] [output.c]

デフォルト:
  入力: rom_apu_32.nes
  出力: ../fc_pico_gb/res/rom.c
"""

import sys
import os

def convert_rom_to_c(input_path, output_path):
    with open(input_path, 'rb') as f:
        data = f.read()

    # NES ヘッダー (16バイト) をスキップ
    if data[:4] == b'NES\x1a':
        data = data[16:]
        print(f"NES ヘッダーをスキップ")

    print(f"入力: {input_path}")
    print(f"ROM サイズ: {len(data)} bytes")

    with open(output_path, 'w') as f:
        f.write(f"const unsigned char _rom[{len(data)}] = {{\n")

        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
            if i + 12 < len(data):
                f.write(f"\t{hex_str},\n")
            else:
                f.write(f"\t{hex_str}\n")

        f.write("};\n")

    print(f"出力: {output_path}")
    print("変換完了!")

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))

    # デフォルト値
    input_path = os.path.join(script_dir, "rom_apu_compact.nes")
    output_path = os.path.join(script_dir, "..", "fc_pico_gb", "res", "rom.c")

    # 引数処理
    if len(sys.argv) >= 2:
        input_path = sys.argv[1]
    if len(sys.argv) >= 3:
        output_path = sys.argv[2]

    if not os.path.exists(input_path):
        print(f"エラー: 入力ファイルが見つかりません: {input_path}")
        return 1

    convert_rom_to_c(input_path, output_path)
    return 0

if __name__ == "__main__":
    sys.exit(main())
