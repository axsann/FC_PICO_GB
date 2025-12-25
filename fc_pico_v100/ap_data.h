/*
    ap_data.h
 */

#ifndef ap_data_h
#define ap_data_h

#include "Arduino.h"
#include "ArduinoGL.h"
#include "Canvas.h"
#include "rp_system.h"
#include "Obj3d.h"

enum {
	MDL_TITLE_LOGO = 0,
	MDL_PLAYER_NO,
	MDL_ENEMY_NO,
	MDL_ENEMY2_NO,
	MDL_ENEMY3_NO,

};

extern const unsigned char _font[4096];
extern const unsigned char _acOBJ[4096];
extern void setModelDataObj( Obj3d *obj, int no );


#endif

