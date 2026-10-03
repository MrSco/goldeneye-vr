#include "net_game.h"
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "cheat.h"
#include "chrobjdata.h"
#include "system.h"
#include "player.h"
#include "gun.h"
#include "propobj.h"
#include "objecthandler.h"
#include <math.h>
#include <string.h>
#define EXPORT __declspec(dllexport)
static int online, flags, gun;
int VrGunSizeCheat;
s32 debug_VisCVG_flag;
u8 g_CheatPlayerTextRelated[CHEAT_INVALID+1];
struct ChrModelFileRecord c_item_entries[2];
bool netIsActive(void) { return online; }
int netActiveFunFlags(void) { return flags; }
int netActiveGunSize(void) { return gun; }
s32 get_cur_playernum(void) { return 0; }
/* INSERT_CHEAT_QUERY */
/* INSERT_LINE_QUERY */
/* INSERT_GUN_QUERY */
static float freshBodyScale(void) {
    int body=0,head=0;
    /* INSERT_BODY_SCALE */
    return scale;
}
static s8 s_copy_weapon[MAX_PLAYER_COUNT][2];   /* net_player_sync.c sizes it by GEVR_MAX_PLAYERS */
static struct Model heldModels[2];
static WeaponObjRecord heldWeapons[2];
static PropRecord heldProps[2];
static ChrRecord *heldChr;
static int creations;
void sysLogPrintf(s32 level,const char *fmt,...) {}
enum PROP getPropForHeldItem(ITEM_IDS item) { return item==ITEM_UNARMED ? (enum PROP)-1 : (enum PROP)1; }
void chrSetWeaponFlag4(ChrRecord *chr,GUNHAND hand) {}
void objFreePermanently(ObjectRecord *obj,bool freeprop) {
    for(int h=0;h<2;h++) if(heldChr->weapons_held[h] && heldChr->weapons_held[h]->obj==obj) heldChr->weapons_held[h]=NULL;
}
PropRecord *chrGiveWeapon(ChrRecord *chr,s32 prop,ITEM_IDS item,s32 flags) {
    int h=flags & PROPFLAG_WEAPON_LEFTHANDED ? GUNLEFT : GUNRIGHT;
    memset(&heldWeapons[h],0,sizeof(heldWeapons[h]));
    heldModels[h].scale=.75f;heldWeapons[h].model=&heldModels[h];heldWeapons[h].weaponnum=item;
    heldProps[h].weapon=&heldWeapons[h];chr->weapons_held[h]=&heldProps[h];heldChr=chr;creations++;
    return &heldProps[h];
}
WeaponStats *get_ptr_item_statistics(ITEM_IDS item) { static WeaponStats stats;stats.AmmoType=1;stats.MagSize=7;return &stats; }
/* INSERT_REMOTE_HAND */
EXPORT int test_fun_visuals(void) {
    online=1;flags=0;gun=0;VrGunSizeCheat=2;debug_VisCVG_flag=1;
    memset(g_CheatPlayerTextRelated,255,sizeof(g_CheatPlayerTextRelated));
    if(cheatIsActive(CHEAT_DK_MODE) || cheatIsActive(CHEAT_PAINTBALL) || cheatIsActive(CHEAT_LINEMODE) || get_debug_VisCVG_flag() || gevrGunSizeFactor()!=1) return 1;
    c_item_entries[0].scale=10;
    for(int n=0;n<8;n++) {
        flags=n;
        if(cheatIsActive(CHEAT_DK_MODE)!=((n&1)!=0) || cheatIsActive(CHEAT_PAINTBALL)!=((n&2)!=0) || get_debug_VisCVG_flag()!=((n&4)!=0)) return 2;
        for(int respawn=0;respawn<100;respawn++) if(fabsf(freshBodyScale()-((n&1) ? .8f : 1))>.00001f) return 3;
    }
    gun=1;if(gevrGunSizeFactor()!=.2f) return 4;
    gun=2;if(gevrGunSizeFactor()!=2) return 5;
    struct player player={0};ChrRecord chr={0};PropRecord prop={0};player.prop=&prop;prop.chr=&chr;
    for(int mode=0;mode<3;mode++) {
        gun=mode;creations=0;
        memset(s_copy_weapon,-1,sizeof(s_copy_weapon));memset(chr.weapons_held,0,sizeof(chr.weapons_held));
        for(int h=0;h<2;h++) {
            netSyncCopyHand(&player,1,h,ITEM_WPPK,0);
            float wanted=.75f*netGunSizeFactor(mode);
            if(fabsf(heldModels[h].scale-wanted)>.00001f) return 7;
            for(int tick=0;tick<100;tick++)netSyncCopyHand(&player,1,h,ITEM_WPPK,0);
            if(fabsf(heldModels[h].scale-wanted)>.00001f) return 8;
            netSyncCopyHand(&player,1,h,ITEM_AK47,1);
            if(fabsf(heldModels[h].scale-wanted)>.00001f) return 9;
            chr.weapons_held[h]=NULL;netSyncCopyHand(&player,1,h,ITEM_AK47,0);
            if(fabsf(heldModels[h].scale-wanted)>.00001f) return 10;
        }
        if(creations!=6) return 11;
    }
    online=0;VrGunSizeCheat=1;if(gevrGunSizeFactor()!=.2f || !get_debug_VisCVG_flag() || !cheatIsActive(CHEAT_DK_MODE)) return 6;
    return 0;
}
