/*
    ap_gb.cpp - Game Boy emulation screen handler for FC PICO
*/

#include "ap_gb.h"
#include "rp_gbemu.h"
#include "rp_system.h"
#include "Canvas.h"
#include "ap_data.h"

// FC display palette for GB (grayscale)
static const uint8_t GB_FC_PALETTE[] = {
    0x0F,  // Color 0: Black
    0x00,  // Color 1: Dark Gray
    0x10,  // Color 2: Light Gray
    0x30,  // Color 3: White
};

ap_gb ap_g_gb;

void ap_gb::init() {
    m_sub_state = 0;
    m_frame_count = 0;
    m_status_message = STATUS_NONE;
    m_status_display_frames = 0;

    // Clear FC frame buffer
    uint8_t* fc_fb = c.bitmap();
    memset(fc_fb, 3, CANVAS_WIDTH * CANVAS_HEIGHT);

    // Set palette for GB display
    uint8_t pal_data[0x20];
    memset(pal_data, 0x0F, 0x20);

    // Background palette 0 - GB grayscale
    pal_data[0] = GB_FC_PALETTE[0];
    pal_data[1] = GB_FC_PALETTE[1];
    pal_data[2] = GB_FC_PALETTE[2];
    pal_data[3] = GB_FC_PALETTE[3];

    sys.setPalData(pal_data);
    sys.forcePalUpdate();
    sys.clearAtrData();
    sys.forceAtrUpdate();

    // Start data mode - required for FC PICO display
    sys.startDataMode();

    // Draw border around GB screen area
    drawBorder();
}

void ap_gb::main() {
    if (!gbemu.isInitialized()) {
        m_frame_count++;
        return;
    }

    uint8_t key_now = sys.getKeyNew();    // Currently held keys
    uint8_t key_trg = sys.getKeyTrg();    // Just pressed this frame

    // SELECT is held - check for button combinations
    if (key_now & 0x20) {
        // SELECT + A (just pressed): Save state
        if (key_trg & 0x80) {
            if (gbemu.saveState()) {
                m_status_message = STATUS_STATE_SAVED;
            } else if (gbemu.getLastStateError() == STATE_ERR_NO_FS) {
                m_status_message = STATUS_STATE_NO_FS;
            } else {
                m_status_message = STATUS_STATE_ERROR;
            }
            m_status_display_frames = 60;
        }
        // SELECT + B (just pressed): Load state
        else if (key_trg & 0x40) {
            if (gbemu.loadState()) {
                m_status_message = STATUS_STATE_LOADED;
            } else if (gbemu.getLastStateError() == STATE_ERR_NO_FS) {
                m_status_message = STATUS_STATE_NO_FS;
            } else if (gbemu.getLastStateError() == STATE_ERR_FILE_OPEN) {
                m_status_message = STATUS_STATE_NO_DATA;
            } else {
                m_status_message = STATUS_STATE_ERROR;
            }
            m_status_display_frames = 60;
        }
        // SELECT + START (just pressed): Save RAM
        else if (key_trg & 0x10) {
            if (gbemu.isSaveDirty()) {
                gbemu.saveSave();
                m_status_message = STATUS_RAM_SAVED;
                m_status_display_frames = 60;
            }
        }

        // Don't pass SELECT combo buttons to game
        gbemu.setJoypad(key_now & 0x0F);  // Only pass direction keys
    } else {
        // Normal input - pass all keys to game
        gbemu.setJoypad(key_now);
    }

    // Run one frame of GB emulation
    gbemu.runFrame();

    // Render GB frame to FC frame buffer
    renderToFC();

    // Show status message if active
    if (m_status_display_frames > 0) {
        drawStatusMessage();
        m_status_display_frames--;
        if (m_status_display_frames == 0) {
            m_status_message = STATUS_NONE;
        }
    }

    m_frame_count++;
}

void ap_gb::renderToFC() {
    uint8_t* gb_fb = gbemu.getFrameBuffer();
    uint8_t* fc_fb = c.bitmap();

    // Copy GB pixels to FC frame buffer with offset
    for (int y = 0; y < GB_LCD_HEIGHT; y++) {
        int fc_y = y + GB_OFFSET_Y;
        uint8_t* src = &gb_fb[y * GB_LCD_WIDTH];
        uint8_t* dst = &fc_fb[fc_y * CANVAS_WIDTH + GB_OFFSET_X];

        for (int x = 0; x < GB_LCD_WIDTH; x++) {
            dst[x] = src[x] & 0x03;
        }
    }
}

