/*
    rp_gbemu.cpp - Game Boy Emulator wrapper for FC PICO
    Peanut-GB based GB emulation on Raspberry Pi Pico
*/

#include "rp_gbemu.h"
#include "rp_gbpalette.h"
#include "Canvas.h"

// Include Peanut-GB implementation
#include "peanut-gb/peanut_gb.h"

// Global emulator instance
rp_gbemu gbemu;

// Last error code (0=OK, 1=NULL ROM, 2=RAM alloc fail, 3=checksum, 4=unsupported cart)
int g_gb_last_error = 0;

// LittleFS availability flag
bool g_littlefs_available = false;

// Peanut-GB context
static struct gb_s gb;

// ROM Bank 0 cache (64KB) for faster access
static uint8_t rom_bank0[65536];

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
    // Use cached ROM bank 0 for faster access
    if (addr < 65536) {
        return rom_bank0[addr];
    }
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
    memset(m_state_path, 0, sizeof(m_state_path));
    m_save_dirty = false;
    m_last_state_error = STATE_OK;
    // Default grayscale palette
    m_fc_palette[0] = 0x0F;  // Black
    m_fc_palette[1] = 0x00;  // Dark Gray
    m_fc_palette[2] = 0x10;  // Light Gray
    m_fc_palette[3] = 0x30;  // White
    m_has_game_palette = false;
}

