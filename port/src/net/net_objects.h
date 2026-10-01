#ifndef GEVR_NET_OBJECTS_H
#define GEVR_NET_OBJECTS_H
#include <ultra64.h>
#include "bondtypes.h"
void gevrAmmoResetSpawns(void);
void gevrAmmoRememberSpawn(ObjectRecord *obj, int index);
ObjectRecord *gevrAmmoAt(int ordinal, int *index);
int gevrAmmoNetworked(ObjectRecord *obj);
void gevrAmmoResetPickup(ObjectRecord *obj);
void gevrAmmoImpulseWorld(ObjectRecord *obj, const coord3d *dir);
void gevrAmmoApplyTransform(ObjectRecord *obj, const coord3d *pos, const coord3d *runtime_pos, const Mtxf *mtx,
                            int regen, int enabled, int moving, const coord3d *speed,
                            const Mtxf *rotation);
void netSendAmmoImpulse(ObjectRecord *obj, const coord3d *dir);
#endif
