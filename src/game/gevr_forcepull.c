/*
 * Force pull (docs/force-pull.md; launcher Gestures page, watch VR page; ini
 * GestureForcePull): GEVR PC vr457's force pull. An empty hand pointed at
 * something within 3.5 m, in line of sight, highlights it; squeezing the grip
 * flies it to that hand. Still gripping when it arrives, a gun lands in the
 * hand (the pulling one if the rules allow, else the empty gun hand), ready
 * to fire; let go on the way and it goes to the inventory. Ammo, armour,
 * keys and mission items are collected on the catch. A gun the game won't
 * take (full ammo, the pair carried) is absorbed: it flies in, is gone, and
 * the hand holds the copy carried.
 *
 * The real prop flies (its own model, whatever the art set): its position is
 * set each tick here, after propsTick (lv.c, gevrGripGestureTick), its tile
 * walked and its rooms re-registered as objTick does for a moving object.
 * An airborne drop gives up its projectile to the pull. Walking over a
 * pulled prop doesn't collect it (objTickPlayer, gevrPullBlocksWalkOver).
 *
 * Arithmetic: port/src/gevr_pull.c (tested on its own).
 *
 * Online (protocol 26, net_core.c NET_MSG_FORCE_PULL): each step goes to the
 * other headsets, which fly their own copy of the prop to this player's
 * tracked hand (gevr_avatar.c gevrNetRemoteHandWorld), keep it there, let it
 * go or take it away. A setup pickup is named by its index and the host
 * refuses a second pull on it; a dropped gun or a grenade (the per-headset
 * pools) by its item and where it is. A disarm asks the host's guard to let
 * go. A caught grenade becomes the catcher's: its blast is sent by the
 * catcher's headset (explosion.c), and the others' copies wait for it.
 */

#include <ultra64.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <snd.h>
#include <music.h>
#include "system.h"
#include "bondview.h"
#include "matrixmath.h"
#include "player.h"
#include "lv.h"
#include "net_game.h"
#include "dyn.h"
#include "model.h"
#include "chrai.h"
#include "stan.h"
#include "propobj.h"
#include "loadobjectmodel.h"
#include "gevr_pull.h"

extern int VrForcePull, VrPullDisarm, VrPullVisuals, VrAutoPickup;
extern s32 g_gevrStereo;
extern f32 D_800364CC;
extern bool netIsActive(void);
extern int netGetLocalSlot(void);
extern s32 trigger_haptic_vibration_c(int hand_index, float amplitude, float duration);
extern int vr_haptics_ready(void);
extern s32 getCurrentPlayerWeaponId(s32 hand);
extern bool objCanPickupFromSafe(ObjectRecord *obj);
extern void sub_GAME_7F0402B4(PropRecord *prop, rgba_u8 *color);
extern void projectileFree(Projectile *projectile);
extern Gfx *gevrDrawInteractionRing(Gfx *gdl, const f32 at[3], f32 radius, u32 rgba);
extern Gfx *gevrDrawInteractionHalo(Gfx *gdl, const f32 at[3], f32 radius, u32 rgba);   /* gevr_bodyslots.c */
extern s32 gevrStereoWatchFace(f32 o[3], f32 n[3], f32 *cm);   /* bondview2.c */
/* bondview2.c */
extern s32 gevrPullInPlay(void);
extern s32 gevrPullHandFree(s32 ctrl);
extern s32 gevrPullHandRay(s32 ctrl, f32 p[3], f32 f[3]);
extern void gevrDisarmedMark(ChrRecord *chr);   /* gevr_disarmed.c */
extern s32 gevrPullCollect(PropRecord *prop);
extern s32 gevrPullEquip(s32 ctrl, s32 item);
extern s32 gevrWeaponOwned(s32 item);
extern s32 g_gevrExtraPass;            /* lv.c: an extra view pass (gevrViewPass) */
/* online (net_core.c, net_coop.c, gevr_avatar.c) */
extern void netSendForcePull(s32 phase, s32 hand, ObjectRecord *obj, s32 type, s32 item, s32 aux, s32 guard,
                             const f32 *pos, const f32 *vel, f32 dur, s32 timer);
extern bool netSlotOccupied(int slot);
extern int netHordeActive(void);
extern int netCoopGuardHostSlot(ChrRecord *chr);
extern ChrRecord *netCoopGuardOfHostSlot(int hostslot);
extern int gevrNetRemoteHandWorld(int slot, int hand, float out[3]);
extern void netSendObjectPickup(ObjectRecord *obj, s32 tickop);
extern void gevrCoopGuardForget(ObjectRecord *obj);   /* net_coop.c: a guard's grenade, caught: a player's now */

#define GEVR_UNITS_PER_METRE 100.0f
#define GEVR_PULL_REACH_GRAB_CM 25.0f   /* Grip to hand's own reach (bondview2.c GEVR_GT_PICKUP): closer is a grab */

/*
 * Lock, flick, catch (owner, 2026-11-06: Half-Life: Alyx's gravity gloves,
 * flick always, caught with the grip). The squeeze locks what is lit; a
 * flick while still gripping sends it; the grip closed when it reaches the
 * hand catches it, and an open hand lets it fall.
 */
#define GEVR_PULL_FLICK_RAD_S 3.5f     /* wrist flick: the pointing direction turning this fast (200 deg/s) */
#define GEVR_PULL_FLICK_UP_S  2.0f     /* ... with its tip rising (unit vector a second) */
#define GEVR_PULL_YANK_CM_S   150.0f   /* or the hand pulled back from the target this fast */
#define GEVR_PULL_HOMING_CM   30.0f    /* the flight follows the hand this far from where it flicked */
#define GEVR_PULL_CATCH_CM    20.0f    /* the grip closed with it this near the hand: caught */
#define GEVR_PULL_HALO_CM     26.0f    /* the target's glow, middle to rim: about a rifle's half */
#define GEVR_PULL_MISS_S      0.45f    /* past the flight's end, not caught: it falls (a fresh squeeze needs the time) */

typedef struct
{
    PropRecord *prop;       /* flying, or NULL */
    ObjectRecord *obj;      /* the prop's object at launch (a freed prop gets another) */
    u8 type;
    s32 item;               /* weaponnum, or -1 */
    f32 from[3];            /* world, at launch */
    f32 t, dur;             /* seconds */
    s32 released;           /* a held thing: the grip let go (it is thrown) */
    s32 buzz;
    s32 disarm;             /* still in a guard's hand: waiting for his tick to drop it */
    f32 wait;               /* seconds waited for that */
    f32 to[3];              /* world: where it flies, the hand's point within reach of aim */
    f32 aim[3];             /* world: the hand's point at the flick */
    f32 over;               /* seconds past the flight's end without a catch */
    s32 opened;             /* the hand opened since the flick: a squeeze now catches */
    s32 held;               /* caught and kept: follows the hand until the grip lets go */
    s32 grenade;            /* a live grenade (caught, never collected) */
} GevrPull;

static GevrPull s_pull[2];

/* Online: another player's pull as this headset shows it (its copy of the prop) */
#define GEVR_PULL_REMOTE_MAX 16
typedef struct
{
    PropRecord *prop;       /* NULL: a dropped gun not found yet (pending) */
    ObjectRecord *obj;
    u8 type;                /* the prop's type */
    s32 used;
    s32 slot, ctrl;         /* the puller and its controller (0 off hand, 1 gun hand) */
    s32 objtype, item;      /* to find a pool copy by */
    f32 at[3];              /* where the puller had it */
    f32 from[3], t, dur;
    s32 held;               /* kept in the hand: follows it */
    f32 wait;               /* pending, or past the flight's end without a word */
    u32 stamp;              /* g_GlobalTimer at the last word (the host's claims age out) */
} GevrRemotePull;

static GevrRemotePull s_remote[GEVR_PULL_REMOTE_MAX];
static PropRecord *s_aim[2];      /* what each hand points at (highlight, line) */
static f32 s_aimAt[2][3];         /* its point, view space, hand units */
static f32 s_handAt[2][3];        /* the hand's touch point, view space, hand units */
static s32 s_aimValid[2];
static PropRecord *s_aimLast[2];  /* the last thing lit, kept a moment after the hand moves off */
static f32 s_aimLastAge[2];       /* seconds since it was lit */
static PropRecord *s_lock[2];     /* locked by the squeeze, waiting for the flick */
static f32 s_lockAge[2];
static s32 s_gripHeld[2];         /* the grip as the pad reads it (bondview2.c gevrGripGestureInput) */
static f32 s_handFwd[2][3];       /* each hand's pointing direction in the world, this tick */
static f32 s_handFwdPrev[2][3];   /* and last tick's */
static s32 s_handFwdTicks[2];     /* ticks tracked in a row (a flick needs two) */
static s32 s_caughtAt[2] = { -1000, -1000 };   /* g_GlobalTimer at the last catch */
static f32 s_handRel[2][3];       /* the hand's point from the player's position (walking left out) */
static f32 s_handRelVel[2][3];    /* and its speed, world units a second: the yank's */
static ALSoundState *s_hum;
static f32 s_instantAge[2];       /* gunfire.c g_gevrSwitchInstant: cleared if no switch came of it */
extern s32 g_gevrSwitchInstant[2];
static f32 s_glowPhase;
static f32 s_watchGlow;           /* 0..1: the watch face lit while a pull is on its way */
static f32 s_handWorld[2][3];     /* each hand's touch point in the world, this tick */
static f32 s_handVel[2][3];       /* and its speed, world units a second */
static s32 s_handWorldValid[2];

static f32 gevrPullCm(void)
{
    return GEVR_UNITS_PER_METRE * D_800364CC / 100.0f;
}

/* the props' camera units to the hand's (chrprop.c gevrHandViewScale) */
static f32 gevrPullViewScale(void)
{
    return g_gevrStereo && D_800364CC > 1e-6f ? D_800364CC : 1.0f;
}

/* offline multi-player (no VR there): no pull. Online is synced (protocol 26). */
static s32 gevrPullOnline(void)
{
    return !netIsActive() && getPlayerCount() != 1;
}

static s32 gevrPullIsGun(s32 item)
{
    return (GEVR_ITEM_AS(item) >= ITEM_WPPK && GEVR_ITEM_AS(item) <= ITEM_ROCKETLAUNCH && GEVR_ITEM_AS(item) != ITEM_WATCHLASER)
        || item == ITEM_KNIFE;
}

/* a grenade whose fuse is running, loose (thrown by a guard, a player, or lying there) */
static s32 gevrPullLiveGrenade(ObjectRecord *obj)
{
    return obj != NULL && obj->type == PROPDEF_COLLECTABLE && ((WeaponObjRecord *) obj)->weaponnum == ITEM_GRENADE
        && ((WeaponObjRecord *) obj)->timer >= 0;
}

static s32 gevrPullIsMine(s32 item)
{
    return item == ITEM_REMOTEMINE || item == ITEM_PROXIMITYMINE || item == ITEM_TIMEDMINE;
}

/* the prop is still the one launched (taken, freed and reused props are not) */
static s32 gevrPullAlive(const GevrPull *pl)
{
    PropRecord *prop = pl->prop;

    return prop != NULL && prop->type == pl->type && prop->obj == pl->obj && pl->obj->prop == prop
        && (pl->disarm || (prop->flags & PROPFLAG_ENABLED)) && !(pl->obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE);
}

