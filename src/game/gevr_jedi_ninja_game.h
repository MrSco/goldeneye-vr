#ifndef GEVR_JEDI_NINJA_GAME_H
#define GEVR_JEDI_NINJA_GAME_H

#include <ultra64.h>
#include <bondtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void gevrJnGameReset(void);
void gevrJnGameTick(s32 ticks60);
Gfx *gevrJnDrawWorld(Gfx *gdl);
Gfx *gevrJnDrawFirstPerson(Gfx *gdl);
s32 gevrJnInterceptPlayerDamage(f32 damageAmount, f32 vecX, f32 vecZ);
s32 gevrJnSuppressNormalFire(void);
s32 gevrJnHideNormalGun(void);
s32 gevrJnBlastersActive(void);
s32 gevrJnDrawBlasterActive(void);
s32 gevrJnCanPickupProp(PropRecord *prop);
s32 gevrJnAllyThink(ChrRecord *ally);

#ifdef __cplusplus
}
#endif

#endif
