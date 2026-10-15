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
#include "bg.h"

extern bool netIsActive(void);
extern s32 g_gevrStereo;
extern f32 gevrHandPropDistance(struct PropRecord *prop, const f32 p[3]);
extern int VrGestureGripUse;   /* Gestures: grip to use (vr_settings) */

#define GEVR_DAM_BOAT_MAX_PADS 1024

static struct PropRecord *s_boat;
/* The stage-owned object survives prop removal; the prop slot can be reused. */
static struct ObjectRecord *s_boatObj;
static struct GevrBoatRide s_ride;
static s32 s_island_pad;
static s32 s_jetty_pad;
static struct GevrBoatPad s_pads[GEVR_DAM_BOAT_MAX_PADS];

/*
 * The hull on the water, and Bond on its deck. The ride kept the hull at the
 * jetty pad's height, the jetty's deck, about 300 units over the reservoir: it
 * hung in the air (user, 2026-10-15). Dam's reservoir is flat: the retail
 * background's 101 lit water triangles (texture 1511) all lie at -200, drawn
 * at 1 / levelscale. The water is one plane, which no hull keeps out, so the
 * hull rides high: a tenth of its height in it, by its own bounding box under
 * its group's origin. A quarter, with the origin left out, put the water over
 * the cabin's floor ("we sinking", user, 2026-10-15). Bond stands at the
 * model's origin, its gunwale. The ride's own y stays the jetty's, which
 * boarding measures from.
 */
#define GEVR_DAM_WATER_BG_Y -200.0f
static f32 s_hullY, s_deckY;

/* Its half length and width, for the jetty and the docks (gevrDamBoatHullBlocked) */
static f32 s_halfLength, s_halfWidth;

/* The hull in its own model: the union of its bounding boxes, the whole tree
 * walked (chrobjGetBboxFromObjFile looks two levels down only, and found none
 * on the speedboat), each moved by its groups' origins as the hull is drawn
 * (the speedboat's group lowers it 94). 0 when it has none. */
static int gevrDamBoatHullBox(ModelNode *node, s32 depth, const f32 origin[3], struct bbox *out, s32 found)
{
    for (; node != NULL && depth < 32; node = node->Next)
    {
        f32 below[3] = { origin[0], origin[1], origin[2] };

        if ((node->Opcode & 0xff) == MODELNODE_OPCODE_BBOX && node->Data != NULL)
        {
            const struct bbox *box = &node->Data->BoundingBox.Bounds;

            if (box->ymax > box->ymin)
            {
                if (!found || box->xmin + origin[0] < out->xmin) out->xmin = box->xmin + origin[0];
                if (!found || box->xmax + origin[0] > out->xmax) out->xmax = box->xmax + origin[0];
                if (!found || box->ymin + origin[1] < out->ymin) out->ymin = box->ymin + origin[1];
                if (!found || box->ymax + origin[1] > out->ymax) out->ymax = box->ymax + origin[1];
                if (!found || box->zmin + origin[2] < out->zmin) out->zmin = box->zmin + origin[2];
                if (!found || box->zmax + origin[2] > out->zmax) out->zmax = box->zmax + origin[2];
                found = 1;
            }
        }
        if ((node->Opcode & 0xff) == MODELNODE_OPCODE_GROUP && node->Data != NULL)
        {
            below[0] += node->Data->Group.Origin.f[0];
            below[1] += node->Data->Group.Origin.f[1];
            below[2] += node->Data->Group.Origin.f[2];
        }
        found = gevrDamBoatHullBox(node->Child, depth + 1, below, out, found);
    }
    return found;
}

/*
 * A hull pose that runs into the jetty or the docks: a floor more than 100
 * units over the water under its middle, bow, stern or either side. The ride
 * had no collision, so it launched inside the north dock and sailed through it
 * (user, 2026-10-15). Open water has no floor of its own at that height.
 */
