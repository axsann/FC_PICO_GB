/*
    ap_main.cpp - GB Emulation Mode
 */

#include "ap_main.h"

ap_main ap;

void ap_main::init() {
	Serial.printf("ap_main::init\n");
	setStep(ST_INIT);
}

void ap_main::setStep(uint8_t step) {
	m_step = step;
	m_step_sub = 0;
}

void ap_main::main() {
	sys.setKeyUpdate();

	switch (m_step) {
	case ST_INIT:
		setStep(ST_GB);
		break;

	case ST_GB:
		if (m_step_sub == 0) {
			ap_g_gb.init();
			m_step_sub++;
		} else {
			ap_g_gb.main();
		}
		break;

	case ST_WAIT:
		break;

	default:
		setStep(ST_INIT);
		break;
	}

	ap.m_timer++;
}
