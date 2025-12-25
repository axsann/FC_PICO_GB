/*
    ap_clear.h
 */

#ifndef ap_clear_h
#define ap_clear_h

#include "Arduino.h"
#include "ArduinoGL.h"
#include "Canvas.h"
#include "rp_system.h"

enum {
	OBJ_CLR_MODEL = 0,
	OBJ_CLR_STAR = 1,
};


class ap_clear {
    
public:
    ap_clear() {};
    void init(void);
    void main();

private:

	unsigned long demo_time;
};

extern ap_clear ap_cl;

#endif

