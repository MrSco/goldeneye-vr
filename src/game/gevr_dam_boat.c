#include <ultra64.h>
#include <string.h>
#include <math.h>
#include <memp.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "chrai.h"
#include "loadobjectmodel.h"
#include "model.h"
#include "matrixmath.h"
#include "stan.h"
#include "system.h"
#include "lv.h"
#include "player.h"
#include "bondview.h"
#include "propobj.h"
#include "chrobjdata.h"
#include "gevr_speedboat.h"
#include "gevr_dam_boat.h"
#include "gevr_romlayout.h"

extern bool netIsActive(void);
extern s32 g_gevrStereo;
extern f32 gevrHandPropDistance(struct PropRecord *prop, const f32 p[3]);

#define GEVR_DAM_BOAT_MAX_PADS 1024

static struct PropRecord *s_boat;
static struct GevrBoatRide s_ride;
static s32 s_island_pad;
static struct GevrBoatPad s_pads[GEVR_DAM_BOAT_MAX_PADS];

static s32 gevrDamBoatPadCount(void)
{
    s32 n = 0;

    if (g_CurrentSetup.pads == NULL)
    {
        return 0;
    }
    /* the pad list's own end, as proplvreset2 walks it */
    while (n < GEVR_DAM_BOAT_MAX_PADS && g_CurrentSetup.pads[n].plink != NULL)
    {
        n++;
    }
    return n;
}

static s32 gevrDamBoatReadsPad(u8 type)
{
    switch (type)
    {
        case PROPDEF_DOOR:
        case PROPDEF_PROP:
        case PROPDEF_KEY:
        case PROPDEF_ALARM:
        case PROPDEF_MAGAZINE:
        case PROPDEF_COLLECTABLE:
        case PROPDEF_VEHICHLE:
        case PROPDEF_TANK:
        case PROPDEF_AMMO:
        case PROPDEF_ARMOUR:
            return 1;
        default:
            return 0;
    }
}

static void gevrDamBoatMarkUsed(s32 count)
{
    struct PropDefHeaderRecord *phead;

    if (g_CurrentSetup.propDefs == NULL)
    {
        return;
    }
    phead = g_CurrentSetup.propDefs;
    while (phead->type != PROPDEF_END)
    {
        if (gevrDamBoatReadsPad(phead->type))
        {
            s32 pad = ((struct ObjectRecord *)phead)->pad;

            if (pad >= 0 && pad < count)
            {
                s_pads[pad].used = 1;
            }
        }
        phead = (struct PropDefHeaderRecord *)(((u32 *)phead) + sizepropdef(phead));
    }
}

static void gevrDamBoatPush(void)
{
    struct ObjectRecord *obj;
    f32 pos[3];

    if (s_boat == NULL || s_boat->obj == NULL)
    {
        return;
    }
    obj = s_boat->obj;
    pos[0] = s_ride.x;
    pos[1] = s_ride.y;
    pos[2] = s_ride.z;
    matrix_4x4_set_position_and_rotation_around_y(pos, s_ride.yaw, &obj->mtx);
    /* Ordinary props render this basis directly; model->scale is metadata. */
    matrix_scalar_multiply(obj->model->scale, obj->mtx.m[0]);
    obj->runtime_pos.x = s_boat->pos.x = s_ride.x;
    obj->runtime_pos.y = s_boat->pos.y = s_ride.y;
    obj->runtime_pos.z = s_boat->pos.z = s_ride.z;
    setupUpdateObjectRoomPosition(obj);
}

static void gevrDamBoatAdoptTile(struct PropRecord *prop, struct StandTile *tile)
{
    if (prop == NULL || tile == NULL || prop->stan == tile)
    {
        return;
    }
    chrpropDeregisterRooms(prop);
    prop->stan = tile;
    prop->rooms[0] = tile->room;
    prop->rooms[1] = 0xff;
    chrpropRegisterRooms(prop);
}

static int gevrDamBoatPlayable(void)
{
    return s_boat != NULL && g_CurrentPlayer != NULL && !g_CurrentPlayer->bonddead
        && !isBondInTank() && g_CurrentPlayer->watch_animation_state == 0;
}

