/* The settings table (port/src/gevr_settings.c) against stubbed globals:
 * every row's positions read back as set, Reset lands on the default, rows
 * that depend on another hide and block with it, online rows go to the host,
 * and a section's rows fit the in-game screen's two columns. */
#include "gevr_settings.h"
#include "net/net_core.h"
#include "net/net_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok, const char *message) { if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); } }

float VrUseSnapTurn, VrComfortVignette, VrMotionThrowStrength = 1, VrMotionThrowPitch, VrMotionThrowGazeAssist = .5f;
float VrDismemberDamage = 1, VrScreenDistance = 3, VrScreenFov = 60;
int VrMovementSpeed, VrSmoothTurnSpeed = 120, VrWatchFaceStatus, VrAimSteady, VrTouchModel, VrAimNoLean, VrAimSight;
int VrMotionThrowing, VrGestureHolster, VrGestureGripUse, VrGesturePickup, VrGestureMineGrab;
int VrForcePull, VrPullDisarm, VrAutoPickup, VrPullVisuals, VrBodySlots, VrBodySlotSize, VrWatchGesturePause;
int VrManualReloading, VrPerWeaponRecoil, VrNoKnockback, VrNoHitstun, VrDamageFlash;
int VrThirdPerson, VrThirdPersonDist = 150, VrThirdPersonHeight = 20, VrThirdPersonSide = 35;
int VrLeftHandedMode, VrSwapJoysticks, VrGunFitArmed, VrShowStats, VrGunSizeCheat;
int VrMinesStickToGuards, VrBodiesStay, VrFastReinforcements, VrRagdolls, VrDismemberment, VrDismemberChance = 40;
int VrGexGuns, VrBeanFillTextures, VrBloodPatch, VrOddjobHat, VrEyeMsaa = 1;
int VrScreenCurved, VrScreenPassthrough, VrRefreshRate, VrSpaceWarp = -1;
static int s_bean, s_saves, s_net, s_host, s_coop, s_netSpeed, s_netFast, s_applied;
int gevrHatAvailable(void) { return 1; }
int gevrBeanIsAvailable(void) { return 1; }
int gevrBeanIsEnabled(void) { return s_bean; }
void gevrBeanSetEnabled(int on) { s_bean = on; }
int gevrTexpackFillAvailable(void) { return 1; }
void vr_screen_resize(float dist, float fov) { VrScreenDistance = dist; VrScreenFov = fov; }
int vr_spacewarp_supported(void) { return 1; }
int vr_spacewarp_setting_on(void) { return VrSpaceWarp < 0 ? VrRefreshRate == 120 : VrSpaceWarp; }
int vr_get_supported_refresh_rates(int *rates, int capacity)
{
    static const int r[] = { 72, 90, 120 };
    for (int i = 0; i < 3 && i < capacity; i++) rates[i] = r[i];
    return 3;
}
void vr_apply_refresh_rate(void) { s_applied++; }
int vr_screen_curve_supported(void) { return 1; }
int vr_passthrough_supported(void) { return 1; }
int vr_eye_msaa_supported(void) { return 0; }
void vrSettingsSave(void) { s_saves++; }
bool netIsActive(void) { return s_net; }
bool netIsHost(void) { return s_host; }
int netActiveMovementSpeed(void) { return s_netSpeed; }
int gevrCoopActive(void) { return s_coop; }
int gevrCoopFastReinforcements(void) { return s_netFast; }
void gevrNetConfigSet(int field, int value)
{
    if (!s_host) return;
    if (field == CFG_MOVEMENT_SPEED) s_netSpeed = value;
    if (field == CFG_FAST_REINFORCEMENTS) s_netFast = value;
}

static int find(const char *label)
{
    for (int id = 0; id < gevrSettingCount(); id++) {
        GevrSettingInfo info;
        if (gevrSettingInfo(id, &info) && !strcmp(info.label, label)) return id;
    }
    fprintf(stderr, "FAIL: no row \"%s\"\n", label);
    exit(1);
}

static void text(int id, char *buf) { gevrSettingText(id, -1, buf, 64); }

