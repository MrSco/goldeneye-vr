/* Production spawn lifecycle, with only native room/geometry services stubbed. */
#include <string.h>
#include "../src/net/net_objects.c"
#define EXPORT __declspec(dllexport)
static int cleared[2], resets, freed;
static Projectile projectile;
bool netIsActive(void) { return 1; }
s32 getPlayerCount(void) { return 3; }
void sysLogPrintf(s32 level,const char *fmt,...) {(void)level;(void)fmt;}
u32 randomGetNext(void) { return 0x80000000; }
void objFreeEmbedmentOrProjectile(PropRecord *p) {
    freed++;p->obj->runtime_bitflags &= ~(RUNTIMEBITFLAG_HASPROJECTILE|RUNTIMEBITFLAG_EMBEDDED);
    p->obj->projectile=NULL;
}
void explosionClearBulletImpactRoomByFlag(PropRecord *p,s8 flag) { (void)p;cleared[flag]++; }
void sub_GAME_7F050DE8(Model *m) {(void)m;resets++;}
void objChangeShading(ObjectRecord *obj,coord3d *pos,Mtxf *mtx,StandTile *stan) {
    obj->runtime_pos=obj->prop->pos=*pos;obj->mtx=*mtx;obj->prop->stan=stan;
}
void setupUpdateObjectRoomPosition(ObjectRecord *o) {(void)o;}
void chrobjCollisionRelated(ObjectRecord *o) {(void)o;}
void chrpropDeregisterRooms(PropRecord *p) {(void)p;}
void chrpropEnable(PropRecord *p) {p->flags|=PROPFLAG_ENABLED;}
void chrpropDisable(PropRecord *p) {p->flags&=~PROPFLAG_ENABLED;}
s32 walkTilesBetweenPoints_NoCallback(StandTile **s,f32 a,f32 b,f32 c,f32 d) {
    (void)s;(void)a;(void)b;(void)c;(void)d;return 1;
}
void sub_GAME_7F03FDA8(PropRecord *p) {
    p->obj->projectile=&projectile;p->obj->runtime_bitflags|=RUNTIMEBITFLAG_HASPROJECTILE;
}
void matrix_4x4_set_rotation_around_xyz(coord3d *a,Mtxf *m) {
    (void)a;memset(m,0,sizeof(*m));for(int i=0;i<4;i++)m->m[i][i]=1;
}
EXPORT int test_ammo_lifecycle(void) {
    ObjectRecord o={0};PropRecord p={0};StandTile tile={0};Mtxf m={0},rotation={0};
    coord3d original={10,20,30},moved={900,20,-600},speed={3,4,-2};
    o.type=PROPDEF_AMMO;o.prop=&p;p.obj=&o;p.pos=original;p.stan=&tile;p.flags=PROPFLAG_ENABLED;
    for(int i=0;i<4;i++) m.m[i][i]=rotation.m[i][i]=1;
    o.mtx=m;o.runtime_pos=original;o.runtime_pos.y+=5;
    o.flags=PROPFLAG_00000100;cleared[0]=cleared[1]=resets=freed=0;
    gevrAmmoResetSpawns();gevrAmmoRememberSpawn(&o,29);
    int index=-1;if(gevrAmmoAt(0,&index)!=&o || index!=29 || !gevrAmmoNetworked(&o)) return 1;
    gevrAmmoApplyTransform(&o,&moved,&moved,&m,0,1,1,&speed,&rotation);
    if(p.pos.x!=900 || !o.projectile || o.projectile->speed.z!=-2) return 2;
    o.flags=0;gevrAmmoResetPickup(&o);
    if(memcmp(&p.pos,&original,sizeof(original)) || o.flags!=PROPFLAG_00000100 ||
       o.runtime_pos.y!=25 || p.stan!=&tile || o.projectile || cleared[0]!=1 || cleared[1]!=1 || resets!=1) return 3;
    /* Late-join movement never becomes the recorded spawn. */
    gevrAmmoApplyTransform(&o,&moved,&moved,&m,0,1,0,&speed,&rotation);
    gevrAmmoApplyTransform(&o,&original,&original,&m,1200,0,0,&speed,&rotation);
    if(p.flags&PROPFLAG_ENABLED || p.timetoregen!=1200 || resets!=2) return 4;
    gevrAmmoResetPickup(&o);if(memcmp(&p.pos,&original,sizeof(original))) return 5;
    gevrAmmoResetSpawns();if(gevrAmmoAt(0,&index) || gevrAmmoNetworked(&o)) return 6;
    /* Several reallocations retain every original location and command ID. */
    ObjectRecord objects[100];PropRecord props[100];memset(objects,0,sizeof(objects));memset(props,0,sizeof(props));
    for(int i=0;i<100;i++) {
        objects[i].type=PROPDEF_AMMO;objects[i].prop=&props[i];props[i].obj=&objects[i];props[i].pos.x=i;
        gevrAmmoRememberSpawn(&objects[i],i*3);
    }
    for(int i=0;i<100;i++) {
        if(gevrAmmoAt(i,&index)!=&objects[i] || index!=i*3) return 7;
        props[i].pos.x=999;gevrAmmoResetPickup(&objects[i]);if(props[i].pos.x!=i) return 8;
    }
    gevrAmmoResetSpawns();return 0;
}
