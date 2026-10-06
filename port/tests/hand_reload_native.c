#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gevr_gexweapon.h"
#include "gevr_gexgrip.h"
typedef int s32;
typedef float f32;
typedef unsigned u32;
#define TRUE 1
#define FALSE 0
#define LOG_NOTE 0
#define sysLogPrintf(...) ((void)0)
/* DEFINITIONS */
typedef struct { float m[4][4]; } Mtxf;
struct coord3d { union { struct { float x, y, z; }; float f[3]; }; };
struct hand { int weapon, weapon_current_animation, weapon_ammo_in_magazine, numvisibleshells; Mtxf gunmtx_camspace; };
struct Player { int bonddead, watch_animation_state, mpmenuon, cur_item_weapon_getname, ammoheldarr[2]; struct hand hands[2]; } player;
static struct Player *g_CurrentPlayer = &player;
static int VrManualReloading = 1, g_gevrStereo = 1, g_PlayerIsInTank, VrLeftHandedMode;
static float D_800364CC = 1;
static int s_gevrMagGrab, s_gevrGexGripSpent, s_gevrGexMag[2], s_gevrGexSeatArmed;
static int s_gevrGexHeldRounds = -1;
static int s_gevrGexHeldAmmoType;
static float poses[2][3];
static int gex[2], buzzes, fills, tracked[2] = {1, 1};
#define reserve player.ammoheldarr[1]
static int online, playerSlot, localSlot;
static int s_gevrGripGesture[2], gripHeld[2];
typedef struct { int MagSize, AmmoType; } WeaponStats;
static WeaponStats stats = {30, 1};
static WeaponStats *get_ptr_item_statistics(int item) { (void)item; return &stats; }
#define WEAPONSTATBITFLAG_AMMO_CLIP_LIMIT 1
static int bondwalkItemCheckBitflags(int item, int flags) { (void)item; (void)flags; return 0; }
static void currentPlayerCreateRocket(int hand) { (void)hand; }
float vr_ctrl_quat_play[2][4] = {{1, 0, 0, 0}, {1, 0, 0, 0}};
float vr_ctrl_velocity_play[2][3], vr_head_velocity_play[3];
static int g_ClockTimer = 1, chopHits, whiffs;
static int s_gevrMuzzleValid[2], s_gevrMuzzleItem[2];
static float s_gevrMuzzle[2][3];
static void *g_musicSfxBufferPtr;
#define PUNCHING_AIR_SFX 0
static int gevrStereoWatchGrip(void) { return 0; }
static void sndPlaySfx(void *buffer, int sound, void *state)
{ (void)buffer; (void)sound; (void)state; whiffs++; }
s32 gevrChopHit(const f32 from[3], const f32 to[3], f32 touch, const f32 dir[3], s32 item,
                const f32 velocity[3], f32 need, s32 land, f32 *into)
{
    (void)from; (void)to; (void)touch; (void)dir; (void)item;
    (void)velocity; (void)need; (void)land; (void)into;
    chopHits++;
    return 0;
}
static Mtxf viewMatrix;
static Mtxf *currentPlayerGetViewToWorldMtxf(void) { return &viewMatrix; }
static int netIsActive(void) { return online; }
static int get_cur_playernum(void) { return playerSlot; }
static int netGetLocalSlot(void) { return localSlot; }
static int gevrSpectating(void) { return 0; }
static int gevrCoopLocalDowned(void) { return 0; }
static int getCurrentPlayerWeaponId(int h) { return player.hands[h].weapon; }
static int get_ammo_in_hands_weapon(int h) { (void)h; return reserve; }
static int get_ammo_type_for_weapon(int item) { return item != ITEM_UNARMED; }
static int gevrGexHeld(int hand) { return gex[hand]; }
const GexWeaponDef *gevrGexWeaponForHand(int hand)
{ return gex[hand] ? gevrGexWeaponGet(getCurrentPlayerWeaponId(hand)) : NULL; }
static float gevrGunSizeFactor(void) { return 1.0f; }
static void gevrGunOff(int hand, float off[3])
{ memcpy(off,gevrGexGunFit(getCurrentPlayerWeaponId(hand)),3*sizeof(float)); }
static void gevrReloadTuneRead(void) {}
static int gevrGripAxesRaw(int ctrl, float p[3], float r[3], float u[3], float b[3])
{
    memcpy(p, poses[ctrl], sizeof(poses[ctrl]));
    memset(r, 0, 3 * sizeof(float));
    memset(u, 0, 3 * sizeof(float));
    memset(b, 0, 3 * sizeof(float));
    r[0] = u[1] = b[2] = 1;
    return tracked[ctrl];
}
_Bool get_button_state(int ctrl, const char *button) { (void)button; return gripHeld[ctrl]; }
static int pointsValid;
static float wellPoint[3], heldPoint[3];
static int gevrGexMagPoints(float c[3], float w[3], float h[3])
{
    memset(c, 0, 3 * sizeof(float));
    memcpy(w, wellPoint, sizeof(wellPoint));
    memcpy(h, heldPoint, sizeof(heldPoint));
    return pointsValid;
}
static int supportHeld;
static int gevrStereoTwoHandGrip(void) { return supportHeld; }
static int gevrMagNearerThanFore(float d) { (void)d; return 1; }
static void gevrOffHandGripBuzz(void) {}
static void gevrGexBuzz(float a) { (void)a; }
static int readyEvents;
void gevrGexMagazineReady(int hand) { (void)hand; readyEvents++; }
static int falls;
static void gevrGexMagazineFalls(int h, int f) { (void)h; (void)f; falls++; }
static s32 gevrGexPistolPoint(s32 support, f32 out[3]);
static s32 gevrGexPistolSupportAllowed(void);
void testTopUp(enum GUNHAND hand);
/* Count transfers while executing the game's real reserve/magazine arithmetic. */
void sub_GAME_7F0649D8(enum GUNHAND hand)
{
    fills++;
    testTopUp(hand);
}
s32 trigger_haptic_vibration_c(int hand_index, float amplitude, float duration)
{
    (void)hand_index; (void)amplitude; (void)duration;
    buzzes++;
    return 1;
}
int vr_haptics_ready(void) { return 1; }
/* PRODUCTION */