int main(void)
{
    char t[64];
    VrRagdolls = VrDismemberment = VrThirdPerson = 1;   /* every row shown */

    /* Every row: each position reads back as set, and Reset lands on the default. */
    for (int id = 0; id < gevrSettingCount(); id++) {
        GevrSettingInfo info;
        check(gevrSettingInfo(id, &info) && info.label && info.tooltip, "every row has a label and a tooltip");
        check(info.section >= 0 && info.section < GEVR_SEC_COUNT, "every row is in a section");
        if (gevrSettingBlocked(id)) continue;
        const int n = gevrSettingPositions(id);
        check(n >= 2, "every row has at least two positions");
        if (info.kind == GEVR_SET_TOGGLE) check(n == 2, "a toggle has two positions");
        for (int i = 0; i < n; i++) {
            gevrSettingSet(id, i);
            if (gevrSettingIndex(id) != i) {
                fprintf(stderr, "FAIL: %s position %d reads back %d\n", info.label, i, gevrSettingIndex(id));
                exit(1);
            }
            gevrSettingText(id, i, t, sizeof(t));
            check(t[0] != '\0', "every position has text");
        }
        const int def = gevrSettingDefault(id);
        if (def >= 0) {
            gevrSettingSet(id, def);
            check(gevrSettingIndex(id) == def, "Reset lands on the default");
        }
    }
    check(s_saves > 0, "setting a row saves goldeneye-vr.ini");

    /* Values as the launcher and the watch had them. */
    int id = find("Turn speed");
    check(gevrSettingDefault(id) == 5 && gevrSettingPositions(id) == 14, "turn speed: 45..240 by 15, default 120");
    gevrSettingSet(id, gevrSettingDefault(id));
    check(VrSmoothTurnSpeed == 120, "turn speed's Reset is 120");
    id = find("Throw gaze");
    gevrSettingSet(id, 3); text(id, t);
    check(!strcmp(t, "30%"), "throw gaze reads as a percentage");
    id = find("Vignette");
    gevrSettingSet(id, 0); text(id, t);
    check(!strcmp(t, "Off (default)"), "vignette's zero reads Off, its default");
    id = find("Turning");
    gevrSettingSet(id, 2); text(id, t);
    check(VrUseSnapTurn == 45 && !strcmp(t, "Snap 45"), "turning snaps by 45");
    VrScreenDistance = 3;
    id = find("Screen size");
    gevrSettingSet(id, gevrSettingPositions(id) - 1);
    check(VrScreenFov == 110 && VrScreenDistance == 3, "screen size resizes the screen, keeping its distance");
    id = find("Refresh");
    check(gevrSettingPositions(id) == 4, "refresh: Auto and the headset's three rates");
    gevrSettingSet(id, 3); text(id, t);
    check(VrRefreshRate == 120 && s_applied && !strcmp(t, "120 Hz"), "refresh 120 applies the rate");
    id = find("SpaceWarp");
    VrSpaceWarp = -1; text(id, t);
    check(!strcmp(t, "On (auto)"), "SpaceWarp's default reads On (auto) at 120 Hz");
    id = find("Anti-aliasing");
    check(gevrSettingBlocked(id) && !strcmp(gevrSettingBlocked(id), "Not on this headset"), "anti-aliasing blocks where unsupported");
    id = find("Movement speed");
    gevrSettingSet(id, 2); text(id, t);
    check(!strcmp(t, "100% (default)") && VrMovementSpeed == 0, "movement speed 100% is the normal mode");

    /* Hidden and blocked with the row they depend on. */
    int ids[48], before = gevrSettingSectionRows(GEVR_SEC_DISPLAY, GEVR_SET_INGAME, ids, 48);
    VrThirdPerson = 0;
    check(gevrSettingSectionRows(GEVR_SEC_DISPLAY, GEVR_SET_INGAME, ids, 48) == before - 3,
          "third person's three sliders hide while it is off");
    VrDismemberment = 0;
    check(!gevrSettingShown(find("  Chance")), "dismemberment's thresholds hide while it is off");
    VrRagdolls = 0;
    check(gevrSettingBlocked(find("Dismemberment")) != NULL, "dismemberment needs ragdolls");
    VrGexGuns = 0;
    check(gevrSettingBlocked(find("Hand reload WIP")) != NULL, "hand reload needs GoldenEye X");
    VrBodySlots = 1;
    check(gevrSettingBlocked(find("Holster WIP")) != NULL, "body slots replace the hip holster");

    /* Online: movement speed and fast reinforcements are the host's. */
    s_net = 1; s_host = 0; s_coop = 1; s_netSpeed = 4;
    id = find("Movement speed"); text(id, t);
    check(!strcmp(t, "Host: 150%"), "a joiner sees the host's movement speed");
    check(gevrSettingBlocked(id) && !strcmp(gevrSettingBlocked(id), "Host's choice"), "a joiner can't change it");
    gevrSettingSet(id, 0);
    check(s_netSpeed == 4 && VrMovementSpeed == 0, "a joiner's set changes nothing");
    s_host = 1;
    gevrSettingSet(id, 6);
    check(s_netSpeed == 6 && VrMovementSpeed == 0, "the host's set changes the match's rule, not the solo preference");
    id = find("Fast reinforcements");
    gevrSettingSet(id, 1); text(id, t);
    check(s_netFast == 1 && VrFastReinforcements == 0 && !strcmp(t, "Host: on"), "the co-op host sets fast reinforcements for the match");
    s_coop = 0;
    check(gevrSettingBlocked(id) && !strcmp(gevrSettingBlocked(id), "Co-op only"), "fast reinforcements is co-op only online");
    s_net = 0;

    /* Every section has rows, none more than two columns of seven. */
    VrThirdPerson = VrRagdolls = VrDismemberment = 1;
    for (int s = 0; s < gevrSettingSectionCount(); s++) {
        const int n = gevrSettingSectionRows(s, GEVR_SET_INGAME, ids, 48);
        check(n > 0 && n <= 14, "a section fills one or two columns of at most seven rows");
        check(gevrSettingSectionName(s) != NULL, "every section has a name");
    }
    puts("PASS: settings table: every row round-trips, Reset, hidden and blocked rows, the host's online rules, each section fits two columns");
    return 0;
}
