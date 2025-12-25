/*
    ap_gb.cpp - Game Boy emulation screen handler for FC PICO
*/

#include "ap_gb.h"
#include "rp_gbemu.h"
#include "rp_system.h"
#include "Canvas.h"

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

    // Update joypad from FC controller
    gbemu.setJoypad(sys.getKeyNew());

    // Run one frame of GB emulation
    gbemu.runFrame();

    // Render GB frame to FC frame buffer
    renderToFC();

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