static void gevrDamBoatSeatNow(void)
{
    struct player *player;
    struct coord3d probe;
    struct StandTile *found;
    f32 groundy;
    s32 incentre;

    if (!s_ride.aboard || g_CurrentPlayer == NULL || g_CurrentPlayer->bonddead)
    {
        if (g_CurrentPlayer != NULL && g_CurrentPlayer->bonddead)
        {
            s_ride.aboard = 0;
        }
        return;
    }
    player = g_CurrentPlayer;
    player->field_488.collision_position.f[0] = s_ride.x;
    player->field_488.collision_position.f[2] = s_ride.z;
    player->field_70 = s_ride.y;
    player->stanHeight = s_ride.y;
    player->field_7C = 0.0f;
    player->speedforwards = 0.0f;
    player->speedsideways = 0.0f;
    player->speedtheta = 0.0f;
    if (player->prop != NULL)
    {
        player->prop->pos.x = s_ride.x;
        player->prop->pos.y = s_ride.y;
        player->prop->pos.z = s_ride.z;
    }

    probe.x = s_ride.x;
    probe.y = s_ride.y + 80.0f;
    probe.z = s_ride.z;
    found = stanFindGroundAtCyl(&probe, 0.0f, NULL, player->field_488.current_tile_ptr, &groundy, &incentre);
    if (found != NULL && incentre && fabsf(groundy - s_ride.y) < 250.0f)
    {
        player->field_488.current_tile_ptr = found;
        gevrDamBoatAdoptTile(player->prop, found);
        if (s_boat != NULL)
        {
            s_boat->stan = found;
        }
    }
}

static void gevrDamBoatDisembark(void)
{
    struct PadRecord *pad;

    if (g_CurrentSetup.pads == NULL || s_island_pad < 0)
    {
        s_ride.aboard = 0;
        return;
    }
    pad = &g_CurrentSetup.pads[s_island_pad];
    s_ride.aboard = 0;
    g_CurrentPlayer->field_488.collision_position.f[0] = pad->pos.x;
    g_CurrentPlayer->field_488.collision_position.f[2] = pad->pos.z;
    g_CurrentPlayer->field_70 = pad->pos.y;
    g_CurrentPlayer->stanHeight = pad->pos.y;
    g_CurrentPlayer->field_7C = 0.0f;
    g_CurrentPlayer->speedforwards = 0.0f;
    g_CurrentPlayer->speedsideways = 0.0f;
    g_CurrentPlayer->speedtheta = 0.0f;
    if (pad->stan != NULL)
    {
        g_CurrentPlayer->field_488.current_tile_ptr = pad->stan;
        gevrDamBoatAdoptTile(g_CurrentPlayer->prop, pad->stan);
    }
    if (g_CurrentPlayer->prop != NULL)
    {
        g_CurrentPlayer->prop->pos.x = pad->pos.x;
        g_CurrentPlayer->prop->pos.y = pad->pos.y;
        g_CurrentPlayer->prop->pos.z = pad->pos.z;
    }
    sysLogPrintf(LOG_NOTE, "dam boat: stepped off at the island");
}

