#ifndef GEVR_ALLY_H
#define GEVR_ALLY_H

#include <bondtypes.h>

/* Companion role in the existing reliable NPC spawn record. Native model
 * flags only consume low bits. Horde reserves this generation for the ally. */
#define GEVR_ALLY_SPAWN_FLAG 0x80000000u
#define GEVR_ALLY_HORDE_ID 0xffffffffu
typedef struct GevrAllyState {
    s32 enabled, weapon, aggression, health, damage, invincible;
    s32 spawned, identity;
} GevrAllyState;

void gevrAllyReset(void);
void gevrAllyTick(void);
void gevrAllyStop(void);
void gevrAllyBecameHost(void);
s32 gevrAllyIs(ChrRecord *chr);
s32 gevrAllyThink(ChrRecord *chr);
void gevrAllyAttach(ChrRecord *chr, u32 flags);
void gevrAllyGetState(GevrAllyState *state);
s32 gevrAllyStateValid(const GevrAllyState *state);
void gevrAllyApplyState(const GevrAllyState *state);
void gevrAllyHitGuard(ChrRecord *shooter, ChrRecord *victim, coord3d *direction, s32 weapon);
f32 gevrAllyDamageScale(void);

#endif