/* Supply a body-space point in cm, independently of view scale and handedness. */
static void at(int ctrl, float drop, float side, float ahead)
{
    float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100;
    poses[ctrl][0] = ((ctrl == 0) != (VrLeftHandedMode != 0)) ? -side * cm : side * cm;
    poses[ctrl][1] = -drop * cm;
    poses[ctrl][2] = -ahead * cm;
}
static void belt(int ctrl, float distance)
{
    at(ctrl, VrReloadBelt[0] + distance, VrReloadBelt[1], VrReloadBelt[2]);
}
static void reset(void)
{
    VrManualReloading = 0;
    gevrHandReloadTick();
    VrManualReloading = 1;
    memset(&player, 0, sizeof(player));
    player.hands[GUNRIGHT].weapon = ITEM_WPPK;
    player.hands[GUNLEFT].weapon = ITEM_UNARMED;
    gex[0] = gex[1] = 0;
    pointsValid = supportHeld = falls = 0;
    readyEvents = 0;
    s_gevrGexHeldRounds = -1;
    s_gevrPistolGripOwner = s_gevrPistolGripWas = 0;
    s_gevrPistolGripItem = -1;
    s_gevrGexMag[0] = s_gevrGexMag[1] = GEVR_GEXMAG_IN;
    buzzes = fills = 0;
    reserve = 50;
    online = playerSlot = localSlot = 0;
    tracked[0] = tracked[1] = 1;
    s_gevrGripGesture[0] = s_gevrGripGesture[1] = 0;
    gripHeld[0] = gripHeld[1] = 0;
    memset(vr_ctrl_velocity_play, 0, sizeof(vr_ctrl_velocity_play));
    memset(vr_head_velocity_play, 0, sizeof(vr_head_velocity_play));
    at(0, 20, 20, 40);
    at(1, 20, 20, 40);
    gevrHandReloadTick();
    /* Finish old whiffs and restore the melee detector's slow-to-fast edge. */
    for (int i = 0; i < 60; i++) {
        gevrHandChopTick(0);
        gevrHandChopTick(1);
    }
    chopHits = whiffs = 0;
}
static void calibratedEntry(void)
{
    reset();
    at(1, 45, 25, 15);   /* inside the broad holster region, 36.8 cm from the default belt */
    gevrHandReloadTick();
    assert(player.hands[GUNRIGHT].weapon_current_animation == 0 && buzzes == 0);
    for (int i = 0; i < 10; i++) {
        at(1, i % 2 ? 40.1f : 39.9f, 20, 0);
        gevrHandReloadTick();
    }
    assert(buzzes == 0);
    belt(1, 0);
    gevrHandReloadTick();
    assert(player.hands[GUNRIGHT].weapon_current_animation == 9 && buzzes == 2);
    player.hands[GUNRIGHT].weapon_current_animation = 0;
    for (int i = 0; i < 20; i++) {
        belt(1, i % 2 ? 18.1f : 17.9f);
        gevrHandReloadTick();
        assert(player.hands[GUNRIGHT].weapon_current_animation == 0 && buzzes == 2);
    }
    belt(1, 22.9f); gevrHandReloadTick();
    belt(1, 0); gevrHandReloadTick();
    assert(buzzes == 2);
    belt(1, 23.1f); gevrHandReloadTick();
    belt(1, 0); gevrHandReloadTick();
    assert(player.hands[GUNRIGHT].weapon_current_animation == 9 && buzzes == 4);
}
static void gexEntry(void)
{
    reset();
    gex[GUNRIGHT] = 1;
    player.hands[GUNRIGHT].weapon = ITEM_AK47;
    gevrHandReloadTick();
    s_gevrGexMag[GUNRIGHT] = GEVR_GEXMAG_OUT;
    at(1, 45, 25, 15); gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_OUT && fills == 0);
    belt(1, 18.1f); gevrHandReloadTick();
    assert(fills == 0);
    belt(1, 17.9f); gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && fills == 1);
    gevrHandReloadTick();
    assert(fills == 1);
}
static void lifecycleAndHands(void)
{
    reset();
    belt(1, 0);
    player.watch_animation_state = 1; gevrHandReloadTick();
    player.watch_animation_state = 0; gevrHandReloadTick();
    assert(buzzes == 0);   /* returning to play at the belt must not reload */
    belt(1, 30); gevrHandReloadTick();
    tracked[1] = 0; gevrHandReloadTick();
    tracked[1] = 1; belt(1, 0); gevrHandReloadTick();
    assert(buzzes == 0);
    reset();
    online = 1; localSlot = 7; playerSlot = 3;
    belt(1, 0); gevrHandReloadTick();
    assert(buzzes == 0);
    playerSlot = 7; belt(1, 30); gevrHandReloadTick();
    belt(1, 0); gevrHandReloadTick();
    assert(buzzes == 2);
    reset();
    player.hands[GUNLEFT].weapon = ITEM_WPPK;
    gevrHandReloadTick();   /* arm the newly equipped left gun */
    belt(0, 0); gevrHandReloadTick();
    assert(player.hands[GUNLEFT].weapon_current_animation == 9);
    assert(player.hands[GUNRIGHT].weapon_current_animation == 0);
    belt(1, 0); gevrHandReloadTick();
    assert(player.hands[GUNRIGHT].weapon_current_animation == 9 && buzzes == 4);
    reset(); reserve = 0;
    belt(1, 0); gevrHandReloadTick();
    assert(buzzes == 0 && player.hands[GUNRIGHT].weapon_current_animation == 0);
    reset(); player.hands[GUNRIGHT].weapon = ITEM_AK47;
    belt(1, 0); gevrHandReloadTick();
    assert(buzzes == 2 && player.hands[GUNRIGHT].weapon_current_animation == 9);
    reset(); at(1, 30, -15, 20); gevrHandReloadTick();
    assert(buzzes == 2 && player.hands[GUNRIGHT].weapon_current_animation == 9);
}

