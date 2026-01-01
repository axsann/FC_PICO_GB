/*
    Canvas.h - Simple canvas.
 */

#include "Arduino.h"
#include "Canvas.h"


Canvas::Canvas(void) {
}

void Canvas::clear(void) {

	uint32_t *pFB = (uint32_t *)frame_buff;
	for (int i = 0 ; i < FRAME_BUF_SIZE*2 / sizeof( uint32_t ) ; i++ ) {
		pFB[ i ] = 0;
	}
	zval_L = zval_H = 0;
}

int Canvas::width(void) {
    
    return CANVAS_WIDTH;
}

int Canvas::height(void) {
    
    return CANVAS_HEIGHT;
}

uint8_t* Canvas::bitmap(void) {
    
    return frame_buff;
}

uint8_t Canvas::getDitherCol( int x, int y ) {
	const uint16_t DitherTbl[] = {
	//FEDCBA9876543210
	0b1111111111111111,  // 0
	0b1111111111111110,  // 1
	0b1111101111111110,  // 2
	0b1111101111111010,  // 3
	0b1111101011111010,  // 4
	0b1111101011011010,  // 5
	0b0111101011011010,  // 6
	0b0111101001011010,  // 7
	0b0101101001011010,  // 8
	0b0101101001011000,  // 9
	0b0101001001011000,  // 10
	0b0101001001010000,  // 11
	0b0101000001010000,  // 12
	0b0101000001000000,  // 13
	0b0001000001000000,  // 14
	0b0001000000000000,  // 15
	};

	uint8_t c = defCol;
	if ( DitherNo == 0 ) {
	    return c;
	}
	int idx = (x & 3) | (( y & 3 ) <<2);
	if ( DitherTbl[ DitherNo ] & (1<<idx) ) {
	    return c;
	}
    return 0;
}


void Canvas::setPixel(int x, int y ) {
/*
    if((x >= 0) && (x < CANVAS_WIDTH) && (y >= 0) && (y < CANVAS_HEIGHT)) {
        frame_buff[x + y*CANVAS_WIDTH] = defCol;
    }
*/
    if((x > 0) && (x < CANVAS_WIDTH) && (y > 0) && (y < CANVAS_HEIGHT)) {
        frame_buff[x + y*CANVAS_WIDTH] = defCol;
    }
}


void Canvas::drawLine(int startX, int startY, int endX, int endY) {
    
    int diffX = (endX - startX);
    int diffY = (endY - startY);
    
    if(abs(diffX) > abs(diffY)) {
        
        float dy = diffY/(float)diffX;
        
        if(endX > startX) {
            
            for(int x = startX; x <= endX; x++) {
                
                this->setPixel(x, floor(startY + dy*(x - startX)) );
            }
        }
        
        else {
            
            for(int x = startX; x >= endX; x--) {
                
                this->setPixel(x, floor(startY + dy*(x - startX)) );
            }
        }
    }
    
    else {
        
        float dx = diffX/(float)diffY;
        
        if(endY > startY) {
            
            for(int y = startY; y <= endY; y++) {
                
                this->setPixel(floor(startX + dx*(y - startY)), y );
            }
        }
        
        else {
            
            for(int y = startY; y >= endY; y--) {
                
                this->setPixel(floor(startX + dx*(y - startY)), y );
            }
        }
    }
}

void Canvas::drawCircle(int centreX, int centreY, int radius) {
    
    radius = abs(radius);
    
    for(float angle = 0; angle < 2*M_PI; angle+=(M_PI/(4*radius))) {
        
        this->setPixel(centreX + cos(angle)*radius, centreY + sin(angle)*radius );
    }
}

void Canvas::drawSquare(int startX, int startY, int endX, int endY) {
    
    this->drawLine(startX, startY, startX, endY);
    this->drawLine(startX, endY, endX, endY);
    this->drawLine(endX, endY, endX, startY);
    this->drawLine(endX, startY, startX, startY);
}

