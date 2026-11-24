/*
 * Teleport locomotion (docs/teleport.md; Comfort > Movement, ini Teleport):
 * for players the stick's slide makes ill. With it on, in the headset and on
 * foot, the move stick no longer walks Bond (input.c). Pushed forward it
 * throws an arc from its hand to the floor; let go, the view blinks and Bond
 * stands there. A flick back, left or right is a metre's hop that way, from
 * where the head faces.
 *
 * No cheaper than walking, online or off:
 *  - A jump goes only where the body could walk in a straight line from where
 *    it stands (bondview2.c gevrTeleportWalk, the walk's own collision test in
 *    20 cm steps): not through a wall, a door that is shut, glass, an object,
 *    a guard, nor over a gap or a rail. Aimed past any of those, the mark
 *    stops short of it.
 *  - A jump is paid for from a stamina of distance (port/src/gevr_teleport.c)
 *    that fills at nine tenths of Bond's forward run under the movement speed
 *    rule, so a teleporting player never out-travels a running one. The mark
 *    wears a gauge of it; the arc reaches no farther than it pays for.
 *  - The body is never away: it is hit where it stands before the blink and
 *    where it lands after it.
 *
 * The other headsets need nothing new (no protocol change): the jump is this
 * player's position in its PLAYER_STATE, which they show as a quick dash
 * (net_player_sync.c: up to 5 m eased over a few ticks, more is set at once).
 *
 * Arithmetic and state: port/src/gevr_teleport.c (tested on its own).
 */

#include <ultra64.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "system.h"
#include "bondview.h"
#include "matrixmath.h"
#include "player.h"
#include "lv.h"
#include "net_game.h"
#include "dyn.h"
#include "stan.h"
#include "gevr_teleport.h"

extern int VrTeleport, VrSwapJoysticks;
extern s32 g_gevrStereo;
extern f32 D_800364CC;
extern f32 gevrMoveStickX, gevrMoveStickY;   /* input.c: the move stick, -1..1 */
extern bool netIsActive(void);
extern int netGetLocalSlot(void);
extern s32 gevrSpectating(void);
extern int gevrCoopLocalDowned(void);
extern int gevrDamBoatAboard(void);
extern s32 gevrScopeZoomStick(void);
extern s32 gevrWeaponPanelOpen;   /* bondview2.c: a weapon wheel is up, pointed at with a stick */
extern s32 trigger_haptic_vibration_c(int hand_index, float amplitude, float duration);
extern int vr_haptics_ready(void);
/* bondview2.c */
extern s32 gevrPullHandRay(s32 ctrl, f32 p[3], f32 f[3]);
extern f32 gevrTeleportWalk(f32 dirx, f32 dirz, f32 dist, coord3d *end, StandTile **endstan);
extern void gevrTeleportPlace(const coord3d *pos, StandTile *stan);
extern f32 gevrTeleportSpeedFactor(void);

#define GEVR_UNITS_PER_METRE 100.0f
#define GEVR_TP_ARC_STEP_S  0.04f   /* the arc is followed to the floor in steps of this */
#define GEVR_TP_ARC_STEPS   48
#define GEVR_TP_SETTLE      3       /* ticks after a jump before the hand is read again (the view's matrix is the old one) */
#define GEVR_TP_CURVE_SEGS  14      /* 30 vertices: one load */
#define GEVR_TP_RING_SEGS   16      /* 32 */
#define GEVR_TP_GAUGE_SEGS  12      /* 26 */
#define GEVR_TP_STALE_US    150000  /* no tick for this long: the game is paused or loading */

static GevrTeleport s_tp;
static s32 s_ready;             /* s_tp has been reset once */
static s32 s_show;              /* the arc is up */
static s32 s_valid;             /* and lands somewhere the body can go */
static coord3d s_mark;          /* where: the floor point, world */
static StandTile *s_markTile;
static coord3d s_arcEnd;        /* where the arc came down (the drawn curve's end when there is no mark) */
static s32 s_pending;           /* a jump waits for the blink to go black */
static coord3d s_pendingAt;
static s32 s_settle;
static u64 s_tickUs;            /* the last tick's time: a blink never outlasts the ticks that run it */

