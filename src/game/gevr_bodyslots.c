/*
 * Body slots (launcher Gestures page, watch VR page; ini BodySlots): the
 * weapon wheel's categories on the body, beside the wheel. Pistols at each
 * hip for that hand, rifles and SMGs over the gun hand's shoulder, heavy guns
 * over the off hand's, thrown things on the chest, gadgets at the front of the
 * belt on the off side. A slot holds the last thing used from its category;
 * while a hand is in it, A/X or a flick of that hand's stick steps through the
 * category; a fresh grip there takes it.
 *
 * The torso is Perfect Dark VR's smoothed yaw (VrBodyYaw, ArmBodyFollow)
 * under Doom3Quest's neck model, so a glance aside or a look down at the belt
 * leaves the slots where they are. The arithmetic is
 * port/include/gevr_bodyslot.h, tested on its own; this is the game's side:
 * the frame from the camera, the hands, the wheel's lists, haptics and logs.
 *
 * Tuning: files/gevr_bodyslots.txt, re-read every two seconds, "hipr backr
 * chestr beltr exitcm dwellms deferms follow freezepitch maxtwist markers"
 * (a radius of 0 keeps the size setting's, follow 0 ArmBodyFollow's).
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
#include "model.h"
#include "gevr_bodyslot.h"
#include "gevr_bodyslots.h"

extern int VrBodySlots, VrBodySlotShow, VrBodySlotSize, VrLeftHandedMode;
extern float VrBodySlotFit[GEVR_BODY_SLOTS][3];
extern float VrPlayerHeight, VrArmBodyFollow;
extern s32 g_gevrStereo;
extern s32 gevrWeaponPanelOpen;               /* bondview2.c: a wheel is up */
extern int gevrGunFitActive;                  /* port/src/input.c: 1 while fitting */
extern f32 D_800364CC;
extern bool netIsActive(void);
extern int netGetLocalSlot(void);
extern int gevrCoopLocalDowned(void);
extern void gevrVrHeadQuat(float out[4]);
extern s32 trigger_haptic_vibration_freq_c(int hand_index, float amplitude, float duration, float frequency);
extern int vr_haptics_ready(void);
extern void vrHapticsGetRumble(int id, float *amp, float *dur, float *freq);
/* bondview2.c */
extern s32 gevrWeaponCategory(s32 item);
extern s32 gevrStereoWatchItem(s32 item);
extern s32 gevrLeftPanelAvailable(void);
extern s32 gevrBodyHandSelected(s32 hand);
extern s32 gevrBodyHandList(s32 hand, s32 *items, char (*names)[48], s32 max);
extern s32 gevrBodyGripPos(s32 ctrl, f32 pos[3]);
extern s32 gevrBodyTwoHandNear(void);
extern void gevrHolsterReset(void);
extern void gevrBodyShortName(const char *in, char *out, s32 size);
extern u32 gevrBodyCategoryTint(s32 cat);
extern s32 gevrBodyItemPosed(s32 item);
extern void gevrStereoItemPose(s32 item, Mtxf *m);
extern f32 gevrGunSizeNow(s32 *setting, s32 *online);
extern float VrGunOffX, VrGunOffY, VrGunOffZ;
extern s32 g_gevrExtraPass;                   /* lv.c: an extra view pass */
extern void matrix_4x4_7F058C64(void);
extern void matrix_4x4_7F058C88(void);
extern u16 viGetPerspNorm(void);
/* gevr_heldgun.c, gunfire.c */
extern Gfx *gevrHeldGunDrawPosed(s32 inst, s32 item, Mtxf *root, ModelRenderData *templ, Gfx *gdl);
extern Gfx *gevrDrawViewTag(Gfx *gdl, const char *name, const f32 at[3], f32 k, s32 speaking, u32 panel);
extern void gunRequestHandWeaponChange(enum GUNHAND hand, s32 nextWeapon, s32 cycleDirection);
extern s32 gevrIsThrowable(s32 item);        /* port/src/input.c */
extern int VrMotionThrowing;
extern float vr_ctrl_velocity_play[2][3], vr_head_velocity_play[3];
extern s32 bondinvIsAliveWithFlag(void);
extern MPSCENARIOS get_scenario(void);
extern s32 getPlayerCount(void);

#define GEVR_ACTION_SLOT_HOVER 1004   /* port/vr/vr_haptics.h */
#define GEVR_ACTION_SLOT_TAKE  1005
#define GEVR_ACTION_SLOT_DENY  1006
#define GEVR_ACTION_SLOT_STEP  1007

#define BS_LIST    96                 /* bondview2.c GEVR_WP_MAX */
#define BS_CHOICES 24
#define BS_TICK_MS (1000.0f / 60.0f)

enum { BT_HIPR, BT_BACKR, BT_CHESTR, BT_BELTR, BT_EXIT, BT_DWELL, BT_DEFER, BT_FOLLOW,
       BT_FREEZE, BT_TWIST, BT_MARKERS, BT_COUNT };
static f32 s_bodyTune[BT_COUNT] = {
    0.0f, 0.0f, 0.0f, 0.0f,   /* radii: the size setting's */
    3.0f,     /* cm past a slot's radius before the hand has left it */
    120.0f,   /* ms a throwable's hand stays before the slot takes its grip */
    100.0f,   /* ms hand reload's belt waits for a holstering grip */
    0.0f,     /* the torso's follow rate: ArmBodyFollow's */
    -35.0f,   /* degrees of head pitch below which the torso holds */
    60.0f,    /* degrees the torso may lag the head */
    0.0f,     /* 1: rings at every slot outside Gun fit */
};

typedef struct
{
    s32 slot;                 /* GEVR_BS_*, -1 none */
    f32 ms;                   /* in it so far */
    s32 pick;                 /* what a grip takes: an item, GEVR_BODY_HOLSTER, -1 none */
    s32 steps;
    s32 n;
    s32 choices[BS_CHOICES];
    f32 dist;                 /* cm to its centre */
    f32 at[3];                /* the hand, level frame cm */
    s32 tracked;
    s32 gripHeld;             /* the slot took this grip: nothing else does until it's let go */
    GevrBodyStick stick;      /* its own stick's flicks */
    GevrBodyDefer belt;       /* hand reload's belt touch, waiting for a holstering grip */
    s32 nearFront;            /* within 1.25 radii of a hip, the chest or the belt it could use */
    s32 quiet;                /* ticks the hand stays quiet after a take (no blow, no reload) */
} GevrBodyHand;

