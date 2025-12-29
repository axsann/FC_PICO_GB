/*
    rp_gbemu.cpp - Game Boy Emulator wrapper for FC PICO
    Peanut-GB based GB emulation on Raspberry Pi Pico
*/

#include "rp_gbemu.h"
#include "Canvas.h"

// Include Peanut-GB implementation
#include "peanut-gb/peanut_gb.h"

// Global emulator instance
rp_gbemu gbemu;

// Last error code (0=OK, 1=NULL ROM, 2=RAM alloc fail, 3=checksum, 4=unsupported cart)
int g_gb_last_error = 0;

// Peanut-GB context
static struct gb_s gb;

// Private data for Peanut-GB callbacks
struct gb_priv_s {
    uint8_t* rom;
    uint8_t* cart_ram;
    uint8_t* frame_buffer;
};
static struct gb_priv_s gb_priv;

//=================================================
// Peanut-GB Callback Functions
//=================================================

uint8_t gb_rom_read(struct gb_s* gb, const uint_fast32_t addr) {
    struct gb_priv_s* priv = (struct gb_priv_s*)gb->direct.priv;
    return priv->rom[addr];
}

uint8_t gb_cart_ram_read(struct gb_s* gb, const uint_fast32_t addr) {
    struct gb_priv_s* priv = (struct gb_priv_s*)gb->direct.priv;
    return priv->cart_ram[addr];
}

void gb_cart_ram_write(struct gb_s* gb, const uint_fast32_t addr, const uint8_t val) {
    struct gb_priv_s* priv = (struct gb_priv_s*)gb->direct.priv;
    priv->cart_ram[addr] = val;
    gbemu.markSaveDirty();
}

void gb_error(struct gb_s* gb, const enum gb_error_e err, const uint16_t addr) {
    (void)gb; (void)err; (void)addr;
    // Errors are silently ignored in release build
}

void gb_lcd_draw_line(struct gb_s* gb, const uint8_t* pixels, const uint_fast8_t line) {
    struct gb_priv_s* priv = (struct gb_priv_s*)gb->direct.priv;

    // Copy line to frame buffer
    // Invert colors: GB 0=white, 3=black -> FC 3=white, 0=black
    uint8_t* dest = &priv->frame_buffer[line * GB_LCD_WIDTH];
    for (int x = 0; x < GB_LCD_WIDTH; x++) {
        dest[x] = 3 - (pixels[x] & 0x03);
    }
}

//=================================================
// rp_gbemu Class Implementation
//=================================================

rp_gbemu::rp_gbemu() {
    m_initialized = false;
    m_rom = nullptr;
    m_cart_ram = nullptr;
    m_rom_size = 0;
    m_cart_ram_size = 0;
    memset(m_frame_buffer, 0, sizeof(m_frame_buffer));
    memset(m_rom_title, 0, sizeof(m_rom_title));
    memset(m_save_path, 0, sizeof(m_save_path));
    m_save_dirty = false;
}

bool rp_gbemu::init(const uint8_t* rom_data, uint32_t rom_size) {
    if (rom_data == nullptr || rom_size == 0) {
        g_gb_last_error = 1;
        return false;
    }

    // Store ROM pointer
    m_rom = (uint8_t*)rom_data;
    m_rom_size = rom_size;

    // Allocate cart RAM
    m_cart_ram_size = GB_CART_RAM_MAX_SIZE;
    m_cart_ram = (uint8_t*)malloc(m_cart_ram_size);
    if (m_cart_ram == nullptr) {
        m_cart_ram_size = 32 * 1024;
        m_cart_ram = (uint8_t*)malloc(m_cart_ram_size);
        if (m_cart_ram == nullptr) {
            g_gb_last_error = 2;
            return false;
        }
    }
    memset(m_cart_ram, 0, m_cart_ram_size);

    // Setup private data
    gb_priv.rom = m_rom;
    gb_priv.cart_ram = m_cart_ram;
    gb_priv.frame_buffer = m_frame_buffer;

    // Initialize Peanut-GB
    enum gb_init_error_e ret = gb_init(&gb,
                                        &gb_rom_read,
                                        &gb_cart_ram_read,
                                        &gb_cart_ram_write,
                                        &gb_error,
                                        &gb_priv);

    if (ret != GB_INIT_NO_ERROR) {
        if (ret == GB_INIT_CARTRIDGE_UNSUPPORTED) {
            g_gb_last_error = 4;
        } else if (ret == GB_INIT_INVALID_CHECKSUM) {
            g_gb_last_error = 3;
        }
        free(m_cart_ram);
        m_cart_ram = nullptr;
        return false;
    }

    // Initialize LCD
    gb_init_lcd(&gb, &gb_lcd_draw_line);

    // Extract ROM title
    for (int i = 0; i < 16; i++) {
        char c = (char)m_rom[0x0134 + i];
        if (c < 32 || c > 126) c = '\0';
        m_rom_title[i] = c;
    }
    m_rom_title[16] = '\0';

    // Generate save path and load save data
    generateSavePath();
    loadSave();

    // Clear frame buffer
    memset(m_frame_buffer, 3, sizeof(m_frame_buffer));

    m_initialized = true;
    return true;
}