void Canvas::drawTriangle(int x1, int y1, int x2, int y2, int x3, int y3) {
    
    this->drawLine(x1, y1, x2, y2);
    this->drawLine(x2, y2, x3, y3);
    this->drawLine(x3, y3, x1, y1);
}

//---- impact soft add -----
void Canvas::drawCHR(uint8_t c, int x, int y, const uint8_t* pData ) {

	const unsigned char* pDP = &pData[ c * 16 ];
	int idx = (x + y * 256);
	for ( int dy = 0; dy < 8 ; dy++ ) {
		uint8_t p0 = pDP[0];
		uint8_t p1 = pDP[8];
		for ( int f = 0; f < 8 ; f++ ) {
			int pos_x = x + f;
			if ( (pos_x < 0 ) || ( pos_x > 255 ) )  continue;
			uint8_t pd = (p1 & 1);
			pd <<= 1;
			pd |= (p0 & 1);
			p0 >>= 1;
			p1 >>= 1;
			if( pd != 0 ) {
				frame_buff[ (idx+7 -f) & 0xffff ] = pd;
			}
		}
		pDP++;
		idx += 256;
	}
}


void Canvas::drawSPR16( int x, int y ) {
	drawCHR( SprChr 	 , x-8, y-8, pSprData ); 
	drawCHR( SprChr +1   , x  , y-8, pSprData ); 
	drawCHR( SprChr+0x10 , x-8, y  , pSprData ); 
	drawCHR( SprChr+0x11 , x  , y  , pSprData ); 
}

void Canvas::drawSPR8( int x, int y ) {
	drawCHR( SprChr 	 , x-4, y-4, pSprData ); 
}

void Canvas::drawString( const char *str, int x, int y, const uint8_t* pData ) {
	for( int i = 0 ; ; i++ ) {
		uint8_t c = str[i];
		if ( c == 0 ) return;
		drawCHR( c, x, y, pData );
		x += 8;
	}
}

void Canvas::drawOBJ(const uint8_t* pODT, int x, int y, const uint8_t* pData ) {
	int idx = 0;
	int dx = (int)pODT[idx++];
	int dy = (int)pODT[idx++];
	for ( int oy = 0; oy < dy ; oy++ ) {
		for ( int ox = 0; ox < dx ; ox++ ) {
			drawCHR( pODT[idx++], x + ox*8, y+ oy*8	, pData );
		}
	}
}

#define swap(type,a,b) do{type _c;_c=a;a=b;b=_c;}while(0)
#define ABS(x) ((x) < 0 ? -(x) : (x))

// この関数は将来的にサブコアに負荷分散する
void Canvas::draw_Xaxis( int y, int x2, int x3 ) {
	uint8_t *vram = &frame_buff[ y * CANVAS_WIDTH ];
	uint8_t *zbuff = &frame_buff[ y * CANVAS_WIDTH + FRAME_BUF_SIZE ];
	int d = 0;
	if ( x2 > x3 ) {
		swap(int,x2,x3);
	}

	if ( x2 < 0 ) {
		x2 = 0;
	}
	if ( x3 >= CANVAS_WIDTH ) {
		x3 = CANVAS_WIDTH -1;
	}

	if ( x2 == x3 ) {
    // 書き込みが一点のケース
	    if( zbuff[x2] > zval_H ) return;
	    zbuff[x2] = zval_H;
	    if( zbuff[x2] == zval_H ) {
		    if( vram[x2] > zval_L ) return;
		}
		vram[x2] = getDitherCol( x2, y ) | zval_L;
		return;
	}

	for (int x = x2;  x <= x3; x++ ) {
	    if( zbuff[x] > zval_H ) continue;
	    if( zbuff[x] == zval_H ) {
		    if( vram[x] > zval_L ) continue;
		}
	    zbuff[x] = zval_H;
		vram[x] = getDitherCol( x, y ) | zval_L;
	}
}

