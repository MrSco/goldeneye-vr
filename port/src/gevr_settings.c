/*
 * The settings table (port/include/gevr_settings.h, docs/launcher-unify.md).
 *
 * Each VR setting is one row here. The in-game screen draws these rows
 * (port/vr/gevr_panel_ui.h via gevr_settings_ui.h), and the launcher will too
 * (phase 2). They were src/game/options.c's watch rows (moved to the hold-Menu
 * panel on 2026-11-04), with the same values, steps and special cases, plus
 * the launcher-only rows the panel lacked: anti-aliasing, third person's
 * camera, ragdolls and dismemberment.
 *
 * The values are goldeneye-vr.ini's (port/vr/vr_settings_defaults.c), saved
 * on each change as before.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "gevr_settings.h"
#include "net/net_core.h"
#include "net/net_game.h"
#include "net/net_rules.h"

extern float VrUseSnapTurn;          /* 0 smooth, else the snap angle */
extern int VrMovementSpeed;
extern int VrSmoothTurnSpeed;
extern float VrComfortVignette;
extern int VrWatchFaceStatus;
extern int VrAimSteady;
extern int VrTouchModel;
extern int VrAimNoLean, VrAimSight;
extern int VrMotionThrowing;
extern float VrMotionThrowStrength, VrMotionThrowPitch, VrMotionThrowGazeAssist;
extern int VrGestureHolster, VrGestureGripUse, VrGesturePickup, VrGestureMineGrab;
extern int VrForcePull, VrPullDisarm, VrAutoPickup, VrPullVisuals;
extern int VrBodySlots, VrBodySlotSize;
extern int VrWatchGesturePause;
extern int VrManualReloading;
extern int VrPerWeaponRecoil;
extern int VrNoKnockback, VrNoHitstun, VrDamageFlash;
extern int VrThirdPerson, VrThirdPersonDist, VrThirdPersonHeight, VrThirdPersonSide;
extern int VrLeftHandedMode, VrSwapJoysticks, VrGunFitArmed, VrShowStats, VrGunSizeCheat;
extern int VrMinesStickToGuards, VrBodiesStay, VrFastReinforcements;
extern int VrRagdolls, VrDismemberment, VrDismemberChance;
extern float VrDismemberDamage;
extern int VrGexGuns;
extern int VrBeanFillTextures, VrBloodPatch, VrOddjobHat;
extern int VrEyeMsaa;
extern int VrPlayMode, VrBodySlotShow, VrCampaign;
extern int VrAlly, VrAllyWeapon, VrAllyAggression, VrAllyHealth, VrAllyDamage, VrAllyInvincible;
extern int gevrHatAvailable(void);
extern int gevrBeanIsAvailable(void), gevrBeanIsEnabled(void);
extern void gevrBeanSetEnabled(int on);
extern int gevrTexpackFillAvailable(void);
extern float VrScreenDistance, VrScreenFov;
extern int VrScreenCurved, VrScreenPassthrough;
extern void vr_screen_resize(float dist, float fov);
extern int VrRefreshRate;
extern int VrSpaceWarp;
extern int vr_spacewarp_supported(void), vr_spacewarp_setting_on(void);
extern int vr_get_supported_refresh_rates(int *rates, int capacity);
extern void vr_apply_refresh_rate(void);
extern int vr_screen_curve_supported(void);
extern int vr_passthrough_supported(void);
extern int vr_eye_msaa_supported(void);
extern void vrSettingsSave(void);

/* port/vr/vr_settings.h's ranges (that header is C++-side) */
#define SMOOTHTURN_MIN 45
#define SMOOTHTURN_MAX 240
#define SMOOTHTURN_STEP 15
#define SCREEN_FOV_MIN 25.0f
#define SCREEN_FOV_MAX 110.0f
#define SCREEN_DIST_MIN 1.0f
#define SCREEN_DIST_MAX 8.0f
#define THIRDPERSON_DIST_MIN 60
#define THIRDPERSON_DIST_MAX 300
#define THIRDPERSON_HEIGHT_MIN -40
#define THIRDPERSON_HEIGHT_MAX 80
#define THIRDPERSON_SIDE_MIN -80
#define THIRDPERSON_SIDE_MAX 80

typedef struct Row {
    const char *label;
    const char *tooltip;
    int kind;
    int section;
    unsigned where;
    int lsection;                   /* the launcher's place + 1; 0: the section's (s_launcherOf) */
    /* the value: an int or a float */
    int *ivar;
    float *fvar;
    /* a choice: its values (ints, or floats when fvar) and their names */
    const int *ivalues;
    const float *fvalues;
    const char *const *names;
    int count;
    /* a range: lo + index * step, shown with fmt (value * scale), zero as zero */
    float lo, hi, step, scale;
    const char *fmt;
    const char *zero;
    float def;                      /* the default value, NAN for none */
    /* a toggle's two names, when not Off/On */
    const char *off, *on;
    const char *(*blocked)(void);   /* why it can't change now */
    int (*shown)(void);             /* hidden while this says 0 */
    void (*fset)(float v);          /* sets it instead of the variable */
    void (*after)(void);            /* applies it after setting */
    /* a row the generic kinds don't cover: all four, or none */
    int (*cpositions)(void);
    int (*cget)(void);
    void (*cset)(int index);
    void (*ctext)(int index, char *buf, int size);
    int cdefault;                   /* its default position, -1 none */
} Row;