static void everyGunAndHolster(void)
{
    for (int item = ITEM_WPPK; item <= ITEM_ROCKETLAUNCH; item++) {
        if (item == ITEM_WATCHLASER || item == ITEM_LASER) continue;
        for (int useGex = 0; useGex < 2; useGex++)
        for (int hand = GUNRIGHT; hand <= GUNLEFT; hand++) {
            reset();
            player.hands[hand].weapon = item;
            gex[hand] = useGex;
            gevrHandReloadTick();
            belt(hand == GUNRIGHT ? 1 : 0, 0); gevrHandReloadTick();
            if (gevrGexByHand(hand)) {
                assert(fills == 1 && player.hands[hand].weapon_ammo_in_magazine == stats.MagSize);
                assert(reserve == 20);
            } else {
                assert(player.hands[hand].weapon_current_animation == 9 && buzzes == 2);
            }
        }
    }
    reset(); player.hands[GUNRIGHT].weapon_ammo_in_magazine = stats.MagSize;
    belt(1, 0); gevrHandReloadTick(); assert(buzzes == 0);
    reset(); gex[GUNRIGHT] = 1; player.hands[GUNRIGHT].weapon = ITEM_AK47;
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 7;
    belt(1, 0); gevrHandReloadTick();
    assert(fills == 1 && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 30 && reserve == 27);
    reset(); gex[GUNRIGHT] = 1; player.hands[GUNRIGHT].weapon = ITEM_AK47;
    gevrHandReloadTick();
    s_gevrGexMag[GUNRIGHT] = GEVR_GEXMAG_INHAND;
    gripHeld[0] = 1;
    belt(1, 0); gevrHandReloadTick(); assert(fills == 0);
    for (int useGex = 0; useGex < 2; useGex++) {
        reset(); gex[GUNRIGHT] = useGex; player.hands[GUNRIGHT].weapon = ITEM_AK47;
        /* Grip gesture tick runs first and consumes a successful hip holster. */
        s_gevrGripGesture[1] = 2;
        player.hands[GUNRIGHT].weapon_current_animation = 5;
        belt(1, 0); gevrHandReloadTick();
        assert(fills == 0 && buzzes == 0 && player.hands[GUNRIGHT].weapon_current_animation == 5);
        s_gevrGripGesture[1] = 0; gevrHandReloadTick(); assert(fills == 0 && buzzes == 0);
        player.hands[GUNRIGHT].weapon_current_animation = 0;
        belt(1, 30); gevrHandReloadTick(); belt(1, 0); gevrHandReloadTick();
        assert(useGex ? fills == 1 : buzzes == 2);
    }
}