s32 gevrPullBusy(PropRecord *prop)
{
    s32 i;

    for (i = 0; i < 2; i++)
    {
        if (s_pull[i].prop == prop && prop != NULL)
        {
            return TRUE;
        }
    }
    for (i = 0; i < GEVR_PULL_REMOTE_MAX; i++)
    {
        if (s_remote[i].used && s_remote[i].prop == prop && prop != NULL)
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* online: tell the others (net_core.c); offline nothing */
static void gevrPullSend(s32 phase, s32 ctrl, ObjectRecord *obj, s32 objtype, s32 item, s32 aux, s32 guard,
                         const f32 pos[3], const f32 vel[3], f32 dur, s32 timer)
{
    if (netIsActive())
    {
        netSendForcePull(phase, ctrl, obj, objtype, item, aux, guard, pos, vel, dur, timer);
    }
}

static void gevrPullSendAt(s32 phase, s32 ctrl, PropRecord *prop, s32 item, s32 aux, const f32 vel[3], f32 dur, s32 timer)
{
    f32 pos[3];

    pos[0] = prop->pos.x;
    pos[1] = prop->pos.y;
    pos[2] = prop->pos.z;
    gevrPullSend(phase, ctrl, prop->obj, prop->obj->type, item, aux, -1, pos, vel, dur, timer);
}

/*
 * propobj.c objTickPlayer: walking over doesn't collect a prop in flight to
 * a hand, nor (Auto pickup off, GEVR PC) a gun at all: grab or pull it.
 */
s32 gevrPullBlocksWalkOver(PropRecord *prop)
{
    ObjectRecord *obj;

    if (prop == NULL)
    {
        return FALSE;
    }
    if (gevrPullBusy(prop))
    {
        return TRUE;
    }
    if (VrAutoPickup || !g_gevrStereo || (netIsActive() && get_cur_playernum() != netGetLocalSlot()))
    {
        return FALSE;
    }
    obj = prop->obj;
    return prop->type == PROP_TYPE_WEAPON && obj != NULL && obj->type == PROPDEF_COLLECTABLE
        && gevrPullIsGun(((WeaponObjRecord *) obj)->weaponnum);
}

/*
 * Whether a prop can be pulled: something walking over would collect (or a
 * gun the game would refuse), loose (no guard's or player's, no hat), not
 * in a safe, no mine and nothing live. Line of sight from the player's tile
 * as walking over checks it (objTickPlayer: doors block, the walk must end
 * on the prop's tile); a falling drop is let through once the walk is clear.
 */
static s32 gevrPullCandidate(PropRecord *prop)
{
    ObjectRecord *obj;
    PropRecord *me = getCurrentPlayerProp();
    StandTile *stan;

    if (prop == NULL || !(prop->flags & PROPFLAG_ONSCREEN) || !(prop->flags & PROPFLAG_ENABLED)
        || (prop->type != PROP_TYPE_WEAPON && prop->type != PROP_TYPE_OBJ) || prop->parent != NULL
        || gevrPullBusy(prop) || me == NULL || me->stan == NULL || prop->stan == NULL)
    {
        return FALSE;
    }
    obj = prop->obj;
    if (obj == NULL || obj->model == NULL || obj->model->render_pos == NULL
        || !objIsCollectable((PropDefHeaderRecord *) obj) || obj->type == PROPDEF_HAT
        || (obj->flags & (PROPFLAG_UNCOLLECTABLE | PROPFLAG_00080000))
        || (obj->runtime_bitflags & (RUNTIMEBITFLAG_EMBEDDED | RUNTIMEBITFLAG_REMOVE))
        || !objCanPickupFromSafe(obj))
    {
        return FALSE;
    }
    if (obj->type == PROPDEF_COLLECTABLE)
    {
        WeaponObjRecord *wep = (WeaponObjRecord *) obj;

        if (gevrPullIsMine(wep->weaponnum) || wep->weaponnum == ITEM_ROCKETROUND
            || wep->weaponnum == ITEM_GRENADEROUND || wep->weaponnum == ITEM_BOMBCASE
            || wep->weaponnum == ITEM_BUG || wep->weaponnum == ITEM_MICROCAMERA || wep->weaponnum == ITEM_PLASTIQUE
            || (wep->weaponnum == ITEM_GRENADE && wep->timer < 0 && (obj->runtime_bitflags & 4)))
        {
            return FALSE;   /* mines and the other explosives are not this pull's; a live grenade is caught */
        }
    }
    if ((obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) && obj->projectile != NULL
        && (obj->projectile->flags & PROJECTILEFLAG_STICKY))
    {
        return FALSE;
    }
    stan = me->stan;
    if (stanTestLineUnobstructed(&stan, me->pos.x, me->pos.z, prop->pos.x, prop->pos.z, CDTYPE_DOORS, 30.0f, 30.0f, 0.0f, 1.0f) == 0)
    {
        return FALSE;
    }
    if (stan != prop->stan && !((obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) && obj->projectile != NULL
                                && (obj->projectile->flags & PROJECTILEFLAG_AIRBORNE)))
    {
        return FALSE;
    }
    return TRUE;
}

/* the room's down in view space, for "pointing down" */
static void gevrPullViewDown(f32 down[3])
{
    coord3d up = { 0.0f, 1.0f, 0.0f };
    f32 len;

    mtx4RotateVecInPlace(camGetWorldToScreenMtxf(), &up);
    len = sqrtf(up.x * up.x + up.y * up.y + up.z * up.z);
    if (len < 1e-6f)
    {
        down[0] = 0.0f; down[1] = -1.0f; down[2] = 0.0f;
        return;
    }
    down[0] = -up.x / len;
    down[1] = -up.y / len;
    down[2] = -up.z / len;
}

/*
 * The ray that picks a target (view space, hand units): from the palm along
 * the arm, shoulder through hand (gevrPullArmRay), not the controller's
 * barrel: the barrel made the aim follow the wrist (headset, test.21). The
 * flick still reads the barrel (gevrPullTrackHand): it is a turn of the wrist.
 */
static s32 gevrPullAimRay(s32 ctrl, f32 p[3], f32 f[3])
{
    extern int VrLeftHandedMode;
    f32 barrel[3], down[3];
    s32 phys = VrLeftHandedMode ? 1 - ctrl : ctrl;   /* 1 the right hand (vr_input.cpp gevrPhysHand) */

    if (!gevrPullHandRay(ctrl, p, barrel))
    {
        return FALSE;
    }
    gevrPullViewDown(down);
    gevrPullArmRay(p, barrel, down, phys == 1 ? 1.0f : -1.0f, gevrPullCm(), f);
    return TRUE;
}

/*
 * Pull and disarm (VrPullDisarm, GEVR PC vr457): the gun in a living guard's
 * hand h, one the AI may drop, in line of sight; the guard's own tile and
 * position stand for the gun's (a held gun has neither).
 */
static PropRecord *gevrPullDisarmable(PropRecord *chrprop, s32 h)
{
    ChrRecord *chr = chrprop->chr;
    PropRecord *gun, *me = getCurrentPlayerProp();
    StandTile *stan;

    if (!VrPullDisarm || chr == NULL || chrprop->stan == NULL || me == NULL || me->stan == NULL
        || !(chrprop->flags & PROPFLAG_ONSCREEN) || chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD)
    {
        return NULL;
    }
    if (netIsActive() && (!gevrCoopActive() || netHordeActive() || netCoopGuardHostSlot(chr) < 0))
    {
        return NULL;   /* online: a co-op guard the host runs, asked by its slot */
    }
    gun = chr->weapons_held[h];
    if (gun == NULL || gun->type != PROP_TYPE_WEAPON || gun->obj == NULL || gun->obj->type != PROPDEF_COLLECTABLE
        || gun->parent != chrprop || (gun->obj->flags & PROPFLAG_AIUNDROPPABLE) || gevrPullBusy(gun)
        || !gevrPullIsGun(((WeaponObjRecord *) gun->obj)->weaponnum))
    {
        return NULL;
    }
    stan = me->stan;
    if (stanTestLineUnobstructed(&stan, me->pos.x, me->pos.z, chrprop->pos.x, chrprop->pos.z, CDTYPE_DOORS, 30.0f, 30.0f, 0.0f, 1.0f) == 0
        || stan != chrprop->stan)
    {
        return NULL;
    }
    return gun;
}

/*
 * Where the head's view last drew each gun in a hand (propobj.c
 * chrRenderHeldWeapon), in the world. Its matrices are camera space, and the
 * extra view passes (lv.c gevrViewPass: a hand firing outside the head's
 * view, the copies online) place guards and their guns again in other
 * cameras' spaces, so the camera space is turned into the world as it is
 * drawn, under the camera it was drawn with.
 */
#define GEVR_PULL_HELD_NOTES 16

static struct
{
    PropRecord *gun;
    f32 w[3];
    s32 at;                /* g_GlobalTimer when drawn */
} s_heldNote[GEVR_PULL_HELD_NOTES];
static s32 s_heldNoteNext;

void gevrPullNoteHeldGun(PropRecord *gun, const Mtxf *m, s32 drawn)
{
    Mtxf *v2w;
    coord3d c;
    s32 i, slot = -1;

    if (!drawn || gun == NULL || m == NULL || g_gevrExtraPass || !g_gevrStereo
        || (netIsActive() && get_cur_playernum() != netGetLocalSlot())
        || (v2w = currentPlayerGetViewToWorldMtxf()) == NULL)
    {
        return;
    }
    c.x = m->m[3][0];
    c.y = m->m[3][1];
    c.z = m->m[3][2];
    if (!isfinite(c.x) || !isfinite(c.y) || !isfinite(c.z) || fabsf(c.x) > 1.0e6f || fabsf(c.y) > 1.0e6f || fabsf(c.z) > 1.0e6f)
    {
        return;   /* not computed this frame */
    }
    mtx4TransformVecInPlace(v2w, &c);
    {
        /*
         * m is his hand's bone, so the ring sat on his hand and the gun's grip
         * (headset, test.22). The gun's own root, as a gun on the floor is lit
         * (gevrPullPointOf): its model's origin is the gun's middle, hung from
         * the hand by the root's offset. propobj.c chrRenderHeldWeapon has just
         * worked it out (instcalcmatrices -> render_pos; the matrix list
         * pointer it advanced is what read NaN before). Not sensible (not
         * 2-60 cm from the hand): the hand, as before.
         */
        Model *model = gun->obj != NULL ? gun->obj->model : NULL;
        const Mtxf *root = model != NULL ? (const Mtxf *) model->render_pos : NULL;

        if (root != NULL && isfinite(root->m[3][0]) && isfinite(root->m[3][1]) && isfinite(root->m[3][2])
            && fabsf(root->m[3][0]) < 1.0e6f && fabsf(root->m[3][1]) < 1.0e6f && fabsf(root->m[3][2]) < 1.0e6f)
        {
            coord3d g;
            f32 d2;

            g.x = root->m[3][0];
            g.y = root->m[3][1];
            g.z = root->m[3][2];
            mtx4TransformVecInPlace(v2w, &g);
            d2 = (g.x - c.x) * (g.x - c.x) + (g.y - c.y) * (g.y - c.y) + (g.z - c.z) * (g.z - c.z);
            if (d2 >= 2.0f * 2.0f && d2 <= 60.0f * 60.0f)   /* world cm */
            {
                c = g;
            }
            if ((g_GlobalTimer & 255) == 0)
            {
                sysLogPrintf(LOG_NOTE, "stereo: pull: held gun's middle %.0f cm from the hand%s", sqrtf(d2),
                             d2 >= 2.0f * 2.0f && d2 <= 60.0f * 60.0f ? "" : " (not used)");
            }
        }
    }
    for (i = 0; i < GEVR_PULL_HELD_NOTES; i++)
    {
        if (s_heldNote[i].gun == gun)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        slot = s_heldNoteNext++ % GEVR_PULL_HELD_NOTES;
    }
    s_heldNote[slot].gun = gun;
    s_heldNote[slot].w[0] = c.x;
    s_heldNote[slot].w[1] = c.y;
    s_heldNote[slot].w[2] = c.z;
    s_heldNote[slot].at = g_GlobalTimer;
}

/*
 * Where a guard's gun is, view space, hand units: where the head's view drew
 * it in his hand within the last few ticks (headset, Dam: the first guess,
 * his position plus 1.1 m, put the ring and the line well above his head).
 * Not drawn lately: his feet plus 1 m.
 */
static s32 gevrPullDisarmPoint(PropRecord *chrprop, PropRecord *gun, f32 t[3])
{
    ChrRecord *chr = chrprop->chr;
    f32 s = gevrPullViewScale();
    coord3d c;
    s32 i;

    c.x = chrprop->pos.x;
    c.y = (chr != NULL ? chr->ground : chrprop->pos.y) + 100.0f;
    c.z = chrprop->pos.z;
    for (i = 0; i < GEVR_PULL_HELD_NOTES; i++)
    {
        if (s_heldNote[i].gun == gun && (u32) (g_GlobalTimer - s_heldNote[i].at) < 10u)
        {
            f32 dx = s_heldNote[i].w[0] - chrprop->pos.x, dz = s_heldNote[i].w[2] - chrprop->pos.z;

            /* in his hand, not a stale note for a prop reused since */
            if (dx * dx + dz * dz < 150.0f * 150.0f)
            {
                c.x = s_heldNote[i].w[0];
                c.y = s_heldNote[i].w[1];
                c.z = s_heldNote[i].w[2];
            }
            break;
        }
    }
    mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &c);
    t[0] = c.x * s;
    t[1] = c.y * s;
    t[2] = c.z * s;
    return TRUE;
}

/*
 * A target already lit stays lit (headset: "finicky"): it keeps the down
 * cone's 25 degrees instead of 15 and its score counts half, so a second
 * item near the ray takes over only when the hand clearly moves to it.
 */
#define GEVR_PULL_STICKY_SCORE 0.5f
/* squeezing the grip turns the controller a little: the squeeze pulls what was lit this long ago */
#define GEVR_PULL_GRACE_S      0.25f

static f32 gevrPullScoreFor(PropRecord *prop, PropRecord *prefer, const f32 p[3], const f32 f[3], const f32 t[3],
                            f32 cm, f32 downcos)
{
    f32 score;

    if (prop != prefer || prefer == NULL)
    {
        return gevrPullScore(p, f, t, cm, downcos);
    }
    score = gevrPullScore(p, f, t, cm, 1.0f);
    return score >= 0.0f ? score * GEVR_PULL_STICKY_SCORE : score;
}

/* the best target along the hand's ray (view space, hand units), or NULL; prefer: the one lit now */
static PropRecord *gevrPullFind(const f32 p[3], const f32 f[3], f32 at[3], PropRecord *prefer)
{
    PropRecord **ptr;
    PropRecord *best = NULL;
    f32 bestScore = 1e30f;
    f32 s = gevrPullViewScale();
    f32 cm = gevrPullCm();
    f32 down[3], downcos;

    gevrPullViewDown(down);
    downcos = f[0] * down[0] + f[1] * down[1] + f[2] * down[2];
    for (ptr = g_LastOnScreenProp - 1; ptr >= g_OnScreenPropList; ptr--)
    {
        PropRecord *prop = *ptr;
        Mtxf *m;
        f32 t[3], score, d2;

        if (prop != NULL && prop->type == PROP_TYPE_CHR)
        {
            /* pull and disarm: the guns in a guard's hands */
            s32 h;

            for (h = 0; h < 2; h++)
            {
                PropRecord *gun = gevrPullDisarmable(prop, h);

                if (gun != NULL && gevrPullDisarmPoint(prop, gun, t))
                {
                    score = gevrPullScoreFor(gun, prefer, p, f, t, cm, downcos);
                    if (score >= 0.0f && score < bestScore)
                    {
                        bestScore = score;
                        best = gun;
                        at[0] = t[0]; at[1] = t[1]; at[2] = t[2];
                    }
                }
            }
            continue;
        }
        if (!gevrPullCandidate(prop))
        {
            continue;
        }
        m = (Mtxf *) prop->obj->model->render_pos;
        t[0] = m->m[3][0] * s;
        t[1] = m->m[3][1] * s;
        t[2] = m->m[3][2] * s;
        d2 =(t[0] - p[0]) * (t[0] - p[0]) + (t[1] - p[1]) * (t[1] - p[1]) + (t[2] - p[2]) * (t[2] - p[2]);
        if (d2 < GEVR_PULL_REACH_GRAB_CM * cm * GEVR_PULL_REACH_GRAB_CM * cm * 0.25f)
        {
            continue;   /* in the palm already: Grip to hand's */
        }
        score = gevrPullScoreFor(prop, prefer, p, f, t, cm, downcos);
        if (score >= 0.0f && score < bestScore)
        {
            bestScore = score;
            best = prop;
            at[0] = t[0]; at[1] = t[1]; at[2] = t[2];
        }
    }
    return best;
}

/*
 * The squeeze's grace: what the hand lit a moment ago, if it can still be
 * pulled, is in reach and within 35 degrees of the ray (the squeeze turns the
 * controller; the light is what the player aimed with).
 */
/* a target that can still be pulled, and its point (view space, hand units) */
static s32 gevrPullPointOf(PropRecord *prop, f32 t[3])
{
    f32 s = gevrPullViewScale();
    s32 h;

    if (prop == NULL)
    {
        return FALSE;
    }
    if (prop->parent != NULL && prop->parent->type == PROP_TYPE_CHR)
    {
        for (h = 0; h < 2; h++)
        {
            if (gevrPullDisarmable(prop->parent, h) == prop && gevrPullDisarmPoint(prop->parent, prop, t))
            {
                return TRUE;
            }
        }
        return FALSE;
    }
    if (gevrPullCandidate(prop))
    {
        Mtxf *m = (Mtxf *) prop->obj->model->render_pos;

        t[0] = m->m[3][0] * s;
        t[1] = m->m[3][1] * s;
        t[2] = m->m[3][2] * s;
        return TRUE;
    }
    return FALSE;
}

static PropRecord *gevrPullRecent(s32 ctrl, const f32 p[3], const f32 f[3], f32 at[3])
{
    PropRecord *prop = s_aimLast[ctrl];
    f32 cm = gevrPullCm();
    f32 t[3], d[3], dist;

    if (prop == NULL || s_aimLastAge[ctrl] > GEVR_PULL_GRACE_S || cm < 1e-6f || !gevrPullPointOf(prop, t))
    {
        return NULL;
    }
    d[0] = t[0] - p[0]; d[1] = t[1] - p[1]; d[2] = t[2] - p[2];
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist > GEVR_PULL_RANGE_CM * cm || dist < 1e-4f * cm || (d[0] * f[0] + d[1] * f[1] + d[2] * f[2]) / dist < 0.819f)
    {
        return NULL;
    }
    at[0] = t[0]; at[1] = t[1]; at[2] = t[2];
    return prop;
}

