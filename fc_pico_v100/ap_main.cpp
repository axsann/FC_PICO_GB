/*
    rp_system.h
 */


#include "ap_main.h"
#include "res/gamerom.c"


ap_main ap;




void ap_main::init() {
	Serial.printf("ap_main::init\n" );
	setStep( 0 );
	initObj();
	memset( emu.m_RAM, 0, 0x800);
	emu.setROM( gamerom );
	emu.setROM( gamerom );
	emu.reset();

}


void ap_main::initObj() {
	for( int i = 0 ; i < OBJ_MAX; i++ ) {
		m_obj[i].init();
	}
}

void ap_main::setStep( uint8_t step ) {
//	Serial.printf("setStep %d\n", step );

	m_step = step;
	m_step_sub = 0;
}
void ap_main::setStepSub( uint8_t step_sub ) {
	m_step_sub = step_sub;
}


void ap_main::main() {
	TRACE(DTR_MAIN)
	unsigned long ap_main_time_old = ap_main_time;
	ap_main_time = micros();

	sys.setKeyUpdate();
	switch ( m_step ) {
	case ST_INIT:
		TRACE(DTR_MAIN)
		setStep( ST_TITLE );
		break;

	case ST_TITLE:
		if ( m_step_sub == 0 ) {
			TRACE(DTR_MAIN)
			ap_t.init();
			m_step_sub++;
		} else {
			TRACE(DTR_MAIN)
			ap_t.main();
		}
		break;
	case ST_GAME:
		if ( m_step_sub == 0 ) {
			TRACE(DTR_MAIN)
			ap_g.init();
			m_step_sub++;
		} else {
			TRACE(DTR_MAIN)
			ap_g.main();
		}
		break;
	case ST_DEMO0:
		if ( m_step_sub == 0 ) {
			emu.m_RAM[EM_PLY_STAGE]++;
			if ( emu.m_RAM[EM_PLY_STAGE] >= STAGE_MAX ) {
				emu.m_RAM[EM_PLY_STAGE] = 1;
			}

			TRACE(DTR_MAIN)
			ap_g.init();
			m_step_sub++;
		} else {
			TRACE(DTR_MAIN)
			if ( sys.getKeyTrg() ) {
				sys.setPF_COM( PF_COM_BGM + BGM_STOP );
				sys.FadeOut();
				sys.SleepMS( 500 );
				ap.setStep( ST_TITLE );
				return;
			}
			if ( (ap.m_timer > 60*20 ) || sys.getKeyTrg() ) {
				sys.setPF_COM( PF_COM_BGM + BGM_STOP );
				sys.FadeOut();
				sys.SleepMS( 500 );
				ap.setStep( ST_DEMO1 );
				return;
			}
			ap_g.main();
		}
		break;
	case ST_DEMO1:
		if ( m_step_sub == 0 ) {
			ap_d0.init();
			m_step_sub++;
		} else {
			if ( (ap.m_timer > 60*5 ) || sys.getKeyTrg() ) {
				sys.setPF_COM( PF_COM_BGM + BGM_STOP );
				sys.FadeOut();
				sys.SleepMS( 500 );
				ap.setStep( ST_TITLE );
				return;
			}
			ap_d0.main();
		}
		break;
	case ST_OVER:
		if ( m_step_sub == 0 ) {
			TRACE(DTR_MAIN)
			ap_ov.init();
			m_step_sub++;
		} else {
			TRACE(DTR_MAIN)
			ap_ov.main();
		}
		break;
	case ST_CLEAR:
		if ( m_step_sub == 0 ) {
			TRACE(DTR_MAIN)
			ap_cl.init();
			m_step_sub++;
		} else {
			TRACE(DTR_MAIN)
			ap_cl.main();
		}
		break;
	case ST_WAIT:
		break;
	
	default:
		setStep( ST_INIT );
		break;
	}
//	TRACE_END(DTR_MAIN)

#if 0
	{
		char buffer[40]; // バッファを確保
		sprintf(buffer, "FPS:%8d %8d", micros() - ap_main_time,  ap_main_time - ap_main_time_old );
		c.drawString( buffer, 40, 40, _font );
	}
#endif
	ap.m_timer++;
}

void ap_main::move(void) {
	for( int i = 0 ; i < OBJ_MAX; i++ ) {
		m_obj[i].move();
	}
}

void ap_main::draw(void) {
	for( int i = 0 ; i < OBJ_MAX; i++ ) {
		m_obj[i].draw();
	}
}