/* the move stick's hand, as the hands are numbered here: 0 the off hand, 1 the gun hand */
static s32 tpCtrl(void)
{
    return VrSwapJoysticks ? 1 : 0;
}

static f32 tpCm(void)
{
    return GEVR_UNITS_PER_METRE * D_800364CC / 100.0f;
}

static f32 tpViewScale(void)
{
    return D_800364CC > 1e-6f ? D_800364CC : 1.0f;
}

static void tpBuzz(f32 amp, f32 dur)
{
    if (vr_haptics_ready())
    {
        trigger_haptic_vibration_c(tpCtrl(), amp, dur);
    }
}

/* The stick is this module's: in the headset, on foot, in play (input.c asks). */
s32 gevrTeleportTakesStick(void)
{
    /* this headset's player by its slot: online the current player is each copy in its turn */
    const s32 slot = netIsActive() ? netGetLocalSlot() : -1;
    struct player *pl = slot >= 0 && slot < MAX_PLAYER_COUNT ? g_playerPointers[slot] : g_CurrentPlayer;

    return VrTeleport && g_gevrStereo && !(netIsActive() && (netActiveFunFlags() & NET_FUN_NO_TELEPORT))
        && pl != NULL && pl->prop != NULL && !pl->bonddead
        && g_PlayerIsInTank != 1 && !gevrDamBoatAboard() && !gevrSpectating() && !gevrCoopLocalDowned();
}

/* the hand and where it points, in the world */
static s32 tpHandWorld(f32 hand[3], f32 dir[3])
{
    Mtxf *v2w = currentPlayerGetViewToWorldMtxf();
    f32 p[3], f[3], len;
    f32 s = tpViewScale();
    coord3d a, b;

    if (v2w == NULL || !gevrPullHandRay(tpCtrl(), p, f))
    {
        return FALSE;
    }
    a.x = p[0] / s; a.y = p[1] / s; a.z = p[2] / s;
    b.x = (p[0] + f[0] * 10.0f * tpCm()) / s;
    b.y = (p[1] + f[1] * 10.0f * tpCm()) / s;
    b.z = (p[2] + f[2] * 10.0f * tpCm()) / s;
    mtx4TransformVecInPlace(v2w, &a);
    mtx4TransformVecInPlace(v2w, &b);
    dir[0] = b.x - a.x; dir[1] = b.y - a.y; dir[2] = b.z - a.z;
    len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(len > 1e-6f) || !isfinite(len) || !isfinite(a.x) || !isfinite(a.y) || !isfinite(a.z))
    {
        return FALSE;
    }
    dir[0] /= len; dir[1] /= len; dir[2] /= len;
    hand[0] = a.x; hand[1] = a.y; hand[2] = a.z;
    return TRUE;
}

/*
 * Where the hand's arc comes down, no farther from the body than reach: the
 * floor is the one the body stands on, followed tile by tile from under it
 * (up a stair, down a ramp); past a wall or an edge the last floor known
 * stands in, and the walk test that follows stops the jump there anyway.
 */