/* a hand point (view space, hand units) to the world */
static s32 gevrPullToWorld(const f32 p[3], f32 w[3])
{
    Mtxf *v2w = currentPlayerGetViewToWorldMtxf();
    coord3d c;
    f32 s = gevrPullViewScale();

    if (v2w == NULL)
    {
        return FALSE;
    }
    c.x = p[0] / s;
    c.y = p[1] / s;
    c.z = p[2] / s;
    mtx4TransformVecInPlace(v2w, &c);
    w[0] = c.x; w[1] = c.y; w[2] = c.z;
    return TRUE;
}

/* the prop to world point w, as objTick moves an object (its tile, rooms, light) */
static void gevrPullPlace(PropRecord *prop, const f32 w[3])
{
    ObjectRecord *obj = prop->obj;
    StandTile *tile = prop->stan;

    if (tile != NULL && walkTilesBetweenPoints_NoCallback(&tile, prop->pos.x, prop->pos.z, w[0], w[2]) == TRUE && tile != NULL)
    {
        prop->stan = tile;
    }
    else
    {
        coord3d c;
        StandTile *below;

        c.x = w[0]; c.y = w[1]; c.z = w[2];
        below = stanFindTileBelowPos(&c, NULL, NULL);
        if (below != NULL)
        {
            prop->stan = below;
        }
    }
    obj->runtime_pos.x = prop->pos.x = w[0];
    obj->runtime_pos.y = prop->pos.y = w[1];
    obj->runtime_pos.z = prop->pos.z = w[2];
    chrobjCollisionRelated(obj);
    setupUpdateObjectRoomPosition(obj);
    sub_GAME_7F0402B4(prop, &obj->nextcol);
}

static void gevrPullHaptic(s32 ctrl, f32 amp, f32 dur)
{
    if (vr_haptics_ready())
    {
        trigger_haptic_vibration_c(ctrl, amp, dur);
    }
}

static void gevrPullHum(s32 on)
{
    if (on)
    {
        if (s_hum == NULL || sndGetPlayingState(s_hum) == 0)
        {
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, MAGNETIC_HUM_SFX, (ALSoundState *) &s_hum);
        }
    }
    else if (s_hum != NULL)
    {
        if (sndGetPlayingState(s_hum) != 0)
        {
            sndDeactivate(s_hum);
        }
        s_hum = NULL;
    }
}

/* the flight starts where the prop is; a falling drop gives its projectile up to it */
static void gevrPullLaunch(GevrPull *pl)
{
    PropRecord *prop = pl->prop;
    ObjectRecord *obj = pl->obj;

    pl->from[0] = prop->pos.x;
    pl->from[1] = prop->pos.y;
    pl->from[2] = prop->pos.z;
    pl->t = 0.0f;
    pl->over = 0.0f;
    pl->to[0] = pl->aim[0]; pl->to[1] = pl->aim[1]; pl->to[2] = pl->aim[2];
    pl->disarm = FALSE;
    if ((obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) && obj->projectile != NULL)
    {
        projectileFree(obj->projectile);
        obj->projectile = NULL;
        obj->runtime_bitflags &= ~RUNTIMEBITFLAG_HASPROJECTILE;
        prop->flags &= ~PROPFLAG_00000008;
    }
    gevrPullSend(GEVR_PULL_NET_FLY, (s32) (pl - s_pull), obj, obj->type, pl->item, 0, -1, pl->from, NULL, pl->dur, -1);
}

