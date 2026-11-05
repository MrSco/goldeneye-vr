/*
 * Third person in the flat-screen play mode (issue #230, step 1).
 *
 * Ported from Dab's Mod's third person (MIT; dabs-mod/CLAUDE-notes/
 * third-person.md, playerPullBackCamera and friends), onto GoldenEye's own
 * camera and collision:
 *
 * - Only the copy of the eye the camera matrices are built from moves
 *   (bondview2.c bondviewRenderDebugBondView). Bond's position, the walk,
 *   the aim and the room search keep the eye, as in Dab's Mod.
 * - The offset is along the look (back), its right hand (the shoulder) and
 *   world up (height). A wall scales the whole offset, so the shoulder is
 *   kept while the camera slides in along its line. Coming in is immediate,
 *   going back out is eased.
 * - The trace is the background's drawn triangles (bgTestBulletHitBackground,
 *   the rooms around the eye) plus the walk mesh with doors and path
 *   blockers (stanTestLineUnobstructed, as the retail multiplayer death
 *   camera traces), then the camera is lifted off the floor under it and
 *   kept under the ceiling (gevrBgClampCameraCeiling).
 * - Aiming (R), the watch, death, the tank and anything that is not the
 *   ordinary first-person camera put the camera back on the eye, where the
 *   first-person gun is drawn as always.
 * - Solo play builds Bond's body the multiplayer way (from the heap, not the
 *   gun buffers the solo intro borrows) and animates it with the multiplayer
 *   player animation (bondview2.c playerTick). Online is not handled yet.
 *
 * The shot still leaves the camera through the crosshair, so the crosshair is
 * honest at any offset (Dab's Mod, "The player's shot comes out of the
 * camera"); chrTestHit and the object hit test skip props nearer the camera
 * than Bond (gevrTpShotMinDepth).
 */
#include <ultra64.h>
#include <math.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "bondview.h"
#include "player.h"
#include "chr.h"
#include "stan.h"
#include "bg.h"
#include "system.h"

extern bool netIsActive(void);
extern s32 g_gevrStereo;
extern int VrThirdPerson, VrThirdPersonDist, VrThirdPersonHeight, VrThirdPersonSide;
extern f32 room_data_float1;
extern void gevrBgClampCameraCeiling(float position[3], float floorY, int room, int loadRooms);
extern void chrlvStanPointPointIntersection(coord3d *arg0, coord3d *arg1, coord3d *result);
extern void sub_GAME_7F03D058(PropRecord *prop, bool unset);
extern void sub_GAME_7F09B398(enum GUNHAND hand);
extern f32 bondviewGetPlayerYawRadians(void);
extern void bondviewUpdatePlayerRoom(struct player *player);

/* bondview2.c solo_char_load: build the body the multiplayer way */
s32 g_gevrTpBodyBuild;

/* the body solo_char_load built for third person (not the intro's) */
static Model *s_body;
static s32 s_bodyFailed;
/* the camera this frame */
static coord3d s_camPos;
static s32 s_camValid;
static f32 s_camDist;   /* its distance from the eye */
static f32 s_ease;      /* 0..1, the offset's share let out */

/* The camera keeps this far off what it traced into. */
#define TP_CLEARANCE 16.0f
/* Above the floor under it. */
#define TP_FLOOR_CLEAR 24.0f
/* Nearer than this, the body is hidden: the camera is inside it. */
#define TP_BODY_HIDE 38.0f
/* The ease back out, a share of the offset per frame. */
#define TP_EASE_STEP 0.06f

static s32 gevrTpSolo(void)
{
    return VrThirdPerson && !g_gevrStereo && !netIsActive() && getPlayerCount() == 1;
}

/* Third person is asked for and the camera is the ordinary first-person one. */
static s32 gevrTpWanted(void)
{
    struct player *p = g_CurrentPlayer;

    if (!gevrTpSolo() || p == NULL || p->prop == NULL) return FALSE;
    if (p->cameramode == 1) return FALSE;
    if (g_CameraMode != CAMERAMODE_FP && g_CameraMode != CAMERAMODE_FP_NOINPUT) return FALSE;
    return TRUE;
}

