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
static int VrGexGuns = 1;   /* hand reload is GoldenEye X's (bondview2.c gevrHandReloadEnabled) */
static float D_800364CC = 1;
static int s_gevrMagGrab, s_gevrGexGripSpent, s_gevrGexMag[2], s_gevrGexSeatArmed;
static int s_gevrGexHeldRounds = -1;
static int s_gevrGexHeldAmmoType;
static int s_gevrReloadSeatTicks;
static float s_gevrReloadSeatAt[3];
static float poses[2][3];
static int gex[2], buzzes, fills, tracked[2] = {1, 1};
#define reserve player.ammoheldarr[1]
static int online, playerSlot, localSlot;
static int s_gevrGripGesture[2], gripHeld[2];
static int s_gevrGripPendAge[2], VrGestureHolster, VrGestureGripUse, VrGesturePickup, VrGestureMineGrab;
typedef struct { int MagSize, AmmoType; } WeaponStats;
static WeaponStats stats = {30, 1};
static WeaponStats *get_ptr_item_statistics(int item) { (void)item; return &stats; }
#define WEAPONSTATBITFLAG_AMMO_CLIP_LIMIT 1
static int bondwalkItemCheckBitflags(int item, int flags) { (void)item; (void)flags; return 0; }
static int rockets;
static void currentPlayerCreateRocket(int hand) { (void)hand; rockets++; }
float vr_ctrl_quat_play[2][4] = {{1, 0, 0, 0}, {1, 0, 0, 0}};
float vr_ctrl_velocity_play[2][3], vr_head_velocity_play[3];
static int g_ClockTimer = 1, chopHits, whiffs;
static int s_gevrMuzzleValid[2], s_gevrMuzzleItem[2];
static float s_gevrMuzzle[2][3];
static void *g_musicSfxBufferPtr;
#define PUNCHING_AIR_SFX 0
struct sfx3 { unsigned short half[3]; };   /* a knife swing's slash sounds (gun.c) */
struct sfx3 knife_throw_sounds = { { 101, 102, 103 } };
static unsigned int randomGetNext(void) { return 1; }
static int gevrStereoWatchGrip(void) { return 0; }
static int lastSound, lastChopItem, chopResult;
static float lastNeed = -1;
static void sndPlaySfx(void *buffer, int sound, void *state)
{ (void)buffer; (void)state; lastSound = sound; whiffs++; }
s32 gevrChopHit(const f32 from[3], const f32 to[3], f32 touch, const f32 dir[3], s32 item,
                const f32 velocity[3], f32 need, s32 land, f32 *into)
{
    (void)from; (void)to; (void)touch; (void)dir;
    (void)velocity; (void)need; (void)land; (void)into;
    lastChopItem = item; lastNeed = need;
    chopHits++;
    return chopResult;
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
static int supportCtrl;
static int gevrStereoTwoHandSupportCtrl(void) { return supportCtrl; }
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
/* body slots (src/game/gevr_bodyslots.c): off unless a case turns them on;
 * the belt's wait is the production arithmetic (port/src/gevr_bodyslot.c) */
#include "gevr_bodyslot.h"
static GevrBodyFrame s_bodyFrame;
static int s_bodyFrameValid;
static float s_bodyView[3][3];
static int bodySlots, bodyBusy[2], bodyQuiet[2];
static GevrBodyDefer bodyBelt[2];
int gevrBodySlotsOn(void) { return bodySlots; }
void gevrBodySlotGripLetGo(int ctrl) { (void)ctrl; }
int gevrBodySlotBeltWait(int ctrl, int entered, int inside, int gripped)
{
    if (!bodySlots) { memset(&bodyBelt[ctrl], 0, sizeof(bodyBelt[ctrl])); return -1; }
    return gevrBodyBeltDefer(&bodyBelt[ctrl], entered, inside, gripped, g_ClockTimer * 1000.0f / 60.0f, 100.0f);
}
int gevrBodySlotBusy(int ctrl) { return bodySlots && bodyBusy[ctrl]; }
int gevrBodySlotQuiet(int ctrl) { return bodySlots && (bodyBusy[ctrl] || bodyQuiet[ctrl]); }
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
    s_bodyFrameValid = 0;
    gevrReloadInventoryReset();
    VrManualReloading = 0;
    gevrHandReloadTick();
    VrManualReloading = 1;
    memset(&player, 0, sizeof(player));
    player.hands[GUNRIGHT].weapon = ITEM_WPPK;
    player.hands[GUNLEFT].weapon = ITEM_UNARMED;
    gex[0] = gex[1] = 0;
    pointsValid = supportHeld = supportCtrl = falls = 0;
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
    bodySlots = bodyBusy[0] = bodyBusy[1] = bodyQuiet[0] = bodyQuiet[1] = 0;
    memset(bodyBelt, 0, sizeof(bodyBelt));
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

/* Body slots on: a touch at the belt still reloads, a moment later; a squeeze
 * there (holstering at the hip) drops it until the hand has left; a hand in a
 * slot makes no chest cross and, near one, no blow. */
static void bodySlotArbitration(void)
{
    reset();
    bodySlots = 1;
    belt(1, 0); gevrHandReloadTick();
    assert(buzzes == 0 && player.hands[GUNRIGHT].weapon_current_animation == 0);
    for (int i = 0; i < 4; i++) gevrHandReloadTick();       /* 83 ms */
    assert(buzzes == 0);
    gevrHandReloadTick(); gevrHandReloadTick();             /* 100 ms: it reloads */
    assert(player.hands[GUNRIGHT].weapon_current_animation == 9 && buzzes == 2);
    for (int i = 0; i < 20; i++) gevrHandReloadTick();
    assert(buzzes == 2);                                    /* once */
    reset();
    bodySlots = 1;
    belt(1, 0); gevrHandReloadTick();
    s_gevrGripGesture[1] = 1; gevrHandReloadTick();        /* a squeeze, still deciding */
    s_gevrGripGesture[1] = 2; gevrHandReloadTick();        /* a slot took it */
    s_gevrGripGesture[1] = 0;
    for (int i = 0; i < 20; i++) gevrHandReloadTick();
    assert(buzzes == 0 && player.hands[GUNRIGHT].weapon_current_animation == 0);
    belt(1, 30); gevrHandReloadTick();                      /* out, and back: a touch again */
    belt(1, 0);
    for (int i = 0; i < 7; i++) gevrHandReloadTick();
    assert(player.hands[GUNRIGHT].weapon_current_animation == 9 && buzzes == 2);
    reset();
    bodySlots = 1;
    belt(1, 0); gevrHandReloadTick();
    belt(1, 30); gevrHandReloadTick();                      /* in and straight out: no reload */
    for (int i = 0; i < 10; i++) gevrHandReloadTick();
    assert(buzzes == 0);
    /* a pistol's chest cross: none while the hand is in a slot */
    reset();
    bodySlots = 1; bodyBusy[1] = 1;
    at(1, 30, 15, 20); gevrHandReloadTick();
    at(1, 30, -15, 20); gevrHandReloadTick();
    assert(buzzes == 0);
    bodyBusy[1] = 0; gevrHandReloadTick();                 /* out of the slot, still across: the cross */
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
    /* A dominant bare hand supporting an off-hand gun cannot punch while held. */
    reset(); player.hands[GUNRIGHT].weapon = ITEM_FIST;
    player.hands[GUNLEFT].weapon = ITEM_AK47;
    supportHeld = supportCtrl = 1;
    at(1, 30, VrReloadBelt[1], 80);
    vr_ctrl_velocity_play[1][2] = 2.5f;
    chopHits = whiffs = 0; gevrHandChopTick(1);
    assert(chopHits == 0 && whiffs == 0 && !gevrHandChopSwinging(1));
    reset(); VrManualReloading = 0;
    at(1, 30, VrReloadBelt[1], VrReloadBelt[2]);
    vr_ctrl_velocity_play[1][2] = 2.5f;
    chopHits = 0; gevrHandChopTick(1); assert(chopHits == 1);
    VrManualReloading = 1;
    /* Both knives swing as the knife: its damage and slash sound, no punch
     * whiff; a throw's grip wind-up swings no blow at all (user). */
    for (int k = 0; k < 2; k++) {
        reset(); player.hands[GUNRIGHT].weapon = k ? ITEM_THROWKNIFE : ITEM_KNIFE;
        at(1, 30, VrReloadBelt[1], 80);
        vr_ctrl_velocity_play[1][0] = 1.8f;   /* a knife's slash speed, under a gun's */
        chopHits = whiffs = 0; lastChopItem = -1; lastSound = 0;
        gevrHandChopTick(1);
        assert(chopHits == 1 && lastChopItem == ITEM_KNIFE && lastSound >= 101 && lastSound <= 103 && whiffs == 1);
        vr_ctrl_velocity_play[1][0] = 0;
        for (int i = 0; i < 25; i++) gevrHandChopTick(1);
        assert(whiffs == 1);
        reset(); player.hands[GUNRIGHT].weapon = k ? ITEM_THROWKNIFE : ITEM_KNIFE;
        at(1, 30, VrReloadBelt[1], 80);
        s_gevrThrowWindup[GUNRIGHT] = 1;
        vr_ctrl_velocity_play[1][0] = 2.5f;
        chopHits = whiffs = 0; gevrHandChopTick(1);
        assert(chopHits == 0 && whiffs == 0);
        s_gevrThrowWindup[GUNRIGHT] = 0; vr_ctrl_velocity_play[1][0] = 0;
    }
    /* The taser's trigger reaches as far as the arm: a touch test along the
     * held taser with its damage and no speed asked; none touched, no hit. */
    reset(); player.hands[GUNRIGHT].weapon = ITEM_TASER;
    at(1, 30, VrReloadBelt[1], 80);
    chopHits = 0; lastChopItem = -1; lastNeed = -1; chopResult = 0;
    assert(!gevrTaserTouch(GUNRIGHT) && chopHits == 1 && lastChopItem == ITEM_TASER && lastNeed == 0.0f);
    chopResult = 1; assert(!gevrTaserTouch(GUNRIGHT));   /* in touch but not struck */
    chopResult = 2; assert(gevrTaserTouch(GUNRIGHT));
    chopResult = 0;
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
static void nextGunReloads(void)
{
    assert(gevrGexPullAmount(1,3,8,5)==1 && gevrGexPullAmount(1,3,-2,5)==-1);
    assert(gevrGexPullAmount(0,3,-2,5)==1 && gevrGexPullAmount(0,3,8,5)==-1);
    const int items[]={ITEM_TT33,ITEM_SKORPION,ITEM_UZI,ITEM_MP5K,ITEM_MP5KSIL,ITEM_SPECTRE,ITEM_M16,ITEM_FNP90,ITEM_SNIPERRIFLE};
    for (unsigned i=0;i<sizeof(items)/sizeof(items[0]);i++) {
        reset(); gex[GUNRIGHT]=1; player.hands[GUNRIGHT].weapon=items[i];
        player.hands[GUNRIGHT].weapon_ammo_in_magazine=7;
        pistolTick(); assert(gevrReloadFitAvailable());
        float cm=GEVR_UNITS_PER_METRE*D_800364CC/100.0f, magazine[3];
        assert(gevrGexPistolPoint(0,magazine)); memcpy(poses[0],magazine,sizeof(magazine));
        gripHeld[0]=1; pistolTick(); assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_GRIPPED);
        poses[0][1]+=(gevrGexWeaponGet(items[i])->pullUp ? 1 : -1)*(gevrGexReloadDistance(GEVR_RT_PULL)+0.1f)*cm;
        pistolTick(); assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND);
        assert(s_gevrGexHeldRounds==7 && reserve==50);
        pointsValid=3; memset(wellPoint,0,sizeof(wellPoint)); memset(heldPoint,0,sizeof(heldPoint));
        heldPoint[1]=3*gevrGexReloadDistance(GEVR_RT_SEAT)*cm; pistolTick();
        heldPoint[1]=0; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN);
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==7 && reserve==50 && readyEvents==1);
    }
}
static void singleRoundReloads(void)
{
    const int items[]={ITEM_SHOTGUN,ITEM_AUTOSHOT,ITEM_ROCKETLAUNCH,ITEM_GOLDENGUN};
    for (int i=0;i<4;i++) {
        reset(); stats.MagSize=i>=2 ? 1 : 5; gex[GUNRIGHT]=1;
        player.hands[GUNRIGHT].weapon=items[i]; player.hands[GUNRIGHT].weapon_ammo_in_magazine=i>=2 ? 0 : 2;
        int loaded=player.hands[GUNRIGHT].weapon_ammo_in_magazine;
        pistolTick(); assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN);
        gevrGexDropMagazine(GUNRIGHT); assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==loaded && reserve==50);
        belt(0,0); gripHeld[0]=1; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds==1 && reserve==49);
        int rocketsBefore=rockets;
        pointsValid=3; memset(wellPoint,0,sizeof(wellPoint)); memset(heldPoint,0,sizeof(heldPoint)); pistolTick();
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==loaded+1 && reserve==49 && readyEvents==1);
        /* The seated rocket is GE's own prop; physical insertion must create it. */
        assert(rockets-rocketsBefore==(items[i]==ITEM_ROCKETLAUNCH));
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN);
        pistolTick(); assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==loaded+1 && reserve==49);
        gripHeld[0]=0; pistolTick();
        if (i>=2) { gripHeld[0]=1; pistolTick(); assert(s_gevrGexMag[GUNRIGHT]!=GEVR_GEXMAG_INHAND && reserve==49); continue; }
        // Drop a reserved shell; loaded rounds must survive and reserve is refunded once.
        pointsValid=0; gripHeld[0]=1; pistolTick(); assert(reserve==48);
        gripHeld[0]=0; pistolTick(); assert(reserve==49 && player.hands[GUNRIGHT].weapon_ammo_in_magazine==loaded+1);
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN);
        pistolTick(); assert(reserve==49);
        // Changing ammo type while a shell is held refunds the captured ammo type.
        gripHeld[0]=1; pistolTick(); assert(reserve==48);
        player.hands[GUNRIGHT].weapon=ITEM_RUGER; stats.AmmoType=0; pistolTick();
        assert(reserve==49 && player.ammoheldarr[0]==0);
        stats.AmmoType=1;
    }
    /* Zero reserve, invalid pose, full capacity while held, tracking loss and
     * stage cancellation must conserve the one reserved payload for every rig. */
    for (int i=0;i<4;i++) {
        reset(); stats.MagSize=i>=2 ? 1 : 5; gex[GUNRIGHT]=1;
        player.hands[GUNRIGHT].weapon=items[i]; reserve=0;
        pistolTick(); belt(0,0); gripHeld[0]=1; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN && reserve==0);
        gripHeld[0]=0; pistolTick(); reserve=1; gripHeld[0]=1; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND && reserve==0);
        assert(!gevrReloadNeedsAmmo(GUNRIGHT) && gevrGexPistolSupportAllowed());
        pointsValid=0; pistolTick(); assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==0 && reserve==0);
        player.hands[GUNRIGHT].weapon_ammo_in_magazine=stats.MagSize;
        pointsValid=3; memset(wellPoint,0,sizeof(wellPoint)); memset(heldPoint,0,sizeof(heldPoint)); pistolTick();
        assert(reserve==1 && player.hands[GUNRIGHT].weapon_ammo_in_magazine==stats.MagSize && s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN);
        gripHeld[0]=0; pistolTick(); player.hands[GUNRIGHT].weapon_ammo_in_magazine=0;
        pointsValid=0; gripHeld[0]=1; pistolTick(); assert(reserve==0);
        tracked[0]=0; pistolTick(); assert(reserve==1 && s_gevrGexHeldRounds==-1);
        tracked[0]=1; gripHeld[0]=0; pistolTick(); gripHeld[0]=1; pistolTick(); assert(reserve==0);
        gevrGexReloadReset(GUNRIGHT); assert(reserve==1 && s_gevrGexHeldRounds==-1);
        gevrGexReloadReset(GUNRIGHT); assert(reserve==1);
    }
    stats.MagSize=30;
}
/* Cougar speedloader: one pickup carries as many rounds as fit and the
 * reserve has; insertion adds them all; abandoning it refunds them all. */
