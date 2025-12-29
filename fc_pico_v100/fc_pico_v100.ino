/**
   Copyright (c) 2025 impact soft

*/

#include <LittleFS.h>


#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "pio/fcppu.pio.h"

#include "rp_system.h"
#include "rp_gbemu.h"

#include "Canvas.h"
#include "rp_dma.h"

#include "ap_main.h"

// GB Emulation Mode: Set to 1 to enable Game Boy mode
#define GB_EMU_MODE 1

#define LOOP_MS 1


rp_system sys;

#if SOFT_SOUND
#define TIME_INTERVAL -16  // タイマー割り込みの間隔

#include <BackgroundAudio.h>
#include <PWMAudio.h>

#include "snd/sound_drv.c"
#include "snd/song_dt0.c"

rp_sound snd;
PWMAudio pwm(0);
BackgroundAudioMP3 mp3(pwm);

#else

#define TIME_INTERVAL -500  // タイマー割り込みの間隔
//#define TIME_INTERVAL -50  // タイマー割り込みの間隔

#endif


#include "ap_core0.h"
#include "ap_core1.h"