/* ...and nothing asks for the eye: aiming, the watch, death, the tank. */
static s32 gevrTpOnEye(void)
{
    struct player *p = g_CurrentPlayer;

    return p->insightaimmode || p->pause_state != 0 || p->bonddead || g_PlayerIsInTank;
}

s32 gevrTpOwnsBody(s32 playernum)
{
    struct player *p;

    if (s_body == NULL || playernum < 0 || playernum >= 4) return FALSE;
    p = g_playerPointers[playernum];
    return p != NULL && p->bodyModel == s_body && p->prop != NULL && p->prop->chr != NULL;
}

/* playerTick: animate and draw this player's own body in its own pass. */
s32 gevrTpDrawsOwnBody(s32 playernum)
{
    return playernum == get_cur_playernum() && gevrTpOwnsBody(playernum)
        && gevrTpWanted() && s_camValid && s_camDist >= TP_BODY_HIDE;
}

/* The held-gun updates the multiplayer body gets (gun.c, gunfire.c). */
s32 gevrTpPlayerHasBody(void)
{
    return getPlayerCount() >= 2 || gevrTpOwnsBody(get_cur_playernum());
}

static void gevrTpRemoveBody(void)
{
    struct player *p = g_CurrentPlayer;

    if (!gevrTpOwnsBody(get_cur_playernum()))
    {
        s_body = NULL;
        return;
    }
    chrpropCleanupForRemoval(p->prop);
    p->prop->chr = NULL;
    p->bodyModel = NULL;
    s_body = NULL;
    bondviewUpdatePlayerRoom(p);
    sysLogPrintf(LOG_NOTE, "thirdperson: body removed");
}

static void gevrTpBuildBody(void)
{
    struct player *p = g_CurrentPlayer;
    ChrRecord *chr;

    g_gevrTpBodyBuild = TRUE;
    solo_char_load();
    g_gevrTpBodyBuild = FALSE;

    chr = p->prop->chr;
    if (chr == NULL || p->bodyModel == NULL)
    {
        /* never again this stage: a failed build each frame would leak */
        s_bodyFailed = TRUE;
        sysLogPrintf(LOG_WARNING, "thirdperson: no body (chr %p model %p)", (void *)chr, (void *)p->bodyModel);
        if (chr != NULL)
        {
            chrpropCleanupForRemoval(p->prop);
            p->prop->chr = NULL;
        }
        p->bodyModel = NULL;
        return;
    }
    s_body = p->bodyModel;
    setsuboffset(p->bodyModel, &p->prop->pos);
    setsubroty(p->bodyModel, bondviewGetPlayerYawRadians());
    chr->chrflags |= CHRFLAG_INIT;
    chr->actiontype = ACT_BONDMULTI;
    chr->act_bondmulti.unk2c = NULL;
    /* solo_char_load hangs the right hand's item; the left one too */
    sub_GAME_7F09B398(GUNLEFT);
    sysLogPrintf(LOG_NOTE, "thirdperson: body built (model %p)", (void *)p->bodyModel);
}

/* bondviewSetCameraMode: any camera but first person gets the game's own body
 * handling (the intro, the ending, the death camera) without ours in the way. */
void gevrTpCameraModeChanging(s32 mode)
{
    if (mode != CAMERAMODE_FP && mode != CAMERAMODE_FP_NOINPUT && s_body != NULL)
    {
        gevrTpRemoveBody();
    }
}

/* A new stage: nothing of the last one's body is ours. */
void gevrTpStageReset(void)
{
    s_body = NULL;
    s_bodyFailed = FALSE;
    s_camValid = FALSE;
    s_camDist = 0.0f;
    s_ease = 0.0f;
}

