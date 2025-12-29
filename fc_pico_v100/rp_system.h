/*
    rp_system.h
 */

#ifndef rp_system_h
#define rp_system_h

#include "Arduino.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
//#include "hardware/address_map.h"	// アドレスマップ定義
#include <hardware/watchdog.h>

#include "rp_debug.h"

#if DFPLAYER_MINI

#include <DFRobotDFPlayerMini.h>

#endif

#include "rp_dma.h"
#include "ap_main.h"

//---------------------------------------
// GPIO
//---------------------------------------

#define LED_COUNT 1           // LEDの連結数
#define BRIGHTNESS 32        // 輝度

#define	LED_PIN 25
#define	LED_PIN2 24
#define	USRKEY_PIN 24
//#define DIN_PIN 23            // NeoPixel　の出力ピン番号はGP23


#define	PI_WR		(1<<PI_WR_BIT)
#define	PI_RD		(1<<PI_RD_BIT)
//#define	PI_PA13		(1<<PI_PA13_BIT)
//#define	PI_PA12		(1<<PI_PA12_BIT)
#define	PI_CS1		(1<<PI_CS1_BIT)
//#define	PI_OD_DIR	(1<<PI_OD_DIR_BIT)
#define	PI_DATA_SHIFT	PI_D0_BIT
#define	PI_DATA_MASK	(0x00FF<<PI_DATA_SHIFT)

//#define	PI_INP_PINS		(PI_WR|PI_RD|PI_PA13|PI_PA12|PI_CS1|PI_OD_DIR)
#define	PI_INP_PINS		(PI_WR|PI_RD|PI_CS1)
#define	PI_BIDIR_PINS	(PI_DATA_MASK)


//---------------------------------------
// ステートマシン関連
//---------------------------------------
#define PIO_NO_0	0
#define PIO_NO_1	1
#define PIO_NO_2	2

#define	SM_RECV	0
#define	SM_TRAN	1
#define	SM_BUSDIR	2
#define	SM_TRCNT	3


//---------------------------------------
// PICO->FC command
//---------------------------------------
enum{
	PF_COM_NONE = 0,		// コマンドなし
	PF_COM_DMOD = 1,		// 表示OFFにしてデータ転送モードへ
	PF_COM_FDIN = 2,		// フェードイン	処理終了　FP_COM_ACK
	PF_COM_FDOT = 3,		// フェードアウト	処理終了　FP_COM_ACK

	PF_COM_SE   = 0x80,		// SEセット:0x80 + SE_NO
	PF_COM_VRAM = 0xC0,		// VRAM 書き換え:adrH,ardL,dt


	// データモードコマンド
	PF_DAT_VRAM = 0x80,		//  VRAM 書き換え:adrH,ardL,size,data....
							//  --> size = 0 は256バイト 256バイト以上送りたい場合は分割して送る
	PF_DAT_RAM  = 0x81, 	//  VRAM 書き換え:adrH,ardL,size,data....

	PF_DAT_STEP = 0x82, 	//  データモードを抜けてファミコンの指定ステップへ

	PF_MAGIC_NO = 0xFC	// 受け取ったコマンドの可否チェックコード
};

//---------------------------------------
// FC->PICO command
//---------------------------------------
enum{
//	FP_COM_ACK	= 0x0F,		// FCからのコマンド正常終了応答
//	FP_COM_NAK	= 0x1F,		// FCからのコマンド失敗終了応答
	FP_COM_VER	= 0x2F,		// BIOS-ROM romvarsion
	FP_COM_ROM	= 0x3F,		// BIOS-ROM romdeta load

	FP_COM_LOG	= 0xBF,		// debug log
	FP_COM_DRQ	= 0xCF,		// data request
	FP_COM_DLD	= 0xDF,		// data load
	FP_COM_RST	= 0xEF,		// PICO RESTART
	FP_COM_INI	= 0xFF,		// PIC INIT
};


//---------------------------------------
// Sound Effect NO
//---------------------------------------
enum{
	SE_TOP_NO = 1,

	SE_CUR_SEL	= (SE_TOP_NO+0),	// カーソル 移動
	SE_CUR_ENT	= (SE_TOP_NO+1),	// カーソル 決定
	SE_CUR_CAN	= (SE_TOP_NO+2),	// カーソル キャンセル　(オプション 使用)
	SE_SHOT_A	= (SE_TOP_NO+3),	// 自機ショット音
	SE_PLY_DAME	= (SE_TOP_NO+4),	// 自機ダメージ音
	SE_PLY_DEAD	= (SE_TOP_NO+5),	// 自機死亡
	SE_PLY_FORM	= (SE_TOP_NO+6),	// 自機フォーメーションチェンジ

	SE_BAKU_S	= (SE_TOP_NO+7),	// 敵 撃破 敵サイズ小 ザコ
	SE_BAKU_M	= (SE_TOP_NO+8),	// 敵 撃破 敵サイズ中 ザコ
	SE_BAKU_L	= (SE_TOP_NO+9),	// 敵 撃破 敵サイズ大 ボス

	SE_NO_DAME	= (SE_TOP_NO+10),	// 敵無敵音
	SE_DAME		= (SE_TOP_NO+11),	// ダメージ受け音

	SE_BOSS_MOVE1 = (SE_TOP_NO+12),	// ボス移動1
	SE_BOSS_MOVE2 = (SE_TOP_NO+13),	// ボス移動2

