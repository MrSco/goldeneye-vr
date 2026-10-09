#include <ultra64.h>
#undef GAME_TICKRATE
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondgame.h>
#include "game/bondview.h"
#include "game/matrixmath.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define sysLogPrintf(...) ((void)0)
#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

static struct player player;
struct player *g_CurrentPlayer = &player;
static int g_gevrStereo = 1, VrLeftHandedMode;
float D_800364CC = 1;
static s32 s_gevrTwoHandGun = GUNRIGHT, s_gevrTwoHand, s_gevrTwoHandResetDir;
static f32 s_gevrTwoHandAmt;
static f32 VrGripTrim[2][6], VrGexGripTrim[2][6];
#define s_gevrTwoHandTrim VrGripTrim
static float VrGunOffX, VrGunOffY, VrGunOffZ;
static const float *s_gevrGunOffOverride;
static int s_gevrMuzzleValid[2];
static float s_gevrMuzzle[2][3];
static int VrGexGuns, tracked[2] = {1, 1}, grip[2];
static float pose[2][3]; /* physical controller positions, metres */
static float VrGexHeldMag[3], gexFore[3];
static int gexForeValid;
static int online, localSlot, playerSlot, reloadClaims, haveSniper;
static unsigned frame;
static int buzzes, buzzCtrl = -1;
static int aimQueries;
static int gevrStereoAimCached(int hand, coord3d *out) { (void)hand; (void)out; aimQueries++; return 1; }
static int gevrPhysHand(int ctrl);
static s32 gevrStereoTwoHandGrip(void);
static void gevrTwoHandAim(const f32 pos[3], f32 right[3], f32 up[3], f32 back[3]);
s32 gevrStereoGunMatrix(s32 handnum, Mtxf *out);
static int getCurrentPlayerWeaponId(int hand) { return player.hands[hand].weaponnum; }
static int netIsActive(void) { return online; }
static int get_cur_playernum(void) { return playerSlot; }
static int netGetLocalSlot(void) { return localSlot; }
static int gevrGexHeld(int hand) { (void)hand; return 0; }
static int gevrGexPistolSupportAllowed(void) { return 1; }
static int gevrGexPistolPoint(int support, float out[3]) { (void)support; (void)out; return 0; }
static int gevrGexForePoint(float out[3]) { memcpy(out,gexFore,sizeof(gexFore)); return gexForeValid; }
static int gevrReloadSupportGun(void)
{ return player.hands[GUNRIGHT].weaponnum <= ITEM_FIST ? GUNLEFT : GUNRIGHT; }
static int gevrReloadClaimsOffHand(void) { return reloadClaims; }
static int gevrAimLogEnabled(void) { return 0; }
static unsigned gevrVrGripSnapshotId(void) { return frame; }
static int gevrVrGripTracked(int ctrl) { return tracked[gevrPhysHand(ctrl)]; }
static int gevrVrGripPoseCamera(int ctrl, float pos[3], float quat[4])
{
    memcpy(pos, pose[gevrPhysHand(ctrl)], 3*sizeof(float));
    quat[0] = quat[3] = sqrtf(0.5f); quat[1] = quat[2] = 0;
    return 1;
}
static _Bool get_button_state(int ctrl, const char *button)
{ assert(strcmp(button, "grip") == 0); return grip[gevrPhysHand(ctrl)]; }
static float gevrGunSizeFactor(void) { return 1; }
static void gevrGunOff(int hand, float out[3])
{ (void)hand; out[0] = VrGunOffX; out[1] = VrGunOffY; out[2] = VrGunOffZ; }
static float gevrScopeMagnification(void) { return 1; }
static int vr_haptics_ready(void) { return 1; }
static int trigger_haptic_vibration_c(int ctrl, float amplitude, float duration)
{ assert(amplitude > 0 && duration > 0); buzzes++; buzzCtrl = ctrl; return 1; }
static int bondinvItemAvailable(int item) { assert(item == ITEM_SNIPERRIFLE); return haveSniper; }
typedef struct { float kickPitch, kickYaw, kickPush; } WeaponRecoilProfile;
static WeaponRecoilProfile sRecoilProfile[2];
static int VrPerWeaponRecoil = 1;
static WeaponRecoilProfile GetRecoilProfileForGEClass(int cls)
{ (void)cls; WeaponRecoilProfile p = {2, 3, 4}; return p; }
static void RecoilFireImpulse(int ctrl, WeaponRecoilProfile profile) { (void)ctrl; (void)profile; }
s32 gevrStereoTwoHandGun(void);
/* DEFINITIONS */
/* PRODUCTION */