/* Once a frame, before the camera: the body follows the setting. */
static void gevrTpBodyTick(void)
{
    struct player *p = g_CurrentPlayer;

    if (gevrTpWanted())
    {
        if (p->prop->chr == NULL && !s_bodyFailed && !p->bonddead)
        {
            gevrTpBuildBody();
        }
    }
    else if (s_body != NULL && getPlayerCount() == 1 && p->cameramode != 1
        && (g_CameraMode == CAMERAMODE_FP || g_CameraMode == CAMERAMODE_FP_NOINPUT))
    {
        /* switched off in play */
        gevrTpRemoveBody();
    }
}

/* How far along eye -> to the line is clear, 0..1. */
static f32 gevrTpTrace(const coord3d *eye, const coord3d *to, StandTile **tileout)
{
    struct player *p = g_CurrentPlayer;
    s32 rooms[MAXPORTALSPERROOM + 1];
    s32 count, r;
    f32 frac = 1.0f;
    f32 len, dx, dy, dz;
    StandTile *tile;
    coord3d from = *eye, end = *to;

    dx = end.x - from.x;
    dy = end.y - from.y;
    dz = end.z - from.z;
    len = sqrtf(dx * dx + dy * dy + dz * dz);
    *tileout = p->field_488.current_tile_ptr;
    if (len < 1.0f) return 0.0f;

    /* the rooms' drawn triangles */
    if (p->field_488.current_tile_ptr_for_portals != NULL && g_BgPortals != NULL)
    {
        s32 room = p->field_488.current_tile_ptr_for_portals->room;

        rooms[0] = room;
        count = 1 + bgGetConnectedRooms(room, rooms + 1, MAXPORTALSPERROOM);
        for (r = 0; r < count; r++)
        {
            HitThing hit;

            if (rooms[r] <= 0 || rooms[r] >= g_MaxNumRooms) continue;
            if (!g_BgRoomInfo[rooms[r]].vtx_batch_bounds) continue;
            if (bgTestBulletHitBackground(&from, &end, rooms[r], &hit))
            {
                f32 s = room_data_float1;
                f32 hx = hit.hitpos.x / s - from.x;
                f32 hy = hit.hitpos.y / s - from.y;
                f32 hz = hit.hitpos.z / s - from.z;
                /* an infinite forward ray: only hits within the segment */
                f32 d = (hx * dx + hy * dy + hz * dz) / len;

                if (isfinite(d) && d >= 0.0f && d < len * frac)
                {
                    frac = d / len;
                }
            }
        }
    }

    /* the walk mesh, doors and path blockers */
    tile = p->field_488.current_tile_ptr;
    if (tile != NULL && (dx * dx + dz * dz) > 1.0f)
    {
        coord3d dir, hitpos;
        s32 ok;

        stanResetHits();
        sub_GAME_7F03D058(p->prop, 0);
        ok = stanTestLineUnobstructed(&tile, from.x, from.z, from.x + dx * frac, from.z + dz * frac,
            CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_BG, 0.0f, 1.0f, 0.0f, 1.0f);
        sub_GAME_7F03D058(p->prop, 1);
        if (!ok)
        {
            f32 hd, flat;

            dir.x = dx;
            dir.y = 0.0f;
            dir.z = dz;
            chrlvStanPointPointIntersection(&from, &dir, &hitpos);
            flat = sqrtf(dx * dx + dz * dz);
            hd = ((hitpos.x - from.x) * dx + (hitpos.z - from.z) * dz) / flat;
            if (isfinite(hd) && hd >= 0.0f && hd / flat < frac)
            {
                frac = hd / flat;
            }
            /* the tile the shortened line ends on */
            tile = p->field_488.current_tile_ptr;
            stanResetHits();
            sub_GAME_7F03D058(p->prop, 0);
            stanTestLineUnobstructed(&tile, from.x, from.z,
                from.x + dx * frac * 0.98f, from.z + dz * frac * 0.98f,
                CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_BG, 0.0f, 1.0f, 0.0f, 1.0f);
            sub_GAME_7F03D058(p->prop, 1);
        }
        if (tile != NULL) *tileout = tile;
    }

    /* keep the clearance off what was hit */
    if (frac < 1.0f)
    {
        frac -= TP_CLEARANCE / len;
    }
    return frac < 0.0f ? 0.0f : frac;
}

