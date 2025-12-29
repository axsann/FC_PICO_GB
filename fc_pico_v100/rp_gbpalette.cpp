/*
    rp_gbpalette.cpp - GB to FC palette conversion
    Game-specific FC palette mapping for popular GB titles
*/

#include "rp_gbpalette.h"

// DMG Green palette (classic Game Boy) - default
static const uint8_t DMG_PALETTE[4] = {0x09, 0x19, 0x29, 0x30};

// Game-specific palettes (FC palette indices)
// Format: { darkest, dark, light, lightest }

// Pokemon Red - red theme
static const uint8_t PAL_POKEMON_RED[4] = {0x0F, 0x06, 0x16, 0x30};

// Pokemon Blue - blue theme
static const uint8_t PAL_POKEMON_BLUE[4] = {0x0F, 0x02, 0x12, 0x30};

// Pokemon Yellow - yellow theme
static const uint8_t PAL_POKEMON_YELLOW[4] = {0x0F, 0x07, 0x28, 0x30};

// Tetris - red/yellow theme
static const uint8_t PAL_TETRIS[4] = {0x0F, 0x16, 0x27, 0x30};

// Legend of Zelda: Link's Awakening - green theme
static const uint8_t PAL_ZELDA[4] = {0x0F, 0x1A, 0x2A, 0x30};

// Kirby's Dream Land - pink theme
static const uint8_t PAL_KIRBY[4] = {0x0F, 0x15, 0x25, 0x30};

void initFcPaletteLab()
{
    // No longer needed
}

uint8_t rgb565ToFcIndex(uint16_t rgb565)
{
    // No longer used
    return 0x0F;
}

void convertGbcPaletteToFc(const uint16_t gbc_palette[3][4], uint8_t fc_palette[4])
{
    // Return DMG green palette
    memcpy(fc_palette, DMG_PALETTE, 4);
}

void getFcPaletteForEntry(uint8_t table_entry, uint8_t shuffling_flags, uint8_t fc_palette[4])
{
    // Return DMG green palette
    memcpy(fc_palette, DMG_PALETTE, 4);
}

void getFcPaletteForChecksum(uint8_t checksum, uint8_t fc_palette[4])
{
    switch (checksum)
    {
    // Pokemon Red
    case 0x14: // USA/Europe
    case 0x31: // Japan (Rev 1)
    case 0x32: // Japan
        memcpy(fc_palette, PAL_POKEMON_RED, 4);
        return;

    // Pokemon Yellow (Pikachu)
    case 0x1D: // Japan (Rev 3)
    case 0x1E: // Japan (Rev 2)
    case 0x1F: // Japan (Rev 1)
    case 0x20: // Japan
    case 0x97: // USA/Europe (GBC Enhanced)
        memcpy(fc_palette, PAL_POKEMON_YELLOW, 4);
        return;

    // Kirby's Dream Land
    case 0x85: // Japan (Rev A)
    case 0x86: // Japan
    case 0x98: // USA/Europe
        memcpy(fc_palette, PAL_KIRBY, 4);
        return;

    // Pokemon Blue
    case 0xD3: // USA/Europe
    case 0xE5: // Japan (Ao)
        memcpy(fc_palette, PAL_POKEMON_BLUE, 4);
        return;

    // Legend of Zelda: Link's Awakening
    case 0x6C: // USA/Europe, Japan (Rev A)
    case 0x6D: // Japan
        memcpy(fc_palette, PAL_ZELDA, 4);
        return;

    // Tetris (World) (Rev A)
    case 0x0A:
    // Tetris (Japan) (En)
    case 0x0B:
        memcpy(fc_palette, PAL_TETRIS, 4);
        return;

    // Default: DMG Green palette
    default:
        memcpy(fc_palette, DMG_PALETTE, 4);
        return;
    }
}

bool hasGamePalette(uint8_t checksum)
{
    switch (checksum)
    {
    case 0x14: // Pokemon Red (USA/Europe)
    case 0x31: // Pokemon Red (Japan, Rev 1)
    case 0x32: // Pokemon Red (Japan)
    case 0x1D: // Pokemon Yellow (Japan, Rev 3)
    case 0x1E: // Pokemon Yellow (Japan, Rev 2)
    case 0x1F: // Pokemon Yellow (Japan, Rev 1)
    case 0x20: // Pokemon Yellow (Japan)
    case 0x97: // Pokemon Yellow (USA/Europe)
    case 0x85: // Kirby's Dream Land (Japan, Rev A)
    case 0x86: // Kirby's Dream Land (Japan)
    case 0x98: // Kirby's Dream Land (USA/Europe)
    case 0xD3: // Pokemon Blue (USA/Europe)
    case 0xE5: // Pokemon Blue (Japan, Ao)
    case 0x6C: // Zelda: Link's Awakening (USA/Europe, Japan Rev A)
    case 0x6D: // Zelda: Link's Awakening (Japan)
    case 0x0A: // Tetris (World) (Rev 1)
    case 0x0B: // Tetris (Japan) (En)
    case 0xDB: // Tetris (World), Tetris (World) (Rev A)
        return true;
    default:
        return false;
    }
}