bool rp_gbemu::init(const uint8_t* rom_data, uint32_t rom_size) {
    if (rom_data == nullptr || rom_size == 0) {
        g_gb_last_error = 1;
        return false;
    }

    // Store ROM pointer
    m_rom = (uint8_t*)rom_data;
    m_rom_size = rom_size;

    // Cache first 64KB of ROM for faster access
    uint32_t cache_size = (rom_size < 65536) ? rom_size : 65536;
    memcpy(rom_bank0, rom_data, cache_size);

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

    // Get game-specific FC palette using ROM checksum
    uint8_t checksum = m_rom[0x014D];
    getFcPaletteForChecksum(checksum, m_fc_palette);
    m_has_game_palette = ::hasGamePalette(checksum);

    Serial.printf("Game: %s, Checksum: 0x%02X\n", m_rom_title, checksum);
    Serial.printf("FC Palette: 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
                  m_fc_palette[0], m_fc_palette[1], m_fc_palette[2], m_fc_palette[3]);

    // Generate save path and load save data
    generateSavePath();
    generateStatePath();
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

void rp_gbemu::generateStatePath() {
    // Create state path from ROM title: /saves/TITLE.state
    strcpy(m_state_path, "/saves/");

    int j = 7; // Start after "/saves/"
    for (int i = 0; i < 16 && m_rom_title[i] != '\0' && j < 24; i++) {
        char c = m_rom_title[i];
        // Replace invalid filename characters with underscore
        if (c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
        m_state_path[j++] = c;
    }
    m_state_path[j] = '\0';
    strcat(m_state_path, ".state");

    Serial.printf("State path: %s\n", m_state_path);
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

//=================================================
// Save State Management
//=================================================

bool rp_gbemu::saveState() {
    if (!m_initialized) {
        m_last_state_error = STATE_ERR_NOT_INIT;
        return false;
    }

    if (!g_littlefs_available) {
        m_last_state_error = STATE_ERR_NO_FS;
        return false;
    }

    // Calculate state size
    // Header + ROM header + cpu_reg + counter + flags + MBC + RTC + display + memories
    uint32_t state_size = sizeof(SaveStateHeader)
                        + ROM_HEADER_SIZE
                        + sizeof(gb.cpu_reg)
                        + sizeof(gb.counter)
                        + 16  // flags and MBC state
                        + sizeof(gb.rtc_latched) + sizeof(gb.rtc_real)
                        + 16  // display state (palettes, etc.)
                        + WRAM_SIZE + VRAM_SIZE + OAM_SIZE + HRAM_IO_SIZE;

    // Allocate buffer
    uint8_t* buffer = (uint8_t*)malloc(state_size);
    if (!buffer) {
        Serial.println("saveState: Failed to allocate buffer");
        m_last_state_error = STATE_ERR_FILE_WRITE;
        return false;
    }

    uint8_t* ptr = buffer;

    // 1. Write header
    SaveStateHeader header;
    memcpy(header.magic, FCPICO_STATE_MAGIC, 8);
    header.version = FCPICO_STATE_VERSION;
    header.gb_struct_size = sizeof(gb);
    header.cart_ram_size = m_cart_ram_size;
    header.reserved = 0;
    memcpy(ptr, &header, sizeof(header));
    ptr += sizeof(header);

    // 2. Write ROM header for verification
    memcpy(ptr, &m_rom[ROM_HEADER_START], ROM_HEADER_SIZE);
    ptr += ROM_HEADER_SIZE;

    // 3. Serialize CPU registers
    memcpy(ptr, &gb.cpu_reg, sizeof(gb.cpu_reg));
    ptr += sizeof(gb.cpu_reg);

    // 4. Serialize counters
    memcpy(ptr, &gb.counter, sizeof(gb.counter));
    ptr += sizeof(gb.counter);

    // 5. Serialize flags and MBC state
    *ptr++ = gb.gb_halt;
    *ptr++ = gb.gb_ime;
    *ptr++ = gb.gb_frame;
    *ptr++ = gb.lcd_blank;
    *ptr++ = gb.mbc;
    *ptr++ = gb.cart_ram;
    memcpy(ptr, &gb.num_rom_banks_mask, sizeof(gb.num_rom_banks_mask));
    ptr += sizeof(gb.num_rom_banks_mask);
    *ptr++ = gb.num_ram_banks;
    memcpy(ptr, &gb.selected_rom_bank, sizeof(gb.selected_rom_bank));
    ptr += sizeof(gb.selected_rom_bank);
    *ptr++ = gb.cart_ram_bank;
    *ptr++ = gb.enable_cart_ram;
    *ptr++ = gb.cart_mode_select;
    *ptr++ = 0; // padding

    // 6. Serialize RTC state
    memcpy(ptr, &gb.rtc_latched, sizeof(gb.rtc_latched));
    ptr += sizeof(gb.rtc_latched);
    memcpy(ptr, &gb.rtc_real, sizeof(gb.rtc_real));
    ptr += sizeof(gb.rtc_real);

    // 7. Serialize display state
    memcpy(ptr, gb.display.bg_palette, 4);
    ptr += 4;
    memcpy(ptr, gb.display.sp_palette, 8);
    ptr += 8;
    *ptr++ = gb.display.window_clear;
    *ptr++ = gb.display.WY;
    *ptr++ = gb.display.frame_skip_count;
    *ptr++ = gb.display.interlace_count;

    // 8. Memory dumps
    memcpy(ptr, gb.wram, WRAM_SIZE);
    ptr += WRAM_SIZE;
    memcpy(ptr, gb.vram, VRAM_SIZE);
    ptr += VRAM_SIZE;
    memcpy(ptr, gb.oam, OAM_SIZE);
    ptr += OAM_SIZE;
    memcpy(ptr, gb.hram_io, HRAM_IO_SIZE);
    ptr += HRAM_IO_SIZE;

    // Ensure directory exists
    if (!LittleFS.exists("/saves")) {
        LittleFS.mkdir("/saves");
    }

    // Write to file
    File f = LittleFS.open(m_state_path, "w");
    if (!f) {
        free(buffer);
        Serial.printf("saveState: Failed to open %s\n", m_state_path);
        m_last_state_error = STATE_ERR_FILE_OPEN;
        return false;
    }

    size_t actual_size = ptr - buffer;
    size_t written = f.write(buffer, actual_size);
    f.close();
    free(buffer);

    if (written != actual_size) {
        Serial.printf("saveState: Write failed (%d/%d)\n", written, actual_size);
        m_last_state_error = STATE_ERR_FILE_WRITE;
        return false;
    }

    Serial.printf("State saved: %s (%d bytes)\n", m_state_path, written);
    m_last_state_error = STATE_OK;
    return true;
}

bool rp_gbemu::loadState() {
    if (!m_initialized) {
        m_last_state_error = STATE_ERR_NOT_INIT;
        return false;
    }

    if (!g_littlefs_available) {
        m_last_state_error = STATE_ERR_NO_FS;
        return false;
    }

    if (!LittleFS.exists(m_state_path)) {
        Serial.printf("loadState: File not found: %s\n", m_state_path);
        m_last_state_error = STATE_ERR_FILE_OPEN;
        return false;
    }

    File f = LittleFS.open(m_state_path, "r");
    if (!f) {
        Serial.printf("loadState: Failed to open %s\n", m_state_path);
        m_last_state_error = STATE_ERR_FILE_OPEN;
        return false;
    }

    size_t file_size = f.size();
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) {
        f.close();
        Serial.println("loadState: Failed to allocate buffer");
        m_last_state_error = STATE_ERR_FILE_READ;
        return false;
    }

    size_t read_size = f.read(buffer, file_size);
    f.close();

    if (read_size != file_size) {
        free(buffer);
        Serial.printf("loadState: Read failed (%d/%d)\n", read_size, file_size);
        m_last_state_error = STATE_ERR_FILE_READ;
        return false;
    }

    const uint8_t* ptr = buffer;

    // 1. Validate header
    const SaveStateHeader* header = (const SaveStateHeader*)ptr;
    if (memcmp(header->magic, FCPICO_STATE_MAGIC, 8) != 0) {
        free(buffer);
        Serial.println("loadState: Invalid magic");
        m_last_state_error = STATE_ERR_CORRUPTED;
        return false;
    }
    if (header->version != FCPICO_STATE_VERSION) {
        free(buffer);
        Serial.printf("loadState: Version mismatch (%d != %d)\n", header->version, FCPICO_STATE_VERSION);
        m_last_state_error = STATE_ERR_VERSION;
        return false;
    }
    ptr += sizeof(SaveStateHeader);

    // 2. Verify ROM header matches (first 15 bytes of title)
    if (memcmp(ptr, &m_rom[ROM_HEADER_START], 15) != 0) {
        free(buffer);
        Serial.println("loadState: ROM mismatch");
        m_last_state_error = STATE_ERR_WRONG_ROM;
        return false;
    }
    ptr += ROM_HEADER_SIZE;

    // 3. Restore CPU registers
    memcpy(&gb.cpu_reg, ptr, sizeof(gb.cpu_reg));
    ptr += sizeof(gb.cpu_reg);

    // 4. Restore counters
    memcpy(&gb.counter, ptr, sizeof(gb.counter));
    ptr += sizeof(gb.counter);

    // 5. Restore flags and MBC state
    gb.gb_halt = *ptr++;
    gb.gb_ime = *ptr++;
    gb.gb_frame = *ptr++;
    gb.lcd_blank = *ptr++;
    gb.mbc = *ptr++;
    gb.cart_ram = *ptr++;
    memcpy(&gb.num_rom_banks_mask, ptr, sizeof(gb.num_rom_banks_mask));
    ptr += sizeof(gb.num_rom_banks_mask);
    gb.num_ram_banks = *ptr++;
    memcpy(&gb.selected_rom_bank, ptr, sizeof(gb.selected_rom_bank));
    ptr += sizeof(gb.selected_rom_bank);
    gb.cart_ram_bank = *ptr++;
    gb.enable_cart_ram = *ptr++;
    gb.cart_mode_select = *ptr++;
    ptr++; // padding

    // 6. Restore RTC state
    memcpy(&gb.rtc_latched, ptr, sizeof(gb.rtc_latched));
    ptr += sizeof(gb.rtc_latched);
    memcpy(&gb.rtc_real, ptr, sizeof(gb.rtc_real));
    ptr += sizeof(gb.rtc_real);

    // 7. Restore display state (preserve lcd_draw_line pointer)
    memcpy(gb.display.bg_palette, ptr, 4);
    ptr += 4;
    memcpy(gb.display.sp_palette, ptr, 8);
    ptr += 8;
    gb.display.window_clear = *ptr++;
    gb.display.WY = *ptr++;
    gb.display.frame_skip_count = *ptr++;
    gb.display.interlace_count = *ptr++;

    // 8. Restore memory
    memcpy(gb.wram, ptr, WRAM_SIZE);
    ptr += WRAM_SIZE;
    memcpy(gb.vram, ptr, VRAM_SIZE);
    ptr += VRAM_SIZE;
    memcpy(gb.oam, ptr, OAM_SIZE);
    ptr += OAM_SIZE;
    memcpy(gb.hram_io, ptr, HRAM_IO_SIZE);
    ptr += HRAM_IO_SIZE;

    free(buffer);

    Serial.printf("State loaded: %s\n", m_state_path);
    m_last_state_error = STATE_OK;
    return true;
}