/* ---- the special cases (as options.c had them) ---- */

static const char *blockedCurved(void) { return vr_screen_curve_supported() ? NULL : "Not on this headset"; }
static const char *blockedPassthrough(void) { return vr_passthrough_supported() ? NULL : "Not on this headset"; }
static const char *blockedReload(void) { return VrGexGuns ? NULL : "Needs GoldenEye X"; }
static const char *blockedHat(void) { return gevrHatAvailable() ? NULL : "Get it in Mods"; }
static const char *blockedBeanFill(void) { return gevrTexpackFillAvailable() ? NULL : "Needs an HD pack"; }
static const char *blockedGripUse(void) { return VrManualReloading && VrGexGuns ? "Hand reload needs it" : NULL; }
static const char *blockedHolster(void) { return VrBodySlots ? "Body slots replace it" : NULL; }
static const char *blockedMsaa(void) { return vr_eye_msaa_supported() ? NULL : "Not on this headset"; }
static const char *blockedDismember(void) { return VrRagdolls ? NULL : "Needs ragdolls"; }
static const char *blockedBean(void) { return gevrBeanIsAvailable() ? NULL : "Needs Bean's files"; }
static const char *blockedSpaceWarp(void) { return vr_spacewarp_supported() ? NULL : "Not on this headset"; }
static const char *blockedSlots(void) { return VrBodySlots ? NULL : "Needs body slots"; }
static const char *blockedPull(void) { return VrForcePull ? NULL : "Needs force pull"; }
static const char *blockedThrow(void) { return VrMotionThrowing ? NULL : "Needs motion throw"; }
static const char *blockedTurnSpeed(void) { return VrUseSnapTurn > 0.0f ? "Smooth turning only" : NULL; }
static const char *blockedThirdPerson(void) { return VrPlayMode ? "Flat screen only" : NULL; }
/* The AI ally (#194, src/game/gevr_ally.c): GoldenEye solo, co-op and Horde, the host's */
static const char *blockedAlly(void)
{
    if (VrCampaign != 0) return "GoldenEye only";
    if (netIsActive() && !gevrNetConfigPve()) return "Co-op and Horde only";
    if (netIsActive() && !netIsHost()) return "Host's choice";
    return NULL;
}
static const char *blockedAllyPart(void)
{
    const char *why = blockedAlly();
    return why ? why : VrAlly ? NULL : "Needs the ally on";
}
static const char *blockedAllyHealth(void)
{
    const char *why = blockedAllyPart();
    return why ? why : VrAllyInvincible ? "Invincible" : NULL;
}

static int shownThirdPerson(void) { return VrThirdPerson != 0 && !VrPlayMode; }
static int shownDismember(void) { return VrRagdolls && VrDismemberment; }

static void setScreenFov(float v) { vr_screen_resize(VrScreenDistance, v); }
static void setScreenDist(float v) { vr_screen_resize(v, VrScreenFov); }

/* Turning: smooth, or a snap angle (a float) */
static const float s_snaps[] = { 0.0f, 30.0f, 45.0f, 90.0f };
static int turnPositions(void) { return 4; }
static int turnGet(void)
{
    int i;
    for (i = 0; i < 3 && s_snaps[i] < VrUseSnapTurn; i++) {}
    return i;
}
static void turnSet(int i) { VrUseSnapTurn = s_snaps[i]; }
static void turnText(int i, char *b, int n)
{
    if (i == 0) snprintf(b, n, "Smooth");
    else snprintf(b, n, "Snap %d", (int)s_snaps[i]);
}

/* Movement speed: 50% to 200% in 25s; online, the host's rule */
static int moveCurrentPercent(void)
{
    return netMovementSpeedPercent(netIsActive() ? netActiveMovementSpeed() : VrMovementSpeed);
}
static int movePositions(void) { return 7; }
static int moveGet(void)
{
    int i = (moveCurrentPercent() - 50) / 25;
    return i < 0 ? 0 : i > 6 ? 6 : i;
}
static void moveSet(int i)
{
    int mode = netMovementSpeedMode(50 + i * 25);
    if (netIsActive()) gevrNetConfigSet(CFG_MOVEMENT_SPEED, mode);   /* the host's only: the setter refuses others */
    else VrMovementSpeed = mode;
}
static void moveText(int i, char *b, int n)
{
    int pct = 50 + i * 25;
    if (netIsActive()) snprintf(b, n, "Host: %d%%", pct);
    else if (pct == 100) snprintf(b, n, "100%% (default)");
    else snprintf(b, n, "%d%%", pct);
}
static const char *blockedMove(void) { return netIsActive() && !netIsHost() ? "Host's choice" : NULL; }

