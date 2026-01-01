/*
    rp_gbemu.h - Game Boy Emulator wrapper for FC PICO
    Peanut-GB based GB emulation on Raspberry Pi Pico
*/

#ifndef rp_gbemu_h
#define rp_gbemu_h

#include "Arduino.h"
#include <LittleFS.h>

// GB APU module (must be before peanut_gb.h for audio_read/audio_write)
#include "rp_gbapu.h"

// Peanut-GB configuration - must be before including peanut_gb.h
#define PEANUT_GB_IS_LITTLE_ENDIAN 1
#define ENABLE_SOUND 1
#define ENABLE_LCD 1
#define PEANUT_GB_12_COLOUR 0  // 4-color mode for FC compatibility
#define PEANUT_GB_HIGH_LCD_ACCURACY 1  // Enable for better LCD emulation

// GB screen dimensions
#define GB_LCD_WIDTH  160
#define GB_LCD_HEIGHT 144

// Offset to center GB screen on FC screen (256x240)
#define GB_OFFSET_X ((256 - GB_LCD_WIDTH) / 2)   // 48
#define GB_OFFSET_Y ((240 - GB_LCD_HEIGHT) / 2)  // 48

// Maximum ROM size (2MB)
#define GB_ROM_MAX_SIZE (2 * 1024 * 1024)
// Maximum cart RAM size (128KB)
#define GB_CART_RAM_MAX_SIZE (128 * 1024)

// LittleFS availability flag (set during initialization)
extern bool g_littlefs_available;

// FC key to GB joypad mapping
// FC:  A=0x80, B=0x40, SEL=0x20, RUN=0x10, UP=0x08, DOWN=0x04, LEFT=0x02, RIGHT=0x01
// GB:  A=0x01, B=0x02, SEL=0x04, START=0x08, RIGHT=0x10, LEFT=0x20, UP=0x40, DOWN=0x80

class rp_gbemu {
public:
    rp_gbemu();

    // Initialize emulator with ROM data
    bool init(const uint8_t* rom_data, uint32_t rom_size);

    // Run one frame of emulation
    void runFrame();

    // Reset emulator
    void reset();

    // Set joypad state from FC controller input
    void setJoypad(uint8_t fc_key);

    // Get frame buffer pointer (for direct access)
    uint8_t* getFrameBuffer() { return m_frame_buffer; }

    // Check if emulator is initialized
    bool isInitialized() { return m_initialized; }

    // Get FC palette for current game (4 colors)
    uint8_t* getFcPalette() { return m_fc_palette; }

    // Check if game has specific palette (not DMG green)
    bool hasGamePalette() { return m_has_game_palette; }

    // ROM info
    const char* getRomTitle() { return m_rom_title; }

    // Save data management
    bool loadSave();
    bool saveSave();
    void markSaveDirty() { m_save_dirty = true; }
    bool isSaveDirty() { return m_save_dirty; }

private:
    // Generate save file path from ROM title
    void generateSavePath();

    bool m_initialized;
    uint8_t m_frame_buffer[GB_LCD_WIDTH * GB_LCD_HEIGHT];
    uint8_t* m_rom;
    uint32_t m_rom_size;
    uint8_t* m_cart_ram;
    uint32_t m_cart_ram_size;
    char m_rom_title[17];
    char m_save_path[32];
    bool m_save_dirty;
    uint8_t m_fc_palette[4];  // FC palette indices for current game
    bool m_has_game_palette;  // true if game has specific palette
};

extern rp_gbemu gbemu;

// Debug: last error code (0=OK, 1=NULL ROM, 2=RAM fail, 3=checksum, 4=unsupported)
extern int g_gb_last_error;

#endif