static s32 tpArcLanding(f32 reach, coord3d *land)
{
    struct player *pl = g_CurrentPlayer;
    const f32 bx = pl->field_488.collision_position.x;
    const f32 bz = pl->field_488.collision_position.z;
    StandTile *tile = pl->field_488.current_tile_ptr;
    f32 hand[3], dir[3], prev[3], pt[3];
    f32 floor, prevfloor;
    s32 i;

    if (tile == NULL || !tpHandWorld(hand, dir))
    {
        return FALSE;
    }
    floor = prevfloor = bondviewYPositionRelated(tile, bx, bz);
    prev[0] = hand[0]; prev[1] = hand[1]; prev[2] = hand[2];
    if (prev[1] < floor)
    {
        prev[1] = floor;   /* a hand through the floor */
    }
    for (i = 1; i <= GEVR_TP_ARC_STEPS; i++)
    {
        StandTile *under = tile;
        f32 dx, dz, h, k;

        gevrTeleportArcPoint(hand, dir, i * GEVR_TP_ARC_STEP_S, pt);
        dx = pt[0] - bx; dz = pt[2] - bz;
        h = sqrtf(dx * dx + dz * dz);
        if (h > reach)
        {
            /* as far as the stamina goes: straight down from there */
            const f32 px = prev[0] - bx, pz = prev[2] - bz;
            const f32 ph = sqrtf(px * px + pz * pz);

            k = h - ph > 1e-3f ? (reach - ph) / (h - ph) : 0.0f;
            k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
            land->x = prev[0] + (pt[0] - prev[0]) * k;
            land->z = prev[2] + (pt[2] - prev[2]) * k;
            land->y = prevfloor;
            return TRUE;
        }
        if (walkTilesBetweenPoints_NoCallback(&under, bx, bz, pt[0], pt[2]) && under != NULL)
        {
            floor = bondviewYPositionRelated(under, pt[0], pt[2]);
        }
        if (pt[1] <= floor)
        {
            const f32 above = prev[1] - prevfloor, below = floor - pt[1];

            k = above + below > 1e-3f ? above / (above + below) : 1.0f;
            k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
            land->x = prev[0] + (pt[0] - prev[0]) * k;
            land->z = prev[2] + (pt[2] - prev[2]) * k;
            land->y = prevfloor + (floor - prevfloor) * k;
            return TRUE;
        }
        prev[0] = pt[0]; prev[1] = pt[1]; prev[2] = pt[2];
        prevfloor = floor;
    }
    land->x = prev[0];
    land->z = prev[2];
    land->y = prevfloor;
    return TRUE;
}

/* The straight walk from the body toward (x, z), at most reach: where it gets to. */
static s32 tpWalkTo(f32 x, f32 z, f32 reach, coord3d *end, StandTile **endtile, f32 *reached)
{
    struct player *pl = g_CurrentPlayer;
    f32 dx = x - pl->field_488.collision_position.x;
    f32 dz = z - pl->field_488.collision_position.z;
    f32 dist = sqrtf(dx * dx + dz * dz);

    *reached = 0.0f;
    if (!(dist >= GEVR_TP_MIN_CM) || !isfinite(dist))
    {
        return FALSE;
    }
    dx /= dist; dz /= dist;
    if (dist > reach)
    {
        dist = reach;
    }
    *reached = gevrTeleportWalk(dx, dz, dist, end, endtile);
    return *reached >= GEVR_TP_MIN_CM && *endtile != NULL;
}

/* A hop: a metre (or what the stamina has) back, left or right of where the head faces. */
static void tpHop(s32 which)
{
    struct player *pl = g_CurrentPlayer;
    const f32 fx = pl->field_488.theta_transform.x, fz = pl->field_488.theta_transform.z;
    f32 dx, dz, reach = gevrTeleportReach(&s_tp), reached;
    coord3d end;
    StandTile *tile;

    if (which == GEVR_TP_HOP_BACK) { dx = -fx; dz = -fz; }
    else if (which == GEVR_TP_HOP_LEFT) { dx = fz; dz = -fx; }
    else { dx = -fz; dz = fx; }
    if (reach > GEVR_TP_HOP_CM)
    {
        reach = GEVR_TP_HOP_CM;
    }
    if (s_pending || reach < GEVR_TP_MIN_CM
        || !tpWalkTo(pl->field_488.collision_position.x + dx * GEVR_TP_HOP_CM,
                     pl->field_488.collision_position.z + dz * GEVR_TP_HOP_CM, reach, &end, &tile, &reached))
    {
        tpBuzz(0.2f, 0.02f);   /* nowhere to go, or nothing to go on */
        return;
    }
    s_pending = TRUE;
    s_pendingAt = end;
}