static void tick(void) { frame++; gevrStereoTwoHandUpdate(); }
static void near(float actual, float expected) { assert(fabsf(actual - expected) < 0.0001f); }
static void release(void)
{
    grip[0] = grip[1] = 0;
    tick(); assert(!gevrStereoTwoHandGrip());
    for (int i = 0; i < 60; i++) tick();
}
static void setup(int gun, int leftHanded, int weapon)
{
    release();
    memset(&player, 0, sizeof(player));
    memset(pose, 0, sizeof(pose));
    memset(s_gevrMuzzleValid, 0, sizeof(s_gevrMuzzleValid));
    VrLeftHandedMode = leftHanded;
    player.hands[gun].weaponnum = weapon;
    player.hands[gun].field_87F = 1;
    player.hands[1-gun].weaponnum = gun == GUNLEFT ? ITEM_FIST : ITEM_UNARMED;
    player.cur_item_weapon_getname = ITEM_SNIPERRIFLE;
    player.field_2A44[0] = player.field_2A44[1] = -1;
    haveSniper = 1;
    /* Set the gun's muzzle to 40 cm; stale data for the other slot is far away. */
    s_gevrMuzzleValid[gun] = s_gevrMuzzleValid[1-gun] = 1;
    s_gevrMuzzle[gun][2] = -40 * GEVR_UNITS_PER_METRE / 100;
    s_gevrMuzzle[1-gun][2] = -150 * GEVR_UNITS_PER_METRE / 100;
    pose[gevrPhysHand(gun)][0] = 0.06f;
    pose[gevrPhysHand(gun)][2] = -0.14f;
    buzzes = 0;
}
static void hold(int gun)
{
    grip[gevrPhysHand(1-gun)] = 1;
    tick(); assert(!gevrStereoTwoHandGrip()); /* trigger hand's grip alone */
    grip[gevrPhysHand(gun)] = 1;
    tick(); assert(gevrStereoTwoHandGrip());
    assert(gevrStereoTwoHandGun() == gun && gevrStereoTwoHandSupportCtrl() == gun);
    assert(buzzes == 1 && buzzCtrl == gun);
}
static void testSlot(int gun, int leftHanded, int weapon)
{
    setup(gun, leftHanded, weapon);
    hold(gun);
    for (int i = 0; i < 30; i++) tick();
    float pos[3], r[3], u[3], b[3];
    assert(gevrGripAxes(1-gun, pos, r, u, b));
    assert(b[0] < -0.02f); /* aim turns toward the supporting hand */
    near(pos[0], 0); /* the gun stays on its trigger hand */
    assert(gevrGripAxes(gun, pos, r, u, b));
    near(b[0], 0); /* the supporting controller does not inherit gun aim */
    near(pos[0], 0.06f * GEVR_UNITS_PER_METRE);

    Mtxf m;
    assert(gevrStereoGunMatrix(gun, &m));
    if (gun == GUNRIGHT) for (int i = 0; i < 3; i++) m.m[0][i] = -m.m[0][i];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) m.m[i][j] *= 0.1f;
    assert(gevrStereoTwoHandMatrix(&m));
    float opos[3], snap[3], dist;
    assert(gevrTwoHandBarrel(opos, snap, &dist, 1));
    for (int i = 0; i < 3; i++)
    {
        float palm = m.m[3][i];
        for (int j = 0; j < 3; j++) palm += s_gevrTwoHandPalm[j] * m.m[j][i];
        near(palm, snap[i]);
    }
    assert(gevrStereoTwoHandClass() == 1);
    vrRecoilKick(gun, 0, 1);
    near(sRecoilProfile[1-gun].kickPitch, 0.2f);
    vrRecoilKick(1-gun, 0, 1);
    near(sRecoilProfile[gun].kickPitch, 2.0f); /* reduction belongs only to the held gun */

    /* A trim moves out toward the support hand's side in either handedness mode. */
    VrGripTrim[1][0] = 2;
    assert(gevrStereoTwoHandMatrix(&m));
    assert(gevrGripAxes(1-gun, pos, r, u, b));
    for (int i = 0; i < 3; i++)
    {
        float palm = m.m[3][i];
        for (int j = 0; j < 3; j++) palm += s_gevrTwoHandPalm[j] * m.m[j][i];
        float side = (leftHanded != (gun == GUNLEFT)) ? r[i] : -r[i];
        near(palm, snap[i] + 2 * side * GEVR_UNITS_PER_METRE / 100);
    }
    VrGripTrim[1][0] = 0;

    /* Occluded support tracking keeps the hold; releasing its grip still ends it. */
    tracked[gevrPhysHand(gun)] = 0;
    for (int i = 0; i < 12; i++) tick();
    assert(gevrStereoTwoHandGrip());
    grip[gevrPhysHand(gun)] = 0; tick(); assert(!gevrStereoTwoHandGrip());
    tracked[gevrPhysHand(gun)] = 1;
    grip[gevrPhysHand(gun)] = 1; tick(); assert(gevrStereoTwoHandGrip());

    pose[gevrPhysHand(gun)][0] = 1;
    for (int i = 0; i < 8; i++) tick();
    assert(!gevrStereoTwoHandGrip());
    pose[gevrPhysHand(gun)][0] = 0.06f;
    tick(); assert(gevrStereoTwoHandGrip());
    player.hands[1-gun].weaponnum = ITEM_WPPK;
    tick(); assert(!gevrStereoTwoHandGrip()); /* two equipped guns */
}
static void testClub(void)
{
    setup(GUNLEFT, 0, ITEM_ROCKETLAUNCH);
    player.hand_item[GUNRIGHT] = ITEM_FIST;
    player.hand_invisible[GUNRIGHT] = 1;
    gevrUnarmedModelUpdate();
    assert(player.cur_item_weapon_getname == ITEM_FIST);
    assert(player.field_2A44[GUNRIGHT] == ITEM_FIST && player.hand_invisible[GUNRIGHT] == -3);
    /* Replacing the club uses the same slot; the gun in the other hand stays loaded. */
    assert(player.field_2A44[GUNLEFT] == -1);
    player.field_2A44[GUNRIGHT] = -1;
    player.hand_invisible[GUNRIGHT] = 1;
    gevrUnarmedModelUpdate(); assert(player.hand_invisible[GUNRIGHT] == 1);
    player.hands[GUNLEFT].weapon_animation_trigger = 1;
    player.hands[GUNLEFT].weapon_next_weapon = ITEM_UNARMED;
    gevrUnarmedModelUpdate(); assert(player.cur_item_weapon_getname == ITEM_FIST);
    player.hands[GUNLEFT].weaponnum = ITEM_UNARMED;
    gevrUnarmedModelUpdate(); assert(player.cur_item_weapon_getname == ITEM_SNIPERRIFLE);
    player.field_2A44[GUNRIGHT] = -1;
    player.hands[GUNLEFT].weapon_next_weapon = ITEM_AK47;
    gevrUnarmedModelUpdate(); assert(player.cur_item_weapon_getname == ITEM_FIST);
    haveSniper = 0;
    player.hands[GUNLEFT].weapon_next_weapon = ITEM_UNARMED;
    gevrUnarmedModelUpdate(); assert(player.cur_item_weapon_getname == ITEM_FIST);
    g_gevrStereo = 0; haveSniper = 1;
    assert(gevrUnarmedModelItem() == ITEM_SNIPERRIFLE);
    gevrUnarmedModelUpdate(); assert(player.cur_item_weapon_getname == ITEM_FIST);
    g_gevrStereo = 1;
}
int main(void)
{
    for (int gun = 0; gun < 2; gun++) for (int lh = 0; lh < 2; lh++)
    {
        testSlot(gun, lh, ITEM_ROCKETLAUNCH);
        testSlot(gun, lh, ITEM_AK47);
        /* Use the production GE-X palm getter, with gun and support separated
         * far enough that reading the weapon controller rejects acquisition. */
        setup(gun,lh,ITEM_AK47);
        pose[gevrPhysHand(gun)][2] = -0.35f;
        gexFore[0] = 0.06f*GEVR_UNITS_PER_METRE;
        gexFore[1] = 0;
        gexFore[2] = -0.35f*GEVR_UNITS_PER_METRE;
        VrGexGuns = gexForeValid = 1;
        hold(gun);
        float opos[3],snap[3],distance;
        assert(gevrTwoHandBarrel(opos,snap,&distance,0)); near(distance,0);
        assert(gevrStereoTwoHandSupportCtrl()==gun);
        release(); VrGexGuns = gexForeValid = 0;
        /* A supporting hand has no sight quad, including the scope path;
         * the weapon hand still gets the normal sight. */
        setup(gun, lh, ITEM_AK47); hold(gun);
        for (int i=0;i<20;i++) tick();
        Gfx commands[2]; aimQueries=0;
        assert(sightGuard(commands,1-gun)==commands && aimQueries==0);
        assert(sightGuard(commands,gun)==commands+1 && aimQueries==1);
        grip[gevrPhysHand(gun)]=0; tick();
        assert(sightGuard(commands,1-gun)==commands+1 && aimQueries==2);
    }
    testClub();
    setup(GUNLEFT, 1, ITEM_WPPK);
    pose[gevrPhysHand(GUNLEFT)][0] = 0.04f;
    pose[gevrPhysHand(GUNLEFT)][2] = 0;
    hold(GUNLEFT);
    for (int i = 0; i < 20; i++) tick();
    float p[3], r[3], u[3], b[3];
    assert(gevrGripAxes(0, p, r, u, b)); near(b[0], 0); /* pistol keeps wrist aim */
    assert(gevrStereoTwoHandClass() == 0);
    online = 1; playerSlot = 1; localSlot = 0;
    player.hands[GUNRIGHT].weaponnum = ITEM_AK47;
    assert(!gevrStereoTwoHandUpdate()); /* a remote player's tick cannot change the local grip */
    assert(gevrStereoTwoHandGrip() && gevrStereoTwoHandGun() == GUNLEFT);
    online = 0;
    setup(GUNRIGHT, 0, ITEM_AK47);
    reloadClaims = 1; grip[0] = 1; tick(); assert(!gevrStereoTwoHandGrip());
    reloadClaims = 0; tick(); assert(gevrStereoTwoHandGrip());
    player.bonddead = 1; tick(); assert(!gevrStereoTwoHandGrip());
    player.bonddead = 0; tick(); assert(gevrStereoTwoHandGrip());
    player.watch_animation_state = 1; tick(); assert(!gevrStereoTwoHandGrip());
    puts("PASS: KF7/rocket support in either slot, both handedness modes, aim/recoil, palm placement, grip lifecycle and sniper-club replacement");
}