static void speedloaderReloads(void)
{
    const float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100.0f; (void)cm;
    #define LOADER_INSERT() do { pointsValid=3; memset(wellPoint,0,sizeof(wellPoint)); memset(heldPoint,0,sizeof(heldPoint)); pistolTick(); } while (0)
    #define LOADER_RELEASE() do { gripHeld[0]=0; pistolTick(); } while (0)
    #define LOADER_TAKE() do { pointsValid=0; belt(0,0); gripHeld[0]=1; pistolTick(); } while (0)
    assert(gevrGexWeaponGet(ITEM_RUGER)->loaderRounds==6 && gevrGexWeaponGet(ITEM_RUGER)->singleRound);
    reset(); stats.MagSize=6; gex[GUNRIGHT]=1; player.hands[GUNRIGHT].weapon=ITEM_RUGER;
    player.hands[GUNRIGHT].weapon_ammo_in_magazine=2; pistolTick();
    LOADER_TAKE();
    assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds==4 && reserve==46);
    assert(gevrGexHeldRoundCount()==4);
    LOADER_INSERT();
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==6 && reserve==46 && readyEvents==1);
    assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN);
    LOADER_RELEASE(); LOADER_TAKE();   /* full: nothing to take */
    assert(s_gevrGexMag[GUNRIGHT]!=GEVR_GEXMAG_INHAND && reserve==46);
    /* a short reserve fills the loader only partly */
    LOADER_RELEASE(); player.hands[GUNRIGHT].weapon_ammo_in_magazine=0; reserve=3;
    LOADER_TAKE(); assert(s_gevrGexHeldRounds==3 && reserve==0);
    LOADER_INSERT(); assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==3 && reserve==0);
    /* let go without inserting: every round goes back */
    LOADER_RELEASE(); player.hands[GUNRIGHT].weapon_ammo_in_magazine=1; reserve=50;
    LOADER_TAKE(); assert(s_gevrGexHeldRounds==5 && reserve==45);
    LOADER_RELEASE(); assert(reserve==50 && player.hands[GUNRIGHT].weapon_ammo_in_magazine==1);
    assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN && falls>0);
    /* firing while the loader is held still seats every round it carries */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine=4;
    LOADER_TAKE(); assert(s_gevrGexHeldRounds==2 && reserve==48);
    player.hands[GUNRIGHT].weapon_ammo_in_magazine=2;
    LOADER_INSERT(); assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==4 && reserve==48);
    /* tracking loss while held refunds once */
    LOADER_RELEASE(); player.hands[GUNRIGHT].weapon_ammo_in_magazine=0;
    LOADER_TAKE(); assert(reserve==42);
    tracked[0]=0; pistolTick(); assert(reserve==48 && s_gevrGexHeldRounds==-1);
    tracked[0]=1; LOADER_RELEASE();
    #undef LOADER_INSERT
    #undef LOADER_RELEASE
    #undef LOADER_TAKE
    stats.MagSize=30;
}
/* Grenade launcher: one round per pickup into a six-round drum. */
static void grenadeRounds(void)
{
    const GexWeaponDef *def = gevrGexWeaponGet(ITEM_GRENADELAUNCH);
    assert(def && def->singleRound && def->loaderRounds == 0 && def->payloadProp > 0);
    reset(); stats.MagSize=6; gex[GUNRIGHT]=1; player.hands[GUNRIGHT].weapon=ITEM_GRENADELAUNCH;
    player.hands[GUNRIGHT].weapon_ammo_in_magazine=4; pistolTick();
    for (int round=5; round<=6; round++) {
        pointsValid=0; belt(0,0); gripHeld[0]=1; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds==1);
        pointsValid=3; memset(wellPoint,0,sizeof(wellPoint)); memset(heldPoint,0,sizeof(heldPoint)); pistolTick();
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==round && reserve==50-(round-4));
        gripHeld[0]=0; pistolTick();
    }
    pointsValid=0; belt(0,0); gripHeld[0]=1; pistolTick();   /* full: nothing taken */
    assert(s_gevrGexMag[GUNRIGHT]!=GEVR_GEXMAG_INHAND && reserve==48);
    gripHeld[0]=0; pistolTick();
    stats.MagSize=30;
}
static void magazinePersistence(void)
{
    reset(); gex[GUNRIGHT] = 1;
    player.hands[GUNRIGHT].weapon = ITEM_AK47;
    gevrHandReloadTick();
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 7;
    assert(gevrReloadStow(GUNRIGHT, ITEM_AK47));
    assert(reserve == 50 && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0);
    assert(gevrReloadStoredRounds(GUNRIGHT, ITEM_AK47) == 7 && gevrReloadReservedRounds(1) == 7);
    assert(gevrReloadStow(GUNRIGHT, ITEM_AK47));   /* lower and swap call it: store once */
    player.hands[GUNRIGHT].weapon = ITEM_WPPK;
    gevrGexReloadReset(GUNRIGHT);
    assert(!gevrReloadDraw(GUNRIGHT));   /* never used: initial loading allowed */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 0;
    assert(gevrReloadStow(GUNRIGHT, ITEM_WPPK));   /* empty magazines stay empty */
    player.hands[GUNRIGHT].weapon = ITEM_AK47;
    gevrGexReloadReset(GUNRIGHT);
    assert(gevrReloadDraw(GUNRIGHT));
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 7 && reserve == 50);
    assert(gevrReloadReservedRounds(1) == 0);
    gevrGexDropMagazine(GUNRIGHT);
    assert(reserve == 57 && s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_OUT);
    assert(gevrReloadStow(GUNRIGHT, ITEM_AK47));
    player.hands[GUNRIGHT].weapon = ITEM_WPPK;
    gevrGexReloadReset(GUNRIGHT); assert(gevrReloadDraw(GUNRIGHT));
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0 && reserve == 57);
    assert(gevrReloadStow(GUNRIGHT, ITEM_WPPK));
    player.hands[GUNRIGHT].weapon = ITEM_AK47;
    gevrGexReloadReset(GUNRIGHT); assert(gevrReloadDraw(GUNRIGHT));
    gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_OUT && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0);
    /* The other hand's same-item copy has its own retained rounds. */
    gex[GUNLEFT] = 1; player.hands[GUNLEFT].weapon = ITEM_AK47;
    player.hands[GUNLEFT].weapon_ammo_in_magazine = 3;
    assert(gevrReloadStow(GUNLEFT, ITEM_AK47));
    assert(gevrReloadDraw(GUNLEFT) && player.hands[GUNLEFT].weapon_ammo_in_magazine == 3);
    player.hands[GUNLEFT].weapon_ammo_in_magazine = 3;
    assert(gevrReloadStow(GUNLEFT, ITEM_AK47));
    VrManualReloading = 0; gevrHandReloadTick();
    assert(reserve == 60);   /* turning the mode off refunds holstered rounds once */
    gevrHandReloadTick(); assert(reserve == 60);
    VrManualReloading = 1;
    online = 1; playerSlot = 1;
    assert(!gevrReloadStow(GUNRIGHT, ITEM_AK47) && !gevrHandReloadActive());
    online = playerSlot = 0;
    gevrReloadInventoryReset();
    assert(!gevrReloadDraw(GUNLEFT));   /* a new stage has no saved magazines */
}