/* the flick: the pull leaves for the hand (a guard's gun waits for his tick to drop it) */
static void gevrPullStart(s32 ctrl, PropRecord *prop, const f32 p[3], const f32 at[3])
{
    GevrPull *pl = &s_pull[ctrl];
    ObjectRecord *obj = prop->obj;
    f32 cm = gevrPullCm();
    f32 d[3], dist;

    d[0] = at[0] - p[0]; d[1] = at[1] - p[1]; d[2] = at[2] - p[2];
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) / cm;
    memset(pl, 0, sizeof(*pl));
    pl->prop = prop;
    pl->obj = obj;
    pl->type = prop->type;
    pl->item = prop->type == PROP_TYPE_WEAPON && obj->type == PROPDEF_COLLECTABLE ? ((WeaponObjRecord *) obj)->weaponnum : -1;
    pl->grenade = gevrPullLiveGrenade(obj);
    pl->dur = gevrPullDuration(dist);
    if (s_handWorldValid[ctrl])
    {
        pl->aim[0] = s_handWorld[ctrl][0]; pl->aim[1] = s_handWorld[ctrl][1]; pl->aim[2] = s_handWorld[ctrl][2];
    }
    else if (!gevrPullToWorld(p, pl->aim))
    {
        pl->aim[0] = prop->pos.x; pl->aim[1] = prop->pos.y; pl->aim[2] = prop->pos.z;
    }
    if (prop->parent != NULL && prop->parent->type == PROP_TYPE_CHR && prop->parent->chr != NULL)
    {
        /* pull and disarm: the guard lets go as when a hit makes him drop his
         * guns (chraction.c triggered_on_shot_hit); his tick drops it
         * (chr.c objDrop) and the flight starts from where it is then */
        ChrRecord *guard = prop->parent->chr;

        propobjSetDropped(prop, DROPTYPE_DEFAULT);
        guard->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
        gevrDisarmedMark(guard);   /* he goes for a gun or his fists (gevr_disarmed.c) */
        pl->disarm = TRUE;
        /* online: the host's guard lets go too (this one is its copy here) */
        gevrPullSend(GEVR_PULL_NET_DISARM, ctrl, NULL, obj->type, pl->item, guard->weapons_held[GUNLEFT] == prop ? GUNLEFT : GUNRIGHT,
                     netIsActive() ? netCoopGuardHostSlot(guard) : -1, NULL, NULL, 0.0f, -1);
    }
    else
    {
        gevrPullLaunch(pl);
    }
    s_aim[ctrl] = NULL;
    s_aimLast[ctrl] = NULL;
    s_lock[ctrl] = NULL;
    gevrPullHum(TRUE);
    gevrPullHaptic(ctrl, 0.5f, 0.05f);
    sysLogPrintf(LOG_NOTE, "stereo: pull (%s): flicked: prop type %d obj %d item %d, %.0f cm, %.2f s",
                 ctrl ? "gun hand" : "off hand", prop->type, obj->type, pl->item, dist, pl->dur);
}

/*
 * bondview2.c gevrGripGestureTry, its last branch: a fresh grip with an
 * empty hand that nothing nearer claimed. Locks what is lit (or was, a
 * moment ago); the flick sends it (gevrPullTick). A grip while one is on its
 * way is the catch's (gevrPullTick reads the grip), so nothing else takes it.
 */
s32 gevrPullTry(s32 ctrl)
{
    PropRecord *prop;
    f32 p[3], f[3], at[3];
    f32 cm = gevrPullCm();

    if (!VrForcePull || ctrl < 0 || ctrl > 1 || gevrPullOnline())
    {
        return FALSE;
    }
    if (s_pull[ctrl].prop != NULL)
    {
        return !s_pull[ctrl].held;
    }
    if (!gevrPullInPlay() || !gevrPullHandFree(ctrl) || !gevrPullAimRay(ctrl, p, f) || cm < 1e-6f)
    {
        return FALSE;
    }
    prop = gevrPullFind(p, f, at, s_aim[ctrl]);
    if (prop == NULL && (prop = gevrPullRecent(ctrl, p, f, at)) != NULL)
    {
        sysLogPrintf(LOG_NOTE, "stereo: pull (%s): off the ray at the squeeze, locking what was lit %.2f s ago",
                     ctrl ? "gun hand" : "off hand", s_aimLastAge[ctrl]);
    }
    if (prop == NULL)
    {
        sysLogPrintf(LOG_NOTE, "stereo: pull (%s): nothing pointed at", ctrl ? "gun hand" : "off hand");
        return FALSE;
    }
    s_lock[ctrl] = prop;
    s_lockAge[ctrl] = 0.0f;
    s_aim[ctrl] = prop;
    gevrPullHaptic(ctrl, 0.35f, 0.04f);
    sysLogPrintf(LOG_NOTE, "stereo: pull (%s): locked prop type %d obj %d, %.0f cm: flick to pull",
                 ctrl ? "gun hand" : "off hand", prop->type, prop->obj != NULL ? prop->obj->type : -1,
                 sqrtf((at[0] - p[0]) * (at[0] - p[0]) + (at[1] - p[1]) * (at[1] - p[1]) + (at[2] - p[2]) * (at[2] - p[2])) / cm);
    return TRUE;
}

/*
 * bondview2.c gevrGripGestureTry, before every other grip job: a press while
 * a pull flies to this hand, or one that has just caught it, is the catch's
 * (not a body slot's or the holster's).
 */
s32 gevrPullClaimsPress(s32 ctrl)
{
    if (ctrl < 0 || ctrl > 1)
    {
        return FALSE;
    }
    return (s_pull[ctrl].prop != NULL && !s_pull[ctrl].held) || (s32) (g_GlobalTimer - s_caughtAt[ctrl]) < 12;
}

/* port/src/input.c via bondview2.c gevrGripGestureInput: the grip, every pad read (the catch) */
void gevrPullGripInput(s32 ctrl, s32 held)
{
    if (ctrl >= 0 && ctrl < 2)
    {
        s_gripHeld[ctrl] = held != 0;
        if (!held && s_pull[ctrl].prop != NULL && !s_pull[ctrl].held)
        {
            s_pull[ctrl].opened = TRUE;
        }
    }
}

/* bondview2.c gevrGripGestureInput: the grip let go: a lock ends, a held thing is thrown */
void gevrPullGripLetGo(s32 ctrl)
{
    if (ctrl < 0 || ctrl > 1)
    {
        return;
    }
    s_gripHeld[ctrl] = FALSE;
    if (s_pull[ctrl].prop != NULL && !s_pull[ctrl].held)
    {
        s_pull[ctrl].opened = TRUE;   /* open to catch it */
    }
    if (s_lock[ctrl] != NULL)
    {
        sysLogPrintf(LOG_NOTE, "stereo: pull (%s): let go before a flick, lock dropped", ctrl ? "gun hand" : "off hand");
        s_lock[ctrl] = NULL;
    }
    if (s_pull[ctrl].prop != NULL && s_pull[ctrl].held)
    {
        s_pull[ctrl].released = TRUE;
    }
}

static void gevrPullEnd(s32 ctrl)
{
    s_pull[ctrl].prop = NULL;
    if (s_pull[1 - ctrl].prop == NULL)
    {
        gevrPullHum(FALSE);
    }
}

/*
 * Let go of a held thing (a caught grenade, something the game refused):
 * it leaves the hand at the hand's speed, as Bond's own throw, and falls
 * onto whatever is below (projectile physics, the player's own body left
 * out of its collision as the owner). A grenade is the player's from then
 * on: its blast is credited to him.
 */
/* a prop leaves a hand at vel (world units a second), owner's body out of its collision */
static void gevrPullFling(PropRecord *prop, const f32 vel[3], PropRecord *owner)
{
    ObjectRecord *obj = prop->obj;

    sub_GAME_7F03FDA8(prop);
    if ((obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) && obj->projectile != NULL)
    {
        Projectile *pr = obj->projectile;

        pr->obj = obj;
        pr->flags |= PROJECTILEFLAG_AIRBORNE;
        pr->speed.x = vel[0] / 60.0f;   /* units a second to a 60 Hz tick's */
        pr->speed.y = vel[1] / 60.0f;
        pr->speed.z = vel[2] / 60.0f;
        pr->ownerprop = owner;
        matrix_4x4_set_identity(&pr->mtx);
    }
}

/* a grenade is now this slot's: its blast is sent by that slot's headset (explosion.c) */
static void gevrPullOwnGrenade(ObjectRecord *obj, s32 slot)
{
    if (slot >= 0 && slot < MAX_PLAYER_COUNT)
    {
        obj->runtime_bitflags = (obj->runtime_bitflags & ~RUNTIMEBITFLAG_OWNER_ALL) | RUNTIME_OWNER_BITS(slot);
    }
}

static void gevrPullThrow(s32 ctrl, const f32 vel[3])
{
    GevrPull *pl = &s_pull[ctrl];
    PropRecord *prop = pl->prop;
    ObjectRecord *obj = pl->obj;

    gevrPullFling(prop, vel, getCurrentPlayerProp());
    if (pl->grenade)
    {
        gevrPullOwnGrenade(obj, get_cur_playernum());
    }
    gevrPullSendAt(GEVR_PULL_NET_RELEASE, ctrl, prop, pl->item, 0, vel, 0.0f,
                   pl->grenade ? ((WeaponObjRecord *) obj)->timer : -1);
    sysLogPrintf(LOG_NOTE, "stereo: pull (%s): %s item %d obj %d let go at %.0f %.0f %.0f /s",
                 ctrl ? "gun hand" : "off hand", pl->grenade ? "grenade" : "held", pl->item, obj->type, vel[0], vel[1], vel[2]);
    pl->prop = NULL;
    gevrPullEnd(ctrl);
}

/* a caught thing the hand keeps (it follows the hand until the grip lets go) */
static void gevrPullHold(s32 ctrl)
{
    GevrPull *pl = &s_pull[ctrl];

    pl->held = TRUE;
    pl->disarm = FALSE;
    if ((pl->obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) && pl->obj->projectile != NULL)
    {
        projectileFree(pl->obj->projectile);
        pl->obj->projectile = NULL;
        pl->obj->runtime_bitflags &= ~RUNTIMEBITFLAG_HASPROJECTILE;
        pl->prop->flags &= ~PROPFLAG_00000008;
    }
    if (pl->grenade)
    {
        /* caught: the catcher's, so a blast in the hand is his too (online his headset sends it) */
        gevrPullOwnGrenade(pl->obj, get_cur_playernum());
        gevrCoopGuardForget(pl->obj);
    }
    gevrPullSendAt(GEVR_PULL_NET_HOLD, ctrl, pl->prop, pl->item, 0, NULL, 0.0f, -1);
    gevrPullHum(s_pull[1 - ctrl].prop != NULL && !s_pull[1 - ctrl].held);
}

/* the flight is over: catch it */
static void gevrPullArrive(s32 ctrl)
{
    GevrPull *pl = &s_pull[ctrl];
    PropRecord *prop = pl->prop;
    s32 item = pl->item;
    s32 gun = item >= 0 && gevrPullIsGun(item);
    s32 hand = -1;
    f32 at[3];
    s32 objtype;

    gevrPullHaptic(ctrl, 0.8f, 0.08f);
    if (pl->grenade)
    {
        /* caught: the fuse runs on (propobj.c's weapon tick) */
        gevrPullHold(ctrl);
        sysLogPrintf(LOG_NOTE, "stereo: pull (%s): caught a live grenade", ctrl ? "gun hand" : "off hand");
        return;
    }
    pl->prop = NULL;   /* no longer ours: walking over and the game's rules again */
    at[0] = prop->pos.x;
    at[1] = prop->pos.y;
    at[2] = prop->pos.z;
    objtype = pl->obj->type;
    if (gevrPullCollect(prop))
    {
        if (gun && !pl->released)
        {
            hand = gevrPullEquip(ctrl, item);
        }
        if (hand >= 0)
        {
            s_instantAge[hand] = 1.0f;
        }
        sysLogPrintf(LOG_NOTE, "stereo: pull (%s): caught item %d, %s", ctrl ? "gun hand" : "off hand", item,
                     hand < 0 ? "to the inventory" : hand == GUNRIGHT ? "in the gun hand" : "in the off hand");
        /* after the pickup's own message (gevrHandPickup): a dropped gun's copies go too */
        gevrPullSend(GEVR_PULL_NET_DONE, ctrl, pl->obj, objtype, item, 0, -1, at, NULL, 0.0f, -1);
    }
    else if (gun && gevrWeaponOwned(item))
    {
        /* full ammo or the pair carried: absorbed, the hand takes the copy carried */
        if (pl->obj->state & PROPSTATE_RESPAWN)
        {
            /* a deathmatch gun that comes back: taken as walking over takes one, its
             * timer started here and (its own message) on the other headsets */
            netSendObjectPickup(pl->obj, TICKOP_FREE);
            gevrPullSendAt(GEVR_PULL_NET_DONE, ctrl, prop, item, 0, NULL, 0.0f, -1);
            propExecuteTickOperation(prop, TICKOP_FREE);
        }
        else
        {
            gevrPullSendAt(GEVR_PULL_NET_DONE, ctrl, prop, item, 1, NULL, 0.0f, -1);
            pl->obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
        }
        if (!pl->released)
        {
            hand = gevrPullEquip(ctrl, item);
        }
        if (hand >= 0)
        {
            s_instantAge[hand] = 1.0f;
        }
        sysLogPrintf(LOG_NOTE, "stereo: pull (%s): item %d absorbed (nothing to gain), carried one %s",
                     ctrl ? "gun hand" : "off hand", item, hand < 0 ? "stays put" : "to hand");
    }
    else
    {
        /* the game refuses it (full ammo or armour): a held item (GEVR PC vr457) */
        pl->prop = prop;
        gevrPullHold(ctrl);
        sysLogPrintf(LOG_NOTE, "stereo: pull (%s): item %d obj %d refused by the game's rules, held",
                     ctrl ? "gun hand" : "off hand", item, pl->obj->type);
        return;
    }
    gevrPullEnd(ctrl);
}

