#ifndef GEVR_BLOOD_H
#define GEVR_BLOOD_H
/* Mods: the Blood patch's blood on the walls and floors (gevr_blood.c, #342) */
#include <ultra64.h>
#include <bondtypes.h>

/* the Paintball cheat's splat, bg/wallhit (explosion.c g_ImpactTypes) */
#define GEVR_BLOOD_IMPACT_TYPE 16
/* Ragdolls bodies looked at (gevr_ragdoll_internal.h RD_BODIES) */
#define GEVR_BLOOD_BODIES 24

/* chrHandleBulletHit: a shot that wounded chr at hitpos (world units) */
void gevrBloodChrHit(ChrRecord *chr, const coord3d *hitpos, const coord3d *shotdir);
/* lvlTick, once a frame: growth, drips and pools */
void gevrBloodTick(int ticks);
/* lvlStageLoad */
void gevrBloodReset(void);
/* explosionRenderBulletImpactOnProp: the splats to draw with the bullet holes */
int gevrBloodDecals(void);
const Vtx *gevrBloodDecal(int i, s32 *room);
#endif