static void meleeArbitration(void)
{
    reset();
    at(1, 30, VrReloadBelt[1], VrReloadBelt[2]);
    /* In the grip's gesture frame +Z points down. */
    vr_ctrl_velocity_play[1][2] = 2.5f;
    chopHits = whiffs = 0;
    gevrHandChopTick(1);
    assert(chopHits == 0 && !gevrHandChopSwinging(1));
    /* Reloading must not turn its remaining downward follow-through into melee. */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = stats.MagSize;
    at(1, 90, VrReloadBelt[1], VrReloadBelt[2]);
    gevrHandChopTick(1); assert(chopHits == 0);
    vr_ctrl_velocity_play[1][2] = 0;
    gevrHandChopTick(1);
    vr_ctrl_velocity_play[1][0] = 2.5f;
    gevrHandChopTick(1); assert(chopHits == 1 && gevrHandChopSwinging(1));
    vr_ctrl_velocity_play[1][0] = 0;
    for (int i = 0; i < 20; i++) gevrHandChopTick(1);
    assert(whiffs == 1);   /* a deliberate lateral swing still starts and completes */
    reset();
    at(1, 30, VrReloadBelt[1], 80);
    vr_ctrl_velocity_play[1][2] = 2.5f;
    chopHits = 0; gevrHandChopTick(1); assert(chopHits == 1);   /* downward strike away from belt */
    reset();
    at(1, 30, VrReloadBelt[1], VrReloadBelt[2]);
    vr_ctrl_velocity_play[1][1] = -2.5f;
    chopHits = 0; gevrHandChopTick(1); assert(chopHits == 1);   /* forward thrust */
    reset(); player.hands[GUNRIGHT].weapon = ITEM_FIST;
    at(1, 30, VrReloadBelt[1], VrReloadBelt[2]);
    vr_ctrl_velocity_play[1][2] = 2.5f;
    chopHits = 0; gevrHandChopTick(1); assert(chopHits == 1);   /* bare-hand punch */
    reset(); VrManualReloading = 0;
    at(1, 30, VrReloadBelt[1], VrReloadBelt[2]);
    vr_ctrl_velocity_play[1][2] = 2.5f;
    chopHits = 0; gevrHandChopTick(1); assert(chopHits == 1);
    VrManualReloading = 1;
}