static GevrBodyHand s_bodyHand[2] = { { -1 }, { -1 } };
static GevrBodyTorso s_bodyTorso;
static GevrBodyFrame s_bodyFrame;
static s32 s_bodyFrameValid;
static s32 s_bodyLive;
static f32 s_bodyCentre[GEVR_BODY_SLOTS][3];
static f32 s_bodyRadius[GEVR_BODY_SLOTS];
static GevrBodyMru s_bodyMru[GEVR_BODY_SLOTS];
static s32 s_bodyList[2][BS_LIST];
static char s_bodyNames[2][BS_LIST][48];
static s32 s_bodyListN[2];
static f32 s_bodyView[3][3];   /* the view's right, up and back in the level frame, this tick */
int gevrBodySlotFitting;       /* Gun fit's Slots mode (port/src/input.c) */
static s32 s_bodyFitSlot;      /* the slot it moves */
static f32 s_bodyTwistLogged;
static s32 s_bodyTwistLogTicks;

static const char *gevrBodyHandName(s32 ctrl) { return ctrl ? "gun hand" : "off hand"; }

static void gevrBodyTuneRead(void)
{
    static u32 s_read;
    FILE *f;
    f32 v[BT_COUNT];
    s32 i;

    if ((s_read++ % 120) != 0)
    {
        return;
    }
    f = fopen("/sdcard/Android/data/com.gevr.port/files/gevr_bodyslots.txt", "r");
    if (f == NULL)
    {
        return;
    }
    if (fscanf(f, "%f %f %f %f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
               &v[7], &v[8], &v[9], &v[10]) == BT_COUNT
        && memcmp(v, s_bodyTune, sizeof(v)) != 0)
    {
        for (i = 0; i < BT_COUNT; i++)
        {
            s_bodyTune[i] = v[i];
        }
        sysLogPrintf(LOG_NOTE, "bodyslot: tune radii %.0f/%.0f/%.0f/%.0f, exit %.0f, dwell %.0f ms, defer %.0f ms, "
                     "follow %.3f, freeze %.0f, twist %.0f, markers %.0f",
                     v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10]);
    }
    fclose(f);
}

static void gevrBodyBuzz(s32 ctrl, s32 id)
{
    f32 amp, dur, freq;

    if (!vr_haptics_ready())
    {
        return;
    }
    vrHapticsGetRumble(id, &amp, &dur, &freq);
    if (amp > 0.0f && dur > 0.0f)
    {
        trigger_haptic_vibration_freq_c(ctrl, amp, dur, freq);
    }
}

/* the slot an item lives in, for the hand holding it */
static s32 gevrBodySlotForItem(s32 item, s32 hand)
{
    switch (gevrWeaponCategory(item))
    {
        case GEVR_BODY_CAT_PISTOLS: return hand == GUNRIGHT ? GEVR_BS_HIP_GUN : GEVR_BS_HIP_OFF;
        case GEVR_BODY_CAT_RIFLES:  return GEVR_BS_BACK_GUN;
        case GEVR_BODY_CAT_HEAVY:   return GEVR_BS_BACK_OFF;
        case GEVR_BODY_CAT_THROWN:  return GEVR_BS_CHEST;
        default:                    return GEVR_BS_BELT;
    }
}

/* what a slot can hold: not the empty hands, the tank's shells, the flag, or
 * what the watch carries (the watch items and the detonator stay on the arm) */
static s32 gevrBodyItemKept(s32 item)
{
    return item > ITEM_FIST && item < ITEM_IDS_MAX && item != ITEM_TANKSHELLS && item != ITEM_TOKEN
        && item != ITEM_SUIT_LF_HAND && !gevrStereoWatchItem(item);
}

/* a slot's choices for a hand, in the wheel's order; the count */
static s32 gevrBodyChoicesFor(s32 slot, s32 hand, s32 *out)
{
    s32 cand[BS_LIST];
    s32 n = 0, i;
    s32 held = gevrBodyHandSelected(hand);
    const s32 *list = s_bodyList[hand];

    for (i = 0; i < s_bodyListN[hand]; i++)
    {
        if (gevrBodyItemKept(list[i]) && gevrBodySlotForItem(list[i], hand) == slot)
        {
            cand[n++] = list[i];
        }
    }
    return gevrBodyChoices(cand, n, held,
                           gevrBodyItemKept(held) && gevrBodySlotForItem(held, hand) == slot, out, BS_CHOICES);
}

const char *gevrBodyItemName(s32 hand, s32 item)
{
    s32 i;

    for (i = 0; i < s_bodyListN[hand]; i++)
    {
        if (s_bodyList[hand][i] == item)
        {
            return s_bodyNames[hand][i];
        }
    }
    return "?";
}

static void gevrBodyPickName(s32 hand, s32 pick, char *out, s32 size)
{
    const char *name = pick == GEVR_BODY_HOLSTER ? "holster" : pick < 0 ? "nothing" : gevrBodyItemName(hand, pick);
    s32 i;

    for (i = 0; i < size - 1 && name[i] != 0 && name[i] != '\n'; i++)
    {
        out[i] = name[i];
    }
    out[i] = 0;
}

/* live this tick: the option, stereo first-person play, none of the menus */
static s32 gevrBodyInPlay(void)
{
    return VrBodySlots && g_gevrStereo && g_CurrentPlayer != NULL && !g_CurrentPlayer->bonddead
        && g_CurrentPlayer->watch_animation_state == 0 && !g_CurrentPlayer->mpmenuon
        && g_PlayerIsInTank != 1 && !gevrSpectating() && !gevrCoopLocalDowned()
        && !gevrWeaponPanelOpen && (gevrGunFitActive != 1 || gevrBodySlotFitting) && D_800364CC > 1e-6f;
}

static void gevrBodyLeave(s32 ctrl, const char *why)
{
    GevrBodyHand *h = &s_bodyHand[ctrl];

    if (h->slot >= 0)
    {
        sysLogPrintf(LOG_NOTE, "bodyslot: leave (%s) %s after %.2f s, %d step%s%s%s", gevrBodyHandName(ctrl),
                     gevrBodySlotName(h->slot), h->ms / 1000.0f, h->steps, h->steps == 1 ? "" : "s",
                     why != NULL ? ": " : "", why != NULL ? why : "");
    }
    h->slot = -1;
    h->nearFront = FALSE;
    h->ms = 0.0f;
    h->steps = 0;
    h->pick = -1;
    h->n = 0;
}