static void reloadIndicators(void)
{
    reset(); gex[GUNRIGHT] = 1; player.hands[GUNRIGHT].weapon = ITEM_AK47;
    gevrHandReloadTick();
    gevrGexDropMagazine(GUNRIGHT);
    for (int ctrl = 0; ctrl < 2; ctrl++) {
        float point[3];
        assert(gevrReloadBeltPoint(ctrl, point));
        assert(gevrBeltDist2(ctrl, point) < 0.0001f);
        memcpy(poses[ctrl], point, sizeof(point));
        assert(gevrReloadBeltAmmo(ctrl) == GUNRIGHT && gevrReloadBeltHover(ctrl));
        poses[ctrl][1] += 30 * GEVR_UNITS_PER_METRE * D_800364CC / 100;
        assert(!gevrReloadBeltHover(ctrl));
    }
    pointsValid = 3; memset(wellPoint, 0, sizeof(wellPoint)); memset(heldPoint, 0, sizeof(heldPoint));
    s_gevrGexMag[GUNRIGHT] = GEVR_GEXMAG_INHAND;
    float well[3];
    assert(gevrReloadSeatHover(well));
    heldPoint[0] = 20 * GEVR_UNITS_PER_METRE * D_800364CC / 100;
    assert(!gevrReloadSeatHover(well));
    pointsValid = 1; assert(!gevrReloadSeatHover(well));   /* tracking loss never highlights an insertion */
    s_gevrGexMag[GUNRIGHT] = GEVR_GEXMAG_OUT;
    float atBelt[3]; assert(gevrReloadBeltPoint(1, atBelt)); memcpy(poses[1], atBelt, sizeof(atBelt));
    assert(gevrReloadGrabBelt(1) && s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN);
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == stats.MagSize);
    assert(!gevrReloadGrabBelt(1));   /* one squeeze loads once */
}