/*
 * bondviewRenderDebugBondView, after the eye is final: pull the camera back.
 * campos is the eye in, the camera out; look and up are left alone, so the
 * camera looks where the crosshair aims.
 */
void gevrTpCamera(coord3d *campos, const coord3d *look, const coord3d *up)
{
    coord3d eye = *campos, off, right, cam;
    StandTile *tile = NULL;
    f32 fit, len;

    gevrTpBodyTick();
    s_camValid = FALSE;

    if (!gevrTpWanted() || g_CurrentPlayer->prop->chr == NULL || gevrTpOnEye())
    {
        s_ease = 0.0f;
        s_camDist = 0.0f;
        return;
    }

    /* right hand: look x up */
    right.x = look->y * up->z - look->z * up->y;
    right.y = look->z * up->x - look->x * up->z;
    right.z = look->x * up->y - look->y * up->x;
    len = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
    if (len < 1e-4f) return;
    right.x /= len; right.y /= len; right.z /= len;

    off.x = -look->x * VrThirdPersonDist + right.x * VrThirdPersonSide;
    off.y = -look->y * VrThirdPersonDist + right.y * VrThirdPersonSide + VrThirdPersonHeight;
    off.z = -look->z * VrThirdPersonDist + right.z * VrThirdPersonSide;

    cam.x = eye.x + off.x;
    cam.y = eye.y + off.y;
    cam.z = eye.z + off.z;
    fit = gevrTpTrace(&eye, &cam, &tile);

    /* in at once, out eased */
    if (fit < s_ease) s_ease = fit;
    else
    {
        s_ease += TP_EASE_STEP;
        if (s_ease > fit) s_ease = fit;
    }

    cam.x = eye.x + off.x * s_ease;
    cam.y = eye.y + off.y * s_ease;
    cam.z = eye.z + off.z * s_ease;

    /* off the floor under it, under the ceiling over it */
    if (tile != NULL)
    {
        f32 floory = stanGetPositionYValue(tile, cam.x, cam.z);

        if (isfinite(floory) && cam.y < floory + TP_FLOOR_CLEAR && floory + TP_FLOOR_CLEAR < eye.y + 60.0f)
        {
            cam.y = floory + TP_FLOOR_CLEAR;
        }
        if (isfinite(floory))
        {
            gevrBgClampCameraCeiling(cam.f, floory, tile->room, TRUE);
        }
    }

    off.x = cam.x - eye.x;
    off.y = cam.y - eye.y;
    off.z = cam.z - eye.z;
    s_camDist = sqrtf(off.x * off.x + off.y * off.y + off.z * off.z);
    s_camPos = cam;
    s_camValid = TRUE;
    *campos = cam;
}

/* bg.c's room walk: where the frame is seen from, when it is not the eye. */
s32 gevrTpCameraPos(coord3d *out)
{
    if (!s_camValid || !gevrTpWanted()) return FALSE;
    *out = s_camPos;
    return TRUE;
}

/* The player's shots skip props nearer the camera than this: they stand
 * between the camera and Bond. 0 when the camera is the eye. */
f32 gevrTpShotMinDepth(void)
{
    if (!s_camValid || !gevrTpWanted() || s_camDist < TP_BODY_HIDE) return 0.0f;
    return s_camDist - 30.0f;
}

/* The first-person gun and arms stay away while the camera is off the eye. */
s32 gevrTpHidesGun(void)
{
    return s_camValid && gevrTpWanted() && s_camDist >= TP_BODY_HIDE;
}