/* The blink is black: move, if the way is still clear and paid for. */
static void tpJump(void)
{
    f32 reach = gevrTeleportReach(&s_tp), reached;
    coord3d end;
    StandTile *tile;

    s_pending = FALSE;
    if (!gevrTeleportTakesStick() || reach < GEVR_TP_MIN_CM
        || !tpWalkTo(s_pendingAt.x, s_pendingAt.z, reach, &end, &tile, &reached)
        || !gevrTeleportSpend(&s_tp, reached))
    {
        return;
    }
    gevrTeleportPlace(&end, tile);
    s_settle = GEVR_TP_SETTLE;
    tpBuzz(0.5f, 0.04f);
}

/* bondview2.c MoveBond, this headset's player, before the walk. */
void gevrTeleportTick(void)
{
    struct player *pl = g_CurrentPlayer;
    const f32 dt = g_GlobalTimerDelta / 60.0f;
    s32 ask;

    if (!s_ready)
    {
        gevrTeleportReset(&s_tp);
        s_ready = TRUE;
    }
    {
        const u64 now = sysGetMicroseconds();

        if (now - s_tickUs > GEVR_TP_STALE_US)
        {
            /* back from a pause or a load: no blink or jump left over from before it */
            s_tp.fade = GEVR_TP_FADE_IDLE;
            s_tp.fadeTime = 0.0f;
            s_pending = FALSE;
            gevrTeleportStickDrop(&s_tp);
        }
        s_tickUs = now;
    }
    s_show = FALSE;
    if (pl == NULL)
    {
        return;
    }
    gevrTeleportRest(&s_tp, dt, gevrTeleportSpeedFactor());
    if (s_settle > 0)
    {
        s_settle--;
    }
    if (!gevrTeleportTakesStick() || g_ClockTimer <= 0 || lvlGetControlsLockedFlag()
        || pl->watch_animation_state != 0 || (netIsActive() && pl->mpmenuon) || gevrScopeZoomStick()
        || gevrWeaponPanelOpen)
    {
        /* not now (a wheel taking the stick away must not read as letting it go):
         * nothing held, nothing owed; a blink under way clears */
        gevrTeleportStickDrop(&s_tp);
        s_pending = FALSE;
        gevrTeleportBlinkStep(&s_tp, dt);
        return;
    }

    ask = gevrTeleportStick(&s_tp, gevrMoveStickX, gevrMoveStickY);
    if (ask == GEVR_TP_AIM && s_settle == 0)
    {
        f32 reach = gevrTeleportReach(&s_tp), reached;

        s_valid = FALSE;
        /* out of stamina the arc still shows, short and refused */
        if (tpArcLanding(reach >= GEVR_TP_MIN_CM ? reach : GEVR_TP_MIN_CM, &s_arcEnd))
        {
            s_show = TRUE;
            s_valid = reach >= GEVR_TP_MIN_CM && !s_pending
                && tpWalkTo(s_arcEnd.x, s_arcEnd.z, reach, &s_mark, &s_markTile, &reached);
        }
    }
    else if (ask == GEVR_TP_CONFIRM)
    {
        if (s_valid && !s_pending)
        {
            s_pending = TRUE;
            s_pendingAt = s_mark;
        }
        else
        {
            tpBuzz(0.2f, 0.02f);
        }
        s_valid = FALSE;
    }
    else if (ask >= GEVR_TP_HOP_BACK)
    {
        tpHop(ask);
    }

    /* a jump owed starts its blink once the last one's cooldown is over */
    if (s_pending && s_tp.fade != GEVR_TP_FADE_OUT && s_tp.cooldown <= 0.0f)
    {
        gevrTeleportBlinkStart(&s_tp);
    }
    if (gevrTeleportBlinkStep(&s_tp, dt) && s_pending)
    {
        tpJump();
    }
}

/* gfx_pc.cpp: how black the view is for the blink, 0..1. */
float gevrTeleportBlink(void)
{
    if (!s_ready || !g_gevrStereo)
    {
        return 0.0f;
    }
    /* the ticks stopped (a pause, a load): never left dark. Read only: the
     * redraws between game frames ask too. */
    if (sysGetMicroseconds() - s_tickUs > GEVR_TP_STALE_US)
    {
        return 0.0f;
    }
    return gevrTeleportBlack(&s_tp);
}

