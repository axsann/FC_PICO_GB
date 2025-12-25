/*
    ap_data.cpp
 */


#include "ap_data.h"


#include "res/OBJ.c"
#include "res/font.c"

//#include "res/Pikachu.h"
//#include "res/Bunny.h"

#include "res/fcpico.c"
//#include "res/kenlogo3.c"
#include "res/mdl_enemy.c"
#include "res/mdl_enemy_col.h"
#include "res/mdl_enemy2.c"
#include "res/mdl_enemy2_col.h"
#include "res/mdl_enemy3.c"
#include "res/mdl_enemy3_col.h"
#include "res/mdl_player.c"
#include "res/mdl_player_col.h"
#include "res/mdl_bullet.c"



void setModelDataObj( Obj3d *obj, int no ) {
	obj->init();
	switch( no ) {
	case MDL_TITLE_LOGO:
		obj->setModelData( FCPICO, DM_FCPICO );
		break;
	case MDL_PLAYER_NO:
		obj->setModelData( MDL_PLAYER, DM_MDL_PLAYER, mdl_player_col );
		obj->m_angle_x = 64;
		obj->m_scale = 0.7;
//		obj->m_scale = 2.0;
		break;

	case MDL_ENEMY_NO:
		obj->setModelData( MDL_ENEMY, DM_MDL_ENEMY, mdl_enemy_col );
		obj->m_angle_x = -64;
		obj->m_scale = 0.7;
		break;

	case MDL_ENEMY2_NO:
		obj->setModelData( MDL_ENEMY2, DM_MDL_ENEMY2, mdl_enemy2_col );
		obj->m_angle_x = -64;
		obj->m_scale = 0.7;
		break;

	case MDL_ENEMY3_NO:
		obj->setModelData( MDL_ENEMY3, DM_MDL_ENEMY3, mdl_enemy3_col );
		obj->m_angle_x = -64;
		obj->m_scale = 0.7;
		break;
/*
	case MDL_KENLOGO_NO:
		obj->setModelData( KENLOGO3, DM_KENLOGO3 );
		obj->m_angle_z = 128;
;		obj->m_angle_x = 64;
		obj->m_scale = 0.08f;
		obj->m_color = 2;
		break;
	case MDL_BUNNY_NO:
		obj->setModelData( model_B, sizeof(model_B)/sizeof(float)/9 );
		obj->m_angle_z = 128;
;		obj->m_angle_x = 64;
		obj->m_scale = 0.07f;
		obj->m_color = 1;
		obj->m_y = -2.0f;
		break;
	case MDL_PIKACYU_NO:
		obj->setModelData( model, sizeof(model)/sizeof(float)/9 );
		obj->m_angle_z = 128;
		obj->m_angle_x = 64;
		obj->m_scale = 0.11f;
		obj->m_color = 2;
		obj->m_y = -2.0f;
		break;
*/
	}


}