/*
 * bondview2.c, Grip to hand's own reach: a live grenade, or something the
 * game refused to collect, taken into the hand as a held thing.
 */
s32 gevrPullGrab(s32 ctrl, PropRecord *prop)
{
    GevrPull *pl;
    ObjectRecord *obj;

    if (ctrl < 0 || ctrl > 1 || prop == NULL || s_pull[ctrl].prop != NULL || gevrPullOnline() || gevrPullBusy(prop)
        || (prop->type != PROP_TYPE_WEAPON && prop->type != PROP_TYPE_OBJ) || prop->parent != NULL || prop->obj == NULL
        || (prop->obj->runtime_bitflags & (RUNTIMEBITFLAG_EMBEDDED | RUNTIMEBITFLAG_REMOVE)))
    {
        return FALSE;
    }
    obj = prop->obj;
    pl = &s_pull[ctrl];
    memset(pl, 0, sizeof(*pl));
    pl->prop = prop;
    pl->obj = obj;
    pl->type = prop->type;
    pl->item = prop->type == PROP_TYPE_WEAPON && obj->type == PROPDEF_COLLECTABLE ? ((WeaponObjRecord *) obj)->weaponnum : -1;
    pl->grenade = gevrPullLiveGrenade(obj);
    if (pl->item >= 0 && gevrPullIsMine(pl->item))
    {
        memset(pl, 0, sizeof(*pl));
        return FALSE;   /* mines are the re-grab's */
    }
    gevrPullHold(ctrl);
    sysLogPrintf(LOG_NOTE, "stereo: grab (%s): %s item %d obj %d held", ctrl ? "gun hand" : "off hand",
                 pl->grenade ? "live grenade" : "refused", pl->item, obj->type);
    return TRUE;
}

/* the hand's world point and speed, each tick (a throw's speed) */
static void gevrPullTrackHand(s32 ctrl, f32 dt)
{
    f32 p[3], f[3], w[3];
    s32 k;

    if (!gevrPullHandRay(ctrl, p, f) || !gevrPullToWorld(p, w))
    {
        s_handWorldValid[ctrl] = FALSE;
        s_handFwdTicks[ctrl] = 0;
        return;
    }
    {
        /* the pointing direction in the world (the head's turn left out): the flick's */
        f32 tip[3], q[3], wt[3], len;
        f32 cm = gevrPullCm();

        for (k = 0; k < 3; k++)
        {
            tip[k] = p[k] + f[k] * 10.0f * cm;
        }
        if (gevrPullToWorld(tip, wt))
        {
            q[0] = wt[0] - w[0]; q[1] = wt[1] - w[1]; q[2] = wt[2] - w[2];
            len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
            if (len > 1e-6f)
            {
                for (k = 0; k < 3; k++)
                {
                    s_handFwdPrev[ctrl][k] = s_handFwd[ctrl][k];
                    s_handFwd[ctrl][k] = q[k] / len;
                }
                s_handFwdTicks[ctrl]++;
            }
        }
    }
    if (s_handWorldValid[ctrl] && dt > 1e-4f)
    {
        for (k = 0; k < 3; k++)
        {
            /* eased over a few ticks: one tick's jitter is no throw */
            s_handVel[ctrl][k] += ((w[k] - s_handWorld[ctrl][k]) / dt - s_handVel[ctrl][k]) * 0.5f;
        }
    }
    else
    {
        s_handVel[ctrl][0] = s_handVel[ctrl][1] = s_handVel[ctrl][2] = 0.0f;
    }
    {
        PropRecord *me = getCurrentPlayerProp();

        if (me != NULL)
        {
            f32 rel[3];

            rel[0] = w[0] - me->pos.x;
            rel[1] = w[1] - me->pos.y;
            rel[2] = w[2] - me->pos.z;
            for (k = 0; k < 3; k++)
            {
                s_handRelVel[ctrl][k] = s_handWorldValid[ctrl] && dt > 1e-4f
                    ? s_handRelVel[ctrl][k] + ((rel[k] - s_handRel[ctrl][k]) / dt - s_handRelVel[ctrl][k]) * 0.5f
                    : 0.0f;
                s_handRel[ctrl][k] = rel[k];
            }
        }
    }
    for (k = 0; k < 3; k++)
    {
        s_handWorld[ctrl][k] = w[k];
    }
    s_handWorldValid[ctrl] = TRUE;
}

/* ---- Online: the other players' pulls (net_core.c NET_MSG_FORCE_PULL) ---- */

static void gevrPullRemoteClear(GevrRemotePull *r)
{
    memset(r, 0, sizeof(*r));
}

static GevrRemotePull *gevrPullRemoteOf(s32 slot, s32 ctrl, s32 make)
{
    GevrRemotePull *oldest = &s_remote[0];
    s32 i;

    for (i = 0; i < GEVR_PULL_REMOTE_MAX; i++)
    {
        if (s_remote[i].used && s_remote[i].slot == slot && s_remote[i].ctrl == ctrl)
        {
            return &s_remote[i];
        }
    }
    if (!make)
    {
        return NULL;
    }
    for (i = 0; i < GEVR_PULL_REMOTE_MAX; i++)
    {
        if (!s_remote[i].used)
        {
            oldest = &s_remote[i];
            break;
        }
        if ((s32) (s_remote[i].stamp - oldest->stamp) < 0)
        {
            oldest = &s_remote[i];
        }
    }
    gevrPullRemoteClear(oldest);
    oldest->used = TRUE;
    oldest->slot = slot;
    oldest->ctrl = ctrl;
    oldest->stamp = g_GlobalTimer;
    return oldest;
}

static s32 gevrPullRemoteAlive(const GevrRemotePull *r)
{
    PropRecord *prop = r->prop;

    return prop != NULL && prop->type == r->type && prop->obj == r->obj && r->obj->prop == prop
        && (prop->flags & PROPFLAG_ENABLED) && !(r->obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE);
}

/*
 * This headset's copy of a dropped gun or a grenade the sender named by its
 * item and where it had it: the nearest loose one within radius cm.
 */
static PropRecord *gevrPullNetFind(s32 objtype, s32 item, const f32 at[3], f32 radius)
{
    PropRecord *prop, *best = NULL;
    f32 bestd = radius * radius;

    for (prop = chrpropGetActiveTail(); prop != NULL; prop = prop->prev)
    {
        ObjectRecord *obj = prop->obj;
        f32 dx, dy, dz, d2;

        if ((prop->type != PROP_TYPE_WEAPON && prop->type != PROP_TYPE_OBJ) || obj == NULL || obj->prop != prop
            || obj->type != objtype || prop->parent != NULL || !(prop->flags & PROPFLAG_ENABLED)
            || (obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE) || gevrPullBusy(prop))
        {
            continue;
        }
        if (item >= 0 && (obj->type != PROPDEF_COLLECTABLE || ((WeaponObjRecord *) obj)->weaponnum != item))
        {
            continue;
        }
        dx = prop->pos.x - at[0];
        dy = prop->pos.y - at[1];
        dz = prop->pos.z - at[2];
        d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bestd)
        {
            bestd = d2;
            best = prop;
        }
    }
    return best;
}

/* the prop is this remote pull's from here: it leaves the floor, a fall or this player's own pull */
static void gevrPullRemoteTake(GevrRemotePull *r, PropRecord *prop)
{
    ObjectRecord *obj = prop->obj;
    s32 i;

    for (i = 0; i < 2; i++)
    {
        if (s_pull[i].prop == prop)
        {
            /* the host gave it to the other pull (its claim came first) */
            sysLogPrintf(LOG_NOTE, "stereo: pull (%s): player %d's pull has it", i ? "gun hand" : "off hand", r->slot);
            s_pull[i].prop = NULL;
            gevrPullEnd(i);
        }
    }
    r->prop = prop;
    r->obj = obj;
    r->type = prop->type;
    r->from[0] = prop->pos.x;
    r->from[1] = prop->pos.y;
    r->from[2] = prop->pos.z;
    r->t = 0.0f;
    r->wait = 0.0f;
    if ((obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) && obj->projectile != NULL)
    {
        projectileFree(obj->projectile);
        obj->projectile = NULL;
        obj->runtime_bitflags &= ~RUNTIMEBITFLAG_HASPROJECTILE;
        prop->flags &= ~PROPFLAG_00000008;
    }
}

/* the remote pull is over without a catch: what it carried falls from where it is */
static void gevrPullRemoteDrop(GevrRemotePull *r)
{
    static const f32 still[3] = { 0.0f, 0.0f, 0.0f };

    if (gevrPullRemoteAlive(r))
    {
        gevrPullFling(r->prop, still, NULL);
    }
    gevrPullRemoteClear(r);
}

/* a guard lets go of the gun in his hand: the host's (a client asked), or its copy here */
static void gevrPullNetDisarm(s32 guard, s32 hand, s32 item)
{
    ChrRecord *chr = guard >= 0 ? netCoopGuardOfHostSlot(guard) : NULL;
    PropRecord *gun;

    if (chr == NULL || hand < 0 || hand > 1 || netHordeActive() || chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD)
    {
        return;
    }
    gun = chr->weapons_held[hand];
    if (gun == NULL || gun->type != PROP_TYPE_WEAPON || gun->obj == NULL || gun->obj->type != PROPDEF_COLLECTABLE
        || (gun->obj->flags & PROPFLAG_AIUNDROPPABLE)
        || (item >= 0 && ((WeaponObjRecord *) gun->obj)->weaponnum != item))
    {
        return;
    }
    propobjSetDropped(gun, DROPTYPE_DEFAULT);
    chr->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
    gevrDisarmedMark(chr);
    sysLogPrintf(LOG_NOTE, "stereo: pull: guard %d lets go of item %d (a pull online)", guard, item);
}

/*
 * The host, before it relays a FLY on a setup pickup: FALSE when this
 * headset's own pull, or another player's, has it (the first claim wins;
 * the claimant hears REFUSED). A claim with no word for ten seconds lapses.
 */
