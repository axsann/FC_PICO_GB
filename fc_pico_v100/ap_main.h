/*
    ap_main.h
 */

#ifndef ap_main_h
#define ap_main_h

#include "Arduino.h"
#include "ArduinoGL.h"
#include "Canvas.h"
#include "rp_system.h"

#include "Obj3d.h"

#include "ap_title.h"
#include "ap_game.h"
#include "ap_over.h"
#include "ap_clear.h"
#include "ap_demo0.h"
#include "ap_data.h"

enum {
	ST_INIT = 0,
	ST_TITLE,
	ST_GAME,
	ST_DEMO0,
	ST_DEMO1,
	ST_OVER,
	ST_CLEAR,

	ST_WAIT = 255,
};




enum {

	OBJ_MAX = 256,
};


class ap_main {
    
public:

    ap_main() {};
    void init();
    void initObj();

    void main();
    void move();
    void draw();

	void setStep( uint8_t step );
	void setStepSub( uint8_t step_sub );

	uint8_t getStep() { return m_step; }
	uint8_t getStepSub() { return m_step_sub; }
	Obj3d  m_obj[ OBJ_MAX ];

    void initStarObj( uint16_t StartStarObjNo, uint16_t StarObjNum, float StarSpd );
    void moveStarObj();
    void initStarObjGame( uint16_t StartStarObjNo, uint16_t StarObjNum, float StarSpd );
    void moveStarObjGame();

	uint16_t m_timer;

private:
	uint8_t m_step;
	uint8_t m_step_sub;
	uint16_t m_StartStarObjNo;
	uint16_t m_StarObjNum;
	float m_StarSpd;
	unsigned long ap_main_time;

};

extern ap_main ap;

#endif