static void tpVtx(Vtx *v, f32 x, f32 y, f32 z, u32 rgba)
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

/* a world point in the view's space, the hand's units (as gevr_forcepull.c's targets) */
static void tpToView(f32 x, f32 y, f32 z, f32 out[3])
{
    coord3d c;
    f32 s = tpViewScale();

    c.x = x; c.y = y; c.z = z;
    mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &c);
    out[0] = c.x * s; out[1] = c.y * s; out[2] = c.z * s;
}

/* vertices are mm about `at` (view space), drawn over everything, shaded by their own colours */
static Gfx *tpBegin(Gfx *gdl, const f32 at[3], Vtx *v, s32 count)
{
    Mtxf mf;
    Mtx *mv = dynAllocateMatrix();

    if (mv == NULL)
    {
        return NULL;
    }
    matrix_4x4_set_identity(&mf);
    mf.m[0][0] = mf.m[1][1] = mf.m[2][2] = 0.1f * D_800364CC;
    mf.m[3][0] = at[0];
    mf.m[3][1] = at[1];
    mf.m[3][2] = at[2];
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
    gSPVertex(gdl++, osVirtualToPhysical(v), count, 0);
    return gdl;
}

static Gfx *tpEnd(Gfx *gdl)
{
    gDPPipeSync(gdl++);
    gSPSetGeometryMode(gdl++, G_ZBUFFER);
    return gdl;
}

/* the curve from the hand to the mark, a strip facing the eye, brighter and wider toward the mark */
static Gfx *tpDrawCurve(Gfx *gdl, const f32 hand[3], const f32 dir[3], const f32 end[3], u32 rgb)
{
    Vtx *v = dynAllocateVertices((GEVR_TP_CURVE_SEGS + 1) * 2);
    Gfx *g;
    f32 cm = tpCm();
    f32 q[3], n[3], d[3], side[3], sl, half;
    s32 i;

    if (v == NULL || cm < 1e-6f)
    {
        return gdl;
    }
    for (i = 0; i <= GEVR_TP_CURVE_SEGS; i++)
    {
        f32 u = (f32) i / GEVR_TP_CURVE_SEGS;
        u32 alpha = (u32) (0x40 + 0xA0 * u);

        gevrTeleportCurvePoint(hand, dir, end, u, q);
        gevrTeleportCurvePoint(hand, dir, end, u + (i < GEVR_TP_CURVE_SEGS ? 0.02f : -0.02f), n);
        d[0] = n[0] - q[0]; d[1] = n[1] - q[1]; d[2] = n[2] - q[2];
        /* across the curve and the eye's ray to it */
        side[0] = d[1] * q[2] - d[2] * q[1];
        side[1] = d[2] * q[0] - d[0] * q[2];
        side[2] = d[0] * q[1] - d[1] * q[0];
        sl = sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (sl < 1e-6f)
        {
            side[0] = 1.0f; side[1] = 0.0f; side[2] = 0.0f;
            sl = 1.0f;
        }
        half = 3.0f + 12.0f * u;   /* mm */
        tpVtx(&v[i * 2], (q[0] - hand[0]) / cm * 10.0f + side[0] / sl * half, (q[1] - hand[1]) / cm * 10.0f + side[1] / sl * half,
              (q[2] - hand[2]) / cm * 10.0f + side[2] / sl * half, rgb | alpha);
        tpVtx(&v[i * 2 + 1], (q[0] - hand[0]) / cm * 10.0f - side[0] / sl * half, (q[1] - hand[1]) / cm * 10.0f - side[1] / sl * half,
              (q[2] - hand[2]) / cm * 10.0f - side[2] / sl * half, rgb | alpha);
    }
    g = tpBegin(gdl, hand, v, (GEVR_TP_CURVE_SEGS + 1) * 2);
    if (g == NULL)
    {
        return gdl;
    }
    gdl = g;
    for (i = 0; i < GEVR_TP_CURVE_SEGS; i++)
    {
        s32 o = i * 2;

        gSP2Triangles(gdl++, o, o + 1, o + 2, 0, o + 1, o + 3, o + 2, 0);
    }
    return tpEnd(gdl);
}