static int gevrDamBoatHullBlocked(f32 x, f32 z, f32 yaw)
{
    const f32 water = GEVR_DAM_WATER_BG_Y * get_room_data_float2();
    const f32 fx = sinf(yaw), fz = cosf(yaw);
    const f32 along[5] = { 0.0f, s_halfLength, -s_halfLength, 0.0f, 0.0f };
    const f32 across[5] = { 0.0f, 0.0f, 0.0f, s_halfWidth, -s_halfWidth };
    s32 i;

    for (i = 0; i < 5; i++)
    {
        struct coord3d p;
        f32 floory = 0.0f;

        p.x = x + fx * along[i] + fz * across[i];
        p.y = s_ride.jetty_y + 300.0f;
        p.z = z + fz * along[i] - fx * across[i];
        if (stanFindTileBelowPos(&p, NULL, &floory) != NULL && floory > water + 100.0f)
        {
            return 1;
        }
    }
    return 0;
}

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

static void gevrDamBoatAdoptTile(struct PropRecord *prop, struct StandTile *tile);

/* Ends the ride with Bond standing on a pad: the island landing, or the
 * north jetty when the hull is lost under him. */
static void gevrDamBoatStepOffAt(s32 padnum)
{
    struct PadRecord *pad;

    s_ride.aboard = 0;
    if (g_CurrentSetup.pads == NULL || padnum < 0 || g_CurrentPlayer == NULL)
    {
        return;
    }
    pad = &g_CurrentSetup.pads[padnum];
    g_CurrentPlayer->field_488.collision_position.f[0] = pad->pos.x;
    g_CurrentPlayer->field_488.collision_position.f[2] = pad->pos.z;
    g_CurrentPlayer->field_70 = pad->pos.y;
    g_CurrentPlayer->stanHeight = pad->pos.y;
    g_CurrentPlayer->field_7C = 0.0f;
    g_CurrentPlayer->speedforwards = 0.0f;
    g_CurrentPlayer->speedsideways = 0.0f;
    g_CurrentPlayer->speedtheta = 0.0f;   /* stepped off: standing still */
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
}

/* A cached prop pointer alone is insufficient: objFree clears obj->prop but
 * leaves the released prop's obj pointer, until its pool slot is allocated again. */
static int gevrDamBoatLive(void)
{
    if (s_boat != NULL && s_boatObj != NULL && s_boatObj->prop == s_boat
        && s_boat->type == PROP_TYPE_OBJ && s_boat->obj == s_boatObj
        && !(s_boatObj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE)
        && !(s_boatObj->state & PROPSTATE_DESTROYED) && s_boatObj->model != NULL)
    {
        return 1;
    }
    if (s_boat != NULL)
        sysLogPrintf(LOG_NOTE, "dam boat: hull removed; ride ended");
    s_boat = NULL;
    s_boatObj = NULL;
    if (s_ride.aboard)
    {
        /* Not left treading water mid-reservoir: back to the mainland. */
        if (g_CurrentPlayer != NULL && !g_CurrentPlayer->bonddead)
        {
            gevrDamBoatStepOffAt(s_jetty_pad);
            sysLogPrintf(LOG_NOTE, "dam boat: hull lost aboard; back on the north jetty");
        }
        s_ride.aboard = 0;
    }
    return 0;
}

static void gevrDamBoatPush(void)
{
    struct ObjectRecord *obj;
    f32 pos[3];

    if (!gevrDamBoatLive())
    {
        return;
    }
    obj = s_boatObj;
    pos[0] = s_ride.x;
    pos[1] = s_hullY;
    pos[2] = s_ride.z;
    /* the model's bow is its -z: turned round, it goes bow first (user, 2026-10-15) */
    matrix_4x4_set_position_and_rotation_around_y(pos, s_ride.yaw + 3.1415927f, &obj->mtx);
    /* Ordinary props render this basis directly; model->scale is metadata. */
    matrix_scalar_multiply(obj->model->scale, obj->mtx.m[0]);
    obj->runtime_pos.x = s_boat->pos.x = s_ride.x;
    obj->runtime_pos.y = s_boat->pos.y = s_hullY;
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
    return gevrDamBoatLive() && g_CurrentPlayer != NULL && !g_CurrentPlayer->bonddead
        && !isBondInTank() && g_CurrentPlayer->watch_animation_state == 0;
}

