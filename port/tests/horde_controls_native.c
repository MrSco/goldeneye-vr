/* Engine entry points extracted by test_horde.py, with only their dependencies stubbed. */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
static Mtxf camera;
static f32 D_800364CC;
Mtxf *currentPlayerGetViewToWorldMtxf(void) {return &camera;}
void mtx4TransformVecInPlace(Mtxf *m,coord3d *p) {
    coord3d in=*p;
    for(int k=0;k<3;k++) p->f[k]=m->m[3][k]+in.x*m->m[0][k]+in.y*m->m[1][k]+in.z*m->m[2][k];
}
static int held,opens,closes,boss,randoms;
int gevrHordeDoorHeldOpen(const DoorRecord *door) {(void)door;return held;}
void doorStartOpen(DoorRecord *door) {door->flags&=~PROPFLAG_DOOR_KEEPOPEN;opens++;}
void doorStartClose(DoorRecord *door) {door->flags&=~PROPFLAG_DOOR_KEEPOPEN;closes++;}
int gevrHordeBossNoStun(const ChrRecord *chr) {return boss && chr->damage<chr->maxdamage;}
u32 randomGetNext(void) {randoms++;return 1;}
static int weapon_ready=1,shots;
int gevrHordeXeniaWeapon(ChrRecord *chr) {(void)chr;return weapon_ready;}
void chrlvFireWeaponRelated(ChrRecord *chr,s32 hand) {(void)chr;(void)hand;shots++;}
#include "horde_controls_production.h"
int main(void) {
    /* A scaled map, turned camera, eye far from world origin. */
    camera.m[0][2]=1;camera.m[1][1]=1;camera.m[2][0]=-1;
    camera.m[3][0]=5000;camera.m[3][1]=160;camera.m[3][2]=-3000;
    D_800364CC=0.2f;coord3d world;const f32 hand[3]={20,-10,-30};
    assert(gevrHordeGripWorld(hand,&world));
    assert(fabsf(world.x-5150)<0.01f && fabsf(world.y-110)<0.01f && fabsf(world.z+2900)<0.01f);
    D_800364CC=0;assert(!gevrHordeGripWorld(hand,&world));
    /* A linked/auto-close request cannot clear KEEP_OPEN or start a sound. */
    DoorRecord door={0};door.openstate=DOORSTATE_STATIONARY;door.openPosition=door.maxFrac=1;
    door.flags|=PROPFLAG_DOOR_KEEPOPEN;held=1;
    for(int i=0;i<100;i++) {
        doorSetOpenState(&door,DOORSTATE_CLOSING);doorSetOpenState(&door,DOORSTATE_OPENING);
        doorSetOpenState(&door,DOORSTATE_WAITING);
    }
    assert(!opens && !closes && door.openstate==DOORSTATE_STATIONARY && (door.flags&PROPFLAG_DOOR_KEEPOPEN));
    held=0;doorSetOpenState(&door,DOORSTATE_CLOSING);
    assert(closes==1 && door.openstate==DOORSTATE_CLOSING && !(door.flags&PROPFLAG_DOOR_KEEPOPEN));
    door.openstate=DOORSTATE_STATIONARY;doorSetOpenState(&door,DOORSTATE_OPENING);
    assert(opens==1 && door.openstate==DOORSTATE_OPENING);
    ChrRecord chr={0};chr.maxdamage=10;chr.flinchcnt=-1;boss=1;
    chrSetHiddenToRandom(&chr);assert(chr.flinchcnt==-1 && !randoms);
    boss=0;chrSetHiddenToRandom(&chr);assert(chr.flinchcnt==1 && randoms==2);
    chr.damage=10;chr.flinchcnt=-1;boss=1;chrSetHiddenToRandom(&chr);assert(chr.flinchcnt==1);
    weapon_ready=0;chr.hidden=CHRHIDDEN_FIRE_WEAPON_RIGHT|CHRHIDDEN_FIRE_TRACER;
    chrlvTriggerFireWeapon(&chr);assert(!shots && !(chr.hidden&(CHRHIDDEN_FIRE_WEAPON_RIGHT|CHRHIDDEN_FIRE_TRACER)));
    weapon_ready=1;chr.hidden=CHRHIDDEN_FIRE_WEAPON_RIGHT;chrlvTriggerFireWeapon(&chr);assert(shots==1);
    puts("PASS: scaled camera-to-world grip, held-door sound suppression and boss flinch entry point");
}
