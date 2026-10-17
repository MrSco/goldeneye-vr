#ifndef GEVR_ALLY_H
#define GEVR_ALLY_H

#include <bondtypes.h>

/* Solo companion, independent of mission AI IDs and objective bits. */
void gevrAllyReset(void);
void gevrAllyTick(void);
s32 gevrAllyIs(ChrRecord *chr);
s32 gevrAllyThink(ChrRecord *chr);
void gevrAllyHitGuard(ChrRecord *shooter, ChrRecord *victim, coord3d *direction, s32 weapon);
f32 gevrAllyDamageScale(void);

#endif
