/*
    ap_gb.h - Game Boy emulation screen handler for FC PICO
*/

#ifndef ap_gb_h
#define ap_gb_h

#include "Arduino.h"

// Status message types for display
enum StatusMessage {
    STATUS_NONE = 0,
    STATUS_RAM_SAVED,
    STATUS_STATE_SAVED,
    STATUS_STATE_LOADED,
    STATUS_STATE_NO_DATA,
    STATUS_STATE_NO_FS,
    STATUS_STATE_ERROR
};

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

    // Draw status message
    void drawStatusMessage();

    uint8_t m_sub_state;
    uint16_t m_frame_count;
    StatusMessage m_status_message;
    uint8_t m_status_display_frames;  // Frames remaining to show message
};

extern ap_gb ap_g_gb;

#endif