/* Fast reinforcements: a solo preference; in co-op the host's rule */
static int fastGet(void)
{
    if (netIsActive()) return gevrNetConfigCoop() && gevrCoopFastReinforcements() ? 1 : 0;
    return VrFastReinforcements != 0;
}
static void fastSet(int i)
{
    /* The net setter accepts only the co-op host. A joiner's saved
     * solo preference cannot change this mission's reinforcement rule. */
    if (netIsActive()) gevrNetConfigSet(CFG_FAST_REINFORCEMENTS, i);
    else VrFastReinforcements = i;
}
static int fastPositions(void) { return 2; }
static void fastText(int i, char *b, int n)
{
    if (netIsActive()) snprintf(b, n, "%s", !gevrNetConfigCoop() ? "N/A" : i ? "Host: on" : "Host: off");
    else snprintf(b, n, "%s", i ? "On" : "Off");
}
static const char *blockedFast(void)
{
    if (!netIsActive()) return NULL;
    if (!gevrNetConfigCoop()) return "Co-op only";
    return netIsHost() ? NULL : "Host's choice";
}

/* The display rate: Auto, then the rates this headset offers */
static int refreshRates(int *rates)
{
    int n = vr_get_supported_refresh_rates(rates, 16);
    return n < 0 ? 0 : n > 16 ? 16 : n;
}
static int refreshPositions(void)
{
    int rates[16];
    return refreshRates(rates) + 1;
}
static int refreshGet(void)
{
    int rates[16], n = refreshRates(rates), i;
    for (i = 0; i < n && rates[i] != VrRefreshRate; i++) {}
    return VrRefreshRate <= 0 || i >= n ? 0 : i + 1;
}
static void refreshSet(int i)
{
    int rates[16], n = refreshRates(rates);
    VrRefreshRate = i <= 0 || i > n ? 0 : rates[i - 1];
    vr_apply_refresh_rate();
}
static void refreshText(int i, char *b, int n)
{
    int rates[16], count = refreshRates(rates);
    if (i <= 0 || i > count) snprintf(b, n, "Auto");
    else snprintf(b, n, "%d Hz", rates[i - 1]);
}

/* SpaceWarp: on or off as chosen; -1 (default) on with Bean at 120 Hz */
static int swPositions(void) { return 2; }
static int swGet(void) { return vr_spacewarp_setting_on() != 0; }
static void swSet(int i) { VrSpaceWarp = i ? 1 : 0; }
static void swText(int i, char *b, int n)
{
    snprintf(b, n, "%s%s", i ? "On" : "Off", VrSpaceWarp < 0 ? " (auto)" : "");
}

/* Project Bean */
static int beanPositions(void) { return 2; }
static int beanGet(void) { return gevrBeanIsEnabled() != 0; }
static void beanSet(int i) { if (gevrBeanIsAvailable()) gevrBeanSetEnabled(i); }
static void beanText(int i, char *b, int n) { snprintf(b, n, "%s", i ? "On" : "Off"); }