static void gevrDamBoatSeatNow(void)
{
    struct player *player;
    struct coord3d probe;
    struct StandTile *found;
    f32 groundy;
    s32 incentre;

    if (!gevrDamBoatLive() || !s_ride.aboard || g_CurrentPlayer == NULL || g_CurrentPlayer->bonddead)
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
    player->field_70 = s_deckY;
    player->stanHeight = s_deckY;
    player->field_7C = 0.0f;
    /* the turn stays: on the 2D screen the right stick turns Bond aboard */
    player->speedforwards = 0.0f;
    player->speedsideways = 0.0f;
    if (player->prop != NULL)
    {
        player->prop->pos.x = s_ride.x;
        player->prop->pos.y = s_deckY;
        player->prop->pos.z = s_ride.z;
    }

    probe.x = s_ride.x;
    probe.y = s_deckY + 80.0f;
    probe.z = s_ride.z;
    found = stanFindGroundAtCyl(&probe, 0.0f, NULL, player->field_488.current_tile_ptr, &groundy, &incentre);
    if (found != NULL && incentre && fabsf(groundy - s_deckY) < 250.0f)
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
    gevrDamBoatStepOffAt(s_island_pad);
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
    s_boatObj = NULL;
    s_island_pad = -1;
    s_jetty_pad = -1;
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
    obj->damage = 1000.0f;   /* standard prop health; objInit does not supply it */
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
    s_hullY = s_deckY = s_ride.y;
    s_halfLength = s_halfWidth = 0.0f;
    {
        struct bbox hull;
        const f32 none[3] = { 0.0f, 0.0f, 0.0f };

        memset(&hull, 0, sizeof(hull));
        if (obj->model->obj != NULL && gevrDamBoatHullBox(obj->model->obj->RootNode, 0, none, &hull, 0))
        {
            const f32 lo = hull.ymin * obj->model->scale;
            const f32 hi = hull.ymax * obj->model->scale;
            const f32 water = GEVR_DAM_WATER_BG_Y * get_room_data_float2();

            s_hullY = water - (lo + 0.1f * (hi - lo));
            s_deckY = s_hullY;
            s_halfLength = fmaxf(fabsf(hull.zmin), fabsf(hull.zmax)) * obj->model->scale;
            s_halfWidth = fmaxf(fabsf(hull.xmin), fabsf(hull.xmax)) * obj->model->scale;
            sysLogPrintf(LOG_NOTE, "dam boat: hull %.0f..%.0f, water %.0f: hull at %.0f, deck %.0f (jetty %.0f)",
                         lo, hi, water, s_hullY, s_deckY, s_ride.y);
        }
        else
        {
            sysLogPrintf(LOG_NOTE, "dam boat: the hull has no bounding box; it stays at the jetty's height");
        }
    }
    {
        /* out from the jetty, toward the island, until the hull is clear of the dock */
        s32 step;

        for (step = 0; step < 80 && gevrDamBoatHullBlocked(s_ride.x, s_ride.z, s_ride.yaw); step++)
        {
            s_ride.x += sinf(s_ride.yaw) * 50.0f;
            s_ride.z += cosf(s_ride.yaw) * 50.0f;
        }
        sysLogPrintf(LOG_NOTE, "dam boat: launched %d units further out, clear of the dock%s (hull %.0f long, %.0f wide)",
                     step * 50, step == 80 ? " - NOT" : "", s_halfLength * 2.0f, s_halfWidth * 2.0f);
    }
    s_boat = prop;
    s_boatObj = obj;
    s_island_pad = place.island;
    s_jetty_pad = place.jetty;
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
    return gevrDamBoatLive() && s_ride.aboard;
}

/* How far a point is from the hull's outline on the water (its bounding box
 * turned with it), 0 inside. Boarding measured to the middle of the hull, and
 * once the launch moved out clear of the dock it was out of reach of the
 * jetty (user, 2026-10-15). */
static f32 gevrDamBoatFromHull(f32 x, f32 z)
{
    const f32 dx = x - s_ride.x, dz = z - s_ride.z;
    const f32 along = fabsf(dx * sinf(s_ride.yaw) + dz * cosf(s_ride.yaw)) - s_halfLength;
    const f32 across = fabsf(dx * cosf(s_ride.yaw) - dz * sinf(s_ride.yaw)) - s_halfWidth;
    const f32 ea = along > 0.0f ? along : 0.0f, ec = across > 0.0f ? across : 0.0f;

    return sqrtf(ea * ea + ec * ec);
}

/* Bond near the hull from the jetty, or beside it at the landing, on foot */
static int gevrDamBoatInReach(void)
{
    return gevrDamBoatFromHull(g_CurrentPlayer->field_488.collision_position.f[0],
                               g_CurrentPlayer->field_488.collision_position.f[2]) <= GEVR_DAM_BOAT_BOARD_UNITS
        && fabsf(g_CurrentPlayer->field_70 - s_ride.y) <= GEVR_DAM_BOAT_BOARD_UNITS;
}

