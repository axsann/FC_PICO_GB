#!/usr/bin/env python3
"""
GB ROM to C array converter for FC-PICO
Usage: python rom2c.py input.gb output.c
"""

import sys
import os

def convert_rom_to_c(input_file, output_file):
    with open(input_file, 'rb') as f:
        rom_data = f.read()

    rom_size = len(rom_data)
    rom_name = os.path.splitext(os.path.basename(input_file))[0]

    with open(output_file, 'w') as f:
        f.write(f'// Generated from {os.path.basename(input_file)}\n')
        f.write(f'// ROM size: {rom_size} bytes\n\n')
        f.write('#include <stdint.h>\n\n')
        f.write(f'const uint8_t gb_rom_data[] = {{\n')

        for i, byte in enumerate(rom_data):
            if i % 16 == 0:
                f.write('    ')
            f.write(f'0x{byte:02X},')
            if i % 16 == 15:
                f.write('\n')
            else:
                f.write(' ')

        if rom_size % 16 != 0:
            f.write('\n')

        f.write('};\n\n')
        f.write(f'const uint32_t gb_rom_size = {rom_size};\n')

    print(f'Converted: {input_file} -> {output_file}')
    print(f'ROM size: {rom_size} bytes ({rom_size/1024:.1f} KB)')

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('Usage: python rom2c.py input.gb output.c')
        sys.exit(1)

    convert_rom_to_c(sys.argv[1], sys.argv[2])