static void requiredGripUse(void)
{
    reset();
    VrGestureHolster = VrGestureGripUse = VrGesturePickup = VrGestureMineGrab = 0;
    gevrGripGestureInput(1, 1, 1); assert(s_gevrGripGesture[1] == 1);
    gevrGripGestureInput(1, 0, 0); assert(s_gevrGripGesture[1] == 0);
    VrManualReloading = 0;
    gevrGripGestureInput(1, 1, 1); assert(s_gevrGripGesture[1] == 3);
    gevrGripGestureInput(1, 0, 0);
    VrManualReloading = 1;
}

static void torsoReloadBelt(void)
{
    Mtxf savedView = viewMatrix;
    float savedFit[3]; memcpy(savedFit, VrReloadBelt, sizeof(savedFit));
    reset();
    memset(&s_bodyFrame, 0, sizeof(s_bodyFrame));
    s_bodyFrame.right[0] = s_bodyFrame.up[1] = 1;
    s_bodyFrame.fwd[2] = -1;
    s_bodyFrame.origin[0] = 2; s_bodyFrame.origin[1] = 3; s_bodyFrame.origin[2] = 4;
    s_bodyFrameValid = 1;
    /* The torso stays fixed as the head yaws, pitches and rolls. */
    for (int turn = 0; turn < 8; turn++) {
        int angle = turn % 4;
        float yaw = angle * 0.31f, pitch = angle * -0.29f, roll = angle * 0.12f;
        float cy=cosf(yaw), sy=sinf(yaw), cp=cosf(pitch), sp=sinf(pitch), cr=cosf(roll), sr=sinf(roll);
        float right[3]={cy,0,sy}, up[3]={sy*sp,cp,-cy*sp}, back[3]={-sy*cp,sp,cy*cp};
        memset(&viewMatrix,0,sizeof(viewMatrix)); viewMatrix.m[3][3]=1;
        for (int i=0;i<3;i++) {
            viewMatrix.m[0][i]=(turn>=4?-1:1)*(cr*right[i]+sr*up[i]);
            viewMatrix.m[1][i]=-sr*right[i]+cr*up[i];
            viewMatrix.m[2][i]=back[i];
            for (int j=0;j<3;j++) s_bodyView[j][i]=viewMatrix.m[j][i];
        }
        for (int ctrl=0;ctrl<2;ctrl++) {
            float point[3], pose[4][3], world[3], local[3];
            assert(gevrReloadBeltPoint(ctrl,point) && gevrBodyReloadPose(ctrl,VrReloadBelt,pose));
            gevrBodySlotCentre(&s_bodyFrame,ctrl?GEVR_BS_HIP_GUN:GEVR_BS_HIP_OFF,VrReloadBelt,VrLeftHandedMode,world);
            struct coord3d p={.x=point[0],.y=point[1],.z=point[2]};
            mtx4RotateVecInPlace(&viewMatrix,&p);
            for(int i=0;i<3;i++) assert(fabsf(p.f[i]/D_800364CC-world[i])<0.001f);
            for(int row=0;row<3;row++) {
                struct coord3d axis={.x=pose[row][0],.y=pose[row][1],.z=pose[row][2]};
                mtx4RotateVecInPlace(&viewMatrix,&axis);
                for(int i=0;i<3;i++) assert(fabsf(axis.f[i]-(row==i))<0.001f);
            }
            assert(gevrBeltDist2(ctrl,point)<0.0001f);   /* displayed model is the grab hotspot */
            assert(gevrReloadBeltLocal(ctrl,point,local));
            for(int i=0;i<3;i++) assert(fabsf(local[i]-VrReloadBelt[i])<0.001f);
            memcpy(poses[ctrl],point,sizeof(point));
            if(ctrl==0) {
                gevrReloadFitSetBelt();
                for(int i=0;i<3;i++) assert(fabsf(VrReloadBelt[i]-savedFit[i])<0.001f);
            }
        }
    }
    s_bodyFrameValid=0; viewMatrix=savedView; memcpy(VrReloadBelt,savedFit,sizeof(savedFit));
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
        bodySlotArbitration();
        meleeArbitration();
        pp7Reload();
        nextGunReloads();
        singleRoundReloads();
        speedloaderReloads();
        grenadeRounds();
        magazinePersistence();
        reloadIndicators();
        requiredGripUse();
        torsoReloadBelt();
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
    /* Hand reload needs GoldenEye X's models (user): with them off the belt does nothing,
     * the setting kept for when they are back. */
    reset(); VrGexGuns = 0;
    {
        const int before = buzzes;
        belt(1, 13.1f); gevrHandReloadTick();
        belt(1, 0); gevrHandReloadTick(); assert(buzzes == before && VrManualReloading);
    }
    VrGexGuns = 1;
    reset(); gex[GUNRIGHT] = 1; player.hands[GUNRIGHT].weapon = ITEM_AK47;
    gevrHandReloadTick();
    s_gevrGexMag[GUNRIGHT] = GEVR_GEXMAG_OUT;
    at(1, oldBelt[0], oldBelt[1], oldBelt[2]); gevrHandReloadTick(); assert(fills == 0);
    belt(1, 0); gevrHandReloadTick(); assert(fills == 1);
    puts("PASS: calibrated reloads, PP7 fresh-grip ownership, physical seating, partial/fresh magazines, ammo conservation, handedness, belt and lifecycle");
    return 0;
}