// この関数は将来的にサブコアに負荷分散する
void Canvas::draw_flatTriangle( int x1, int y1, int x2, int y2, int x3 ) {
	int ady = ABS(y1 - y2);
	if ( ady == 0 ) {
	    draw_Xaxis( y2, x2, x3 );
		return;
	}

	int sdy = 1;
	if (y1 < y2) sdy = -1;

	int adxa = ((x1 - x2) << 16) / ady;
	int adxb = ((x1 - x3) << 16) / ady;
	int xa = (x2 << 16);
	int xb = (x3 << 16);
	int y = y2;

	for (int ypos = 0; ypos <= ady; y += sdy, ypos++) {
		if ( (y < 0) || ( y >= CANVAS_HEIGHT ) ) break;

		draw_Xaxis( y, (xa >> 16), (xb >> 16) );
		xa += adxa;
	    xb += adxb;
	}
}

// この関数は CPU で計算する
void Canvas::draw_triangle( int x1, int y1, int x2, int y2, int x3, int y3 ) {
  // y1 < y2 < y3 となるように並べ直す
  if (y1 > y2) {
    swap(int, x1, x2);
    swap(int, y1, y2);
  }
  if (y1 > y3) {
    swap(int, x1, x3);
    swap(int, y1, y3);
  }
  if (y2 > y3) {
    swap(int, x2, x3);
    swap(int, y2, y3);
  }

  // 例外的なパターンの排除
  if (y1 == y3) {
    return;
  }
  if (x1 == x2 && x2 == x3) {
    return;
  }

  if (y1 == y2) {
    draw_flatTriangle(x3, y3, x1, y1, x2);
  } else if (y2 == y3) {
    draw_flatTriangle(x1, y1, x2, y2, x3);
  } else {
    int xa = x3 * (y2 - y1) / (y3 - y1) + x1 * (y2 - y3) / (y1 - y3);
    draw_flatTriangle(x1, y1, xa, y2, x2);
    draw_flatTriangle(x3, y3, xa, y2, x2);

	// 描画処理上無意味なこの行を抜くと何故が表示がバグる時がある。気持ち悪いけど後回し
/*
	c.dvi0[0] = x1;
	c.dvi0[1] = x2;
	c.dvi0[2] = x3;
	c.dvi1[0] = y1;
	c.dvi1[1] = y2;
	c.dvi1[2] = y3;
*/
  }
}

/*
// この関数は CPU で計算する
void Canvas::draw_triangle2( int x1, int y1, int x2, int y2, int x3, int y3 ) {
  // y1 < y2 < y3 となるように並べ直す
  if (y1 > y2) {
    swap(int, x1, x2);
    swap(int, y1, y2);
  }
  if (y1 > y3) {
    swap(int, x1, x3);
    swap(int, y1, y3);
  }
  if (y2 > y3) {
    swap(int, x2, x3);
    swap(int, y2, y3);
  }

  // 例外的なパターンの排除
  if (y1 == y3) {
    return;
  }
  if (x1 == x2 && x2 == x3) {
    return;
  }

  if (y1 == y2) {
    draw_flatTriangle(x3, y3, x1, y1, x2);
  } else if (y2 == y3) {
    draw_flatTriangle(x1, y1, x2, y2, x3);
  } else {
    int xa = x3 * (y2 - y1) / (y3 - y1) + x1 * (y2 - y3) / (y1 - y3);
    draw_flatTriangle(x1, y1, xa, y2, x2);
    draw_flatTriangle(x3, y3, xa, y2, x2);


  }
}
*/


void Canvas::setZval( float fz ) {
	float zd = (1.0f - fz) *0x7fff;
	int z = (int)zd;
	if ( z < 0 ) z = 0;
	if ( z > 0xffff ) z = 0xffff;
	zval_L = (z & 0xfc);
	zval_H = (z >> 8);
}

void Canvas::setDitherNo( int no ) {
	no += m_DitherAdd;
	if ( no < 0 ) {
		no = 0;
	}
	if ( no > 16 ) {
		no = 15;
	}
	DitherNo = (uint8_t)no;
}	


Canvas c;

