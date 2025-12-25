/*
    ap_demo0.h
 */

#ifndef ap_demo0_h
#define ap_demo0_h

#include "Arduino.h"
#include "ArduinoGL.h"
#include "Canvas.h"
#include "rp_system.h"

enum {
	OBJ_DEM_MODEL = 0,
	OBJ_DEM_STAR = 1,
};


class ap_demo0 {
    
public:
    ap_demo0() {};
    void init(void);
    void main();

private:

	unsigned long demo_time;
};

extern ap_demo0 ap_d0;

#endif

