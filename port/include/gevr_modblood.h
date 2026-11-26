#ifndef GEVR_MODBLOOD_H
#define GEVR_MODBLOOD_H

#include <PR/gbi.h>
#include "gevr_modapi.h"

#ifdef __cplusplus
extern "C" {
#endif

int gevrModBloodEnabled(void);
int gevrModBloodFirearm(int item);
int gevrModBloodHeadChance(int body);
int gevrModBloodImpact(int room, const float point[3], const float direction[3]);
void gevrModBloodHeadBurst(int room, const float point[3], const float direction[3]);
void gevrModBloodTick(void);
Gfx *gevrModBloodDraw(Gfx *gdl);
int gevrModBloodTexture(const void *key, const unsigned char **rgba, int *width, int *height);

#ifdef __cplusplus
}
#endif
#endif
