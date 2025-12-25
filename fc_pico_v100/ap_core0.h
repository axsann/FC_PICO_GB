/**
   Copyright (c) 2025 impact soft

*/

#define SOFT_SOUND 0     // =1 USE SOFT SOUND TEST
#define DFPLAYER_MINI 0  // =1 USE DFPLAYER_MINI







//=================================================
//			LED点滅関数
//=================================================
void BLINK_LED() {
	static char led_fg = 0;
	led_fg ^= 1;
	gpio_put(LED_PIN, led_fg);
}

//=================================================
//			割り込み関数
//=================================================


// 割り込みハンドラ
void pio0_itr0() {
	// IRQ命令でセットされた値を取得
	uint32_t irq = pio0_hw->irq;
	// 各ビットは１書き込む事でクリアできます
	pio0_hw->irq = irq;

	//	Serial.println("IRQ0");
	sys.jobRcvCom();

//	sys.WDT_update();
//	BLINK_LED();
	// 割り込み要求のクリア
	irq_clear(PIO0_IRQ_0);
}

void enable_pico_ir() {
	irq_set_exclusive_handler(PIO0_IRQ_0, pio0_itr0);
	irq_set_enabled(PIO0_IRQ_0, true);
	//	pio0_hw->inte0 = PIO_IRQ0_INTE_SM0_BITS;
	pio0_hw->inte0 = PIO_IRQ0_INTE_SM0_RXNEMPTY_BITS;
}


//=================================================
//			タイマー割り込み
//=================================================

bool timer_callback(repeating_timer_t *rt) {
	BLINK_LED();
	/* 500us待機 */
	//  busy_wait_us_32( 500 );
//	WDT_check();


#if SOFT_SOUND
	// famistudio_update
	emu.run(0xE000 + 4 * 7, 0, 0x00, 0x00);
	snd.system();
#endif

	return (true);
}

void init_timer_callback() {
	static repeating_timer_t timer;
	/* インターバルタイマ設定 */
//	add_repeating_timer_ms(TIME_INTERVAL, &timer_callback, NULL, &timer);
}





//=================================================
//			GB ROM Loading Functions
//=================================================
#if GB_EMU_MODE

// Include embedded ROM data
#include "res/gbrom.c"

// Initialize GB emulator with embedded ROM
bool initGBEmulator() {
	Serial.println("Initializing GB emulator with embedded ROM");
	Serial.printf("ROM size: %lu bytes\n", gb_rom_size);

	if (gb_rom_size < 0x150) {
		Serial.println("Error: ROM too small");
		return false;
	}

	// Initialize emulator with embedded ROM
	bool result = gbemu.init(gb_rom_data, gb_rom_size);
	if (!result) {
		Serial.println("GB emulator init failed");
		return false;
	}

	Serial.println("GB emulator OK");
	return true;
}

#endif // GB_EMU_MODE

void setup() {

	Serial.begin(115200);
	sleep_ms(1800);

	sys.init();

	if (watchdog_caused_reboot()){
		Serial.printf("Rebooted by Watchdog!\n");
	}

	//	ap.init();

	enable_pico_ir();
	init_timer_callback();  // タイマー割り込み開始


	sys.frame_draw = 1;
	ap.init();

#if GB_EMU_MODE
	// Initialize LittleFS for save data
	if (!LittleFS.begin()) {
		Serial.println("LittleFS mount failed, formatting...");
		LittleFS.format();
		LittleFS.begin();
	}
	Serial.println("LittleFS initialized");

	// Initialize GB emulator and switch to GB mode
	if (initGBEmulator()) {
		Serial.println("Switching to GB emulation mode");
		ap.setStep(ST_GB);
	} else {
		Serial.println("GB mode disabled");
	}
#endif
}

void loop() {
	if( sys.frame_draw == 0 ) {
		WDT_update();
		TRACE(DTR_ROOT)
		ap.main();
		TRACE(DTR_ROOT)
		sys.frame_draw++;
		sys.update();
		TRACE(DTR_ROOT)
		TRACE_END(DTR_ROOT)
	}
	sleep_ms(LOOP_MS);
}
