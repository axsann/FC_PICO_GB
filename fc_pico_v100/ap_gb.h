/*
    ap_gb.h - Game Boy emulation screen handler for FC PICO
*/

#ifndef ap_gb_h
#define ap_gb_h

#include "Arduino.h"

class ap_gb {
public:
    ap_gb() {};

    // Initialize GB mode
    void init();

    // Main loop - run one frame
    void main();

private:
    // Render GB frame buffer to FC frame buffer
    void renderToFC();

    // Draw border/frame around GB screen
    void drawBorder();

    // Draw "SAVED" message
    void drawSavedMessage();

    uint8_t m_sub_state;
    uint16_t m_frame_count;
    uint8_t m_saved_display_frames;  // Frames remaining to show "SAVED"
};

extern ap_gb ap_g_gb;

#endif