/* Aboard at the island landing or back at the jetty: off there. 0 elsewhere,
 * mid-crossing, where nothing drops him in the water. */
static int gevrDamBoatStepOffHere(void)
{
    const f32 dx = s_ride.x - s_ride.jetty_x, dz = s_ride.z - s_ride.jetty_z;

    if (gevrBoatAtLanding(&s_ride))
    {
        gevrDamBoatDisembark();
        return 1;
    }
    if (dx * dx + dz * dz <= GEVR_DAM_BOAT_LANDING_UNITS * GEVR_DAM_BOAT_LANDING_UNITS)
    {
        gevrDamBoatStepOffAt(s_jetty_pad);
        sysLogPrintf(LOG_NOTE, "dam boat: stepped off at the north jetty");
        return 1;
    }
    return 0;
}

int gevrDamBoatOnB(void)
{
    if (!gevrDamBoatPlayable())
    {
        return 0;
    }
    /* With grip to use (GE-X style) the grip boards and steps off, and B is
     * left to its own job (user, 2026-10-15) */
    if (g_gevrStereo && VrGestureGripUse)
    {
        return 0;
    }
    if (s_ride.aboard)
    {
        gevrDamBoatStepOffHere();
        return 1;
    }
    if (gevrDamBoatInReach())
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

    if (p == NULL || !gevrDamBoatPlayable() || !g_gevrStereo)
    {
        return 0;
    }
    if (s_ride.aboard)
    {
        /* a grip aboard steps off at the landing or the jetty; mid-crossing
         * the grip keeps its own jobs */
        return gevrDamBoatStepOffHere();
    }
    view_units = 100.0f * D_800364CC;
    {
        /* From the jetty the hull lies below, beyond a hand's box test: Bond
         * within reach of its outline, the hand over its edge (80 units). */
        const f32 s = D_800364CC > 1.0e-6f ? D_800364CC : 1.0f;

        if (gevrDamBoatInReach() && gevrDamBoatFromHull(p[0] / s, p[2] / s) <= 80.0f)
        {
            s_ride.aboard = 1;
            gevrDamBoatSeatNow();
            sysLogPrintf(LOG_NOTE, "dam boat: hand over the hull, boarded");
            return 1;
        }
    }
    dist = gevrHandPropDistance(s_boat, p);
    if (!(dist < 1.0e20f))
    {
        /* No hull box yet: the hand's world distance to the boat. */
        f32 s = D_800364CC > 1.0e-6f ? D_800364CC : 1.0f;
        f32 dx = p[0] / s - s_ride.x;
        f32 dy = p[1] / s - s_hullY;
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
    if (!gevrDamBoatPlayable() || !s_ride.aboard)
    {
        return;
    }
    {
        /* left on the stick turns the bow left (user, 2026-10-15); the dock stops it */
        const f32 x = s_ride.x, z = s_ride.z, yaw = s_ride.yaw;

        gevrBoatDrive(&s_ride, -stick_x, stick_y, g_GlobalTimerDelta);
        if (gevrDamBoatHullBlocked(s_ride.x, s_ride.z, s_ride.yaw))
        {
            s_ride.x = x;
            s_ride.z = z;
            s_ride.yaw = yaw;
        }
    }
    gevrDamBoatPush();
    {
        static u64 last;
        const u64 now = sysGetMicroseconds();

        if (now - last > 1000000)
        {
            last = now;
            sysLogPrintf(LOG_NOTE, "dam boat: ride at %.0f %.0f yaw %.2f, stick %.0f %.0f", s_ride.x, s_ride.z, s_ride.yaw,
                         stick_x, stick_y);
        }
    }
}

int gevrDamBoatBoardNow(void)
{
    if (!gevrDamBoatPlayable())
    {
        return 0;
    }
    s_ride.aboard = 1;
    gevrDamBoatSeatNow();
    gevrDamBoatPush();
    return 1;
}

void gevrDamBoatApplySeat(void)
{
    if (!gevrDamBoatLive() || !s_ride.aboard)
    {
        return;
    }
    gevrDamBoatSeatNow();
    gevrDamBoatPush();
}
