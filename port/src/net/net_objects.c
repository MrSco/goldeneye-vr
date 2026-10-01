/* Setup command IDs are stable across headsets; recycled pool indices are not. */
#include "net_objects.h"
#include "game/propobj.h"
#include "game/player.h"
#include "game/prop.h"
#include "game/chrai.h"
#include "game/explosion.h"
#include "game/model.h"
#include "game/loadobjectmodel.h"
#include "game/matrixmath.h"
#include "random.h"
#include "game/stan.h"
#include "system.h"
#include "fr.h"
#include <stdlib.h>
extern bool netIsActive(void);

typedef struct {
    ObjectRecord *obj;
    int index;
    coord3d pos, runtime_pos;
    Mtxf mtx;
    StandTile *stan;
    u32 flags;
} AmmoSpawn;
static AmmoSpawn *s_ammo;
static int s_count, s_capacity;

void gevrAmmoResetSpawns(void) {
    free(s_ammo); s_ammo = NULL; s_count = s_capacity = 0;
}
void gevrAmmoRememberSpawn(ObjectRecord *obj, int index) {
    if (!obj || !obj->prop || obj->type != PROPDEF_AMMO || getPlayerCount() < 2) return;
    if (s_count == s_capacity) {
        int capacity = s_capacity ? s_capacity * 2 : 32;
        AmmoSpawn *next = realloc(s_ammo, capacity * sizeof(*next));
        if (!next) { sysLogPrintf(LOG_ERROR, "ammo: cannot allocate spawn records"); return; }
        s_ammo = next; s_capacity = capacity;
    }
    s_ammo[s_count++] = (AmmoSpawn){obj, index, obj->prop->pos, obj->runtime_pos, obj->mtx,
                                   obj->prop->stan, obj->flags};
}
ObjectRecord *gevrAmmoAt(int ordinal, int *index) {
    if (ordinal < 0 || ordinal >= s_count) return NULL;
    if (index) *index = s_ammo[ordinal].index;
    return s_ammo[ordinal].obj;
}
int gevrAmmoNetworked(ObjectRecord *obj) {
    if (!netIsActive() || !obj || obj->type != PROPDEF_AMMO) return 0;
    for (int i=0;i<s_count;i++) if (s_ammo[i].obj == obj) return 1;
    return 0;
}
void gevrAmmoResetPickup(ObjectRecord *obj) {
    for (int i=0;i<s_count;i++) if (s_ammo[i].obj == obj && obj->prop) {
        AmmoSpawn *spawn = &s_ammo[i];
        objFreeEmbedmentOrProjectile(obj->prop);
        explosionClearBulletImpactRoomByFlag(obj->prop, 0);
        explosionClearBulletImpactRoomByFlag(obj->prop, 1);
        sub_GAME_7F050DE8(obj->model);
        obj->flags = spawn->flags;
        objChangeShading(obj, &spawn->pos, &spawn->mtx, spawn->stan);
        obj->runtime_pos = spawn->runtime_pos;
        chrobjCollisionRelated(obj);
        setupUpdateObjectRoomPosition(obj);
        return;
    }
}
void gevrAmmoImpulseWorld(ObjectRecord *obj, const coord3d *dir) {
    if (!obj || !obj->prop || obj->prop->timetoregen || !(obj->prop->flags & PROPFLAG_ENABLED)) return;
    sub_GAME_7F03FDA8(obj->prop);
    if (!(obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) || !obj->projectile) return;
    Projectile *p = obj->projectile;
    p->speed.x = RANDOMFRAC() * 6.6666666f - 3.3333333f + 3.3333333f * dir->x;
    p->speed.y = RANDOMFRAC() * 3.3333333f + 3.3333333f;
    p->speed.z = RANDOMFRAC() * 6.6666666f - 3.3333333f + 3.3333333f * dir->z;
    coord3d rot = {RANDOMFRAC() * .098174774f - .049087387f,
                  RANDOMFRAC() * .098174774f - .049087387f,
                  RANDOMFRAC() * .098174774f - .049087387f};
    matrix_4x4_set_rotation_around_xyz(&rot, &p->mtx);
    p->flags |= PROJECTILEFLAG_AIRBORNE;
    p->ownerprop = NULL; /* The host advances crates independently of a shooter's slot. */
    p->unk90 = 1;
}
void gevrAmmoApplyTransform(ObjectRecord *obj, const coord3d *pos, const coord3d *runtime_pos, const Mtxf *mtx,
                            int regen, int enabled, int moving, const coord3d *speed,
                            const Mtxf *rotation) {
    if (!obj || !obj->prop) return;
    PropRecord *prop = obj->prop;
    if (regen && !prop->timetoregen) gevrAmmoResetPickup(obj);
    objFreeEmbedmentOrProjectile(prop);
    StandTile *stan = prop->stan;
    if (stan) walkTilesBetweenPoints_NoCallback(&stan, prop->pos.x, prop->pos.z, pos->x, pos->z);
    coord3d nextpos = *pos;
    Mtxf nextmtx = *mtx;
    chrpropDeregisterRooms(prop);
    objChangeShading(obj, &nextpos, &nextmtx, stan);
    obj->runtime_pos = *runtime_pos;
    chrobjCollisionRelated(obj);
    prop->timetoregen = regen;
    if (enabled) { chrpropEnable(prop); setupUpdateObjectRoomPosition(obj); }
    else chrpropDisable(prop);
    if (moving && !regen && enabled) {
        sub_GAME_7F03FDA8(prop);
        if ((obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) && obj->projectile) {
            obj->projectile->speed = *speed;
            obj->projectile->mtx = *rotation;
            obj->projectile->ownerprop = NULL;
            obj->projectile->flags |= PROJECTILEFLAG_AIRBORNE;
            obj->projectile->unk90 = 1;
        }
    }
}
