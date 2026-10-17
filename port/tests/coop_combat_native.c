/* Guard combat with distinct observer/target cameras and real damage packets. */
#include "net_core.h"
#include "net_coop.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/player.h"
#include "game/gun.h"
#include "game/bondview.h"
#include "game/bondhead.h"
#include "game/matrixmath.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define COOP_MAX_SLOTS 512
static ChrRecord guards[2];
ChrRecord *g_ChrSlots=guards;
s32 g_NumChrSlots=2,g_GlobalTimer;
f32 g_GlobalTimerDelta=8,g_AiAccuracyModifier=1,g_AiDamageModifier=1;
static struct player players[4];
struct player *g_playerPointers[MAX_PLAYER_COUNT],*g_CurrentPlayer;
static PropRecord player_props[4],guard_prop,weapon_prop;
static WeaponObjRecord weapon;
static Model gun_model;
static ModelFileHeader gun_header;
static ModelNode muzzle;
static union ModelRoData muzzle_data;
static ModelNode *switches[2];
static Mtxf cameras[4],gun_matrix;
static int cur,local,hosting=1,downed[4],damage_events;
static float last_damage;
static u8 raw[64];static unsigned packet_bytes;
int netGetLocalSlot(void) {return local;}
bool netIsHost(void) {return hosting;}
int netCoopActive(void) {return 1;}
bool netSlotOccupied(int slot) {return slot>=0 && slot<2;}
bool netSlotIsSpectator(int slot) {(void)slot;return false;}
int gevrCoopDowned(int slot) {return downed[slot];}
s32 get_cur_playernum(void) {return cur;}
void set_cur_player(s32 slot) {assert(slot>=0 && slot<4);cur=slot;g_CurrentPlayer=&players[slot];}
PropRecord *getCurrentPlayerProp(void) {return g_CurrentPlayer->prop;}
int bondviewGetIfCurrentPlayerDamageShowTime(void) {return g_CurrentPlayer->damageshowtime>=0;}
f32 chrlvGetSubrotySideback(ChrRecord *chr) {(void)chr;return 0;}
f32 get_007_accuracy_mod(void) {return 1;}
f32 get_007_damage_mod(void) {return 1;}
f32 gunItemGetDestructionAmount(ITEM_IDS item) {(void)item;return 1;}
s8 bondwalkItemGetAutomaticFiringRate(ITEM_IDS item) {(void)item;return -1;}
Mtxf *currentPlayerGetViewToWorldMtxf(void) {return &cameras[cur];}
PropRecord *chrGetEquippedWeaponProp(ChrRecord *chr,GUNHAND hand) {(void)chr;(void)hand;return &weapon_prop;}
Mtxf *modelFindNodeMtx(Model *m,ModelNode *node,s32 index) {(void)m;(void)node;(void)index;return &gun_matrix;}
void netCoopBroadcast(const unsigned char *data,unsigned size,int reliable) {assert(reliable && size<=sizeof(raw));memcpy(raw,data,size);packet_bytes=size;}
void sysLogPrintf(s32 level,const char *fmt,...) {(void)level;(void)fmt;}
s32 gevrAllyIs(ChrRecord *chr) {(void)chr;return 0;}
/* PRODUCTION */
void record_damage_kills(f32 amount,f32 x,f32 z,s32 attacker,s32 armour) {
    (void)attacker;(void)armour;
    if(gevrCoopForwardGuardDamage(amount,x,z))return;
    last_damage=amount;damage_events++;g_CurrentPlayer->bondhealth-=amount;
}
void bondviewCallRecordDamageKills(f32 amount,f32 angle,s32 attacker,s32 armour) {
    record_damage_kills(amount,sinf(angle),cosf(angle),attacker,armour);
}
int main(void) {
    for(int i=0;i<4;i++) {
        g_playerPointers[i]=&players[i];players[i].prop=&player_props[i];
        players[i].bondhealth=1;players[i].damageshowtime=0;
        for(int k=0;k<4;k++)cameras[i].m[k][k]=1;
    }
    guards[0].prop=&guard_prop;
    player_props[0].pos.z=500;player_props[1].pos.z=100;
    set_cur_player(0);
    assert(gevrCoopGuardTarget(&guards[0])==1); /* joining player is nearest */
    gevrCoopGuardProvoked(&guards[0],0);assert(gevrCoopGuardTarget(&guards[0])==0);
    downed[0]=1;assert(gevrCoopGuardTarget(&guards[0])==1);downed[0]=0;
    /* Copies start with a flash timer of zero and never render that flash.
     * It must not prevent probabilistic guard hits on their owner. */
    set_cur_player(1);g_gevrCoopGuardTick=1;
    s32 aimed,hit;
    chrlvUpdateShotbondsum(&guards[0],&aimed,&hit,ITEM_TT33);
    assert(aimed && hit && packet_bytes && !damage_events && players[1].bondhealth==1);
    struct netbuf b;netbufStartReadData(&b,raw,packet_bytes);
    assert(netbufReadU32(&b)==GEVR_NET_MAGIC && netbufReadU16(&b)==GEVR_NET_VERSION);
    assert(netbufReadU8(&b)==NET_MSG_COOP_DAMAGE);netbufReadU8(&b);
    local=1;hosting=0;g_gevrCoopGuardTick=0;set_cur_player(0);
    coopApplyDamage(&b);
    assert(damage_events==1 && last_damage>0 && players[1].bondhealth<1 && cur==0);
    /* The host's own normal immunity remains in force. */
    local=0;hosting=1;set_cur_player(0);g_gevrCoopGuardTick=1;
    guards[0].shotbondsum=0;chrlvUpdateShotbondsum(&guards[0],&aimed,&hit,ITEM_TT33);
    assert(guards[0].shotbondsum==0 && damage_events==1);
    players[0].damageshowtime=-1;chrlvUpdateShotbondsum(&guards[0],&aimed,&hit,ITEM_TT33);
    assert(damage_events==2);
    /* Cached gun matrices came from the host, not the remote target camera. */
    for(int k=0;k<4;k++)gun_matrix.m[k][k]=1;
    cameras[0].m[3][0]=1000;cameras[1].m[3][0]=-2000;gun_matrix.m[3][0]=10;
    weapon_prop.obj=(ObjectRecord *)&weapon;weapon.model=&gun_model;weapon_prop.flags=2;
    gun_model.obj=&gun_header;gun_header.Switches=switches;switches[0]=&muzzle;
    muzzle.Data=&muzzle_data;
    coord3d point;
    g_gevrCoopGuardViewToWorld=&cameras[0];set_cur_player(1);
    assert(sub_GAME_7F02D630(&guards[0],GUNRIGHT,&point) && fabsf(point.x-1010)<.001f);
    g_gevrCoopGuardViewToWorld=NULL;
    assert(sub_GAME_7F02D630(&guards[0],GUNRIGHT,&point) && fabsf(point.x+1990)<.001f);
    puts("PASS: host/client targeting, remote flash gating, owner damage packets and observer-space muzzle transforms");
}