	SE_BOSS_ATK1 = (SE_TOP_NO+14),	// ボス攻撃
	SE_TITLE	 = (SE_TOP_NO+15),	// タイトルＳＥ
	SE_START_JET = (SE_TOP_NO+16),	// スタートジェット
	SE_HADOU_CHG = (SE_TOP_NO+17),	// 波動砲　チャージ
	SE_HADOU_SHT = (SE_TOP_NO+18),	// 波動砲　発射
	SE_DM_DIVE	 = (SE_TOP_NO+19),	// 次元潜航
	SE_YAMATO_S	 = (SE_TOP_NO+20),  // ヤマト発進

	SNDTST_MAX = (SE_TOP_NO+21),

	SE_TITLE_START = SE_CUR_ENT,
	SE_POWUP	   = SE_CUR_ENT,
	SE_BAKU_EFC	   = SE_BAKU_S,
	SE_BAKU_BG	   = SE_BAKU_M,
	SE_SPECIAL	   = SE_PLY_FORM

};


//---------------------------------------
// FCキー入力定義
//---------------------------------------
#define KEY_A		0x80
#define KEY_B		0x40
#define KEY_SEL		0x20
#define KEY_RUN		0x10
#define KEY_UP		0x08
#define KEY_DOWN	0x04
#define KEY_LEFT	0x02
#define KEY_RIGHT	0x01


//---------------------------------------
// フェードイン、アウト
//---------------------------------------
#define FADE_WAIT	8


//---------------------------------------
// FC STEP
//---------------------------------------
#define FCST_OPTION  3

//---------------------------------------
//---------------------------------------
#define MICROS_1S  (1000*1000)
#define MICROS_1MS  (1000)

#define FC_COM_BUF_SIZE	16


#define PPU_COUNT_VAL	(15426 + FC_COM_BUF_SIZE)




//=================================================
//			仮想VRAM関連
//=================================================

#define VRAM_BUF_SIZE ((36 * 2 * 240 + FC_COM_BUF_SIZE) / sizeof(uint32_t))
//#define VRAM_BUF_SIZE ((32 * 2 * 240) / sizeof(uint32_t))


class rp_system {
    
public:
    rp_system();
    void init(void);
    void init2();
    void update(void);
	void soft_reset();
    void initVram();
    void convVram();
	void jobRcvCom();

    void ppu_dma(void);
    void ver_dma();

	void FadeIn();
	void FadeOut();
	void SleepMS( int ms );

    void playSE( uint8_t seno );

	// APU コマンド関数
	void queueApuWrite(uint8_t reg, uint8_t value);
	void sendApuCommands();


	void setKeyData( uint8_t key ) { m_key_imp = key; }
	void setKeyUpdate();
	bool setPF_COM( uint8_t com );
	bool setPF_VRAM( uint16_t vadr, uint8_t dt );
    void startDataMode(void);

    uint8_t  getKeyNew(void) { return m_key_new; }
    uint8_t  getKeyTrg(void) { return m_key_trg; }
    uint8_t  getKeyRep(void) { return m_key_rep; }

//    void setScreenData( const uint8_t *paldt,  );

	void setPalData( const uint8_t *paldt );
	void forcePalUpdate();  // Force palette to be sent on next update
	void setPal( uint8_t idx, uint8_t dt );
	void setAtrData( const uint8_t *atrdt );
	void clearAtrData( void );
	void forceAtrUpdate();  // Force ATR to be sent on next update
	void setAtr( uint8_t lx, uint8_t ly, uint8_t dt );
	void setFcStep( uint8_t step ) { m_FC_STEP = step; }

	uint32_t ppu_count;
	uint8_t frame_draw;

private:
	void initFC_COM_BUF();
	void jobFP_COM_DRQ();
	void jobFP_COM_DLD( uint8_t adrh );
    void rom_dma( uint8_t adrh );
	void drq_ret( uint8_t com, uint16_t adr, uint16_t size );

	uint8_t getRcvCom();

	uint8_t m_waitFP_COM_DRQ;

	uint8_t m_key_imp;
	uint8_t m_key_new;
	uint8_t m_key_trg;
	uint8_t m_key_rep;
	uint8_t m_key_old;
	uint32_t ppu_count_old;
	uint8_t m_fade_wait;

	uint8_t *m_pDRQ;

	uint8_t m_PAL_W[0x20];
	uint8_t m_PAL_W_old[0x20];
	uint8_t m_PAL_CHG;

	uint8_t m_ATR_W[0x40];
	uint8_t m_ATR_W_old[0x40];
	uint8_t m_ATR_CHG;

	uint8_t FC_COM_BUF[ FC_COM_BUF_SIZE ];
	uint8_t m_FC_COM_IDX;

	bool m_apuSupported;	// FC ROM が APU 対応かどうか

	// APU レジスタバッファ
	static const int APU_REG_COUNT = 24;  // $4000-$4017
	uint8_t m_apuRegLatest[APU_REG_COUNT];


	uint32_t vram_buf0[VRAM_BUF_SIZE];
	uint32_t vram_buf1[VRAM_BUF_SIZE];	// バックバッファ

	uint32_t *vram_buf;
	uint32_t *vram_bufDraw;

	uint8_t *vram;

	uint8_t m_FC_STEP;

	rp_dma vram_dma;

#if DFPLAYER_MINI
	DFRobotDFPlayerMini mp3;
#endif

};

extern rp_system sys;

#endif

