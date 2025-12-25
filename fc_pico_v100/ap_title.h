#include <dummy_rp2350.h>

/*
    ap_title.h
 */

#ifndef ap_title_h
#define ap_title_h

enum {
	OBJ_TIT_LOGC = 0,
	OBJ_TIT_CUBE = 1,
};


class ap_title {
    
public:
    ap_title() {};
    void init(void);
    void main();

private:
    void resetDemoTime();

	uint8_t title_sel;
	unsigned long demo_time;
};

extern ap_title ap_t;

#endif