static void gevrBodyIdle(const char *why)
{
    gevrBodyLeave(0, why);
    gevrBodyLeave(1, why);
    s_bodyFrameValid = FALSE;
    s_bodyLive = FALSE;
}

static void gevrNormalize(f32 v[3])
{
    f32 len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    if (len > 1e-6f)
    {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

static f32 gevrBodyRadiusFor(s32 slot)
{
    static const s32 tune[GEVR_BODY_SLOTS] = { BT_HIPR, BT_HIPR, BT_BACKR, BT_BACKR, BT_CHESTR, BT_BELTR };
    f32 r = s_bodyTune[tune[slot]];

    return r > 0.0f ? r : gevrBodySlotRadius(slot, VrBodySlotSize);
}

/* the frame and the slots' centres, from the camera and the head's play pose */
static s32 gevrBodyFrameUpdate(void)
{
    static const f32 look[3] = { 0.0f, 0.0f, 1.0f }, down[3] = { 0.0f, -1.0f, 0.0f };
    Mtxf *v2w = currentPlayerGetViewToWorldMtxf();
    struct coord3d f = { 0.0f, 0.0f, -1.0f }, u = { 0.0f, 1.0f, 0.0f }, r = { 1.0f, 0.0f, 0.0f };
    f32 fw[3], up[3], rt[3], q[4], pf[3], pu[3], hp[2];
    f32 follow = s_bodyTune[BT_FOLLOW] > 0.0f ? s_bodyTune[BT_FOLLOW] : VrArmBodyFollow;
    s32 freeze = FALSE, ctrl, s;

    if (v2w == NULL)
    {
        return FALSE;
    }
    mtx4RotateVecInPlace(v2w, &f);
    mtx4RotateVecInPlace(v2w, &u);
    mtx4RotateVecInPlace(v2w, &r);
    fw[0] = f.x; fw[1] = f.y; fw[2] = f.z;
    up[0] = u.x; up[1] = u.y; up[2] = u.z;
    rt[0] = r.x; rt[1] = r.y; rt[2] = r.z;
    gevrNormalize(fw);
    gevrNormalize(up);
    gevrNormalize(rt);
    /* the head in the headset's own frame, as gevrStereoLook turns it (no stick yaw) */
    gevrVrHeadQuat(q);
    gevrBodyQuatRotate(q, look, pf);
    gevrBodyQuatRotate(q, down, pu);
    pf[1] = -pf[1];
    pu[1] = -pu[1];
    if (!gevrBodyHeading(pf, pu, hp))
    {
        return FALSE;
    }
    /* a hand in a slot at the front of the body, or the head looking down at
     * it: the torso holds still (The Light Brigade's and H3VR's belts turned
     * away). Not a hand merely near one: hanging arms rest by the hips. */
    if (s_bodyFrameValid)
    {
        freeze = s_bodyFrame.pitch < s_bodyTune[BT_FREEZE];
        for (ctrl = 0; ctrl < 2; ctrl++)
        {
            s = s_bodyHand[ctrl].slot;
            if (s >= 0 && s != GEVR_BS_BACK_GUN && s != GEVR_BS_BACK_OFF)
            {
                freeze = TRUE;
            }
        }
    }
    if (gevrBodyTorsoUpdate(&s_bodyTorso, hp, follow, (f32) g_ClockTimer, freeze, s_bodyTune[BT_TWIST], 45.0f))
    {
        sysLogPrintf(LOG_NOTE, "bodyslot: torso starts at the head (%s)", s_bodyFrameValid ? "it jumped" : "first frame");
    }
    gevrBodyFrameBuild(&s_bodyFrame, &s_bodyTorso, hp, fw, up, rt);
    for (s = 0; s < 3; s++)
    {
        s_bodyView[0][s] = rt[s];
        s_bodyView[1][s] = up[s];
        s_bodyView[2][s] = -fw[s];
    }
    for (s = 0; s < GEVR_BODY_SLOTS; s++)
    {
        f32 off[3];

        gevrBodySlotOffsets(s, VrBodySlotFit[s], VrPlayerHeight, off);
        gevrBodySlotCentre(&s_bodyFrame, s, off, VrLeftHandedMode, s_bodyCentre[s]);
        s_bodyRadius[s] = gevrBodyRadiusFor(s);
    }
    s_bodyTwistLogTicks += g_ClockTimer;
    if (fabsf(s_bodyFrame.twist - s_bodyTwistLogged) > 10.0f && s_bodyTwistLogTicks >= 60)
    {
        sysLogPrintf(LOG_NOTE, "bodyslot: torso %+.0f deg from the head, head pitch %.0f%s", s_bodyFrame.twist,
                     s_bodyFrame.pitch, freeze ? ", held" : "");
        s_bodyTwistLogged = s_bodyFrame.twist;
        s_bodyTwistLogTicks = 0;
    }
    s_bodyFrameValid = TRUE;
    return TRUE;
}

/* the hand in the level frame, cm */
static s32 gevrBodyHandAt(s32 ctrl, f32 out[3])
{
    Mtxf *v2w = currentPlayerGetViewToWorldMtxf();
    struct coord3d p;
    f32 at[3];

    if (v2w == NULL || !gevrBodyGripPos(ctrl, at))
    {
        return FALSE;
    }
    p.x = at[0]; p.y = at[1]; p.z = at[2];
    mtx4RotateVecInPlace(v2w, &p);
    out[0] = p.x / D_800364CC;
    out[1] = p.y / D_800364CC;
    out[2] = p.z / D_800364CC;
    return TRUE;
}

static void gevrBodyHandUpdate(s32 ctrl)
{
    GevrBodyHand *h = &s_bodyHand[ctrl];
    s32 hand = ctrl ? GUNRIGHT : GUNLEFT;
    s32 offOk = ctrl == 1 || (gevrLeftPanelAvailable() && !gevrBodyTwoHandNear());
    s32 eligible[GEVR_BODY_SLOTS], choices[BS_CHOICES];
    f32 dist[GEVR_BODY_SLOTS];
    s32 s, slot;

    h->nearFront = FALSE;
    h->tracked = gevrBodyHandAt(ctrl, h->at);
    if (!h->tracked)
    {
        gevrBodyLeave(ctrl, "hand not tracked");
        return;
    }
    for (s = 0; s < GEVR_BODY_SLOTS; s++)
    {
        f32 d[3] = { h->at[0] - s_bodyCentre[s][0], h->at[1] - s_bodyCentre[s][1], h->at[2] - s_bodyCentre[s][2] };

        dist[s] = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        /* a slot with nothing for this hand isn't there for it */
        eligible[s] = offOk && (gevrBodySlotHands(s) & (1 << ctrl)) && gevrBodyChoicesFor(s, hand, choices) > 0;
        if (eligible[s] && s != GEVR_BS_BACK_GUN && s != GEVR_BS_BACK_OFF && dist[s] < 1.25f * s_bodyRadius[s])
        {
            h->nearFront = TRUE;
        }
    }
    slot = gevrBodyZonePick(h->slot, dist, s_bodyRadius, eligible, s_bodyTune[BT_EXIT], 0.15f);
    if (slot != h->slot)
    {
        f32 loc[3];
        char name[48];

        gevrBodyLeave(ctrl, slot >= 0 ? "another slot" : NULL);
        if (slot < 0)
        {
            return;
        }
        h->slot = slot;
        h->n = gevrBodyChoicesFor(slot, hand, h->choices);
        h->pick = gevrBodyDefaultPick(&s_bodyMru[slot], h->choices, h->n);
        h->dist = dist[slot];
        gevrBodyBuzz(ctrl, GEVR_ACTION_SLOT_HOVER);
        gevrBodySlotFitFrom(&s_bodyFrame, slot, h->at, VrLeftHandedMode, loc);
        gevrBodyPickName(hand, h->pick, name, sizeof(name));
        sysLogPrintf(LOG_NOTE, "bodyslot: hover (%s) %s: %.0f below %.0f out %.0f ahead, %.2f r, takes %d %s (%d choice%s)",
                     gevrBodyHandName(ctrl), gevrBodySlotName(slot), loc[0], loc[1], loc[2],
                     dist[slot] / s_bodyRadius[slot], h->pick, name, h->n, h->n == 1 ? "" : "s");
        return;
    }
    if (slot < 0)
    {
        return;
    }
    /* still there: the inventory may have changed under it */
    h->ms += g_ClockTimer * BS_TICK_MS;
    h->dist = dist[slot];
    h->n = gevrBodyChoicesFor(slot, hand, h->choices);
    for (s = 0; s < h->n && h->choices[s] != h->pick; s++)
    {
    }
    if (s == h->n)
    {
        h->pick = gevrBodyDefaultPick(&s_bodyMru[slot], h->choices, h->n);
    }
}

void gevrBodySlotsTick(void)
{
    s32 ctrl;

    if (VrBodySlots)
    {
        gevrBodyTuneRead();
    }
    if (g_CurrentPlayer == NULL || (netIsActive() && get_cur_playernum() != netGetLocalSlot()))
    {
        return;   /* another player's pass: the local one keeps its state */
    }
    if (!gevrBodyInPlay())
    {
        if (s_bodyLive)
        {
            gevrBodyIdle(VrBodySlots ? "not in play" : "off");
        }
        return;
    }
    if (!gevrBodyFrameUpdate())
    {
        gevrBodyIdle("no frame");
        return;
    }
    s_bodyLive = TRUE;
    for (ctrl = 0; ctrl < 2; ctrl++)
    {
        s32 hand = ctrl ? GUNRIGHT : GUNLEFT;
        s32 held;

        s_bodyListN[hand] = gevrBodyHandList(hand, s_bodyList[hand], s_bodyNames[hand], BS_LIST);
        /* what the hand holds is the newest use of its slot, however it got there
         * (the wheel, a tap, a pickup, a slot) */
        held = gevrBodyHandSelected(hand);
        if (gevrBodyItemKept(held))
        {
            gevrBodyMruTouch(&s_bodyMru[gevrBodySlotForItem(held, hand)], held);
        }
    }
    for (ctrl = 0; ctrl < 2; ctrl++)
    {
        if (s_bodyHand[ctrl].quiet > 0)
        {
            s_bodyHand[ctrl].quiet -= g_ClockTimer;
        }
        gevrBodyHandUpdate(ctrl);
    }
}

/*
 * A fresh grip in a slot (bondview2.c gevrGripGestureTry, after the grips'
 * own jobs): the hand takes the slot's choice, what it held going back to its
 * own slot, or puts away what it holds (GEVR PC vr456: a hip grip is never a
 * no-op). Nothing for the hand: the press goes on to the other gestures.
 */
int gevrBodySlotGrip(int ctrl)
{
    GevrBodyHand *h;
    s32 hand, held, action;
    char from[48], to[48];

    if (!s_bodyLive || ctrl < 0 || ctrl > 1 || s_bodyHand[ctrl].slot < 0)
    {
        return FALSE;
    }
    h = &s_bodyHand[ctrl];
    hand = ctrl ? GUNRIGHT : GUNLEFT;
    held = gevrBodyHandSelected(hand);
    if (VrMotionThrowing && gevrIsThrowable(held))
    {
        /* its grip winds up a throw (gevrMotionThrowTick): the slot takes it only
         * from a hand that has settled there, so reach back and throw still throws */
        f32 v[3], speed;
        s32 i;

        for (i = 0; i < 3; i++)
        {
            v[i] = vr_ctrl_velocity_play[ctrl][i] - vr_head_velocity_play[i];
        }
        speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!gevrBodyThrowGate(h->ms, speed, s_bodyTune[BT_DWELL], 1.0f))
        {
            sysLogPrintf(LOG_NOTE, "bodyslot: grip (%s) %s with throwable %d: a throw (%.0f ms there, %.1f m/s)",
                         gevrBodyHandName(ctrl), gevrBodySlotName(h->slot), held, h->ms, speed);
            return FALSE;
        }
    }
    action = gevrBodyGripAction(h->pick, getPlayerCount() >= 2 && get_scenario() == 2 && bondinvIsAliveWithFlag());
    gevrBodyPickName(hand, held, from, sizeof(from));
    gevrBodyPickName(hand, h->pick, to, sizeof(to));
    switch (action)
    {
        case GEVR_BODY_DENY:
            /* the flag's carrier keeps it (gevrCycleHandWeaponInternal) */
            sysLogPrintf(LOG_NOTE, "bodyslot: refuse (%s) %s: carrying the flag", gevrBodyHandName(ctrl),
                         gevrBodySlotName(h->slot));
            gevrBodyBuzz(ctrl, GEVR_ACTION_SLOT_DENY);
            h->gripHeld = TRUE;
            return TRUE;
        case GEVR_BODY_DRAW:
            gunRequestHandWeaponChange(hand, h->pick, 1);
            sysLogPrintf(LOG_NOTE, "bodyslot: take (%s) %s: %d %s for %d %s", gevrBodyHandName(ctrl),
                         gevrBodySlotName(h->slot), h->pick, to, held, from);
            break;
        case GEVR_BODY_STOW:
            gunRequestHandWeaponChange(hand, hand == GUNLEFT ? ITEM_UNARMED : ITEM_FIST, 1);
            sysLogPrintf(LOG_NOTE, "bodyslot: stow (%s) %s: %d %s", gevrBodyHandName(ctrl),
                         gevrBodySlotName(h->slot), held, from);
            break;
        default:
            return FALSE;
    }
    if (gevrBodyItemKept(held))
    {
        gevrBodyMruTouch(&s_bodyMru[gevrBodySlotForItem(held, hand)], held);   /* it shows where it went */
    }
    gevrBodyBuzz(ctrl, GEVR_ACTION_SLOT_TAKE);
    h->gripHeld = TRUE;
    h->quiet = 15;
    return TRUE;
}

/* the hand's choice moves on by dir (A/X, the stick) */
static void gevrBodyStep(s32 ctrl, s32 dir, const char *how)
{
    GevrBodyHand *h = &s_bodyHand[ctrl];
    s32 hand = ctrl ? GUNRIGHT : GUNLEFT;
    char name[48];

    if (h->n <= 1)
    {
        return;   /* one choice: nothing to step to */
    }
    h->pick = gevrBodyStepPick(h->choices, h->n, h->pick, dir);
    h->steps++;
    if (h->pick >= 0)
    {
        gevrBodyMruTouch(&s_bodyMru[h->slot], h->pick);   /* the slot keeps it, taken or not */
    }
    gevrBodyBuzz(ctrl, GEVR_ACTION_SLOT_STEP);
    gevrBodyPickName(hand, h->pick, name, sizeof(name));
    sysLogPrintf(LOG_NOTE, "bodyslot: step (%s) %s by %s: %d %s", gevrBodyHandName(ctrl),
                 gevrBodySlotName(h->slot), how, h->pick, name);
}

/* port/src/input.c: the hand's own A or X, pressed. 1: the slot has it */
int gevrBodySlotButton(int ctrl)
{
    if (!s_bodyLive || ctrl < 0 || ctrl > 1 || s_bodyHand[ctrl].slot < 0)
    {
        return FALSE;
    }
    gevrBodyStep(ctrl, 1, ctrl ? "A" : "X");
    return TRUE;
}

/* port/src/input.c, each poll: the hand's own stick's X. 1: the slot has it */
int gevrBodySlotStick(int ctrl, float x, float dtMs)
{
    GevrBodyHand *h;
    s32 take, step;

    if (ctrl < 0 || ctrl > 1)
    {
        return FALSE;
    }
    h = &s_bodyHand[ctrl];
    step = gevrBodyStickStep(&h->stick, s_bodyLive && h->slot >= 0, x, dtMs, &take);
    if (step != 0)
    {
        gevrBodyStep(ctrl, step, "stick");
    }
    return take;
}

/*
 * Hand reload's belt (bondview2.c gevrHandReloadTick) sits where the hip
 * holster does: a touch there still reloads, after deferms, and a squeeze in
 * that time (the hand is holstering) drops it until the hand has left. -1
 * with the slots off: the belt reloads at once, as always.
 */
int gevrBodySlotBeltWait(int ctrl, int entered, int inside, int gripped)
{
    GevrBodyDefer *d;
    s32 was, fire;

    if (ctrl < 0 || ctrl > 1)
    {
        return -1;
    }
    d = &s_bodyHand[ctrl].belt;
    if (!s_bodyLive)
    {
        memset(d, 0, sizeof(*d));
        return -1;
    }
    was = d->pending;
    fire = gevrBodyBeltDefer(d, entered, inside, gripped, g_ClockTimer * BS_TICK_MS, s_bodyTune[BT_DEFER]);
    if (d->pending && !was)
    {
        sysLogPrintf(LOG_NOTE, "bodyslot: belt (%s): touch, reload waits %.0f ms", gevrBodyHandName(ctrl),
                     s_bodyTune[BT_DEFER]);
    }
    else if (was && !d->pending)
    {
        sysLogPrintf(LOG_NOTE, "bodyslot: belt (%s): %s", gevrBodyHandName(ctrl),
                     fire ? "reloads" : gripped ? "a squeeze: no reload" : "left: no reload");
    }
    return fire;
}

/* the hand is in a slot, or just took from one: no chest-cross reload */
int gevrBodySlotBusy(int ctrl)
{
    return s_bodyLive && ctrl >= 0 && ctrl < 2 && (s_bodyHand[ctrl].slot >= 0 || s_bodyHand[ctrl].quiet > 0);
}

/* and near one at the front of the body: no blow either (bondview2.c gevrHandChopTick) */
int gevrBodySlotQuiet(int ctrl)
{
    return gevrBodySlotBusy(ctrl) || (s_bodyLive && ctrl >= 0 && ctrl < 2 && s_bodyHand[ctrl].nearFront);
}

/* port/src/input.c through gevrGripGestureInput: the grip is let go */
void gevrBodySlotGripLetGo(int ctrl)
{
    if (ctrl >= 0 && ctrl < 2)
    {
        s_bodyHand[ctrl].gripHeld = FALSE;
    }
}

int gevrBodySlotHoldsGrip(int ctrl)
{
    return ctrl >= 0 && ctrl < 2 && s_bodyHand[ctrl].gripHeld;
}

/* ------------------------------------------------------------- drawing */

/* a level-frame direction into view space; a point too (cm into view units) with cm */
static void gevrBodyToView(const f32 p[3], f32 scale, f32 out[3])
{
    s32 i;

    for (i = 0; i < 3; i++)
    {
        out[i] = (p[0] * s_bodyView[i][0] + p[1] * s_bodyView[i][1] + p[2] * s_bodyView[i][2]) * scale;
    }
}

/* what a slot shows: a hovering hand's choice, else what its own hand would take */
static s32 gevrBodyShownItem(s32 slot, s32 *hoveredBy)
{
    s32 choices[BS_CHOICES];
    s32 ctrl, n, pick;

    *hoveredBy = -1;
    for (ctrl = 0; ctrl < 2; ctrl++)
    {
        if (s_bodyHand[ctrl].slot == slot)
        {
            *hoveredBy = ctrl;
            return s_bodyHand[ctrl].pick >= 0 ? s_bodyHand[ctrl].pick : -1;
        }
    }
    for (ctrl = 1; ctrl >= 0; ctrl--)
    {
        if (!(gevrBodySlotHands(slot) & (1 << ctrl)) || (ctrl == 0 && !gevrLeftPanelAvailable()))
        {
            continue;
        }
        n = gevrBodyChoicesFor(slot, ctrl ? GUNRIGHT : GUNLEFT, choices);
        pick = gevrBodyDefaultPick(&s_bodyMru[slot], choices, n);
        if (pick >= 0)
        {
            return pick;
        }
    }
    return -1;
}

/*
 * A slot's model matrix, view space, as gevrStereoGunMatrix builds the gun
 * hand's: the viewmodel's 0.85 cm a unit (and the flat game's 0.1, gunfire.c),
 * the grip on the slot's centre. A pistol hangs muzzle down with its slide
 * forward, the hand reaching down for its grip; a knife too; grenades, mines
 * and gadgets sit upright, facing ahead, gadgets at their in-hand size.
 */
static void gevrBodySlotRoot(s32 slot, s32 item, Mtxf *root)
{
    const f32 *R = s_bodyFrame.right, *U = s_bodyFrame.up, *F = s_bodyFrame.fwd;
    s32 down = slot == GEVR_BS_HIP_GUN || slot == GEVR_BS_HIP_OFF || item == ITEM_KNIFE || item == ITEM_THROWKNIFE;
    f32 x[3], y[3], z[3], xv[3], yv[3], zv[3], cv[3];
    f32 cm = D_800364CC;
    s32 setting, online, i, j;
    f32 size = gevrGunSizeNow(&setting, &online);
    f32 k = 0.85f * cm * size;

    for (i = 0; i < 3; i++)
    {
        x[i] = -R[i];
        y[i] = down ? F[i] : U[i];
        z[i] = down ? -U[i] : F[i];
    }
    gevrBodyToView(x, 1.0f, xv);
    gevrBodyToView(y, 1.0f, yv);
    gevrBodyToView(z, 1.0f, zv);
    gevrBodyToView(s_bodyCentre[slot], cm, cv);
    for (i = 0; i < 3; i++)
    {
        root->m[0][i] = xv[i] * k;
        root->m[1][i] = yv[i] * k;
        root->m[2][i] = zv[i] * k;
        root->m[3][i] = cv[i] + (-VrGunOffX * xv[i] + VrGunOffY * yv[i] - (12.0f + VrGunOffZ) * zv[i]) * cm * size;
    }
    root->m[0][3] = root->m[1][3] = root->m[2][3] = 0.0f;
    root->m[3][3] = 1.0f;
    if (gevrBodyItemPosed(item))
    {
        gevrStereoItemPose(item, root);
    }
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            root->m[i][j] *= 0.1f;
        }
    }
}