void rp_gbemu::runFrame() {
    if (!m_initialized) return;
    gb_run_frame(&gb);
}

void rp_gbemu::reset() {
    if (!m_initialized) return;
    gb_reset(&gb);
    memset(m_frame_buffer, 3, sizeof(m_frame_buffer));
}

void rp_gbemu::setJoypad(uint8_t fc_key) {
    if (!m_initialized) return;

    // Use bitfield (active-low: 0=pressed, 1=released)
    gb.direct.joypad_bits.a      = !(fc_key & 0x80);
    gb.direct.joypad_bits.b      = !(fc_key & 0x40);
    gb.direct.joypad_bits.select = !(fc_key & 0x20);
    gb.direct.joypad_bits.start  = !(fc_key & 0x10);
    gb.direct.joypad_bits.up     = !(fc_key & 0x08);
    gb.direct.joypad_bits.down   = !(fc_key & 0x04);
    gb.direct.joypad_bits.left   = !(fc_key & 0x02);
    gb.direct.joypad_bits.right  = !(fc_key & 0x01);
}

//=================================================
// Save Data Management
//=================================================

void rp_gbemu::generateSavePath() {
    // Create save path from ROM title: /saves/TITLE.sav
    strcpy(m_save_path, "/saves/");

    int j = 7; // Start after "/saves/"
    for (int i = 0; i < 16 && m_rom_title[i] != '\0' && j < 28; i++) {
        char c = m_rom_title[i];
        // Replace invalid filename characters with underscore
        if (c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
        m_save_path[j++] = c;
    }
    m_save_path[j] = '\0';
    strcat(m_save_path, ".sav");

    Serial.printf("Save path: %s\n", m_save_path);
}

bool rp_gbemu::loadSave() {
    if (m_cart_ram == nullptr || m_save_path[0] == '\0') {
        return false;
    }

    // Create saves directory if it doesn't exist
    if (!LittleFS.exists("/saves")) {
        LittleFS.mkdir("/saves");
    }

    if (!LittleFS.exists(m_save_path)) {
        Serial.printf("No save file found: %s\n", m_save_path);
        return false;
    }

    File f = LittleFS.open(m_save_path, "r");
    if (!f) {
        Serial.printf("Failed to open save file: %s\n", m_save_path);
        return false;
    }

    size_t read_size = f.read(m_cart_ram, m_cart_ram_size);
    f.close();

    Serial.printf("Loaded save: %s (%d bytes)\n", m_save_path, read_size);
    m_save_dirty = false;
    return true;
}

bool rp_gbemu::saveSave() {
    if (m_cart_ram == nullptr || m_save_path[0] == '\0') {
        Serial.println("saveSave: cart_ram or save_path is null");
        return false;
    }

    Serial.printf("saveSave: Attempting to save to %s\n", m_save_path);

    // Create saves directory if it doesn't exist
    if (!LittleFS.exists("/saves")) {
        LittleFS.mkdir("/saves");
    }

    File f = LittleFS.open(m_save_path, "w");
    if (!f) {
        Serial.printf("Failed to create save file: %s\n", m_save_path);
        m_save_dirty = false;  // Give up to avoid retry loop
        return false;
    }

    // Only save actual cart RAM used (check first non-zero from end)
    uint32_t save_size = 32 * 1024; // Default 32KB for most games
    size_t written = f.write(m_cart_ram, save_size);
    f.close();

    Serial.printf("Saved: %s (%d bytes)\n", m_save_path, written);
    m_save_dirty = false;
    return true;
}