static void pistolTick(void)
{
    gevrGexPistolSupportAllowed(); /* same ordering as the real support update */
    gevrHandReloadTick();
}

static void pp7Reload(void)
{
    assert(gevrGexPistolGripPick(2,2,3) == GEVR_GEXGRIP_SUPPORT);
    assert(gevrGexPistolGripPick(0,4,1.99f) == GEVR_GEXGRIP_SUPPORT);
    assert(gevrGexPistolGripPick(0,4,2) == GEVR_GEXGRIP_MAG);
    assert(gevrGexPistolGripPick(4.01f,6,3) == GEVR_GEXGRIP_SUPPORT);
    assert(gevrGexGrabFit(ITEM_WPPK) == gevrGexGrabFit(ITEM_WPPKSIL));
    assert(gevrGexGrabFit(ITEM_WPPK) != gevrGexGrabFit(ITEM_AK47));
    for (int variant=0; variant<2; variant++) {
        reset();
        stats.MagSize=7;
        gex[GUNRIGHT]=1;
        player.hands[GUNRIGHT].weapon=variant ? ITEM_WPPKSIL : ITEM_WPPK;
        assert(gevrReloadFitAvailable());
        gex[GUNRIGHT]=0; assert(!gevrReloadFitAvailable()); gex[GUNRIGHT]=1;
        VrManualReloading=0; assert(!gevrReloadFitAvailable()); VrManualReloading=1;
        player.hands[GUNRIGHT].weapon_ammo_in_magazine=3;
        pistolTick();
        float cm=GEVR_UNITS_PER_METRE*D_800364CC/100.0f;
        float magazine[3], support[3];
        assert(gevrGexPistolPoint(0,magazine) && gevrGexPistolPoint(1,support));

        /* Cupping owns support, even after moving down onto the magazine. */
        memcpy(poses[0],support,sizeof(support));
        gripHeld[0]=1;
        assert(gevrGexPistolSupportAllowed());
        supportHeld=1; pistolTick();
        memcpy(poses[0],magazine,sizeof(magazine));
        supportHeld=0; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN && !gevrGexClaimsOffHand());
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==3 && reserve==50);

        /* A new underside grip selects reload; moving both hands isn't a pull. */
        gripHeld[0]=0; pistolTick();
        gripHeld[0]=1; pistolTick();
        assert(s_gevrPistolGripOwner==GEVR_GEXGRIP_MAG);
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_GRIPPED);
        poses[0][1]-=8*cm; poses[1][1]-=8*cm; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_GRIPPED);
        poses[0][1]-=5.1f*cm; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND);
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==0 && s_gevrGexHeldRounds==3 && reserve==50);
        online=1; localSlot=0; playerSlot=1; gevrHandReloadTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds==3 && reserve==50);
        online=0; playerSlot=0;

        /* Real production seating: leave by 2r, reject outside r, seat once. */
        pointsValid=3;
        memset(wellPoint,0,sizeof(wellPoint));
        memset(heldPoint,0,sizeof(heldPoint));
        heldPoint[1]=6.1f*cm; pistolTick();
        heldPoint[1]=3.1f*cm; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND);
        heldPoint[1]=2.9f*cm; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN && s_gevrGexGripSpent);
        assert(readyEvents==1);
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==3 && reserve==50);
        pistolTick(); assert(gevrGexClaimsOffHand());

        gevrGexDropMagazine(GUNRIGHT);
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_OUT && reserve==53 && falls==1);
        gripHeld[0]=0; pistolTick();
        belt(0,0); gripHeld[0]=1; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds==-1);
        memset(heldPoint,0,sizeof(heldPoint)); pistolTick();
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==7 && reserve==46);
        gripHeld[0]=0; pistolTick();

        /* Switching between two supported models must return the held rounds. */
        assert(gevrGexPistolPoint(0,magazine));
        memcpy(poses[0],magazine,sizeof(magazine)); gripHeld[0]=1; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_GRIPPED);
        poses[0][1]-=5.1f*cm; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND);
        player.hands[GUNRIGHT].weapon=ITEM_RUGER; stats.AmmoType=0; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN && s_gevrGexHeldRounds==-1 && reserve==53);
        assert(player.ammoheldarr[0]==0);
        stats.MagSize=30; stats.AmmoType=1;
    }
}
int main(void)
{
    const float scales[] = {0.2f, 1.0f};
    for (int lefty = 0; lefty < 2; lefty++)
    for (int scale = 0; scale < 2; scale++)
    for (int yaw = 0; yaw < 2; yaw++) {
        VrLeftHandedMode = lefty;
        D_800364CC = scales[scale];
        memset(&viewMatrix, 0, sizeof(viewMatrix));
        float a = yaw ? 0.7f : 0;
        viewMatrix.m[0][0] = viewMatrix.m[2][2] = cosf(a);
        viewMatrix.m[0][2] = sinf(a);
        viewMatrix.m[2][0] = -sinf(a);
        viewMatrix.m[1][1] = viewMatrix.m[3][3] = 1;
        calibratedEntry();
        gexEntry();
        lifecycleAndHands();
        everyGunAndHolster();
        meleeArbitration();
        pp7Reload();
    }
    /* A custom belt and radius must control both reload paths. */
    reset();
    float oldBelt[3]; memcpy(oldBelt, VrReloadBelt, sizeof(oldBelt));
    VrReloadBelt[0] = 55; VrReloadBelt[1] = 30; VrReloadBelt[2] = 10;
    s_gevrReloadTune[GEVR_RT_BELTRADIUS] = 8;
    belt(1, 8.1f); gevrHandReloadTick(); assert(buzzes == 0);
    belt(1, 7.9f); gevrHandReloadTick(); assert(buzzes == 2);
    player.hands[GUNRIGHT].weapon_current_animation = 0;
    belt(1, 12.9f); gevrHandReloadTick();
    belt(1, 0); gevrHandReloadTick(); assert(buzzes == 2);
    belt(1, 13.1f); gevrHandReloadTick();
    belt(1, 0); gevrHandReloadTick(); assert(buzzes == 4);
    reset(); gex[GUNRIGHT] = 1; player.hands[GUNRIGHT].weapon = ITEM_AK47;
    gevrHandReloadTick();
    s_gevrGexMag[GUNRIGHT] = GEVR_GEXMAG_OUT;
    at(1, oldBelt[0], oldBelt[1], oldBelt[2]); gevrHandReloadTick(); assert(fills == 0);
    belt(1, 0); gevrHandReloadTick(); assert(fills == 1);
    puts("PASS: calibrated reloads, PP7 fresh-grip ownership, physical seating, partial/fresh magazines, ammo conservation, handedness, belt and lifecycle");
    return 0;
}