/* in view, and near enough the view's axis to be seen (the hips when looking down) */
static s32 gevrBodyInView(const f32 v[3])
{
    f32 len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    return -v[2] > 10.0f * D_800364CC && -v[2] > 0.57f * len;   /* inside about 55 degrees */
}

/*
 * bondview2.c maybe_mp_interface, before the first-person guns, culling off:
 * what the hips, the chest and the belt hold, drawn on the body (Doom3Quest
 * and Perfect Dark VR draw their holster and belt magazine always). Only the
 * slots in view are drawn, so looking ahead costs nothing.
 */
Gfx *gevrBodySlotsDraw(Gfx *gdl)
{
    static const s32 shown[] = { GEVR_BS_HIP_GUN, GEVR_BS_HIP_OFF, GEVR_BS_CHEST, GEVR_BS_BELT };
    ModelRenderData rd;
    u32 tint;
    s32 i, inst = 0, set = FALSE;

    if (!s_bodyLive || !s_bodyFrameValid || !VrBodySlotShow || g_gevrExtraPass || g_CurrentPlayer == NULL)
    {
        return gdl;
    }
    gDPNoOpTag(gdl++, 0x565F0003);   /* VR_HAND_DRAW 3: the body (fast3d's in-between frames) */
    rd = (ModelRenderData){0};
    rd.zbufferenabled = TRUE;
    rd.flags = 3;
    rd.PropType = 4;
    tint = g_CurrentPlayer->tileColor.a | ((u32) g_CurrentPlayer->tileColor.r << 24)
         | ((u32) g_CurrentPlayer->tileColor.g << 16) | ((u32) g_CurrentPlayer->tileColor.b << 8);
    for (i = 0; i < (s32) (sizeof(shown) / sizeof(shown[0])); i++)
    {
        s32 slot = shown[i], by;
        s32 item = gevrBodyShownItem(slot, &by);
        f32 cv[3];
        Mtxf root;

        if (item < 0)
        {
            continue;
        }
        gevrBodyToView(s_bodyCentre[slot], D_800364CC, cv);
        if (!gevrBodyInView(cv))
        {
            continue;
        }
        if (!set)
        {
            gSPPerspNormalize(gdl++, matrix_4x4_calc_depth_scale(0.0f, 300.0f));   /* as the guns are */
            set = TRUE;
        }
        rd.envcolour.word = tint;
        if (by >= 0)
        {
            /* the one the hand is on, lit up */
            u32 r = (tint >> 24) & 0xff, g = (tint >> 16) & 0xff, b = (tint >> 8) & 0xff;

            r = r * 3 / 2 + 24; g = g * 3 / 2 + 20; b = b * 3 / 2 + 8;
            rd.envcolour.word = (r > 255 ? 255u : r) << 24 | (g > 255 ? 255u : g) << 16
                              | (b > 255 ? 255u : b) << 8 | (tint & 0xff);
        }
        gevrBodySlotRoot(slot, item, &root);
        if (gevrBodyItemPosed(item))
        {
            gDPNoOpTag(gdl++, 0x565B0001);   /* a gadget keeps its culling (gunfire.c s_gevrHiddenShown) */
        }
        matrix_4x4_7F058C64();
        gdl = gevrHeldGunDrawPosed(inst++, item, &root, &rd, gdl);
        matrix_4x4_7F058C88();
        if (gevrBodyItemPosed(item))
        {
            gDPNoOpTag(gdl++, 0x565B0000);
        }
    }
    if (set)
    {
        gSPPerspNormalize(gdl++, viGetPerspNorm());
    }
    gDPNoOpTag(gdl++, 0x565F0000);   /* the world again */
    return gdl;
}

