/*
    ap_main.h - GB Emulation Mode
 */

#ifndef ap_main_h
#define ap_main_h

#include "Arduino.h"
#include "Canvas.h"
#include "rp_system.h"
#include "ap_data.h"
#include "ap_gb.h"

enum {
	ST_INIT = 0,
	ST_GB,        // Game Boy emulation mode
	ST_WAIT = 255,
};

class ap_main {

public:
    ap_main() {};
    void init();
    void main();

	void setStep( uint8_t step );
	uint8_t getStep() { return m_step; }

	uint16_t m_timer;

private:
	uint8_t m_step;
	uint8_t m_step_sub;
};

extern ap_main ap;

#endif