void ap_gb::drawBorder() {
    uint8_t* fc_fb = c.bitmap();

    // Top border
    for (int y = 0; y < GB_OFFSET_Y; y++) {
        memset(&fc_fb[y * CANVAS_WIDTH], 0, CANVAS_WIDTH);
    }

    // Bottom border
    for (int y = GB_OFFSET_Y + GB_LCD_HEIGHT; y < CANVAS_HEIGHT; y++) {
        memset(&fc_fb[y * CANVAS_WIDTH], 0, CANVAS_WIDTH);
    }

    // Left and right borders
    for (int y = GB_OFFSET_Y; y < GB_OFFSET_Y + GB_LCD_HEIGHT; y++) {
        memset(&fc_fb[y * CANVAS_WIDTH], 0, GB_OFFSET_X);
        memset(&fc_fb[y * CANVAS_WIDTH + GB_OFFSET_X + GB_LCD_WIDTH], 0,
               CANVAS_WIDTH - GB_OFFSET_X - GB_LCD_WIDTH);
    }

    // Frame around GB screen
    for (int x = GB_OFFSET_X - 1; x <= GB_OFFSET_X + GB_LCD_WIDTH; x++) {
        if (x >= 0 && x < CANVAS_WIDTH) {
            fc_fb[(GB_OFFSET_Y - 1) * CANVAS_WIDTH + x] = 2;
            fc_fb[(GB_OFFSET_Y + GB_LCD_HEIGHT) * CANVAS_WIDTH + x] = 2;
        }
    }
    for (int y = GB_OFFSET_Y - 1; y <= GB_OFFSET_Y + GB_LCD_HEIGHT; y++) {
        if (y >= 0 && y < CANVAS_HEIGHT) {
            fc_fb[y * CANVAS_WIDTH + GB_OFFSET_X - 1] = 2;
            fc_fb[y * CANVAS_WIDTH + GB_OFFSET_X + GB_LCD_WIDTH] = 2;
        }
    }
}

void ap_gb::drawStatusMessage() {
    if (m_status_message == STATUS_NONE) return;

    // Fill GB screen area with black
    uint8_t* fc_fb = c.bitmap();
    for (int y = 0; y < GB_LCD_HEIGHT; y++) {
        memset(&fc_fb[(GB_OFFSET_Y + y) * CANVAS_WIDTH + GB_OFFSET_X], 0, GB_LCD_WIDTH);
    }

    // Select message text based on status
    const char* text;
    int start_x;
    switch (m_status_message) {
        case STATUS_RAM_SAVED:
            text = "SAVED";
            start_x = GB_OFFSET_X + 60;  // (160 - 5*8) / 2 = 60
            break;
        case STATUS_STATE_SAVED:
            text = "STATE SAVED";
            start_x = GB_OFFSET_X + 36;  // (160 - 11*8) / 2 = 36
            break;
        case STATUS_STATE_LOADED:
            text = "STATE LOADED";
            start_x = GB_OFFSET_X + 32;  // (160 - 12*8) / 2 = 32
            break;
        case STATUS_STATE_NO_DATA:
            text = "NO DATA";
            start_x = GB_OFFSET_X + 52;  // (160 - 7*8) / 2 = 52
            break;
        case STATUS_STATE_NO_FS:
            text = "NO FS";
            start_x = GB_OFFSET_X + 60;  // (160 - 5*8) / 2 = 60
            break;
        case STATUS_STATE_ERROR:
            text = "ERROR";
            start_x = GB_OFFSET_X + 60;  // (160 - 5*8) / 2 = 60
            break;
        default:
            return;
    }

    int start_y = GB_OFFSET_Y + 68;  // Centered vertically

    // Draw text in center of GB screen area (1x scale, 8x8 font)
    for (int i = 0; text[i] != '\0'; i++) {
        uint8_t ch = text[i];
        const uint8_t* glyph = &_font[ch * 16];  // 16 bytes per char (8 bytes plane0, 8 bytes plane1)

        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];  // Use first plane (bytes 0-7)
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    int px = start_x + i * 8 + col;
                    int py = start_y + row;
                    fc_fb[py * CANVAS_WIDTH + px] = 3;
                }
            }
        }
    }
}