void ap_main::initStarObj( uint16_t StartStarObjNo, uint16_t StarObjNum, float StarSpd ) {
	m_StartStarObjNo = StartStarObjNo;
	m_StarObjNum = StarObjNum;
	m_StarSpd = StarSpd;
	
	for( int i=0 ; i< m_StarObjNum ; i++ ) {
		ap.m_obj[ m_StartStarObjNo +i ].m_mode = OMD_PSET;
		ap.m_obj[ m_StartStarObjNo +i ].m_color = 3;
		
		ap.m_obj[ m_StartStarObjNo +i ].m_x = -20.0f + random( 0, 255) * (20.0f*2 / 256);
		ap.m_obj[ m_StartStarObjNo +i ].m_y = -16.0f + random( 0, 255) * (16.0f*2 / 256);
		ap.m_obj[ m_StartStarObjNo +i ].m_z = 1.0f + i * 1.0f;
		ap.m_obj[ m_StartStarObjNo +i ].m_vz = m_StarSpd;
	}
}



/* 実験の残骸
	for( int i=0 ; i<80 ; i++ ) {
//	for( int i=0 ; i<1 ; i++ ) {
//		ap.m_obj[ OBJ_TIT_CUBE +i ].m_scale = 0.5f;
//		ap.m_obj[ OBJ_TIT_CUBE +i ].m_mode = OMD_CUBE;
		ap.m_obj[ OBJ_TIT_CUBE +i ].m_mode = OMD_PSET;
		ap.m_obj[ OBJ_TIT_CUBE +i ].m_color = 3;
//		ap.m_obj[ OBJ_TIT_CUBE +i ].setAngle( 32, 32, 32 );
//		ap.m_obj[ OBJ_TIT_CUBE +i ].m_angle_vx = random( -14, 14);
//		ap.m_obj[ OBJ_TIT_CUBE +i ].m_angle_vy = random( -14, 14);
		
		ap.m_obj[ OBJ_TIT_CUBE +i ].m_x = -20.0f + random( 0, 255) * (20.0f*2 / 256);
		ap.m_obj[ OBJ_TIT_CUBE +i ].m_y = -16.0f + random( 0, 255) * (16.0f*2 / 256);
		ap.m_obj[ OBJ_TIT_CUBE +i ].m_z = 1.0f + i * 1.0f;
		ap.m_obj[ OBJ_TIT_CUBE +i ].m_vz = -2.0f;
	}
*/


void ap_main::moveStarObj() {
	for( int i=0 ; i< m_StarObjNum ; i++ ) {
		if ( ap.m_obj[ m_StartStarObjNo +i ].m_z < 0.0f ) {
//		if ( ap.m_obj[ m_StartStarObjNo +i ].m_z < 0.5f ) {
			ap.m_obj[ m_StartStarObjNo +i ].m_x = -20.0f + (random( 0, 255) * (20.0f*2 / 256));
			ap.m_obj[ m_StartStarObjNo +i ].m_y = -16.0f + (random( 0, 255) * (16.0f*2 / 256));
			ap.m_obj[ m_StartStarObjNo +i ].m_z = 1.0f + 12 * 8.0f;
		}
	}
}

void ap_main::initStarObjGame( uint16_t StartStarObjNo, uint16_t StarObjNum, float StarSpd ) {
	m_StartStarObjNo = StartStarObjNo;
	m_StarObjNum = StarObjNum;
	m_StarSpd = StarSpd;
	
	for( int i=0 ; i< m_StarObjNum ; i++ ) {
		ap.m_obj[ m_StartStarObjNo +i ].m_mode = OMD_PSET;
		ap.m_obj[ m_StartStarObjNo +i ].m_color = 3;
		
		ap.m_obj[ m_StartStarObjNo +i ].m_x = -12.0f + random( 0, 255) * (12.0f*2 / 256);
		ap.m_obj[ m_StartStarObjNo +i ].m_y = -16.0f + random( 0, 255) * (8.0f / 256);
		ap.m_obj[ m_StartStarObjNo +i ].m_z = 1.0f + i * 1.0f;
		ap.m_obj[ m_StartStarObjNo +i ].m_vz = m_StarSpd;
	}
}

void ap_main::moveStarObjGame() {
	for( int i=0 ; i< m_StarObjNum ; i++ ) {
		if ( ap.m_obj[ m_StartStarObjNo +i ].m_z < -0.5f ) {
//		if ( ap.m_obj[ m_StartStarObjNo +i ].m_z < 0.5f ) {
			ap.m_obj[ m_StartStarObjNo +i ].m_x = -12.0f + random( 0, 255) * (12.0f*2 / 256);
			ap.m_obj[ m_StartStarObjNo +i ].m_y = -16.0f + random( 0, 255) * (8.0f / 256);
			ap.m_obj[ m_StartStarObjNo +i ].m_z = 1.0f + 12 * 8.0f;
		}
	}
}

