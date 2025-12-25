// Core1: Background tasks

#include "ap_main.h"

void setup1() {
	Serial.printf("ap_core1:setup1\n");
}

void loop1() {
	WDT_check();
	sleep_ms(LOOP_MS);
}