/*
 * A band lying on the floor round the mark, between radii r0 and r1 (cm),
 * over the first `part` of the circle (1: all of it), from the world's +Z
 * round. Its points are placed in the world and brought to
 * the view one by one, so it lies flat whatever the head does.
 */
static Gfx *tpDrawBand(Gfx *gdl, const coord3d *mark, f32 r0, f32 r1, f32 part, s32 segs, u32 rgba)
{
    Vtx *v = dynAllocateVertices((segs + 1) * 2);
    Gfx *g;
    f32 cm = tpCm();
    f32 at[3], q[3];
    s32 i;

    if (v == NULL || cm < 1e-6f || !(part > 0.0f))
    {
        return gdl;
    }
    tpToView(mark->x, mark->y, mark->z, at);
    for (i = 0; i <= segs; i++)
    {
        f32 t = (f32) i / segs * part * 6.2831853f;
        f32 c = cosf(t), s = sinf(t);

        tpToView(mark->x + s * r0, mark->y, mark->z + c * r0, q);
        tpVtx(&v[i * 2], (q[0] - at[0]) / cm * 10.0f, (q[1] - at[1]) / cm * 10.0f, (q[2] - at[2]) / cm * 10.0f, rgba);
        tpToView(mark->x + s * r1, mark->y, mark->z + c * r1, q);
        tpVtx(&v[i * 2 + 1], (q[0] - at[0]) / cm * 10.0f, (q[1] - at[1]) / cm * 10.0f, (q[2] - at[2]) / cm * 10.0f, rgba);
    }
    g = tpBegin(gdl, at, v, (segs + 1) * 2);
    if (g == NULL)
    {
        return gdl;
    }
    gdl = g;
    for (i = 0; i < segs; i++)
    {
        s32 o = i * 2;

        gSP2Triangles(gdl++, o, o + 1, o + 2, 0, o + 1, o + 3, o + 2, 0);
    }
    return tpEnd(gdl);
}

/*
 * bondview2.c, with the pull's line: the arc from the move stick's hand and
 * the mark where the jump lands, blue when it can be made and red when not;
 * round the mark, the stamina left (a full ring: a full jump in hand).
 */
Gfx *gevrTeleportDraw(Gfx *gdl)
{
    f32 p[3], f[3], end[3];
    const coord3d *to = s_valid ? &s_mark : &s_arcEnd;
    coord3d floor;
    u32 rgb = s_valid ? 0x40C8FF00 : 0xFF403000;

    if (!s_show || !g_gevrStereo || !VrTeleport
        || (netIsActive() && (netActiveFunFlags() & NET_FUN_NO_TELEPORT)) || g_CurrentPlayer == NULL
        || (netIsActive() && get_cur_playernum() != netGetLocalSlot())
        || !gevrPullHandRay(tpCtrl(), p, f))
    {
        return gdl;
    }
    floor = *to;
    floor.y += 2.0f;   /* clear of the floor it lies on */
    tpToView(floor.x, floor.y, floor.z, end);
    gdl = tpDrawCurve(gdl, p, f, end, rgb);
    if (s_valid)
    {
        gdl = tpDrawBand(gdl, &floor, 20.0f, 26.0f, 1.0f, GEVR_TP_RING_SEGS - 1, rgb | 0xE0);
        gdl = tpDrawBand(gdl, &floor, 0.0f, 20.0f, 1.0f, GEVR_TP_RING_SEGS - 1, rgb | 0x30);
    }
    else
    {
        gdl = tpDrawBand(gdl, &floor, 6.0f, 10.0f, 1.0f, GEVR_TP_RING_SEGS - 1, rgb | 0xC0);
    }
    return tpDrawBand(gdl, &floor, 29.0f, 32.0f, s_tp.stamina / GEVR_TP_MAX_CM, GEVR_TP_GAUGE_SEGS, 0xFFFFFF00 | 0xB0);
}