static const int s_bodies[] = { 0, 12, 24 };
static const char *const s_bodyNames[] = { "Fade", "12", "24" };
static const int s_three[] = { 0, 1, 2 };
static const char *const s_watchNames[] = { "Off", "On", "Only" };
static const char *const s_steadyNames[] = { "Off", "Low", "High" };
static const char *const s_gunSizeNames[] = { "Normal", "Tiny", "Big" };
static const char *const s_slotSizeNames[] = { "Small", "Normal", "Large" };
static const int s_touch[] = { 0, 1, 2, 3, 4 };
static const char *const s_touchNames[] = { "Auto", "Quest", "Quest 2", "Plus", "Pro" };
static const int s_msaa[] = { 1, 2 };
static const char *const s_msaaNames[] = { "Off", "2x" };
static const int s_two[] = { 0, 1 };
static const char *const s_playNames[] = { "Flat screen", "Stereo VR" };
static const int s_ten[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
static const char *const s_allyGunNames[] = { "PP7", "Silenced PP7", "DD44", "KF7 Soviet", "D5K Deutsche",
                                              "ZMG", "AR33", "RC-P90", "Shotgun", "Automatic shotgun" };
static const char *const s_allyMoodNames[] = { "Cautious", "Normal", "Aggressive" };

#define L(x) ((x) + 1)   /* a row's launcher place, when not its section's */

#define TOGGLE(sec, lab, var, tip) \
    { .label = lab, .tooltip = tip, .kind = GEVR_SET_TOGGLE, .section = sec, .where = GEVR_SET_BOTH, .ivar = &var, .def = NAN }
#define CHOICE(sec, lab, var, vals, nm, tip) \
    { .label = lab, .tooltip = tip, .kind = GEVR_SET_CHOICE, .section = sec, .where = GEVR_SET_BOTH, .ivar = &var, \
      .ivalues = vals, .names = nm, .count = (int)(sizeof(vals) / sizeof(vals[0])), .def = NAN }
#define CUSTOM(sec, lab, kd, pos, get, set, text, dflt, tip) \
    { .label = lab, .tooltip = tip, .kind = kd, .section = sec, .where = GEVR_SET_BOTH, .def = NAN, \
      .cpositions = pos, .cget = get, .cset = set, .ctext = text, .cdefault = dflt }

static const Row s_rows[] = {
    /* Comfort */
    CUSTOM(GEVR_SEC_COMFORT, "Movement speed", GEVR_SET_RANGE, movePositions, moveGet, moveSet, moveText, 2,
           "How fast Bond walks and runs. Online it is the host's rule."),
    CUSTOM(GEVR_SEC_COMFORT, "Turning", GEVR_SET_CHOICE, turnPositions, turnGet, turnSet, turnText, 0,
           "Smooth turning, or snapping by a set angle."),
    { .label = "Turn speed", .tooltip = "Smooth turning's speed, degrees a second.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_COMFORT, .where = GEVR_SET_BOTH, .ivar = &VrSmoothTurnSpeed,
      .lo = SMOOTHTURN_MIN, .hi = SMOOTHTURN_MAX, .step = SMOOTHTURN_STEP, .scale = 1, .fmt = "%.0f", .def = 120,
      .blocked = blockedTurnSpeed },
    { .label = "Vignette", .tooltip = "Darkens the edges of your view while you move or turn.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_COMFORT, .where = GEVR_SET_BOTH, .fvar = &VrComfortVignette,
      .lo = 0, .hi = 1, .step = 0.1f, .scale = 1, .fmt = "%.1f", .zero = "Off", .def = 0 },
    TOGGLE(GEVR_SEC_COMFORT, "No knockback", VrNoKnockback, "Hits don't push you around."),
    TOGGLE(GEVR_SEC_COMFORT, "No hitstun", VrNoHitstun, "Hits don't stop you moving."),
    TOGGLE(GEVR_SEC_COMFORT, "Hit flash", VrDamageFlash, "The screen flashes red when you're hit."),

    /* Controls */
    TOGGLE(GEVR_SEC_CONTROLS, "Left-handed", VrLeftHandedMode, "Swaps the gun hand and the off hand."),
    TOGGLE(GEVR_SEC_CONTROLS, "Swap sticks", VrSwapJoysticks, "Moves walking and turning to the other sticks."),
    TOGGLE(GEVR_SEC_CONTROLS, "Aim: no lean", VrAimNoLean, "Leaning your head doesn't move your aim."),
    TOGGLE(GEVR_SEC_CONTROLS, "Aim: crosshair", VrAimSight, "Shows the sight where your gun points."),
    CHOICE(GEVR_SEC_CONTROLS, "Aim steady", VrAimSteady, s_three, s_steadyNames, "Smooths small shakes of your gun hand."),
    TOGGLE(GEVR_SEC_CONTROLS, "Gun fit", VrGunFitArmed, "Adjust where guns sit in your hands."),
    CHOICE(GEVR_SEC_CONTROLS, "Controllers", VrTouchModel, s_touch, s_touchNames, "Which Touch controllers the watch's Controls page shows."),

    /* Gestures */
    TOGGLE(GEVR_SEC_GESTURES, "Watch gesture", VrWatchGesturePause, "Raise your watch to open it."),
    TOGGLE(GEVR_SEC_GESTURES, "Body slots WIP", VrBodySlots, "Holsters on your body: reach and grip to draw or put away."),
    { .label = "Slot size", .tooltip = "How far from a slot your hand still reaches it.", .kind = GEVR_SET_CHOICE,
      .section = GEVR_SEC_GESTURES, .where = GEVR_SET_BOTH, .ivar = &VrBodySlotSize, .ivalues = s_three,
      .names = s_slotSizeNames, .count = 3, .def = NAN, .blocked = blockedSlots },
    { .label = "Show slots", .tooltip = "Draw what the hips, chest and belt hold when you look down.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_GESTURES, .where = GEVR_SET_BOTH, .ivar = &VrBodySlotShow, .def = NAN, .blocked = blockedSlots },
    { .label = "Holster WIP", .tooltip = "Grip at your hip to holster or draw.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_GESTURES, .where = GEVR_SET_BOTH, .ivar = &VrGestureHolster, .def = NAN, .blocked = blockedHolster },
    { .label = "Hand reload WIP", .tooltip = "Reload by taking a magazine from your belt (GoldenEye X guns).", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_GESTURES, .where = GEVR_SET_BOTH, .ivar = &VrManualReloading, .def = NAN, .blocked = blockedReload },
    { .label = "Grip use", .tooltip = "Grip doors, buttons and switches to use them.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_GESTURES, .where = GEVR_SET_BOTH, .ivar = &VrGestureGripUse, .def = NAN, .blocked = blockedGripUse },
    TOGGLE(GEVR_SEC_GESTURES, "Grip hand WIP", VrGesturePickup, "Grip near a gun or item to take it into that hand."),
    TOGGLE(GEVR_SEC_GESTURES, "Mine re-grab", VrGestureMineGrab, "Grip your own stuck mine to take it back."),
    TOGGLE(GEVR_SEC_GESTURES, "Force pull WIP", VrForcePull, "Point an empty hand at a gun or item and squeeze: it flies to you."),
    { .label = "Pull disarm", .tooltip = "Force pull can take a gun out of a guard's hand. Never another player's.",
      .kind = GEVR_SET_TOGGLE, .section = GEVR_SEC_GESTURES, .where = GEVR_SET_BOTH, .ivar = &VrPullDisarm,
      .def = NAN, .blocked = blockedPull },
    TOGGLE(GEVR_SEC_GESTURES, "Auto pickup", VrAutoPickup, "Walking over a gun picks it up. Off: grab or pull guns."),
    { .label = "Pull visuals", .tooltip = "The glow, ring and line on what force pull points at. The buzz stays.",
      .kind = GEVR_SET_TOGGLE, .section = GEVR_SEC_GESTURES, .where = GEVR_SET_BOTH, .ivar = &VrPullVisuals,
      .def = NAN, .blocked = blockedPull },

    /* Weapons */
    { .label = "Gun recoil", .tooltip = "Each gun's own kick, or one for all.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_WEAPONS, .where = GEVR_SET_BOTH, .ivar = &VrPerWeaponRecoil, .def = NAN, .off = "Generic", .on = "Per gun" },
    TOGGLE(GEVR_SEC_WEAPONS, "Motion throw", VrMotionThrowing, "Throw grenades and knives with your arm."),
    { .label = "Throw power", .tooltip = "How far a throw goes for your swing.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_WEAPONS, .where = GEVR_SET_BOTH, .fvar = &VrMotionThrowStrength,
      .lo = 0.5f, .hi = 2.0f, .step = 0.1f, .scale = 1, .fmt = "%.1fx", .def = 1.0f, .blocked = blockedThrow },
    { .label = "Throw pitch", .tooltip = "Tilts every throw up or down, degrees.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_WEAPONS, .where = GEVR_SET_BOTH, .fvar = &VrMotionThrowPitch,
      .lo = -20, .hi = 20, .step = 2, .scale = 1, .fmt = "%+.0f", .def = 0, .blocked = blockedThrow },
    { .label = "Throw gaze", .tooltip = "How much throws bend toward where you look.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_WEAPONS, .where = GEVR_SET_BOTH, .fvar = &VrMotionThrowGazeAssist,
      .lo = 0, .hi = 1, .step = 0.1f, .scale = 100, .fmt = "%.0f%%", .def = 0.5f, .blocked = blockedThrow },

    /* Display */
    { .label = "Play mode", .tooltip = "Stereo VR: you are Bond. Flat screen: the game on a big screen.", .kind = GEVR_SET_CHOICE,
      .section = GEVR_SEC_DISPLAY, .lsection = L(GEVR_LSEC_PLAY), .where = GEVR_SET_LAUNCHER, .ivar = &VrPlayMode,
      .ivalues = s_two, .names = s_playNames, .count = 2, .def = NAN },
    CUSTOM(GEVR_SEC_DISPLAY, "Refresh", GEVR_SET_CHOICE, refreshPositions, refreshGet, refreshSet, refreshText, 0,
           "The headset's display rate. Auto leaves it to the headset."),
    { .label = "SpaceWarp", .tooltip = "Draws half the frames and has the headset make the rest. "
                                       "On by default with Bean at 120 Hz.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_DISPLAY, .where = GEVR_SET_BOTH, .def = NAN, .blocked = blockedSpaceWarp,
      .cpositions = swPositions, .cget = swGet, .cset = swSet, .ctext = swText, .cdefault = -1 },
    { .label = "Anti-aliasing", .tooltip = "Smooths jagged edges. Changes right away; costs GPU time, which is short on battery.",
      .kind = GEVR_SET_CHOICE, .section = GEVR_SEC_DISPLAY, .where = GEVR_SET_BOTH, .ivar = &VrEyeMsaa,
      .ivalues = s_msaa, .names = s_msaaNames, .count = 2, .def = NAN, .blocked = blockedMsaa },
    { .label = "Watch face", .tooltip = "Health, armour and radar on your watch. Only: and nowhere else.", .kind = GEVR_SET_CHOICE,
      .section = GEVR_SEC_DISPLAY, .lsection = L(GEVR_LSEC_HANDS), .where = GEVR_SET_BOTH, .ivar = &VrWatchFaceStatus,
      .ivalues = s_three, .names = s_watchNames, .count = 3, .def = NAN },
    { .label = "Screen size", .tooltip = "The flat screen's size, degrees across.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_DISPLAY, .where = GEVR_SET_BOTH, .fvar = &VrScreenFov,
      .lo = SCREEN_FOV_MIN, .hi = SCREEN_FOV_MAX, .step = 5, .scale = 1, .fmt = "%.0f", .def = NAN, .fset = setScreenFov },
    { .label = "Screen dist", .tooltip = "How far away the flat screen is.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_DISPLAY, .where = GEVR_SET_BOTH, .fvar = &VrScreenDistance,
      .lo = SCREEN_DIST_MIN, .hi = SCREEN_DIST_MAX, .step = 0.25f, .scale = 1, .fmt = "%.2f m", .def = NAN, .fset = setScreenDist },
    { .label = "Curved", .tooltip = "Curves the flat screen around you.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_DISPLAY, .where = GEVR_SET_BOTH, .ivar = &VrScreenCurved, .def = NAN, .blocked = blockedCurved },
    { .label = "Passthrough", .tooltip = "Your room around the flat screen.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_DISPLAY, .where = GEVR_SET_BOTH, .ivar = &VrScreenPassthrough, .def = NAN, .blocked = blockedPassthrough },
    { .label = "Third person", .tooltip = "The camera follows behind Bond in the flat screen, solo. "
                                          "Aiming, the watch and the tank go back to first person.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_DISPLAY, .lsection = L(GEVR_LSEC_PLAY), .where = GEVR_SET_BOTH, .ivar = &VrThirdPerson,
      .def = NAN, .blocked = blockedThirdPerson },
    { .label = "  Distance", .tooltip = "Third person: how far behind Bond, cm.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_DISPLAY, .lsection = L(GEVR_LSEC_PLAY), .where = GEVR_SET_BOTH, .ivar = &VrThirdPersonDist,
      .lo = THIRDPERSON_DIST_MIN, .hi = THIRDPERSON_DIST_MAX, .step = 10, .scale = 1, .fmt = "%.0f", .def = 150, .shown = shownThirdPerson },
    { .label = "  Height", .tooltip = "Third person: how far above his eyes, cm.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_DISPLAY, .lsection = L(GEVR_LSEC_PLAY), .where = GEVR_SET_BOTH, .ivar = &VrThirdPersonHeight,
      .lo = THIRDPERSON_HEIGHT_MIN, .hi = THIRDPERSON_HEIGHT_MAX, .step = 5, .scale = 1, .fmt = "%.0f", .def = 20, .shown = shownThirdPerson },
    { .label = "  Shoulder", .tooltip = "Third person: how far to his right, cm.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_DISPLAY, .lsection = L(GEVR_LSEC_PLAY), .where = GEVR_SET_BOTH, .ivar = &VrThirdPersonSide,
      .lo = THIRDPERSON_SIDE_MIN, .hi = THIRDPERSON_SIDE_MAX, .step = 5, .scale = 1, .fmt = "%.0f", .def = 35, .shown = shownThirdPerson },
    { .label = "Show stats", .tooltip = "Frame rate and timing in the corner, and in the log.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_DISPLAY, .lsection = L(GEVR_LSEC_PLAY), .where = GEVR_SET_BOTH, .ivar = &VrShowStats, .def = NAN },

    /* Game rules */
    TOGGLE(GEVR_SEC_RULES, "Mines stick", VrMinesStickToGuards, "Thrown mines stick to guards (solo)."),
    CHOICE(GEVR_SEC_RULES, "Bodies stay", VrBodiesStay, s_bodies, s_bodyNames, "How many bodies stay before the oldest fades."),
    CUSTOM(GEVR_SEC_RULES, "Fast reinforcements", GEVR_SET_TOGGLE, fastPositions, fastGet, fastSet, fastText, 0,
           "Reinforcements arrive sooner. In co-op it is the host's rule."),
    TOGGLE(GEVR_SEC_RULES, "Ragdolls", VrRagdolls, "Bodies fall with physics. Each headset simulates its own."),
    { .label = "Dismemberment", .tooltip = "Strong hits can take limbs off a ragdoll.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_RULES, .where = GEVR_SET_BOTH, .ivar = &VrDismemberment, .def = NAN, .blocked = blockedDismember },
    { .label = "  Damage needed", .tooltip = "How hard a hit must be to take a limb.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_RULES, .where = GEVR_SET_BOTH, .fvar = &VrDismemberDamage,
      .lo = 0.5f, .hi = 10.0f, .step = 0.5f, .scale = 1, .fmt = "%.1f", .def = 1.0f, .shown = shownDismember },
    { .label = "  Chance", .tooltip = "How often such a hit takes a limb.", .kind = GEVR_SET_RANGE,
      .section = GEVR_SEC_RULES, .where = GEVR_SET_BOTH, .ivar = &VrDismemberChance,
      .lo = 0, .hi = 100, .step = 5, .scale = 1, .fmt = "%.0f%%", .def = 40, .shown = shownDismember },
    { .label = "AI ally", .tooltip = "Natalya as a companion: GoldenEye solo, campaign co-op and Horde, one for the party. "
                                     "The host chooses; it applies from the next mission. Not in missions with their own Natalya.",
      .kind = GEVR_SET_TOGGLE, .section = GEVR_SEC_RULES, .lsection = L(GEVR_LSEC_ALLY), .where = GEVR_SET_LAUNCHER,
      .ivar = &VrAlly, .def = NAN, .blocked = blockedAlly },
    { .label = "Weapon", .tooltip = "The ally's gun.", .kind = GEVR_SET_CHOICE, .section = GEVR_SEC_RULES,
      .lsection = L(GEVR_LSEC_ALLY), .where = GEVR_SET_LAUNCHER, .ivar = &VrAllyWeapon, .ivalues = s_ten,
      .names = s_allyGunNames, .count = 10, .def = NAN, .blocked = blockedAllyPart },
    { .label = "Aggression", .tooltip = "Cautious: engages alerted enemies nearby. Normal and aggressive: a wider range. "
                                        "Always stays close to Bond.", .kind = GEVR_SET_CHOICE, .section = GEVR_SEC_RULES,
      .lsection = L(GEVR_LSEC_ALLY), .where = GEVR_SET_LAUNCHER, .ivar = &VrAllyAggression, .ivalues = s_three,
      .names = s_allyMoodNames, .count = 3, .def = NAN, .blocked = blockedAllyPart },
    { .label = "Damage", .tooltip = "How hard the ally's shots hit.", .kind = GEVR_SET_RANGE, .section = GEVR_SEC_RULES,
      .lsection = L(GEVR_LSEC_ALLY), .where = GEVR_SET_LAUNCHER, .ivar = &VrAllyDamage,
      .lo = 25, .hi = 400, .step = 25, .scale = 1, .fmt = "%.0f%%", .def = 100, .blocked = blockedAllyPart },
    { .label = "Invincible", .tooltip = "Nothing hurts the ally.", .kind = GEVR_SET_TOGGLE, .section = GEVR_SEC_RULES,
      .lsection = L(GEVR_LSEC_ALLY), .where = GEVR_SET_LAUNCHER, .ivar = &VrAllyInvincible, .def = NAN,
      .blocked = blockedAllyPart },
    { .label = "Health", .tooltip = "How much the ally can take.", .kind = GEVR_SET_RANGE, .section = GEVR_SEC_RULES,
      .lsection = L(GEVR_LSEC_ALLY), .where = GEVR_SET_LAUNCHER, .ivar = &VrAllyHealth,
      .lo = 25, .hi = 1000, .step = 25, .scale = 1, .fmt = "%.0f%%", .def = 100, .blocked = blockedAllyHealth },

    /* Mods and fun */
    { .label = "Project Bean", .tooltip = "The XBLA remaster's models and textures, from your own files.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_MODS, .where = GEVR_SET_INGAME, .def = NAN, .blocked = blockedBean,
      .cpositions = beanPositions, .cget = beanGet, .cset = beanSet, .ctext = beanText, .cdefault = -1 },
    { .label = "Bean HD fill", .tooltip = "Fills Bean's missing pictures from the HD pack.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_MODS, .where = GEVR_SET_INGAME, .ivar = &VrBeanFillTextures, .def = NAN, .blocked = blockedBeanFill },
    /* the launcher's Mods tab draws these with their packs (vr_launcher.cpp gevrModsPage) */
    { .label = "GoldenEye X", .tooltip = "GoldenEye X's guns, hands and reloads.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_MODS, .where = GEVR_SET_INGAME, .ivar = &VrGexGuns, .def = NAN },
    { .label = "Gun size", .tooltip = "Tiny or big guns, as the cheat.", .kind = GEVR_SET_CHOICE,
      .section = GEVR_SEC_MODS, .lsection = L(GEVR_LSEC_CHEATS), .where = GEVR_SET_BOTH, .ivar = &VrGunSizeCheat,
      .ivalues = s_three, .names = s_gunSizeNames, .count = 3, .def = NAN },
    { .label = "Blood patch", .tooltip = "Shot guards spray the red blood sparks Rare left in the game (by Wreck).",
      .kind = GEVR_SET_TOGGLE, .section = GEVR_SEC_MODS, .where = GEVR_SET_INGAME, .ivar = &VrBloodPatch, .def = NAN },
    { .label = "Oddjob's hat", .tooltip = "Oddjob's hat in place of the throwing knife.", .kind = GEVR_SET_TOGGLE,
      .section = GEVR_SEC_MODS, .where = GEVR_SET_INGAME, .ivar = &VrOddjobHat, .def = NAN, .blocked = blockedHat },
};

#define ROWS ((int)(sizeof(s_rows) / sizeof(s_rows[0])))

static const char *const s_sections[GEVR_SEC_COUNT] = {
    "Comfort", "Controls", "Gestures", "Weapons", "Display", "Game rules", "Mods and fun",
};

static const Row *row(int id) { return id >= 0 && id < ROWS ? &s_rows[id] : NULL; }

/* The launcher's place for a section's rows, where a row names none */
static const int s_launcherOf[GEVR_SEC_COUNT] = {
    GEVR_LSEC_COMFORT, GEVR_LSEC_HANDS, GEVR_LSEC_GESTURES, GEVR_LSEC_THROWING,
    GEVR_LSEC_SCREEN, GEVR_LSEC_RULES, GEVR_LSEC_MODS,
};
static int launcherSection(const Row *r) { return r->lsection ? r->lsection - 1 : s_launcherOf[r->section]; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a range's steps */
static int rangeSteps(const Row *r)
{
    int n = (int)floorf((r->hi - r->lo) / r->step + 0.5f);
    return n < 0 ? 0 : n;
}

static float rangeValue(const Row *r, int i) { return r->lo + (float)i * r->step; }

int gevrSettingCount(void) { return ROWS; }

int gevrSettingInfo(int id, GevrSettingInfo *out)
{
    const Row *r = row(id);
    if (!r || !out) return 0;
    out->label = r->label;
    out->tooltip = r->tooltip;
    out->kind = r->kind;
    out->section = r->section;
    out->launcherSection = launcherSection(r);
    out->where = r->where;
    return 1;
}

int gevrSettingShown(int id)
{
    const Row *r = row(id);
    return r && (!r->shown || r->shown());
}

int gevrSettingPositions(int id)
{
    const Row *r = row(id);
    if (!r) return 0;
    if (r->cpositions) return r->cpositions();
    switch (r->kind) {
        case GEVR_SET_TOGGLE: return 2;
        case GEVR_SET_CHOICE: return r->count;
        default:              return rangeSteps(r) + 1;
    }
}

int gevrSettingIndex(int id)
{
    const Row *r = row(id);
    int i;
    if (!r) return 0;
    if (r->cget) return r->cget();
    switch (r->kind) {
        case GEVR_SET_TOGGLE:
            return r->ivar && *r->ivar ? 1 : 0;
        case GEVR_SET_CHOICE:
            /* the value it holds, else the first at or above it (options.c's walk), else the last */
            for (i = 0; i < r->count; i++) {
                if (r->ivalues[i] >= *r->ivar) return i;
            }
            return r->count - 1;
        default: {
            float v = r->fvar ? *r->fvar : (float)*r->ivar;
            return clampi((int)floorf((v - r->lo) / r->step + 0.5f), 0, rangeSteps(r));
        }
    }
}

int gevrSettingDefault(int id)
{
    const Row *r = row(id);
    if (!r) return -1;
    if (r->cpositions) return r->cdefault;
    if (r->kind == GEVR_SET_RANGE && !isnan(r->def)) {
        return clampi((int)floorf((r->def - r->lo) / r->step + 0.5f), 0, rangeSteps(r));
    }
    return -1;
}

void gevrSettingSet(int id, int index)
{
    const Row *r = row(id);
    if (!r || gevrSettingBlocked(id)) return;
    index = clampi(index, 0, gevrSettingPositions(id) - 1);
    if (r->cset) {
        r->cset(index);
    } else if (r->kind == GEVR_SET_TOGGLE) {
        *r->ivar = index;
    } else if (r->kind == GEVR_SET_CHOICE) {
        *r->ivar = r->ivalues[index];
    } else {
        float v = rangeValue(r, index);
        if (r->fset) r->fset(v);
        else if (r->fvar) *r->fvar = v;
        else *r->ivar = (int)floorf(v + 0.5f);
    }
    if (r->after) r->after();
    vrSettingsSave();
}

void gevrSettingStep(int id, int dir)
{
    int n = gevrSettingPositions(id), i = gevrSettingIndex(id);
    if (n <= 0) return;
    if (dir == 2) i = i >= n - 1 ? 0 : i + 1;   /* A: the next value, wrapping */
    else i = clampi(i + dir, 0, n - 1);
    gevrSettingSet(id, i);
}

void gevrSettingText(int id, int index, char *buf, int size)
{
    const Row *r = row(id);
    if (!buf || size <= 0) return;
    buf[0] = '\0';
    if (!r) return;
    if (index < 0) index = gevrSettingIndex(id);
    if (r->ctext) {
        r->ctext(index, buf, size);
        return;
    }
    switch (r->kind) {
        case GEVR_SET_TOGGLE:
            snprintf(buf, size, "%s", index ? (r->on ? r->on : "On") : (r->off ? r->off : "Off"));
            break;
        case GEVR_SET_CHOICE:
            snprintf(buf, size, "%s", r->names[clampi(index, 0, r->count - 1)]);
            break;
        default: {
            float v = rangeValue(r, index);
            if (r->zero && fabsf(v) < r->step * 0.5f) snprintf(buf, size, "%s", r->zero);
            else snprintf(buf, size, r->fmt, v * r->scale);
            if (!isnan(r->def) && fabsf(v - r->def) < r->step * 0.5f && (int)strlen(buf) + 11 < size) {
                strcat(buf, " (default)");
            }
            break;
        }
    }
}

const char *gevrSettingBlocked(int id)
{
    const Row *r = row(id);
    if (!r) return "";
    if (r->blocked) return r->blocked();
    if (r->cpositions == movePositions) return blockedMove();
    if (r->cpositions == fastPositions) return blockedFast();
    return NULL;
}

int gevrSettingSectionCount(void) { return GEVR_SEC_COUNT; }

const char *gevrSettingSectionName(int section)
{
    return section >= 0 && section < GEVR_SEC_COUNT ? s_sections[section] : "";
}

int gevrSettingSectionRows(int section, unsigned where, int *ids, int max)
{
    int n = 0, id;
    for (id = 0; id < ROWS; id++) {
        const int place = where == GEVR_SET_LAUNCHER ? launcherSection(&s_rows[id]) : s_rows[id].section;
        if (place != section || !(s_rows[id].where & where) || !gevrSettingShown(id)) continue;
        if (ids && n < max) ids[n] = id;
        n++;
    }
    return n;
}
