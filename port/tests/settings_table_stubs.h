/* What the settings table (port/src/gevr_settings.c) reads, stubbed: the
 * goldeneye-vr.ini globals, the headset's answers and the net layer's. For
 * one test file each (settings_table_native.c, launcher_frame_native.cpp);
 * port/tests/settings_stub_dir.py writes the net headers it includes. */
#ifndef GEVR_SETTINGS_TABLE_STUBS_H
#define GEVR_SETTINGS_TABLE_STUBS_H
#include "net/net_core.h"
#include "net/net_game.h"

#ifdef __cplusplus
extern "C" {
#endif

float VrUseSnapTurn, VrComfortVignette, VrMotionThrowStrength = 1, VrMotionThrowPitch, VrMotionThrowGazeAssist = .5f;
float VrDismemberDamage = 1, VrScreenDistance = 3, VrScreenFov = 60;
int VrMovementSpeed, VrSmoothTurnSpeed = 120, VrWatchFaceStatus, VrAimSteady, VrTouchModel, VrAimNoLean, VrAimSight;
int VrMotionThrowing, VrGestureHolster, VrGestureGripUse, VrGesturePickup, VrGestureMineGrab;
int VrForcePull, VrPullDisarm, VrAutoPickup, VrPullVisuals, VrBodySlots, VrBodySlotSize, VrBodySlotShow = 1;
int VrWatchGesturePause, VrManualReloading, VrPerWeaponRecoil, VrNoKnockback, VrNoHitstun, VrDamageFlash;
int VrThirdPerson, VrThirdPersonDist = 150, VrThirdPersonHeight = 20, VrThirdPersonSide = 35;
int VrLeftHandedMode, VrSwapJoysticks, VrGunFitArmed, VrShowStats, VrGunSizeCheat;
int VrMinesStickToGuards, VrBodiesStay, VrFastReinforcements, VrRagdolls, VrDismemberment, VrDismemberChance = 40;
int VrGexGuns, VrBeanFillTextures, VrBloodPatch, VrOddjobHat, VrEyeMsaa = 1;
int VrScreenCurved, VrScreenPassthrough, VrRefreshRate, VrSpaceWarp = -1;
int VrPlayMode = 1, VrCampaign;
int VrAlly, VrAllyWeapon, VrAllyAggression = 1, VrAllyHealth = 100, VrAllyDamage = 100, VrAllyInvincible;
static int s_bean, s_saves, s_net, s_host, s_coop, s_horde, s_netSpeed, s_netFast, s_applied;
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
int gevrNetConfigCoop(void) { return s_coop; }
int gevrNetConfigPve(void) { return s_coop || s_horde; }
int gevrCoopFastReinforcements(void) { return s_netFast; }
void gevrNetConfigSet(int field, int value)
{
    if (!s_host) return;
    if (field == CFG_MOVEMENT_SPEED) s_netSpeed = value;
    if (field == CFG_FAST_REINFORCEMENTS) s_netFast = value;
}

#ifdef __cplusplus
}
#endif
#endif
