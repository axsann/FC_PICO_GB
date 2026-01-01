/*
    rp_gbpalette.h - GB to FC palette conversion
    Converts RGB565 GBC palettes to FC/NES palette indices using LAB color space
*/

#ifndef rp_gbpalette_h
#define rp_gbpalette_h

#include "Arduino.h"

// Initialize FC palette LAB values (call once at startup)
void initFcPaletteLab();

// Convert RGB565 color to closest FC palette index
uint8_t rgb565ToFcIndex(uint16_t rgb565);

// Convert GBC palette (RGB565) to FC palette indices (legacy, returns mono)
// Input: gbc_palette[3][4] - OBJ0, OBJ1, BG palettes with 4 colors each
// Output: fc_palette[4] - FC palette indices for BG palette only (FC 4-color limit)
void convertGbcPaletteToFc(const uint16_t gbc_palette[3][4], uint8_t fc_palette[4]);

// Get fixed FC palette for GBC palette entry (new method - uses lookup table)
// Input: table_entry, shuffling_flags from get_colour_palette()
// Output: fc_palette[4] - Fixed FC palette indices
void getFcPaletteForEntry(uint8_t table_entry, uint8_t shuffling_flags, uint8_t fc_palette[4]);

// Get FC palette directly from ROM checksum (recommended method)
// Input: checksum - ROM checksum byte from 0x014D
// Output: fc_palette[4] - Fixed FC palette indices
void getFcPaletteForChecksum(uint8_t checksum, uint8_t fc_palette[4]);

// Check if ROM has game-specific palette (not default DMG green)
bool hasGamePalette(uint8_t checksum);

#endif