void gevrDamBoatPrepare(s32 stageId)
{
    struct GevrBoatPlace place;
    struct ObjectRecord *obj;
    struct PropRecord *prop;
    s32 count, i;
    f32 retail;

    s_boat = NULL;
    s_island_pad = -1;
    memset(&s_ride, 0, sizeof(s_ride));
    /* g_gevrStereo is per frame and still clear while the stage is loading. */
    /* a hack campaign puts its own level in Dam's slot (Goldfinger's Ramirez Cartel) */
    if (stageId != LEVELID_DAM || netIsActive() || getPlayerCount() != 1 || gevrRomIsVariant())
    {
        return;
    }

    count = gevrDamBoatPadCount();
    if (count < 2)
    {
        sysLogPrintf(LOG_NOTE, "dam boat: no pad list");
        return;
    }
    for (i = 0; i < count; i++)
    {
        struct PadRecord *pad = &g_CurrentSetup.pads[i];

        s_pads[i].x = pad->pos.x;
        s_pads[i].y = pad->pos.y;
        s_pads[i].z = pad->pos.z;
        s_pads[i].used = 0;
        s_pads[i].has_stan = pad->stan != NULL;
    }
    gevrDamBoatMarkUsed(count);
    if (!gevrBoatChoosePads(s_pads, count, &place))
    {
        sysLogPrintf(LOG_NOTE, "dam boat: no jetty and island among %d pads", count);
        return;
    }

    if (PitemZ_entries[PROP_SPEEDBOAT].header == NULL)
    {
        sysLogPrintf(LOG_NOTE, "dam boat: speedboat model is missing");
        return;
    }
    modelLoad(PROP_SPEEDBOAT);
    retail = PitemZ_entries[PROP_SPEEDBOAT].scale;
    obj = mempAllocBytesInBank(sizeof(struct ObjectRecord), MEMPOOL_STAGE);
    if (obj == NULL)
    {
        return;
    }
    memset(obj, 0, sizeof(*obj));
    obj->type = PROPDEF_PROP;
    obj->obj = PROP_SPEEDBOAT;
    obj->flags2 = PROPFLAG2_NOFALL | PROPFLAG2_00020000;
    prop = objInitWithModelDef(obj, PitemZ_entries[PROP_SPEEDBOAT].header);
    if (prop == NULL || obj->model == NULL)
    {
        sysLogPrintf(LOG_NOTE, "dam boat: model did not instantiate");
        return;
    }
    modelSetScale(obj->model, gevrDamBoatVisualScale(retail));
    gevrBoatBegin(&s_ride,
                  s_pads[place.jetty].x, s_pads[place.jetty].y, s_pads[place.jetty].z,
                  s_pads[place.island].x, s_pads[place.island].y, s_pads[place.island].z);
    s_boat = prop;
    s_island_pad = place.island;
    if (g_CurrentSetup.pads[place.jetty].stan != NULL)
    {
        prop->stan = g_CurrentSetup.pads[place.jetty].stan;
    }
    gevrDamBoatPush();
    chrpropActivate(prop);
    chrpropEnable(prop);
    sysLogPrintf(LOG_NOTE, "dam boat: north jetty pad %d, island pad %d, hull %.2fx retail (%.3f)",
                 place.jetty, place.island, GEVR_DAM_BOAT_SCALE, obj->model->scale);
}

int gevrDamBoatAboard(void)
{
    return s_ride.aboard;
}

int gevrDamBoatOnB(void)
{
    if (!gevrDamBoatPlayable())
    {
        return 0;
    }
    if (s_ride.aboard)
    {
        if (gevrBoatAtLanding(&s_ride))
        {
            gevrDamBoatDisembark();
        }
        return 1;
    }
    if (gevrBoatWithin(&s_ride, g_CurrentPlayer->field_488.collision_position.f[0],
                       g_CurrentPlayer->field_70, g_CurrentPlayer->field_488.collision_position.f[2],
                       GEVR_DAM_BOAT_BOARD_UNITS))
    {
        s_ride.aboard = 1;
        gevrDamBoatSeatNow();
        sysLogPrintf(LOG_NOTE, "dam boat: boarded at the north jetty");
        return 1;
    }
    return 0;
}

int gevrDamBoatTryHand(const f32 p[3])
{
    f32 view_units;
    f32 dist;

    if (p == NULL || !gevrDamBoatPlayable() || s_ride.aboard || !g_gevrStereo)
    {
        return 0;
    }
    view_units = 100.0f * D_800364CC;
    dist = gevrHandPropDistance(s_boat, p);
    if (!(dist < 1.0e20f))
    {
        /* No hull box yet: the hand's world distance to the boat. */
        f32 s = D_800364CC > 1.0e-6f ? D_800364CC : 1.0f;
        f32 dx = p[0] / s - s_ride.x;
        f32 dy = p[1] / s - s_ride.y;
        f32 dz = p[2] / s - s_ride.z;

        dist = sqrtf(dx * dx + dy * dy + dz * dz) * s;
    }
    if (!gevrBoatHandOnHull(dist, view_units))
    {
        return 0;
    }
    s_ride.aboard = 1;
    gevrDamBoatSeatNow();
    sysLogPrintf(LOG_NOTE, "dam boat: hand on the hull, boarded");
    return 1;
}

void gevrDamBoatDrive(f32 stick_x, f32 stick_y)
{
    if (!s_ride.aboard || !gevrDamBoatPlayable())
    {
        return;
    }
    gevrBoatDrive(&s_ride, stick_x, stick_y, g_GlobalTimerDelta);
    gevrDamBoatPush();
}

void gevrDamBoatApplySeat(void)
{
    if (!s_ride.aboard)
    {
        return;
    }
    gevrDamBoatSeatNow();
    gevrDamBoatPush();
}
