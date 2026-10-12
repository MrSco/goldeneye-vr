#ifndef GEVR_DAM_BOAT_H
#define GEVR_DAM_BOAT_H
/*
 * Solo Dam only. Places the speedboat on the north jetty and keeps Bond in
 * it while he rides to the island. See port/include/gevr_speedboat.h.
 */
#include <ultra64.h>
void gevrDamBoatPrepare(s32 stageId);
int gevrDamBoatAboard(void);
/* B at the hull boards. B at the island steps off. B mid-crossing is ignored. */
int gevrDamBoatOnB(void);
/* A grip with the hand on the hull boards. p is the hand in view units. */
int gevrDamBoatTryHand(const f32 p[3]);
void gevrDamBoatDrive(f32 stick_x, f32 stick_y);
void gevrDamBoatApplySeat(void);
#endif