int gevrPullNetClaim(int slot, int ctrl, ObjectRecord *obj)
{
    PropRecord *prop = obj != NULL ? obj->prop : NULL;
    s32 i;

    if (prop == NULL || prop->obj != obj)
    {
        return TRUE;
    }
    for (i = 0; i < 2; i++)
    {
        if (s_pull[i].prop == prop)
        {
            return FALSE;
        }
    }
    for (i = 0; i < GEVR_PULL_REMOTE_MAX; i++)
    {
        GevrRemotePull *r = &s_remote[i];

        if (r->used && r->prop == prop && !(r->slot == slot && r->ctrl == ctrl)
            && (u32) (g_GlobalTimer - r->stamp) < 600u)
        {
            return FALSE;
        }
    }
    return TRUE;
}

/* The host refused this headset's pull: another's has it. It falls where it is. */
void gevrPullNetRefused(int ctrl)
{
    static const f32 still[3] = { 0.0f, 0.0f, 0.0f };
    GevrPull *pl;
    s32 i, theirs = FALSE;

    if (ctrl < 0 || ctrl > 1)
    {
        return;
    }
    pl = &s_pull[ctrl];
    if (pl->prop == NULL || pl->held)
    {
        return;
    }
    sysLogPrintf(LOG_NOTE, "stereo: pull (%s): refused by the host, another player's pull has it", ctrl ? "gun hand" : "off hand");
    for (i = 0; i < GEVR_PULL_REMOTE_MAX; i++)
    {
        theirs |= s_remote[i].used && s_remote[i].prop == pl->prop;
    }
    if (!theirs && gevrPullAlive(pl))
    {
        gevrPullFling(pl->prop, still, NULL);
    }
    pl->prop = NULL;
    gevrPullEnd(ctrl);
}

/*
 * A pull step from another player's headset (net_core.c, relayed by the host).
 * obj: the setup object it names, NULL for a dropped gun or grenade, found
 * here by objtype, item and pos.
 */
void gevrPullNetReceive(int slot, int phase, int ctrl, ObjectRecord *obj, int objtype, int item,
                        int aux, int guard, const float *pos, const float *vel, float dur, int timer)
{
    GevrRemotePull *r;
    PropRecord *prop = NULL;
    f32 at[3];

    if (slot < 0 || slot >= MAX_PLAYER_COUNT || ctrl < 0 || ctrl > 1 || pos == NULL || vel == NULL)
    {
        return;
    }
    if (phase == GEVR_PULL_NET_DISARM)
    {
        gevrPullNetDisarm(guard, aux, item);
        return;
    }
    at[0] = pos[0]; at[1] = pos[1]; at[2] = pos[2];
    if (obj != NULL && obj->prop != NULL && obj->prop->obj == obj && (obj->prop->flags & PROPFLAG_ENABLED)
        && obj->prop->parent == NULL)
    {
        prop = obj->prop;
    }
    r = gevrPullRemoteOf(slot, ctrl, FALSE);
    switch (phase)
    {
        case GEVR_PULL_NET_FLY:
            if (r != NULL)
            {
                gevrPullRemoteClear(r);
            }
            if (obj != NULL && prop == NULL)
            {
                break;   /* a setup pickup gone here already: nothing to fly (never searched for by place) */
            }
            r = gevrPullRemoteOf(slot, ctrl, TRUE);
            r->objtype = objtype;
            r->item = item;
            r->at[0] = at[0]; r->at[1] = at[1]; r->at[2] = at[2];
            r->dur = dur > 0.05f ? dur : 0.35f;
            if (prop == NULL && obj == NULL)
            {
                prop = gevrPullNetFind(objtype, item, at, 150.0f);
            }
            if (prop != NULL)
            {
                /* relayed, so the host gave it to them: this headset's own pull on it yields */
                gevrPullRemoteTake(r, prop);
            }
            /* else pending: a disarm's drop may come a tick later (gevrPullRemoteTick) */
            break;

        case GEVR_PULL_NET_HOLD:
            if (r == NULL || !gevrPullRemoteAlive(r))
            {
                if (prop == NULL && obj == NULL)
                {
                    prop = gevrPullNetFind(objtype, item, at, item == ITEM_GRENADE ? 400.0f : 150.0f);
                }
                if (prop == NULL)
                {
                    if (r != NULL)
                    {
                        gevrPullRemoteClear(r);
                    }
                    break;
                }
                r = gevrPullRemoteOf(slot, ctrl, TRUE);
                r->objtype = objtype;
                r->item = item;
                gevrPullRemoteTake(r, prop);
            }
            r->held = TRUE;
            r->stamp = g_GlobalTimer;
            if (r->obj->type == PROPDEF_COLLECTABLE && ((WeaponObjRecord *) r->obj)->weaponnum == ITEM_GRENADE)
            {
                gevrPullOwnGrenade(r->obj, slot);   /* the catcher's headset sends its blast now */
                gevrCoopGuardForget(r->obj);        /* a host guard's (or a client's copy of one): no more */
            }
            break;

        case GEVR_PULL_NET_RELEASE:
            if (r != NULL && gevrPullRemoteAlive(r))
            {
                prop = r->prop;
            }
            else if (prop == NULL && obj == NULL)
            {
                prop = gevrPullNetFind(objtype, item, at, item == ITEM_GRENADE ? 400.0f : 150.0f);
            }
            if (r != NULL)
            {
                gevrPullRemoteClear(r);
            }
            if (prop != NULL)
            {
                struct player *by = g_playerPointers[slot];

                gevrPullPlace(prop, at);
                gevrPullFling(prop, vel, by != NULL ? by->prop : NULL);
                if (prop->obj->type == PROPDEF_COLLECTABLE && ((WeaponObjRecord *) prop->obj)->weaponnum == ITEM_GRENADE)
                {
                    gevrPullOwnGrenade(prop->obj, slot);
                    if (timer >= 0)
                    {
                        ((WeaponObjRecord *) prop->obj)->timer = timer;   /* the catcher's fuse */
                    }
                }
            }
            break;

        case GEVR_PULL_NET_DONE:
            if (r != NULL && gevrPullRemoteAlive(r))
            {
                prop = r->prop;
            }
            else if (prop == NULL && obj == NULL)
            {
                prop = gevrPullNetFind(objtype, item, at, 150.0f);
            }
            if (r != NULL)
            {
                gevrPullRemoteClear(r);
            }
            /* a dropped gun's copy, or one absorbed (nothing gained, gone there): gone here too.
             * A collected setup pickup went with its own message (OBJECT_STATE). */
            if (prop != NULL && (obj == NULL || aux == 1) && prop->obj != NULL
                && !(prop->obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE))
            {
                objFreePermanently(prop->obj, TRUE);
            }
            break;

        case GEVR_PULL_NET_CANCEL:
            if (r != NULL)
            {
                gevrPullRemoteDrop(r);
            }
            break;
    }
}

/* each tick: the other players' pulls fly to their tracked hands here */
static void gevrPullRemoteTick(f32 dt)
{
    s32 i;

    for (i = 0; i < GEVR_PULL_REMOTE_MAX; i++)
    {
        GevrRemotePull *r = &s_remote[i];
        f32 to[3], w[3], up[3] = { 0.0f, 1.0f, 0.0f };

        if (!r->used)
        {
            continue;
        }
        if (!netIsActive() || !netSlotOccupied(r->slot))
        {
            gevrPullRemoteDrop(r);   /* the puller left */
            continue;
        }
        if (r->prop == NULL)
        {
            PropRecord *prop = gevrPullNetFind(r->objtype, r->item, r->at, 150.0f);

            if (prop != NULL)
            {
                gevrPullRemoteTake(r, prop);
            }
            else if ((r->wait += dt) > 1.5f)
            {
                gevrPullRemoteClear(r);   /* no copy here (the guard's gun was gone already) */
            }
            continue;
        }
        if (!gevrPullRemoteAlive(r))
        {
            gevrPullRemoteClear(r);   /* collected (its OBJECT_STATE), exploded or freed */
            continue;
        }
        if (!gevrNetRemoteHandWorld(r->slot, r->ctrl ? GUNRIGHT : GUNLEFT, to))
        {
            continue;
        }
        if (r->held)
        {
            gevrPullPlace(r->prop, to);
            continue;
        }
        r->t += dt;
        if (r->t >= r->dur)
        {
            /* in the hand, waiting for its word (DONE, HOLD, CANCEL) */
            gevrPullPlace(r->prop, to);
            if ((r->wait += dt) > 3.0f)
            {
                gevrPullRemoteDrop(r);
            }
            continue;
        }
        gevrPullFlight(r->from, to, up, GEVR_PULL_LIFT_CM * GEVR_UNITS_PER_METRE / 100.0f, r->t / r->dur, w);
        gevrPullPlace(r->prop, w);
    }
}

/*
 * The flick, while locked: the wrist snapping the pointing direction up
 * (Half-Life: Alyx), or the hand yanked back from the target. World space,
 * so turning the head is no flick; the yank is the hand's own motion, so
 * walking backwards is none either.
 */
