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
        && !gevrWeaponPanelOpen && gevrGunFitActive != 1 && D_800364CC > 1e-6f;
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
    /* a hand reaching for the front of the body, or the head looking down at it:
     * the torso holds still (The Light Brigade's and H3VR's belts turned away) */
    if (s_bodyFrameValid)
    {
        freeze = s_bodyFrame.pitch < s_bodyTune[BT_FREEZE];
        for (ctrl = 0; ctrl < 2 && !freeze; ctrl++)
        {
            if (!s_bodyHand[ctrl].tracked)
            {
                continue;
            }
            for (s = 0; s < GEVR_BODY_SLOTS; s++)
            {
                f32 d[3] = { s_bodyHand[ctrl].at[0] - s_bodyCentre[s][0], s_bodyHand[ctrl].at[1] - s_bodyCentre[s][1],
                             s_bodyHand[ctrl].at[2] - s_bodyCentre[s][2] };

                if (s != GEVR_BS_BACK_GUN && s != GEVR_BS_BACK_OFF
                    && sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) < 1.5f * s_bodyRadius[s])
                {
                    freeze = TRUE;
                }
            }
        }
    }
    if (gevrBodyTorsoUpdate(&s_bodyTorso, hp, follow, (f32) g_ClockTimer, freeze, s_bodyTune[BT_TWIST], 45.0f))
    {
        sysLogPrintf(LOG_NOTE, "bodyslot: torso starts at the head (%s)", s_bodyFrameValid ? "it jumped" : "first frame");
    }
    gevrBodyFrameBuild(&s_bodyFrame, &s_bodyTorso, hp, fw, up, rt);
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

    gevrBodyTuneRead();
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