static void gevrBodyVtx(Vtx *v, s32 x, s32 y, u32 rgba)
{
    v->v.ob[0] = x;
    v->v.ob[1] = y;
    v->v.ob[2] = 0;
    v->v.flag = 0;
    v->v.tc[0] = v->v.tc[1] = 0;
    v->v.cn[0] = rgba >> 24;
    v->v.cn[1] = (rgba >> 16) & 0xff;
    v->v.cn[2] = (rgba >> 8) & 0xff;
    v->v.cn[3] = rgba & 0xff;
}

/* a ring facing the eye at the view point at, radius cm (gevrDrawMuzzleMarker's way: vertices in mm) */
static Gfx *gevrBodyRing(Gfx *gdl, const f32 at[3], f32 radius, u32 rgba)
{
    Mtxf mf;
    Mtx *mv = dynAllocateMatrix();
    Vtx *v = dynAllocateVertices(32);
    f32 outer = radius * 10.0f, inner = outer - 4.0f;
    s32 i;

    matrix_4x4_set_identity(&mf);
    mf.m[0][0] = mf.m[1][1] = mf.m[2][2] = 0.1f * D_800364CC;
    mf.m[3][0] = at[0];
    mf.m[3][1] = at[1];
    mf.m[3][2] = at[2];
    guMtxF2L(mf.m, mv);
    for (i = 0; i < 16; i++)
    {
        f32 a = i * (6.2831853f / 16.0f);

        gevrBodyVtx(&v[i * 2], (s32) (cosf(a) * outer), (s32) (sinf(a) * outer), rgba);
        gevrBodyVtx(&v[i * 2 + 1], (s32) (cosf(a) * inner), (s32) (sinf(a) * inner), rgba);
    }
    gSPMatrix(gdl++, osVirtualToPhysical((void *) currentPlayerGetProjectionMatrix()), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gdl++, osVirtualToPhysical(mv), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(gdl++);
    gSPClearGeometryMode(gdl++, G_ZBUFFER | G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gSPVertex(gdl++, osVirtualToPhysical(v), 32, 0);
    for (i = 0; i < 16; i++)
    {
        s32 o = i * 2, n = ((i + 1) % 16) * 2;

        gSP2Triangles(gdl++, o, o + 1, n, 0, o + 1, n + 1, n, 0);
    }
    gDPPipeSync(gdl++);
    gSPSetGeometryMode(gdl++, G_ZBUFFER);
    return gdl;
}

/* the label's text: what a grip takes, and which of how many */
static void gevrBodyLabel(s32 ctrl, char *out, s32 size)
{
    GevrBodyHand *h = &s_bodyHand[ctrl];
    s32 hand = ctrl ? GUNRIGHT : GUNLEFT;
    char name[48];
    s32 items = 0, at = 0, i;

    if (getPlayerCount() >= 2 && get_scenario() == 2 && bondinvIsAliveWithFlag())
    {
        snprintf(out, size, "FLAG");
        return;
    }
    if (h->pick == GEVR_BODY_HOLSTER)
    {
        gevrBodyShortName(gevrBodyItemName(hand, gevrBodyHandSelected(hand)), name, sizeof(name));
        snprintf(out, size, "HOLSTER %s", name);
        return;
    }
    gevrBodyShortName(gevrBodyItemName(hand, h->pick), name, sizeof(name));
    for (i = 0; i < h->n; i++)
    {
        if (h->choices[i] >= 0)
        {
            items++;
            if (h->choices[i] == h->pick)
            {
                at = items;
            }
        }
    }
    if (items > 1)
    {
        snprintf(out, size, "%s %d/%d", name, at, items);
    }
    else
    {
        snprintf(out, size, "%s", name);
    }
}

/*
 * bondview2.c maybe_mp_interface, after the sight: by a hand in a slot, what a
 * grip takes there (the shoulders are behind the head: theirs sits in front of
 * the chest), and a ring on a front slot's centre. Gun fit's Slots mode, or the
 * tune file's markers, ring every slot.
 */
Gfx *gevrBodySlotsDrawLabels(Gfx *gdl)
{
    s32 ctrl;

    if (!s_bodyLive || !s_bodyFrameValid || g_gevrExtraPass || g_CurrentPlayer == NULL)
    {
        return gdl;
    }
    if (s_bodyTune[BT_MARKERS] != 0.0f || gevrBodySlotFitting)
    {
        s32 s;

        for (s = 0; s < GEVR_BODY_SLOTS; s++)
        {
            const s32 chosen = gevrBodySlotFitting && s == s_bodyFitSlot;
            f32 cv[3];

            gevrBodyToView(s_bodyCentre[s], D_800364CC, cv);
            if (cv[2] < 0.0f)
            {
                gDPNoOpTag(gdl++, 0x565F0003);
                gdl = gevrBodyRing(gdl, cv, s_bodyRadius[s],
                                   gevrBodyCategoryTint(gevrBodySlotCategory(s)) | (chosen ? 0xF0 : 0x60));
                if (chosen)
                {
                    gdl = gevrBodyRing(gdl, cv, 2.0f, 0xFFFFFFF0);   /* and its centre */
                }
                gDPNoOpTag(gdl++, 0x565F0000);
            }
        }
    }
    for (ctrl = 0; ctrl < 2; ctrl++)
    {
        GevrBodyHand *h = &s_bodyHand[ctrl];
        s32 back = h->slot == GEVR_BS_BACK_GUN || h->slot == GEVR_BS_BACK_OFF;
        u32 tint;
        f32 p[3], v[3];
        char text[48];
        s32 i;

        if (h->slot < 0 || h->pick == -1)
        {
            continue;
        }
        tint = gevrBodyCategoryTint(gevrBodySlotCategory(h->slot));
        if (back)
        {
            /* in front of the chest, on the shoulder's side */
            const f32 side = (f32) gevrBodySlotSide(h->slot, VrLeftHandedMode);

            for (i = 0; i < 3; i++)
            {
                p[i] = s_bodyFrame.origin[i] + 35.0f * s_bodyFrame.fwd[i] + 15.0f * side * s_bodyFrame.right[i]
                     - 15.0f * s_bodyFrame.up[i];
            }
        }
        else
        {
            for (i = 0; i < 3; i++)
            {
                p[i] = h->at[i] + 9.0f * s_bodyFrame.up[i];
            }
            gevrBodyToView(s_bodyCentre[h->slot], D_800364CC, v);
            if (v[2] < 0.0f)
            {
                gDPNoOpTag(gdl++, 0x565F0003);   /* on the body */
                gdl = gevrBodyRing(gdl, v, 4.0f, tint | 0xC0);
                gDPNoOpTag(gdl++, 0x565F0000);
            }
        }
        gevrBodyToView(p, D_800364CC, v);
        if (v[2] > -5.0f * D_800364CC)
        {
            continue;   /* behind the eye */
        }
        gevrBodyLabel(ctrl, text, sizeof(text));
        /* with the hand (gunfire.c gevrHandTag), or for a shoulder's, the body */
        gDPNoOpTag(gdl++, 0x565F0000 | (u32) (back ? 3 : ctrl + 1));
        gdl = gevrDrawViewTag(gdl, text, v, 0.12f * D_800364CC, FALSE, (tint & 0xffffff00) | 0xB0);
        gDPNoOpTag(gdl++, 0x565F0000);
    }
    return gdl;
}

/* ------------------------------------------------------------- Gun fit's Slots mode */

/* X offers it while body slots are on (port/src/input.c, bondview2.c gevrFitNextLine) */
s32 gevrBodySlotFitAvailable(void)
{
    return VrBodySlots && g_gevrStereo;
}

/* the move stick's sideways flick: the next slot */
void gevrBodySlotFitPick(int dir)
{
    s_bodyFitSlot = ((s_bodyFitSlot + (dir < 0 ? -1 : 1)) % GEVR_BODY_SLOTS + GEVR_BODY_SLOTS) % GEVR_BODY_SLOTS;
    sysLogPrintf(LOG_NOTE, "bodyslot: fit the %s", gevrBodySlotName(s_bodyFitSlot));
}

/* a fresh grip: the slot where that hand is (Hand reload's belt is set the same way) */
void gevrBodySlotFitSet(int ctrl)
{
    f32 at[3], *fit = VrBodySlotFit[s_bodyFitSlot];

    if (!s_bodyFrameValid || ctrl < 0 || ctrl > 1 || !gevrBodyHandAt(ctrl, at))
    {
        return;
    }
    gevrBodySlotFitFrom(&s_bodyFrame, s_bodyFitSlot, at, VrLeftHandedMode, fit);
    if (fit[0] == 0.0f && fit[1] == 0.0f && fit[2] == 0.0f)
    {
        fit[0] = 0.01f;   /* 0 0 0 reads as "the default" */
    }
    gevrBodyBuzz(ctrl, GEVR_ACTION_SLOT_TAKE);
    sysLogPrintf(LOG_NOTE, "bodyslot: fit the %s at the %s: %.1f below %.1f out %.1f ahead", gevrBodySlotName(s_bodyFitSlot),
                 gevrBodyHandName(ctrl), fit[0], fit[1], fit[2]);
}

/* Y: the slot's default for your height again */
void gevrBodySlotFitReset(void)
{
    VrBodySlotFit[s_bodyFitSlot][0] = VrBodySlotFit[s_bodyFitSlot][1] = VrBodySlotFit[s_bodyFitSlot][2] = 0.0f;
    sysLogPrintf(LOG_NOTE, "bodyslot: fit the %s: back to its default", gevrBodySlotName(s_bodyFitSlot));
}

/* the readout (bondview2.c gevrDrawGunFit) */
void gevrBodySlotFitText(char *out, s32 size)
{
    static const char *upper[GEVR_BODY_SLOTS] = {
        "GUN HAND'S HIP", "OFF HAND'S HIP", "GUN HAND'S SHOULDER", "OFF HAND'S SHOULDER", "CHEST", "BELT",
    };
    const s32 s = s_bodyFitSlot;
    f32 off[3], near = -1.0f;
    s32 ctrl, i;
    char away[24];

    gevrBodySlotOffsets(s, VrBodySlotFit[s], VrPlayerHeight, off);
    for (ctrl = 0; ctrl < 2 && s_bodyFrameValid; ctrl++)
    {
        if (s_bodyHand[ctrl].tracked)
        {
            f32 d = 0.0f;

            for (i = 0; i < 3; i++)
            {
                d += (s_bodyHand[ctrl].at[i] - s_bodyCentre[s][i]) * (s_bodyHand[ctrl].at[i] - s_bodyCentre[s][i]);
            }
            d = sqrtf(d);
            if (near < 0.0f || d < near)
            {
                near = d;
            }
        }
    }
    snprintf(away, sizeof(away), near < 0.0f ? "?" : "%.0f CM", near);
    snprintf(out, size,
             "BODY SLOT FIT: %s (%d/%d)\n%.0f BELOW  %.0f OUT  %.0f AHEAD CM%s\nNEAREST HAND %s AWAY, REACH %.0f CM\n"
             "MOVE STICK SIDEWAYS: NEXT SLOT\nGRIP: PUT IT AT THAT HAND\nY: DEFAULT FOR YOUR HEIGHT\n",
             upper[s], s + 1, GEVR_BODY_SLOTS, off[0], off[1], off[2],
             VrBodySlotFit[s][0] == 0.0f && VrBodySlotFit[s][1] == 0.0f && VrBodySlotFit[s][2] == 0.0f ? " (DEFAULT)" : "",
             away, s_bodyRadius[s]);
}

void gevrBodySlotsReset(void)
{
    memset(s_bodyMru, 0, sizeof(s_bodyMru));
    gevrHolsterReset();   /* the old hip holster's memory, which no stage ever cleared */
    gevrBodyTorsoReset(&s_bodyTorso);
    gevrBodyIdle("new stage");
}

void gevrBodySlotsRecentre(void)
{
    gevrBodyTorsoReset(&s_bodyTorso);
}

int gevrBodySlotsOn(void)
{
    return VrBodySlots && s_bodyLive;
}

int gevrBodySlotHover(int ctrl)
{
    return ctrl >= 0 && ctrl < 2 && s_bodyLive ? s_bodyHand[ctrl].slot : -1;
}