static s32 gevrPullFlicked(s32 ctrl, f32 dt, const f32 at[3])
{
    f32 *a = s_handFwdPrev[ctrl], *b = s_handFwd[ctrl];
    f32 c, turn, rise;
    f32 tw[3], d[3], dl;

    if (dt < 1e-4f)
    {
        return FALSE;
    }
    if (s_handFwdTicks[ctrl] >= 2)
    {
        c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        c = c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c;
        turn = acosf(c) / dt;
        rise = (b[1] - a[1]) / dt;
        if (turn > GEVR_PULL_FLICK_RAD_S && rise > GEVR_PULL_FLICK_UP_S)
        {
            sysLogPrintf(LOG_NOTE, "stereo: pull (%s): wrist flick, %.1f rad/s, tip rising %.1f/s", ctrl ? "gun hand" : "off hand", turn, rise);
            return TRUE;
        }
    }
    if (s_handWorldValid[ctrl] && gevrPullToWorld(at, tw))
    {
        d[0] = s_handWorld[ctrl][0] - tw[0];
        d[1] = s_handWorld[ctrl][1] - tw[1];
        d[2] = s_handWorld[ctrl][2] - tw[2];
        dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dl > 1e-3f && (s_handRelVel[ctrl][0] * d[0] + s_handRelVel[ctrl][1] * d[1] + s_handRelVel[ctrl][2] * d[2]) / dl
                              > GEVR_PULL_YANK_CM_S * GEVR_UNITS_PER_METRE / 100.0f)
        {
            sysLogPrintf(LOG_NOTE, "stereo: pull (%s): yanked back", ctrl ? "gun hand" : "off hand");
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * lv.c via bondview2.c gevrGripGestureTick, each game tick after propsTick:
 * the hands' targets (highlight, line, a tick when one locks), the flights
 * and what the hands hold.
 */
void gevrPullTick(void)
{
    f32 dt = g_GlobalTimerDelta / 60.0f;
    s32 ctrl;

    s_glowPhase += dt;
    gevrPullRemoteTick(dt);
    for (ctrl = 0; ctrl < 2; ctrl++)
    {
        if (s_instantAge[ctrl] > 0.0f && (s_instantAge[ctrl] -= dt) <= 0.0f)
        {
            g_gevrSwitchInstant[ctrl] = 0;   /* indexed by hand */
        }
        gevrPullTrackHand(ctrl, dt);
    }
    if (s_glowPhase > 1000.0f)
    {
        s_glowPhase = 0.0f;
    }
    {
        /* the watch glows while either pull flies or waits on a guard (GEVR PC vr457) */
        s32 on = (s_pull[0].prop != NULL && !s_pull[0].held) || (s_pull[1].prop != NULL && !s_pull[1].held);

        s_watchGlow += on ? dt / 0.08f : -dt / 0.30f;
        s_watchGlow = s_watchGlow < 0.0f ? 0.0f : s_watchGlow > 1.0f ? 1.0f : s_watchGlow;
    }
    for (ctrl = 0; ctrl < 2; ctrl++)
    {
        GevrPull *pl = &s_pull[ctrl];
        f32 p[3], f[3];

        s_aimValid[ctrl] = FALSE;
        if (pl->prop != NULL)
        {
            f32 w[3], up[3] = { 0.0f, 1.0f, 0.0f };

            if (!gevrPullAlive(pl) || !gevrPullInPlay())
            {
                sysLogPrintf(LOG_NOTE, "stereo: pull (%s): %s", ctrl ? "gun hand" : "off hand",
                             pl->held ? "held thing gone (taken, exploded or out of play)" : "lost on the way");
                if (pl->held && gevrPullAlive(pl))
                {
                    gevrPullThrow(ctrl, s_handVel[ctrl]);   /* out of play: it drops */
                }
                else
                {
                    gevrPullSend(GEVR_PULL_NET_CANCEL, ctrl, NULL, 0, pl->item, 0, -1, NULL, NULL, 0.0f, -1);
                    gevrPullEnd(ctrl);
                }
                continue;
            }
            if (pl->held)
            {
                if (pl->released)
                {
                    gevrPullThrow(ctrl, s_handVel[ctrl]);
                }
                else if (s_handWorldValid[ctrl])
                {
                    gevrPullPlace(pl->prop, s_handWorld[ctrl]);
                }
                continue;
            }
            if (pl->disarm)
            {
                /* the guard's tick lets go of it (chr.c CHRHIDDEN_DROP_HELD_ITEMS) */
                if (pl->prop->parent == NULL)
                {
                    gevrPullLaunch(pl);
                    sysLogPrintf(LOG_NOTE, "stereo: pull (%s): disarmed, item %d on its way", ctrl ? "gun hand" : "off hand", pl->item);
                }
                else if ((pl->wait += dt) > (netIsActive() ? 1.0f : 0.5f))
                {
                    sysLogPrintf(LOG_NOTE, "stereo: pull (%s): the guard kept item %d", ctrl ? "gun hand" : "off hand", pl->item);
                    gevrPullSend(GEVR_PULL_NET_CANCEL, ctrl, NULL, 0, pl->item, 0, -1, NULL, NULL, 0.0f, -1);
                    gevrPullEnd(ctrl);
                }
                continue;
            }
            /* it flies to the hand, followed within reach of where the flick was;
             * untracked, to where the hand last was */
            if (s_handWorldValid[ctrl] && gevrPullHandRay(ctrl, p, f))
            {
                f32 o[3], ol, lim = GEVR_PULL_HOMING_CM * GEVR_UNITS_PER_METRE / 100.0f;
                s32 k;

                s_handAt[ctrl][0] = p[0]; s_handAt[ctrl][1] = p[1]; s_handAt[ctrl][2] = p[2];
                for (k = 0; k < 3; k++)
                {
                    o[k] = s_handWorld[ctrl][k] - pl->aim[k];
                }
                ol = sqrtf(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
                for (k = 0; k < 3; k++)
                {
                    pl->to[k] = pl->aim[k] + (ol > lim ? o[k] * lim / ol : o[k]);
                }
            }
            pl->t += dt;
            gevrPullFlight(pl->from, pl->to, up, GEVR_PULL_LIFT_CM * GEVR_UNITS_PER_METRE / 100.0f,
                           pl->dur > 0.0f ? pl->t / pl->dur : 1.0f, w);
            {
                /* the catch: the hand opened after the flick and closed with it at the hand,
                 * from halfway (user, test.22: keeping the flick's grip held caught it) */
                f32 v[3], reach = GEVR_PULL_CATCH_CM * GEVR_UNITS_PER_METRE / 100.0f, d2 = 1e30f;

                if (s_handWorldValid[ctrl])
                {
                    d2 = (w[0] - s_handWorld[ctrl][0]) * (w[0] - s_handWorld[ctrl][0])
                       + (w[1] - s_handWorld[ctrl][1]) * (w[1] - s_handWorld[ctrl][1])
                       + (w[2] - s_handWorld[ctrl][2]) * (w[2] - s_handWorld[ctrl][2]);
                }
                if (s_gripHeld[ctrl] && pl->opened && d2 <= reach * reach && pl->t >= 0.5f * pl->dur)
                {
                    s_caughtAt[ctrl] = g_GlobalTimer;
                    gevrPullPlace(pl->prop, w);
                    gevrPullArrive(ctrl);
                    continue;
                }
                if (pl->t >= pl->dur && (pl->over += dt) > GEVR_PULL_MISS_S)
                {
                    /* not caught: it falls from where it is, moving as it was */
                    v[0] = dt > 1e-4f ? (w[0] - pl->prop->pos.x) / dt : 0.0f;
                    v[1] = dt > 1e-4f ? (w[1] - pl->prop->pos.y) / dt : 0.0f;
                    v[2] = dt > 1e-4f ? (w[2] - pl->prop->pos.z) / dt : 0.0f;
                    gevrPullPlace(pl->prop, w);
                    sysLogPrintf(LOG_NOTE, "stereo: pull (%s): missed (grip %s%s, %.0f cm from the hand), it falls",
                                 ctrl ? "gun hand" : "off hand", s_gripHeld[ctrl] ? "closed" : "open",
                                 pl->opened ? "" : ", never opened after the flick",
                                 d2 < 1e29f ? sqrtf(d2) * 100.0f / GEVR_UNITS_PER_METRE : -1.0f);
                    gevrPullHaptic(ctrl, 0.2f, 0.03f);
                    gevrPullThrow(ctrl, v);
                    continue;
                }
            }
            gevrPullPlace(pl->prop, w);
            if ((pl->buzz++ & 3) == 0 && pl->t < pl->dur)
            {
                gevrPullHaptic(ctrl, 0.15f, 0.03f);
            }
            continue;
        }
        if (s_lock[ctrl] != NULL)
        {
            /* locked: it stays lit wherever the hand turns, until the flick or the grip lets go */
            PropRecord *lock = s_lock[ctrl];
            f32 at[3], cm = gevrPullCm(), reach = 1.2f * GEVR_PULL_RANGE_CM * cm;
            s32 ok = VrForcePull && !gevrPullOnline() && gevrPullInPlay() && gevrPullHandFree(ctrl) && gevrPullHandRay(ctrl, p, f)
                     && gevrPullPointOf(lock, at) && cm >= 1e-6f;

            if (ok && (at[0] - p[0]) * (at[0] - p[0]) + (at[1] - p[1]) * (at[1] - p[1]) + (at[2] - p[2]) * (at[2] - p[2]) > reach * reach)
            {
                ok = FALSE;   /* a little past the pull's range: walked away from it */
            }
            if (!ok)
            {
                sysLogPrintf(LOG_NOTE, "stereo: pull (%s): lock lost (taken, out of sight or out of reach)", ctrl ? "gun hand" : "off hand");
                s_lock[ctrl] = NULL;
                s_aim[ctrl] = NULL;
                gevrPullHaptic(ctrl, 0.15f, 0.02f);
                continue;
            }
            s_lockAge[ctrl] += dt;
            s_aim[ctrl] = lock;
            s_aimAt[ctrl][0] = at[0]; s_aimAt[ctrl][1] = at[1]; s_aimAt[ctrl][2] = at[2];
            s_handAt[ctrl][0] = p[0]; s_handAt[ctrl][1] = p[1]; s_handAt[ctrl][2] = p[2];
            s_aimValid[ctrl] = TRUE;
            if (VrPullVisuals)
            {
                lock->obj->shadecol.r = 0xFF;
                lock->obj->shadecol.g = 0xE6;
                lock->obj->shadecol.b = 0x9A;
            }
            if (s_lockAge[ctrl] > 0.05f && gevrPullFlicked(ctrl, dt, at))
            {
                gevrPullStart(ctrl, lock, p, at);
            }
            continue;
        }
        if (!VrForcePull || gevrPullOnline() || !gevrPullInPlay() || !gevrPullHandFree(ctrl) || !gevrPullAimRay(ctrl, p, f))
        {
            s_aim[ctrl] = NULL;
            s_aimLast[ctrl] = NULL;
            continue;
        }
        {
            f32 at[3];
            PropRecord *aim = gevrPullFind(p, f, at, s_aim[ctrl]);

            if (aim != NULL && aim != s_aim[ctrl])
            {
                gevrPullHaptic(ctrl, 0.25f, 0.02f);   /* lit */
            }
            s_aim[ctrl] = aim;
            s_aimLastAge[ctrl] += dt;
            if (aim != NULL)
            {
                s_aimLast[ctrl] = aim;
                s_aimLastAge[ctrl] = 0.0f;
                s_aimAt[ctrl][0] = at[0]; s_aimAt[ctrl][1] = at[1]; s_aimAt[ctrl][2] = at[2];
                s_handAt[ctrl][0] = p[0]; s_handAt[ctrl][1] = p[1]; s_handAt[ctrl][2] = p[2];
                s_aimValid[ctrl] = TRUE;
                if (VrPullVisuals)
                {
                    /* the glow: its light at full, gold, eased back by objTick when it's no longer aimed at */
                    aim->obj->shadecol.r = 0xFF;
                    aim->obj->shadecol.g = 0xE6;
                    aim->obj->shadecol.b = 0x9A;
                }
            }
        }
    }
}

/* level start or exit: nothing in flight, nothing aimed at */
void gevrPullReset(void)
{
    memset(s_pull, 0, sizeof(s_pull));
    memset(s_remote, 0, sizeof(s_remote));
    s_aim[0] = s_aim[1] = NULL;
    s_aimLast[0] = s_aimLast[1] = NULL;
    s_lock[0] = s_lock[1] = NULL;
    s_handFwdTicks[0] = s_handFwdTicks[1] = 0;
    s_aimValid[0] = s_aimValid[1] = FALSE;
    s_watchGlow = 0.0f;
    memset(s_heldNote, 0, sizeof(s_heldNote));
    s_hum = NULL;
    s_handWorldValid[0] = s_handWorldValid[1] = FALSE;
}

static void gevrPullVtx(Vtx *v, f32 x, f32 y, f32 z, u32 rgba)
{
    v->v.ob[0] = (s16) x;
    v->v.ob[1] = (s16) y;
    v->v.ob[2] = (s16) z;
    v->v.flag = 0;
    v->v.tc[0] = v->v.tc[1] = 0;
    v->v.cn[0] = rgba >> 24;
    v->v.cn[1] = (rgba >> 16) & 0xff;
    v->v.cn[2] = (rgba >> 8) & 0xff;
    v->v.cn[3] = rgba & 0xff;
}

#define GEVR_PULL_LINE_SEGS 12   /* 26 vertices: one load (the ring uses 32) */

/*
 * The line and the ring have no depth test and a size in the world, so near
 * the eye they fill the view (headset: an orange flash as a pulled gun came
 * past the face). Gone within 25 cm of the eye, whole past 45 cm.
 */
#define GEVR_PULL_NEAR_HIDE_CM 25.0f
#define GEVR_PULL_NEAR_FULL_CM 45.0f

static f32 gevrPullNearFade(const f32 q[3])
{
    f32 cm = gevrPullCm();
    f32 d, k;

    if (cm < 1e-6f)
    {
        return 0.0f;
    }
    d = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]) / cm;
    k = (d - GEVR_PULL_NEAR_HIDE_CM) / (GEVR_PULL_NEAR_FULL_CM - GEVR_PULL_NEAR_HIDE_CM);
    return k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
}

/*
 * The pull line, palm to target, as a strip facing the eye (the view's
 * origin), widening with distance and brighter toward the target, in the
 * interaction ring's units (gevr_bodyslots.c: vertices in mm about a point).
 */
static Gfx *gevrPullDrawLine(Gfx *gdl, const f32 a[3], const f32 b[3], u32 rgb)
{
    Mtxf mf;
    Mtx *mv = dynAllocateMatrix();
    Vtx *v = dynAllocateVertices((GEVR_PULL_LINE_SEGS + 1) * 2);
    f32 cm = gevrPullCm();
    f32 d[3], len;
    s32 i;

    if (mv == NULL || v == NULL || cm < 1e-6f)
    {
        return gdl;
    }
    d[0] = b[0] - a[0]; d[1] = b[1] - a[1]; d[2] = b[2] - a[2];
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < cm)
    {
        return gdl;
    }
    for (i = 0; i <= GEVR_PULL_LINE_SEGS; i++)
    {
        f32 u = (f32) i / GEVR_PULL_LINE_SEGS;
        f32 q[3], side[3], sl, half;
        u32 alpha;

        q[0] = a[0] + d[0] * u; q[1] = a[1] + d[1] * u; q[2] = a[2] + d[2] * u;
        alpha = (u32) ((0x30 + 0x90 * u) * gevrPullNearFade(q));
        /* across the line and the eye's ray to it */
        side[0] = d[1] * q[2] - d[2] * q[1];
        side[1] = d[2] * q[0] - d[0] * q[2];
        side[2] = d[0] * q[1] - d[1] * q[0];
        sl = sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (sl < 1e-6f)
        {
            side[0] = 1.0f; side[1] = 0.0f; side[2] = 0.0f;
            sl = 1.0f;
        }
        half = 2.0f + 4.0f * u * len / (100.0f * cm);   /* mm: 2 at the palm, wider out there */
        /* mm about a (10 a cm), the strip's two edges */
        gevrPullVtx(&v[i * 2], (q[0] - a[0]) / cm * 10.0f + side[0] / sl * half, (q[1] - a[1]) / cm * 10.0f + side[1] / sl * half,
                    (q[2] - a[2]) / cm * 10.0f + side[2] / sl * half, rgb | alpha);
        gevrPullVtx(&v[i * 2 + 1], (q[0] - a[0]) / cm * 10.0f - side[0] / sl * half, (q[1] - a[1]) / cm * 10.0f - side[1] / sl * half,
                    (q[2] - a[2]) / cm * 10.0f - side[2] / sl * half, rgb | alpha);
    }
    matrix_4x4_set_identity(&mf);
    mf.m[0][0] = mf.m[1][1] = mf.m[2][2] = 0.1f * D_800364CC;
    mf.m[3][0] = a[0];
    mf.m[3][1] = a[1];
    mf.m[3][2] = a[2];
    guMtxF2L(mf.m, mv);
    gSPMatrix(gdl++, osVirtualToPhysical((void *) currentPlayerGetProjectionMatrix()), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gdl++, osVirtualToPhysical(mv), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(gdl++);
    gSPClearGeometryMode(gdl++, G_ZBUFFER | G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gSPVertex(gdl++, osVirtualToPhysical(v), (GEVR_PULL_LINE_SEGS + 1) * 2, 0);
    for (i = 0; i < GEVR_PULL_LINE_SEGS; i++)
    {
        s32 o = i * 2;

        gSP2Triangles(gdl++, o, o + 1, o + 2, 0, o + 1, o + 3, o + 2, 0);
    }
    gDPPipeSync(gdl++);
    gSPSetGeometryMode(gdl++, G_ZBUFFER);
    return gdl;
}

#define GEVR_PULL_GLOW_SEGS 12

/*
 * The watch glow: a gold disc lying on the left watch face, brightest at the
 * centre, with a soft rim past the dial's edge, pulsing with the hum. It has
 * no depth test (as the ring and the line), so it fades out as the face turns
 * away from the eye rather than showing through the wrist.
 */
static Gfx *gevrPullDrawWatchGlow(Gfx *gdl)
{
    Mtxf mf;
    Mtx *mv;
    Vtx *v;
    f32 o[3], n[3], a[3], b[3], cm, dist, facing, k, scale, r1, r2;
    u32 core, rim;
    s32 i;

    if (s_watchGlow <= 0.01f || g_CurrentPlayer == NULL || g_CurrentPlayer->watch_animation_state
        || !gevrStereoWatchFace(o, n, &cm) || cm < 1e-6f)
    {
        return gdl;
    }
    dist = sqrtf(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
    facing = dist > 1e-6f ? -(n[0] * o[0] + n[1] * o[1] + n[2] * o[2]) / dist : 0.0f;
    facing = (facing - 0.05f) * 4.0f;
    if (facing <= 0.0f)
    {
        return gdl;
    }
    facing = facing > 1.0f ? 1.0f : facing;
    k = s_watchGlow * facing * (0.75f + 0.25f * sinf(s_glowPhase * 6.2831853f * 5.0f));
    core = (u32) (0xD0 * k);
    rim = (u32) (0x80 * k);

    /* two axes on the face: across the view's up, or its right if the face looks up */
    a[0] = n[2]; a[1] = 0.0f; a[2] = -n[0];
    if (a[0] * a[0] + a[2] * a[2] < 1e-4f)
    {
        a[0] = 0.0f; a[1] = -n[2]; a[2] = n[1];
    }
    k = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    a[0] /= k; a[1] /= k; a[2] /= k;
    b[0] = n[1] * a[2] - n[2] * a[1];
    b[1] = n[2] * a[0] - n[0] * a[2];
    b[2] = n[0] * a[1] - n[1] * a[0];

    mv = dynAllocateMatrix();
    v = dynAllocateVertices(1 + GEVR_PULL_GLOW_SEGS * 2);
    if (mv == NULL || v == NULL)
    {
        return gdl;
    }
    scale = cm / gevrPullCm();   /* mm at the watch's size (the tiny/big guns cheat) */
    r1 = 20.0f * scale;          /* the dial's edge (bondview2.c GEVR_WATCH_EDGE_CM) */
    r2 = 32.0f * scale;
    gevrPullVtx(&v[0], 0.0f, 0.0f, 0.0f, 0xFFF0B000 | core);
    for (i = 0; i < GEVR_PULL_GLOW_SEGS; i++)
    {
        f32 t = i * (6.2831853f / GEVR_PULL_GLOW_SEGS);
        f32 c = cosf(t), s = sinf(t);

        gevrPullVtx(&v[1 + i], (a[0] * c + b[0] * s) * r1, (a[1] * c + b[1] * s) * r1, (a[2] * c + b[2] * s) * r1,
                    0xFFE69A00 | rim);
        gevrPullVtx(&v[1 + GEVR_PULL_GLOW_SEGS + i], (a[0] * c + b[0] * s) * r2, (a[1] * c + b[1] * s) * r2,
                    (a[2] * c + b[2] * s) * r2, 0xFFE69A00);
    }
    matrix_4x4_set_identity(&mf);
    mf.m[0][0] = mf.m[1][1] = mf.m[2][2] = 0.1f * D_800364CC;
    /* a few mm off the face, clear of the hands and the readout */
    mf.m[3][0] = o[0] + n[0] * 0.3f * cm;
    mf.m[3][1] = o[1] + n[1] * 0.3f * cm;
    mf.m[3][2] = o[2] + n[2] * 0.3f * cm;
    guMtxF2L(mf.m, mv);
    gSPMatrix(gdl++, osVirtualToPhysical((void *) currentPlayerGetProjectionMatrix()), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gdl++, osVirtualToPhysical(mv), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(gdl++);
    gSPClearGeometryMode(gdl++, G_ZBUFFER | G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gSPVertex(gdl++, osVirtualToPhysical(v), 1 + GEVR_PULL_GLOW_SEGS * 2, 0);
    for (i = 0; i < GEVR_PULL_GLOW_SEGS; i++)
    {
        s32 j = (i + 1) % GEVR_PULL_GLOW_SEGS;

        gSP1Triangle(gdl++, 0, 1 + i, 1 + j, 0);
        gSP2Triangles(gdl++, 1 + i, 1 + GEVR_PULL_GLOW_SEGS + i, 1 + GEVR_PULL_GLOW_SEGS + j, 0,
                      1 + i, 1 + GEVR_PULL_GLOW_SEGS + j, 1 + j, 0);
    }
    gDPPipeSync(gdl++);
    gSPSetGeometryMode(gdl++, G_ZBUFFER);
    return gdl;
}

/*
 * bondview2.c, beside the body slots' labels: each hand's target ring and
 * line while it points, the line to a gun on its way, and the watch glow
 * while one flies. Pull visuals off draws nothing (the haptics and the hum
 * stay).
 */
Gfx *gevrPullDraw(Gfx *gdl)
{
    s32 ctrl;
    f32 s = gevrPullViewScale();

    if (!g_gevrStereo || !VrPullVisuals || !VrForcePull)
    {
        return gdl;
    }
    for (ctrl = 0; ctrl < 2; ctrl++)
    {
        f32 at[3];
        s32 show = FALSE;

        if (s_pull[ctrl].prop != NULL && !s_pull[ctrl].disarm && !s_pull[ctrl].held && gevrPullAlive(&s_pull[ctrl]))
        {
            /* where this tick put it (gevrPullPlace), not its model's matrix:
             * render_pos is only written when the model draws (the disarm
             * point's NaN and 1e38 lesson), and a prop at the face may not */
            coord3d c;

            c.x = s_pull[ctrl].prop->pos.x;
            c.y = s_pull[ctrl].prop->pos.y;
            c.z = s_pull[ctrl].prop->pos.z;
            mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &c);
            at[0] = c.x * s; at[1] = c.y * s; at[2] = c.z * s;
            show = (s_pull[ctrl].prop->flags & PROPFLAG_ONSCREEN) != 0;
        }
        else if (s_aimValid[ctrl] && s_aim[ctrl] != NULL)
        {
            at[0] = s_aimAt[ctrl][0]; at[1] = s_aimAt[ctrl][1]; at[2] = s_aimAt[ctrl][2];
            show = TRUE;
            {
                /* a slow pulse on the target's halo, steady and bright once locked; gone near the eye */
                f32 k = s_lock[ctrl] != NULL ? 1.0f : 0.5f + 0.5f * sinf(s_glowPhase * 6.2831853f * 1.5f);
                u32 a = (u32) ((s_lock[ctrl] != NULL ? 0xF0 : 0x60 + 0x60 * k) * gevrPullNearFade(at));

                /* the target's glow, under its ring: the gun lit gold wherever it lies or
                 * whoever holds it (its shading never took a tint, headset test.22) */
                gdl = gevrDrawInteractionHalo(gdl, at, GEVR_PULL_HALO_CM,
                                              0xFFD86000 | (u32) ((s_lock[ctrl] != NULL ? 0xB0 : 0x70 + 0x30 * k) * gevrPullNearFade(at)));
                gdl = gevrDrawInteractionRing(gdl, at, 7.0f, 0xFFE69A00 | a);
                gdl = gevrDrawInteractionRing(gdl, at, 10.0f, 0xFFE69A00 | (a >> 2));
            }
        }
        if (show)
        {
            gdl = gevrPullDrawLine(gdl, s_handAt[ctrl], at, 0xFFE69A00);
        }
    }
    return gevrPullDrawWatchGlow(gdl);
}
