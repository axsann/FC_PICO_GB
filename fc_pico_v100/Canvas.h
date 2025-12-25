/*
    Canvas.h - Simple canvas.
 */

#ifndef Canvas_h
#define Canvas_h

#include "Arduino.h"

#define CANVAS_WIDTH 256
#define CANVAS_HEIGHT 240
#define FRAME_BUF_SIZE (CANVAS_WIDTH * CANVAS_HEIGHT)

class Canvas {
    
public:
    Canvas();
    void clear();
    void setPixel(int x, int y );
    void drawLine(int startX, int startY, int endX, int endY);
    void drawCircle(int centreX, int centreY, int radius);
    void drawSquare(int startX, int startY, int endX, int endY);
    void drawTriangle(int x1, int y1, int x2, int y2, int x3, int y3);

	void draw_triangle( int x1, int y1, int x2, int y2, int x3, int y3);
//	void draw_triangle2( int x1, int y1, int x2, int y2, int x3, int y3);

	void drawSPR16( int x, int y );
	void drawSPR8( int x, int y );
	void drawCHR( uint8_t c, int x, int y, const uint8_t* pData );
	void drawString( const char *str, int x, int y, const uint8_t* pData );
	void drawOBJ( const uint8_t* pODT, int x, int y, const uint8_t* pData );
    
    uint8_t* bitmap();
    
    int width();
    int height();

    void setDefCol( uint8_t c ) {
		defCol = c;
	}
    void setDitherAdd( int add ) {
		m_DitherAdd = add;
	}
    void setZval( float fz );
    void setDitherNo( int no );
    uint8_t getDitherCol( int x, int y );
	
	uint8_t debug_u8t;
	float debug_flt;
	float debug_f2;
	float debug_f3;
	float dv0[3];
	float dv1[3];
	int dvi0[3];
	int dvi1[3];
	int  m_DitherAdd;
	uint8_t SprChr;
	const uint8_t *pSprData;

	
private:
	void draw_Xaxis( int y, int x2, int x3 );
	void draw_flatTriangle( int x1, int y1, int x2, int y2, int x3);
	uint8_t frame_buff[FRAME_BUF_SIZE *2];
	uint8_t defCol;
	uint8_t DitherNo;
	uint8_t zval_L;
	uint8_t zval_H;

};

extern Canvas c;

#endif

