#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gevr_gexweapon.h"
#include "gevr_gexgrip.h"
#include "gevr_hand_reload.h"
#include "gevr_rack.h"
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
static const float *s_gevrGunOffOverride;
static int s_gevrMagGrab, s_gevrGexGripSpent, s_gevrGexMag[2], s_gevrGexSeatArmed;
static int s_gevrGexHeldRounds = -1;
static int s_gevrGexHeldGun = GUNRIGHT;
static float VrGexHeldMag[3];
static int s_gevrGexHeldAmmoType;
static int s_gevrReloadSeatTicks, s_gevrGexSeatedTicks[2];
static GevrRack s_gevrRack[2];   /* bondview2.c: the rack's state per gun hand */
int gevrReloadFitting;   /* input.c: Gun fit is setting the reload's places */
static float s_gevrReloadSeatAt[3];
static float poses[2][3];
static int gex[2], buzzes, fills, tracked[2] = {1, 1};
#define reserve player.ammoheldarr[1]
static int online, playerSlot, localSlot;
static int s_gevrGripGesture[2], gripHeld[2];
static int s_gevrModGripHeld[2], s_gevrModGripDown[2], s_gevrModGripUp[2];   /* bondview2.c's: a mod's grips */
static int s_gevrGripPendAge[2], VrGestureGripUse, VrGestureMineGrab, VrForcePull;
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
#define GUN_RIFLECOCK_SFX 7
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
/* mods' weapons area (gevr_modapi.c): a magazine out is a reload_start, one seated a reload_end */
static int modReloadStarts, modReloadEnds;
void gevrModApiReloadStart(int hand, int item) { (void)hand; (void)item; modReloadStarts++; }
void gevrModApiReloadEnd(int hand, int item) { (void)hand; (void)item; modReloadEnds++; }
static int get_ammo_in_hands_weapon(int h) { (void)h; return reserve; }
static int get_ammo_type_for_weapon(int item) { return item != ITEM_UNARMED; }
static int gevrGexHeld(int hand) { return gex[hand]; }
int gevrStereoClubBat(int hand) { (void)hand; return 0; }   /* bondview2.c: no bat here */
int gevrGexBatEnd(int hand, float out[3]) { (void)hand; (void)out; return 0; }   /* gun.c */
/* bondview2.c gevrFitLook(): Bean's models stay off here (GoldenEye's or GE-X's) */
int gevrFitLook(int hand) { return gevrGexHeld(hand) ? 1 : 0; }
/* Bean's models (gevr_bean.c, gun.c gevrBeanMag*): on or off, whose hand holds
 * a model with a magazine cut from it, and that magazine's foot as drawn */
static int beanOn, beanMag[2], beanPoints;
static float beanFoot[3], beanFit[3][3];
static int gevrBeanIsEnabled(void) { return beanOn; }
/* GoldenEye's own guns' magazines (gevr_n64mag.h): the ROM's art is GoldenEye's, not a hack's */
static int ownArt;
static int gevrRomRetailArt(void) { return ownArt; }
static int gevrBeanPp7(int item)
{ return item == ITEM_WPPK || item == ITEM_WPPKSIL || item == ITEM_SILVERWPPK || item == ITEM_GOLDWPPK; }
static int beanKind;   /* gun.c gevrBeanMagKind: 1 a loose round, 2 gone once fired, 4 never pulled */
static int gevrBeanMagKind(int hand);
static int gevrBeanMagHeld(int hand) { return !VrGexGuns && (beanOn || ownArt) && beanMag[hand]; }
static int beanLiftsOff;
s32 gevrBeanMagPullsUp(s32 hand) { return gevrBeanMagHeld(hand) && beanLiftsOff; }
static int gevrBeanMagKind(int hand) { return gevrBeanMagHeld(hand) ? beanKind : 0; }
float *gevrBeanMagFit(s32 item, s32 which) { (void)item; return beanFit[which]; }
s32 gevrWeaponCategory(s32 item);   /* bondview2.c's own, below: the pistols are its first wedge */
s32 gevrBeanHeldPalm(f32 out[3]);   /* bondview2.c's own, below: the off hand's palm */
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
static int gevrGripAxes(int ctrl, float p[3], float r[3], float u[3], float b[3])
{ return gevrGripAxesRaw(ctrl,p,r,u,b); }
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
/* gun.c: the shells a GoldenEye X shotgun carries, once it has been drawn */
static int gexSaddle;
static float gexSaddleAt[3];
static int gevrGexSaddlePoint(float at[3])
{
    memcpy(at, gexSaddleAt, sizeof(gexSaddleAt));
    return gexSaddle;
}
static int gevrBeanMagPoints(float c[3], float w[3], float h[3])
{
    memcpy(c, beanFoot, sizeof(beanFoot));
    memcpy(w, wellPoint, sizeof(wellPoint));
    memcpy(h, heldPoint, sizeof(heldPoint));
    return beanPoints;
}
static int supportHeld;
static int supportCtrl;
static int gevrStereoTwoHandSupportCtrl(void) { return supportCtrl; }
static int gevrStereoTwoHandGrip(void) { return supportHeld; }
static int gevrMagNearerThanFore(float d) { (void)d; return 1; }
static int foreEnd;   /* bondview2.c gevrRackAtForeEnd: the off hand on the fore-end a long gun is held by */
static int gevrRackAtForeEnd(void) { return foreEnd; }
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
void gevrPullGripLetGo(int ctrl) { (void)ctrl; }   /* gevr_forcepull.c */
void gevrPullGripInput(int ctrl, int held) { (void)ctrl; (void)held; }   /* gevr_forcepull.c: the catch's grip */
int gevrPullClaimsPress(int ctrl) { (void)ctrl; return 0; }   /* gevr_forcepull.c: no pull in flight */
void gevrHordeHoldInput(int channel, int pressed, int held, const float *hand, unsigned ticks)
{ (void)channel; (void)pressed; (void)held; (void)hand; (void)ticks; }
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
    beanOn = beanMag[0] = beanMag[1] = beanPoints = beanLiftsOff = beanKind = ownArt = 0;
    memset(beanFit, 0, sizeof(beanFit));
    pointsValid = supportHeld = supportCtrl = falls = foreEnd = 0;
    readyEvents = 0;
    s_gevrGexHeldRounds = -1;
    s_gevrGexHeldGun = GUNRIGHT;
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

/*
 * Bean's PP7 (gevr_beanmag.h; user, 2026-11-18): GoldenEye X off, Bean's models
 * on, the magazine cut from the release's mesh. The same states as GE-X's, with
 * the magazine taken at its foot where the gun is drawn; a Bean gun without a
 * magazine keeps the game's own reload, and GE-X's hand never takes a Bean one.
 */
static void beanPp7Reload(void)
{
    const float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100.0f;
    const int magSize = stats.MagSize;
    int before;
    float palm[3];

    reset();
    VrGexGuns = 0;
    assert(!gevrHandReloadActive() && !gevrManualReloadOn(GUNRIGHT));   /* a ROM hack's own guns: the game's reload */
    /* GoldenEye's own guns reload by hand as Bean's do (owner, 2026-11-21: "also original mode
     * guns"): the same states, by the magazine gevr_n64mag.c lays out of the cartridge's model */
    ownArt = 1; beanMag[GUNRIGHT] = 1;
    stats.MagSize = 7;
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 3;
    at(1, 20, 20, 40);
    at(0, 20, -20, 40);
    gevrHandReloadTick();
    assert(gevrHandReloadActive() && gevrManualReloadOn(GUNRIGHT) && gevrReloadButtonDrops(GUNRIGHT));
    gevrGexDropMagazine(GUNRIGHT);
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_OUT && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0);
    assert(gevrBeanMagState(GUNRIGHT, NULL) == GEVR_GEXMAG_OUT && reserve == 53);
    /* a gun whose model has nothing to take (the Cougar) is still reloaded by a gesture, never by itself */
    beanMag[GUNRIGHT] = 0;
    assert(gevrHandReloadActive() && gevrManualReloadOn(GUNRIGHT) && gevrBeanMagState(GUNRIGHT, NULL) == GEVR_GEXMAG_IN);
    reset();
    VrGexGuns = 0;
    beanOn = 1; beanMag[GUNRIGHT] = 1;
    stats.MagSize = 7;
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 3;
    at(1, 20, 20, 40);
    at(0, 20, -20, 40);
    gevrHandReloadTick();
    assert(gevrHandReloadActive() && gevrManualReloadOn(GUNRIGHT) && !gevrManualReloadOn(GUNLEFT));
    assert(gevrReloadButtonDrops(GUNRIGHT) && !gevrReloadButtonDrops(GUNLEFT));
    assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);   /* loaded: nothing to do */

    /* A pistol's magazine is inside its grip (user, 2026-11-18): nothing to take
     * hold of. A fresh grip right at its foot, and a pull, leave it where it is. */
    memcpy(beanFoot, poses[1], sizeof(beanFoot));
    beanFoot[1] -= 8 * cm;
    beanPoints = 1;
    memcpy(poses[0], beanFoot, sizeof(beanFoot));
    assert(!gevrGexClaimsOffHand());
    gripHeld[0] = 1; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && !gevrGexClaimsOffHand());
    poses[0][1] -= 5.1f * cm; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && gevrBeanMagState(GUNRIGHT, NULL) == GEVR_GEXMAG_IN);
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 3 && reserve == 50 && falls == 0);
    gripHeld[0] = 0; gevrHandReloadTick();

    /* B drops it out, its rounds back to the reserve; a fresh one is taken at the belt */
    gevrGexDropMagazine(GUNRIGHT);
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_OUT && reserve == 53 && falls == 1);
    assert(gevrBeanMagState(GUNRIGHT, NULL) == GEVR_GEXMAG_OUT && gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_OUT);
    assert(gevrGexMagState(GUNRIGHT, NULL) == GEVR_GEXMAG_IN);   /* GE-X's hand has no part in a Bean gun's */
    assert(gevrReloadBeltAmmo(0) == GUNRIGHT);
    belt(0, 0); gripHeld[0] = 1; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds == -1);
    assert(gevrBeanMagState(GUNRIGHT, NULL) == GEVR_GEXMAG_INHAND);
    assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_INSERT);

    /* Gun fit's held magazine moves the palm from the off hand's grip */
    beanFit[0][0] = 2; beanFit[0][1] = -1; beanFit[0][2] = 3;
    assert(gevrBeanHeldPalm(palm));
    assert(fabsf(palm[0] - poses[0][0] - 2 * cm) < 1e-3f && fabsf(palm[1] - poses[0][1] + cm) < 1e-3f
        && fabsf(palm[2] - poses[0][2] - 3 * cm) < 1e-3f);

    /* seating: its top within the pistol's 4 cm of the well's mouth, and it fills */
    beanPoints = 3;
    memset(wellPoint, 0, sizeof(wellPoint));
    memset(heldPoint, 0, sizeof(heldPoint));
    heldPoint[1] = 4.1f * cm; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND);
    heldPoint[1] = 3.9f * cm; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && s_gevrGexGripSpent);
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 7 && reserve == 46);
    assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_SEATED);
    gripHeld[0] = 0; gevrHandReloadTick();

    /* holstered with its magazine out, it comes back so; its rounds are its own */
    gevrGexDropMagazine(GUNRIGHT);
    assert(reserve == 53 && gevrReloadStow(GUNRIGHT, ITEM_WPPK));
    /* A gun whose model has no magazine to take (one not cut, the sniper rifle): still
     * never reloads itself and B does not reload it (user: "it should force gestures"),
     * as a GE-X gun without one; the belt's touch runs the game's own reload. It has
     * no magazine's label, pull or fit. */
    player.hands[GUNRIGHT].weapon = ITEM_AK47; beanMag[GUNRIGHT] = 0;
    gevrHandReloadTick();
    assert(gevrHandReloadActive() && gevrManualReloadOn(GUNRIGHT) && gevrReloadButtonDrops(GUNRIGHT));
    assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);
    gevrGexDropMagazine(GUNRIGHT);
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && falls == 2);
    before = buzzes;
    belt(1, 13.1f); gevrHandReloadTick();
    belt(1, 0); gevrHandReloadTick();
    assert(buzzes == before + 2 && fills == 1);
    at(1, 20, 20, 40);
    assert(!gevrReloadClaimsOffHand() && !gevrReloadFitAvailable());
    /* ... and so does the chest cross: a rifle here, which under GE-X would be pulled
     * instead (the sniper rifle, user). The off hand's grip at the belt does nothing
     * for it: that is a magazine's gesture, and GE-X has no such reload either. */
    player.hands[GUNRIGHT].weapon_current_animation = 0;
    gevrHandReloadTick();
    before = buzzes;
    belt(0, 0); gripHeld[0] = 1; gevrHandReloadTick();
    assert(buzzes == before && player.hands[GUNRIGHT].weapon_current_animation == 0);
    gripHeld[0] = 0; at(0, 20, -20, 40); gevrHandReloadTick();
    player.hands[GUNRIGHT].weapon = ITEM_SNIPERRIFLE;
    at(1, 20, 20, 40); gevrHandReloadTick();
    before = buzzes;
    at(1, 30, -12, 20); gevrHandReloadTick();    /* the gun hand across the chest */
    assert(buzzes == before + 2 && player.hands[GUNRIGHT].weapon_current_animation == 9);
    player.hands[GUNRIGHT].weapon_current_animation = 0;
    player.hands[GUNRIGHT].weapon = ITEM_AK47;
    at(1, 20, 20, 40); gevrHandReloadTick();
    player.hands[GUNRIGHT].weapon = ITEM_WPPK; beanMag[GUNRIGHT] = 1;
    assert(gevrReloadDraw(GUNRIGHT));
    gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_OUT && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0);

    /* The off hand is where its palm is, not the controller's grip (user: "it thinks
     * like my hand is the end of the wrist"): with the palm fitted 18 cm ahead of
     * the grip, the belt answers the palm there, not the grip. */
    reset();
    VrGexGuns = 0; beanOn = 1; beanMag[GUNRIGHT] = 1;
    stats.MagSize = 7;
    at(1, 20, 20, 40);
    gevrHandReloadTick();
    gevrGexDropMagazine(GUNRIGHT);
    beanFit[0][2] = -18;
    belt(0, 0); gripHeld[0] = 1; gevrHandReloadTick();           /* the grip at the belt, the palm 18 cm off */
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_OUT && !gevrReloadBeltHover(0));
    gripHeld[0] = 0; gevrHandReloadTick();
    belt(0, 0); poses[0][2] += 18 * cm;                          /* the palm at the belt */
    assert(gevrReloadBeltHover(0) && gevrGexClaimsOffHand());
    gripHeld[0] = 1; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND);
    gripHeld[0] = 0; gevrHandReloadTick();
    stats.MagSize = magSize;

    /* A rifle's magazine hangs out of the gun: Bean's is pulled as GE-X's, taken
     * at its middle where the gun is drawn, within the rifles' 10 cm and out by
     * their 8; the RC-P90's lifts off upward. Put back, it has its rounds. */
    for (int lifts = 0; lifts < 2; lifts++) {
        reset();
        VrGexGuns = 0; beanOn = 1; beanMag[GUNRIGHT] = 1; beanLiftsOff = lifts;
        stats.MagSize = 30;
        player.hands[GUNRIGHT].weapon = lifts ? ITEM_FNP90 : ITEM_M16;
        player.hands[GUNRIGHT].weapon_ammo_in_magazine = 12;
        at(1, 20, 20, 40);
        at(0, 20, -20, 40);
        gevrHandReloadTick();
        assert(gevrManualReloadOn(GUNRIGHT) && gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);
        memcpy(beanFoot, poses[1], sizeof(beanFoot));
        beanFoot[2] -= 15 * cm;
        beanPoints = 1;
        memcpy(poses[0], beanFoot, sizeof(beanFoot));
        poses[0][0] += 10.1f * cm;
        assert(!gevrGexClaimsOffHand());
        poses[0][0] -= 0.2f * cm;
        assert(gevrGexClaimsOffHand());
        gripHeld[0] = 1; gevrHandReloadTick();
        assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_GRIPPED && gevrBeanMagState(GUNRIGHT, NULL) == GEVR_GEXMAG_GRIPPED);
        assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_GRIPPED);
        poses[0][1] += (lifts ? -1 : 1) * 8.1f * cm; gevrHandReloadTick();   /* the wrong way: still in */
        assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_GRIPPED);
        poses[0][1] += (lifts ? 1 : -1) * 16.2f * cm; gevrHandReloadTick();
        assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds == 12 && reserve == 50);
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0 && gevrReloadCarriesRounds(GUNRIGHT));
        beanPoints = 3;
        memset(wellPoint, 0, sizeof(wellPoint));
        memset(heldPoint, 0, sizeof(heldPoint));
        heldPoint[1] = 12.1f * cm; gevrHandReloadTick();
        heldPoint[1] = 6.1f * cm; gevrHandReloadTick();
        assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND);
        heldPoint[1] = 5.9f * cm; gevrHandReloadTick();
        assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && s_gevrGexGripSpent);
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 12 && reserve == 50 && falls == 0);
        gripHeld[0] = 0; gevrHandReloadTick();
    }

    /* A Bean shotgun's shell (gevr_beanmag.h single): a round at a time, as GE-X's.
     * The gun is never open; a grip at the belt takes one shell out of the reserve,
     * its middle at the port puts it in, and B drops nothing. */
    reset();
    VrGexGuns = 0; beanOn = 1; beanMag[GUNRIGHT] = 1; beanKind = 1 | 4;
    stats.MagSize = 5;
    player.hands[GUNRIGHT].weapon = ITEM_SHOTGUN;
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 3;
    at(1, 20, 20, 40);
    at(0, 20, -20, 40);
    gevrHandReloadTick();
    assert(gevrManualReloadOn(GUNRIGHT) && gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);
    gevrGexDropMagazine(GUNRIGHT);
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && falls == 0 && reserve == 50);
    {
        int rounds = 0, hover = 0;
        assert(gevrReloadBeltAmmo(0) < 0);                       /* no magazine at the belt: a round */
        belt(0, 0);
        assert(gevrReloadBeltRound(0, &rounds, &hover) == GUNRIGHT && rounds == 1 && hover);
        assert(gevrGexClaimsOffHand());
    }
    gripHeld[0] = 1; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds == 1 && reserve == 49);
    assert(gevrBeanMagState(GUNRIGHT, NULL) == GEVR_GEXMAG_INHAND);
    beanPoints = 3;
    memset(wellPoint, 0, sizeof(wellPoint));
    memset(heldPoint, 0, sizeof(heldPoint));
    heldPoint[1] = 6.1f * cm; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND);
    heldPoint[1] = 5.9f * cm; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 4 && reserve == 49);
    gripHeld[0] = 0; gevrHandReloadTick();
    /*
     * Drawn, the shotgun's shells are the ones it carries (user, 2026-11-21:
     * "move the ... shotgun shells up on top of the shotgun, where there already
     * are some"): the belt has none for it, so the hip's holster is never in the
     * way nor hidden for it; a fresh grip at the saddle takes one. (Above, before
     * the gun had been drawn, the belt stood in.)
     */
    {
        int rounds = 0, hover = 0;
        float ring[3];

        memcpy(beanFoot, poses[1], sizeof(beanFoot));
        beanFoot[1] += 10 * cm;                                   /* the saddle, on top of the receiver */
        belt(0, 0);
        assert(gevrReloadBeltRound(0, &rounds, &hover) < 0 && !gevrReloadBeltOffers(0) && !gevrReloadBeltAtHand(0));
        assert(!gevrReloadBeltWanted(0) && !gevrGexClaimsOffHand());
        gripHeld[0] = 1; gevrHandReloadTick();
        assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && reserve == 49);   /* a grip at the belt: nothing */
        gripHeld[0] = 0; gevrHandReloadTick();
        /* the ring on the saddle's shell: with the hand coming for it, green in reach */
        assert(!gevrReloadSaddleShown(GUNRIGHT, ring, &hover));
        memcpy(poses[0], beanFoot, sizeof(beanFoot)); poses[0][0] += (GEVR_RELOAD_SADDLE_SHOW_CM - 0.5f) * cm;
        assert(gevrReloadSaddleShown(GUNRIGHT, ring, &hover) && !hover && !gevrGexClaimsOffHand());
        assert(fabsf(ring[0] - beanFoot[0]) < 1e-3f && fabsf(ring[1] - beanFoot[1]) < 1e-3f);
        memcpy(poses[0], beanFoot, sizeof(beanFoot)); poses[0][0] += (GEVR_RELOAD_SADDLE_CM + 0.5f) * cm;
        assert(gevrReloadSaddleShown(GUNRIGHT, ring, &hover) && !hover && !gevrGexClaimsOffHand());
        poses[0][0] -= 1.0f * cm;
        assert(gevrReloadSaddleShown(GUNRIGHT, ring, &hover) && hover && gevrGexClaimsOffHand());
    }
    gripHeld[0] = 1; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND && reserve == 48);
    {
        float ring[3];
        int hover;
        assert(!gevrReloadSaddleShown(GUNRIGHT, ring, &hover));   /* one in the hand already */
    }
    /* let go away from the port, the shell goes back to the reserve */
    heldPoint[1] = 30 * cm; gripHeld[0] = 0; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 4 && reserve == 49);
    /* full: nothing to take, at the saddle or the belt */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 5;
    {
        int rounds = 0, hover = 0;
        float ring[3];
        memcpy(poses[0], beanFoot, sizeof(beanFoot));
        assert(!gevrGexClaimsOffHand() && !gevrReloadSaddleShown(GUNRIGHT, ring, &hover));
        belt(0, 0);
        assert(gevrReloadBeltRound(0, &rounds, &hover) < 0 && !gevrGexClaimsOffHand());
    }

    /* A Bean launcher's rocket (consumed, never pulled): fired, the launcher's is
     * out with nothing dropped; a fresh one from the belt, its tail at the muzzle,
     * loads it and makes GoldenEye's rocket. */
    reset();
    VrGexGuns = 0; beanOn = 1; beanMag[GUNRIGHT] = 1; beanKind = 2 | 4;
    stats.MagSize = 1;
    player.hands[GUNRIGHT].weapon = ITEM_ROCKETLAUNCH;
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 1;
    at(1, 20, 20, 40);
    at(0, 20, -20, 40);
    rockets = 0;
    gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);
    memcpy(beanFoot, poses[1], sizeof(beanFoot)); beanPoints = 1;
    memcpy(poses[0], beanFoot, sizeof(beanFoot));
    gripHeld[0] = 1; gevrHandReloadTick();                       /* a grip right on it: not pulled */
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && !gevrGexClaimsOffHand());
    gripHeld[0] = 0; gevrHandReloadTick();
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 0;          /* fired */
    at(0, 20, -20, 40); gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_OUT && falls == 0 && reserve == 50);
    assert(gevrBeanMagState(GUNRIGHT, NULL) == GEVR_GEXMAG_OUT && gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_OUT);
    belt(0, 0); gripHeld[0] = 1; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND);
    beanPoints = 3;
    memset(wellPoint, 0, sizeof(wellPoint));
    memset(heldPoint, 0, sizeof(heldPoint));
    gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 1);
    assert(reserve == 49 && rockets == 1);
    gripHeld[0] = 0; gevrHandReloadTick();
    /* loaded, B leaves it: one and done, there is nothing to top off (user) */
    gevrGexDropMagazine(GUNRIGHT);
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && reserve == 49 && falls == 0);
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 1);
    stats.MagSize = magSize;

    /* its model not in the hand yet (a switch): no magazine state, nothing to grip.
     * The gun still never reloads itself. */
    reset();
    VrGexGuns = 0; beanOn = 1;
    gevrHandReloadTick();
    assert(gevrManualReloadOn(GUNRIGHT) && gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);
    assert(gevrBeanMagState(GUNRIGHT, NULL) == GEVR_GEXMAG_IN);

    stats.MagSize = magSize;
    VrGexGuns = 1;
    reset();
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

        /* A pistol's magazine is inside its grip (user, 2026-11-18): a new grip
         * clearly under it is no magazine's, and a pull takes nothing out. */
        gripHeld[0]=0; pistolTick();
        gripHeld[0]=1; pistolTick();
        assert(s_gevrPistolGripOwner!=GEVR_GEXGRIP_MAG && !gevrGexClaimsOffHand());
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN);
        poses[0][1]-=5.1f*cm; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN && falls==0);
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==3 && reserve==50);
        gripHeld[0]=0; pistolTick();

        /* B drops it; a fresh one from the belt is the off hand's to seat */
        gevrGexDropMagazine(GUNRIGHT);
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_OUT && reserve==53 && falls==1);
        assert(gevrReloadLabel(GUNRIGHT)==GEVR_RELOAD_LABEL_OUT);
        belt(0,0); gripHeld[0]=1; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds==-1);
        assert(gevrReloadLabel(GUNRIGHT)==GEVR_RELOAD_LABEL_INSERT);
        online=1; localSlot=0; playerSlot=1; gevrHandReloadTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds==-1 && reserve==53);
        online=0; playerSlot=0;

        /* Real production seating: reject outside r, seat once. */
        pointsValid=3;
        memset(wellPoint,0,sizeof(wellPoint));
        memset(heldPoint,0,sizeof(heldPoint));
        heldPoint[1]=3.1f*cm; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND);
        heldPoint[1]=2.9f*cm; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN && s_gevrGexGripSpent);
        assert(readyEvents==1 && gevrReloadLabel(GUNRIGHT)==GEVR_RELOAD_LABEL_SEATED);
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==7 && reserve==46);
        pistolTick(); assert(gevrGexClaimsOffHand());
        gripHeld[0]=0; pistolTick();

        /* Switching models with a magazine in the hand lets it go, nothing lost. */
        gevrGexDropMagazine(GUNRIGHT);
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_OUT && reserve==53 && falls==2);
        belt(0,0); gripHeld[0]=1; pistolTick();
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
    /* the guns whose magazine hangs out of them; a pistol's is not pulled (below) */
    const int items[]={ITEM_SKORPION,ITEM_UZI,ITEM_MP5K,ITEM_MP5KSIL,ITEM_SPECTRE,ITEM_M16,ITEM_FNP90,ITEM_SNIPERRIFLE};
    {
        /* the DD44, as the PP7s: a grip at its magazine and a pull take nothing */
        reset(); gex[GUNRIGHT]=1; player.hands[GUNRIGHT].weapon=ITEM_TT33;
        player.hands[GUNRIGHT].weapon_ammo_in_magazine=7;
        pistolTick();
        float cm=GEVR_UNITS_PER_METRE*D_800364CC/100.0f, magazine[3];
        assert(gevrGexPistolPoint(0,magazine)); memcpy(poses[0],magazine,sizeof(magazine));
        gripHeld[0]=1; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN && !gevrGexClaimsOffHand());
        poses[0][1]-=(gevrGexReloadDistance(GEVR_RT_PULL)+0.1f)*cm; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN && player.hands[GUNRIGHT].weapon_ammo_in_magazine==7);
        gripHeld[0]=0; pistolTick();
        gevrGexDropMagazine(GUNRIGHT);
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_OUT && reserve==57);
    }
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
        {   /* the belt offers its round on the off hand's side (gevr_bodyslots.c draws it there) */
            int rounds, hover;
            assert(gevrReloadBeltRound(0,&rounds,&hover)==GUNRIGHT && rounds==1 && !hover);
            assert(gevrReloadBeltRound(1,&rounds,&hover)==-1);
            /* a round at this belt: its hip's holster steps aside; the grip is the belt's only at it */
            assert(gevrReloadBeltOffers(0) && !gevrReloadBeltAtHand(0) && !gevrReloadBeltOffers(1));
            belt(0,0); assert(gevrReloadBeltRound(0,&rounds,&hover)==GUNRIGHT && hover);
            assert(gevrReloadBeltAtHand(0) && !gevrReloadBeltAtHand(1));
            reserve=0; assert(gevrReloadBeltRound(0,&rounds,&hover)==-1);   /* none left: nothing shown */
            assert(!gevrReloadBeltOffers(0) && !gevrReloadBeltAtHand(0)); reserve=50;
            gex[GUNRIGHT]=0; assert(gevrReloadBeltRound(0,&rounds,&hover)==-1); gex[GUNRIGHT]=1;
            at(0,20,20,40);
        }
        gevrGexDropMagazine(GUNRIGHT); assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==loaded && reserve==50);
        belt(0,0); gripHeld[0]=1; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds==1 && reserve==49);
        { int rounds, hover; assert(gevrReloadBeltRound(0,&rounds,&hover)==-1); }   /* one in the hand already */
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
    /*
     * Drawn, a GoldenEye X shotgun's shells are the ones it carries too (owner,
     * 2026-11-21: the shell replacements "the same on GoldenEye X"): nothing at
     * the belt, the holster left alone, a fresh grip at the saddle takes one. The
     * launchers and the Golden Gun carry none and keep the belt.
     */
    for (int i=0;i<4;i++) {
        int rounds, hover;
        float ring[3];
        const float cm=GEVR_UNITS_PER_METRE*D_800364CC/100.0f;
        reset(); stats.MagSize=i>=2 ? 1 : 5; gex[GUNRIGHT]=1;
        player.hands[GUNRIGHT].weapon=items[i]; player.hands[GUNRIGHT].weapon_ammo_in_magazine=i>=2 ? 0 : 2;
        pistolTick();
        memcpy(gexSaddleAt, poses[1], sizeof(gexSaddleAt)); gexSaddleAt[1]+=10*cm; gexSaddle=1;
        if (i>=2) {
            assert(gevrReloadBeltRound(0,&rounds,&hover)==GUNRIGHT && !gevrReloadSaddleShown(GUNRIGHT,ring,&hover));
            gexSaddle=0; continue;
        }
        assert(gevrReloadBeltRound(0,&rounds,&hover)==-1 && !gevrReloadBeltOffers(0) && !gevrReloadBeltWanted(0));
        belt(0,0); gripHeld[0]=1; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_IN && reserve==50);   /* a grip at the belt: nothing */
        gripHeld[0]=0; pistolTick();
        memcpy(poses[0], gexSaddleAt, sizeof(gexSaddleAt)); poses[0][0]+=(GEVR_RELOAD_SADDLE_CM+0.5f)*cm;
        assert(gevrReloadSaddleShown(GUNRIGHT,ring,&hover) && !hover && !gevrGexClaimsOffHand());
        assert(fabsf(ring[1]-gexSaddleAt[1])<1e-3f);
        poses[0][0]-=1.0f*cm;
        assert(gevrReloadSaddleShown(GUNRIGHT,ring,&hover) && hover && gevrGexClaimsOffHand());
        gripHeld[0]=1; pistolTick();
        assert(s_gevrGexMag[GUNRIGHT]==GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds==1 && reserve==49);
        pointsValid=3; memset(wellPoint,0,sizeof(wellPoint)); memset(heldPoint,0,sizeof(heldPoint)); pistolTick();
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine==3 && reserve==49);
        gripHeld[0]=0; pistolTick();
        gex[GUNRIGHT]=0; assert(!gevrReloadSaddleShown(GUNRIGHT,ring,&hover)); gex[GUNRIGHT]=1;
        gexSaddle=0;
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
        { int rounds, hover; assert(gevrReloadBeltRound(0,&rounds,&hover)==-1); }   /* full: nothing offered */
        gripHeld[0]=0; pistolTick(); player.hands[GUNRIGHT].weapon_ammo_in_magazine=0;
        pointsValid=0; gripHeld[0]=1; pistolTick(); assert(reserve==0);
        tracked[0]=0; pistolTick(); assert(reserve==1 && s_gevrGexHeldRounds==-1);
        tracked[0]=1; gripHeld[0]=0; pistolTick(); gripHeld[0]=1; pistolTick(); assert(reserve==0);
        gevrGexReloadReset(GUNRIGHT); assert(reserve==1 && s_gevrGexHeldRounds==-1);
        gevrGexReloadReset(GUNRIGHT); assert(reserve==1);
    }
    stats.MagSize=30;
}
static void reloadRoleChanges(void)
{
    /* Filling the free hand cancels the held magazine without leaking rounds. */
    for (int owner=0;owner<2;owner++) {
        reset();
        player.hands[owner].weapon=ITEM_AK47; gex[owner]=1;
        player.hands[1-owner].weapon=ITEM_UNARMED; pistolTick();
        player.hands[owner].weapon_ammo_in_magazine=7;
        gevrGexMagOut(owner,GEVR_GEXMAG_INHAND,"test pull");
        player.hands[1-owner].weapon=ITEM_WPPK; gex[1-owner]=1;
        pistolTick(); pistolTick();
        assert(s_gevrGexMag[owner]==GEVR_GEXMAG_OUT && s_gevrGexHeldRounds==-1 && reserve==57);
    }
    reset(); player.hands[GUNRIGHT].weapon=ITEM_FIST; player.hands[GUNLEFT].weapon=ITEM_LASER;
    assert(gevrReloadSupportGun()==GUNLEFT);   /* support includes guns without magazines */
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
    {   /* the belt's speedloader carries what a grip would take */
        int rounds, hover;
        assert(gevrReloadBeltRound(0,&rounds,&hover)==GUNRIGHT && rounds==4);
        reserve=3; assert(gevrReloadBeltRound(0,&rounds,&hover)==GUNRIGHT && rounds==3); reserve=50;
    }
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

/* The label over the gun: each stage of the reload, and none once it is done */
static void reloadLabels(void)
{
    float atBelt[3];
    reset(); stats.MagSize = 7; gex[GUNRIGHT] = 1; player.hands[GUNRIGHT].weapon = ITEM_WPPK;
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 2; pistolTick();
    assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);   /* loaded: nothing to do */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 0;
    assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_EMPTY);
    reserve = 0; assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NOAMMO); reserve = 50;
    g_gevrStereo = 0; assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE); g_gevrStereo = 1;
    gex[GUNRIGHT] = 0; assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE); gex[GUNRIGHT] = 1;
    player.watch_animation_state = 1; assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);
    player.watch_animation_state = 0;
    gevrGexDropMagazine(GUNRIGHT);
    assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_OUT);
    reserve = 0; assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NOAMMO); reserve = 50;
    assert(gevrReloadBeltPoint(1, atBelt)); memcpy(poses[1], atBelt, sizeof(atBelt));
    assert(gevrReloadGrabBelt(1) && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 7);
    for (int t = 0; t < 59; t++) {
        assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_SEATED);
        gevrHandReloadTick();
    }
    gevrHandReloadTick(); assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);   /* the "in" fades */
    stats.MagSize = 30;
}

static void requiredGripUse(void)
{
    reset();
    VrGestureGripUse = VrGestureMineGrab = 0;
    gevrGripGestureInput(1, 1, 1); assert(s_gevrGripGesture[1] == 1);
    gevrGripGestureInput(1, 0, 0); assert(s_gevrGripGesture[1] == 0);
    VrManualReloading = 0;
    /* grabbing within reach is always on (test.22): a press is still a gesture to decide */
    gevrGripGestureInput(1, 1, 1); assert(s_gevrGripGesture[1] == 1);
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

/* All physical reload steps must follow the gun slot, not a fixed hand. */
/* Compute from the actual gun matrix and renderer's left-slot mirror;
 * this reference must not use the interaction getter under test. */
static void fittedGrabPoint(int gun, float out[3])
{
    Mtxf base; assert(gevrStereoGunMatrix(gun,&base));
    const GexWeaponDef *def=gevrGexWeaponForHand(gun);
    const float *fit=gevrGexGrabFit(def->item);
    if (gun==GUNLEFT) for (int i=0;i<3;i++) base.m[0][i]=-base.m[0][i];
    for (int row=0;row<3;row++) for (int i=0;i<3;i++) base.m[row][i]*=0.1f;
    for (int i=0;i<3;i++) {
        out[i]=base.m[3][i];
        for (int row=0;row<3;row++) out[i]+=def->grabRoot[row]*base.m[row][i];
        const float k=GEVR_VIEWMODEL_CM*0.1f;
        out[i]+=(-fit[0]*base.m[0][i]+fit[1]*base.m[1][i]-fit[2]*base.m[2][i])/k;
    }
}
static void klobbPullFromEitherSlot(void)
{
    float cm=GEVR_UNITS_PER_METRE*D_800364CC/100;
    const int items[]={ITEM_SKORPION,ITEM_UZI};
    for (int gun=0;gun<2;gun++) for (unsigned item=0;item<sizeof(items)/sizeof(items[0]);item++) {
        reset();
        player.hands[gun].weapon=items[item]; gex[gun]=1;
        player.hands[1-gun].weapon=ITEM_FIST;
        player.hands[gun].weapon_ammo_in_magazine=7;
        pistolTick();
        float want[3],actual[3]; fittedGrabPoint(gun,want);
        assert(gevrGexPistolPoint(0,actual));
        for (int i=0;i<3;i++) {
            if (fabsf(want[i]-actual[i])/cm>=0.001f)
                fprintf(stderr,"grab mismatch: item %d gun %d left-handed %d axis %d, %.2f cm\n",
                    items[item],gun,VrLeftHandedMode,i,(actual[i]-want[i])/cm);
            assert(fabsf(want[i]-actual[i])/cm<0.001f);
        }
        memcpy(poses[gun],want,sizeof(want));
        gripHeld[gun]=1; pistolTick();
        assert(s_gevrPistolGripOwner==GEVR_GEXGRIP_MAG);
        assert(s_gevrGexMag[gun]==GEVR_GEXMAG_GRIPPED);
        poses[gun][1]-=(gevrGexReloadDistance(GEVR_RT_PULL)+0.1f)*cm;
        pistolTick();
        assert(s_gevrGexMag[gun]==GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds==7);
        assert(player.hands[gun].weapon_ammo_in_magazine==0 && reserve==50);
    }
}
static void reloadGrabFitMirroring(void)
{
    const float cm=GEVR_UNITS_PER_METRE*D_800364CC/100;
    reset(); player.hands[GUNRIGHT].weapon=ITEM_SKORPION; gex[GUNRIGHT]=1; pistolTick();
    float *fit=gevrGexGrabFit(ITEM_SKORPION), saved[3],target[3],actual[3];
    memcpy(saved,fit,sizeof(saved)); fittedGrabPoint(GUNRIGHT,target);
    target[0]+=2*cm; target[1]-=cm; target[2]+=3*cm;
    memcpy(poses[0],target,sizeof(target)); gevrReloadFitSetGrab();
    assert(gevrGexPistolPoint(0,actual));
    for (int i=0;i<3;i++) assert(fabsf(actual[i]-target[i])/cm<0.001f);
    /* The same saved calibration also follows the reversed weapon slot. */
    player.hands[GUNLEFT].weapon=ITEM_SKORPION; player.hands[GUNRIGHT].weapon=ITEM_FIST;
    gex[GUNLEFT]=1; gex[GUNRIGHT]=0;
    fittedGrabPoint(GUNLEFT,target); assert(gevrGexPistolPoint(0,actual));
    for (int i=0;i<3;i++) assert(fabsf(actual[i]-target[i])/cm<0.001f);
    /* Gun fit with that left-hand gun (user, 2026-11-22): offered, and the reloading
     * hand - controller 1, the bare right - sets the place, not the gun's hand. */
    assert(gevrReloadFitAvailable());
    target[0]-=cm; target[1]+=2*cm; target[2]-=cm;
    memcpy(poses[1],target,sizeof(target)); gevrReloadFitSetGrab();
    assert(gevrGexPistolPoint(0,actual));
    for (int i=0;i<3;i++) assert(fabsf(actual[i]-target[i])/cm<0.001f);
    memcpy(fit,saved,sizeof(saved));
    /* KF7 uses the calibrated controller-relative point, without a root point. */
    for (int gun=0;gun<2;gun++) {
        reset(); player.hands[gun].weapon=ITEM_AK47; player.hands[1-gun].weapon=ITEM_FIST; gex[gun]=1;
        float r[3],u[3],b[3],at[3]; gevrGripAxesRaw(1-gun,at,r,u,b);
        gevrReloadMagPoint(at,r,u,b,cm,1,actual);
        const float *grab=gevrGexGrabFit(ITEM_AK47);
        float sign=(VrLeftHandedMode != (gun==GUNLEFT)) ? -1 : 1;
        assert(fabsf(actual[0]-at[0]-sign*grab[0]*cm)/cm<0.001f);
        assert(fabsf(actual[1]-at[1]-grab[1]*cm)/cm<0.001f);
        assert(fabsf(actual[2]-at[2]-grab[2]*cm)/cm<0.001f);
    }
}

static void reversedReloads(void)
{
    const int items[] = {ITEM_AK47, ITEM_UZI, ITEM_M16};   /* not the pistols: theirs is not pulled */
    const float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100.0f;
    for (unsigned n=0; n<sizeof(items)/sizeof(items[0]); n++) {
        reset();
        player.hands[GUNRIGHT].weapon = ITEM_FIST;
        player.hands[GUNLEFT].weapon = items[n];
        player.hands[GUNLEFT].weapon_ammo_in_magazine = 7;
        gex[GUNLEFT] = 1;
        pistolTick();
        assert(gevrReloadSupportGun() == GUNLEFT);
        float mag[3], r[3], u[3], b[3], at[3];
        gevrGripAxesRaw(0, at, r, u, b);
        gevrReloadMagPoint(at, r, u, b, cm, 1, mag);
        memcpy(poses[1], mag, sizeof(mag));
        gripHeld[1] = 1; pistolTick();
        assert(gevrGexClaimsOffHand() && gevrReloadHoldsHand(1));
        assert(s_gevrGexMag[GUNLEFT] == GEVR_GEXMAG_GRIPPED);
        const GexWeaponDef *def = gevrGexWeaponGet(items[n]);
        poses[1][1] += (def->pullUp ? 1 : -1)*(gevrGexReloadDistance(GEVR_RT_PULL)+0.1f)*cm;
        pistolTick();
        assert(s_gevrGexMag[GUNLEFT] == GEVR_GEXMAG_INHAND);
        assert(player.hands[GUNLEFT].weapon_ammo_in_magazine == 0 && reserve == 50);
        assert(gevrReloadCarriesRounds(GUNLEFT) && !gevrReloadCarriesRounds(GUNRIGHT));
        assert(gevrReloadReservedRounds(1) == 7);
        assert(gevrGexMagState(GUNLEFT, at) == GEVR_GEXMAG_INHAND);
        assert(!memcmp(at, poses[1], sizeof(at)));
        assert(gevrGexHeldPalm(at) && !memcmp(at, poses[1], sizeof(at)));
        Mtxf magHand;
        assert(gevrStereoReloadHandMatrix(&magHand));
        float *fit=gevrGexGunFit(items[n]);
        float side=VrLeftHandedMode ? -1 : 1;
        assert(fabsf(magHand.m[0][0]-side*GEVR_VIEWMODEL_CM*cm*0.1f)<0.0001f);
        assert(fabsf(magHand.m[3][0]-(poses[1][0]-side*fit[0]*cm))<0.0001f);
        assert(fabsf(magHand.m[3][1]-(poses[1][1]+fit[1]*cm))<0.0001f);
        assert(fabsf(magHand.m[3][2]-(poses[1][2]+(GEVR_GRIP_TO_ORIGIN_CM+fit[2])*cm))<0.0001f);

        pointsValid = 3; memset(wellPoint,0,sizeof(wellPoint)); memset(heldPoint,0,sizeof(heldPoint));
        heldPoint[1] = 3*gevrGexReloadDistance(GEVR_RT_SEAT)*cm; pistolTick();
        heldPoint[1] = 0; assert(gevrReloadSeatHover(at)); pistolTick();
        assert(s_gevrGexMag[GUNLEFT] == GEVR_GEXMAG_IN && s_gevrGexGripSpent);
        assert(player.hands[GUNLEFT].weapon_ammo_in_magazine == 7 && reserve == 50);

        /* Eject, show the other hip, and take a fresh magazine with primary. */
        gevrGexDropMagazine(GUNLEFT);
        assert(s_gevrGexMag[GUNLEFT] == GEVR_GEXMAG_OUT && reserve == 57);
        assert(gevrReloadBeltAmmo(0) == GUNLEFT && gevrReloadBeltAmmo(1) == GUNLEFT);
        gripHeld[1] = 0; pistolTick();
        belt(1,0); assert(gevrReloadBeltHover(1));
        assert(!gevrReloadGrabBelt(1));   /* the free hand physically carries it */
        gripHeld[1] = 1; pistolTick();
        assert(s_gevrGexMag[GUNLEFT] == GEVR_GEXMAG_INHAND && s_gevrGexHeldGun == GUNLEFT);
        pistolTick();
        assert(s_gevrGexMag[GUNLEFT] == GEVR_GEXMAG_IN && readyEvents == 2);
        assert(player.hands[GUNLEFT].weapon_ammo_in_magazine == 30 && reserve == 27);

        /* Dropping a partial magazine refunds it once to its captured ammo. */
        gripHeld[1]=0; pistolTick();
        gevrGripAxesRaw(0, at, r, u, b);
        gevrReloadMagPoint(at,r,u,b,cm,1,mag); memcpy(poses[1],mag,sizeof(mag));
        gripHeld[1]=1; pistolTick();
        poses[1][1]+=(def->pullUp ? 1 : -1)*(gevrGexReloadDistance(GEVR_RT_PULL)+0.1f)*cm; pistolTick();
        assert(s_gevrGexHeldRounds == 30 && reserve == 27);
        gripHeld[1]=0; pistolTick(); pistolTick();
        assert(reserve == 57 && s_gevrGexHeldRounds == -1 && s_gevrGexMag[GUNLEFT] == GEVR_GEXMAG_OUT);
        assert(gevrReloadStow(GUNLEFT,items[n]));
        assert(s_gevrStoredMag[GUNLEFT][items[n]].missing);
        player.hands[GUNLEFT].weapon_ammo_in_magazine=30;
        assert(gevrReloadDraw(GUNLEFT));
        assert(player.hands[GUNLEFT].weapon_ammo_in_magazine==0 && s_gevrGexMag[GUNLEFT]==GEVR_GEXMAG_OUT);
    }
    /* Single rounds and speedloaders use the same free-hand role. */
    const int rounds[] = {ITEM_SHOTGUN, ITEM_ROCKETLAUNCH, ITEM_RUGER};
    for (unsigned n=0;n<sizeof(rounds)/sizeof(rounds[0]);n++) {
        reset(); stats.MagSize = n==1 ? 1 : 5;
        player.hands[GUNRIGHT].weapon=ITEM_UNARMED;
        player.hands[GUNLEFT].weapon=rounds[n]; gex[GUNLEFT]=1; pistolTick();
        belt(1,0); gripHeld[1]=1; pistolTick();
        int amount = n==2 ? 5 : 1;
        assert(s_gevrGexMag[GUNLEFT]==GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds==amount);
        assert(gevrReloadCarriesRounds(GUNLEFT) && reserve==50-amount);
        pointsValid=3; memset(wellPoint,0,sizeof(wellPoint)); memset(heldPoint,0,sizeof(heldPoint)); pistolTick();
        /* the empty pump shotgun's shell waits for the pump (gevr_rack.h); the others are loaded */
        assert(s_gevrGexMag[GUNLEFT]==GEVR_GEXMAG_IN
            && player.hands[GUNLEFT].weapon_ammo_in_magazine + s_gevrRack[GUNLEFT].pending==amount);
        assert(s_gevrRack[GUNLEFT].pending == (n==0 ? amount : 0));
        assert(reserve==50-amount);
    }
    stats.MagSize=30;
}

/*
 * The rack (gevr_rack.h; user, 2026-11-21: "drop the clip, put a clip in, and
 * then rack the slide"): a pistol, a submachine gun or the KF7 that ran dry
 * takes a third step after its magazine. Bean models here; the states are the
 * ones GoldenEye X uses.
 */
static void rackSeat(int item, int magSize, int loaded)
{
    const float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100.0f;

    reset();
    VrGexGuns = 0; beanOn = 1; beanMag[GUNRIGHT] = 1;
    stats.MagSize = magSize;
    player.hands[GUNRIGHT].weapon = item;
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = loaded;
    at(1, 20, 20, 40);
    at(0, 20, -20, 40);
    gevrHandReloadTick();
    assert(!gevrReloadBeltWanted(0) && !gevrReloadBeltWanted(1));   /* its magazine in: the holsters are holsters */
    gevrGexDropMagazine(GUNRIGHT);
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_OUT && reserve == 50 + loaded);
    /* its magazine out: the free hand at a hip is after a magazine wherever it is there
     * (gevr_bodyslots.c: no holster for it); the gun hand only at the belt's own place */
    assert(gevrReloadBeltWanted(0) && !gevrReloadBeltAtHand(0) && !gevrReloadBeltWanted(1));
    belt(1, 0); assert(gevrReloadBeltWanted(1)); at(1, 20, 20, 40);
    belt(0, 0); gripHeld[0] = 1; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND && gevrReloadBeltWanted(0));
    beanPoints = 3;
    memset(wellPoint, 0, sizeof(wellPoint));
    memset(heldPoint, 0, sizeof(heldPoint));
    heldPoint[1] = 20 * cm; gevrHandReloadTick();
    heldPoint[1] = 0; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && s_gevrGexGripSpent);
    at(0, 20, -20, 40);
    assert(!gevrReloadBeltWanted(0));   /* in again (racked or not): the holsters are back */
}

/* the off hand at the rear of the slide or at the bolt handle, pulled back by `back` cm */
static void rackHand(int pistol, float back)
{
    const float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100.0f;

    memcpy(poses[0], poses[1], sizeof(poses[0]));
    poses[0][1] += (pistol ? 7.0f : 6.0f) * cm;
    poses[0][2] += ((pistol ? 1.0f : -6.0f) + back) * cm;
}

static void rackReload(void)
{
    const int magSize = stats.MagSize;

    for (int pistol = 0; pistol < 2; pistol++) {
        const int item = pistol ? ITEM_WPPK : ITEM_UZI, size = pistol ? 7 : 30;
        const int label = pistol ? GEVR_RELOAD_LABEL_RACK : GEVR_RELOAD_LABEL_BOLT;
        const float throwCm = pistol ? GEVR_RACK_SLIDE_CM : GEVR_RACK_BOLT_CM;
        int before;

        /* a magazine changed with rounds still in the gun: one is chambered, no rack */
        rackSeat(item, size, 3);
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == size && reserve == 53 - size);
        assert(!s_gevrRack[GUNRIGHT].dry && s_gevrRack[GUNRIGHT].pending == 0);
        assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_SEATED);

        /* run dry: the rounds of the new magazine wait, and the gun is still empty */
        rackSeat(item, size, 0);
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0 && s_gevrRack[GUNRIGHT].pending == size);
        assert(reserve == 50 - size && gevrReloadReservedRounds(1) == size && gevrReloadCarriesRounds(GUNRIGHT));
        assert(gevrReloadLabel(GUNRIGHT) == label);
        /* the grip that seated it is not a rack, wherever the hand goes */
        rackHand(pistol, 0); gevrHandReloadTick();
        rackHand(pistol, throwCm + 1); gevrHandReloadTick();
        assert(!s_gevrRack[GUNRIGHT].held && s_gevrRack[GUNRIGHT].pending == size);
        gripHeld[0] = 0; gevrHandReloadTick();

        /* a grip away from the slide takes nothing; at it, the off hand belongs to the rack */
        rackHand(pistol, 0); poses[0][0] += (GEVR_RACK_REACH_CM + 0.5f) * GEVR_UNITS_PER_METRE * D_800364CC / 100.0f;
        assert(!gevrGexClaimsOffHand());
        gripHeld[0] = 1; gevrHandReloadTick();
        assert(!s_gevrRack[GUNRIGHT].held);
        gripHeld[0] = 0; gevrHandReloadTick();
        rackHand(pistol, 0);
        assert(gevrGexClaimsOffHand());
        gripHeld[0] = 1; gevrHandReloadTick();
        assert(s_gevrRack[GUNRIGHT].held && gevrGexClaimsOffHand() && gevrReloadHoldsHand(0));
        assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN);   /* not a grip on the magazine */

        /* let go short of the throw: nothing */
        rackHand(pistol, throwCm - 0.5f); gevrHandReloadTick();
        assert(s_gevrRack[GUNRIGHT].held && !s_gevrRack[GUNRIGHT].cocked);
        gripHeld[0] = 0; gevrHandReloadTick();
        assert(!s_gevrRack[GUNRIGHT].held && s_gevrRack[GUNRIGHT].pending == size);
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0 && gevrReloadLabel(GUNRIGHT) == label);

        /* all the way back, then let go: chambered, the rounds in the gun, nothing lost */
        rackHand(pistol, 0); gripHeld[0] = 1; gevrHandReloadTick();
        before = whiffs;
        rackHand(pistol, throwCm + 0.2f); gevrHandReloadTick();
        assert(s_gevrRack[GUNRIGHT].cocked && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0);
        gripHeld[0] = 0; gevrHandReloadTick();
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == size && s_gevrRack[GUNRIGHT].pending == 0);
        assert(!s_gevrRack[GUNRIGHT].dry && reserve == 50 - size && gevrReloadReservedRounds(1) == 0);
        assert(whiffs == before + 1 && lastSound == GUN_RIFLECOCK_SFX);
        assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_SEATED && !gevrReloadCarriesRounds(GUNRIGHT));

        /* still gripped, run back in: the same, and that grip is not the two-handed hold */
        rackSeat(item, size, 0);
        gripHeld[0] = 0; gevrHandReloadTick();
        rackHand(pistol, 0); gripHeld[0] = 1; gevrHandReloadTick();
        rackHand(pistol, throwCm + 0.2f); gevrHandReloadTick();
        rackHand(pistol, throwCm * 0.4f); gevrHandReloadTick();
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == size && s_gevrGexGripSpent && gevrGexClaimsOffHand());
        gripHeld[0] = 0; gevrHandReloadTick();
        assert(!s_gevrGexGripSpent);

        /* B / Y before the rack: the magazine drops with its rounds, the chamber still empty */
        rackSeat(item, size, 0);
        gripHeld[0] = 0; gevrHandReloadTick();
        gevrGexDropMagazine(GUNRIGHT);
        assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_OUT && reserve == 50 && s_gevrRack[GUNRIGHT].pending == 0);
        assert(s_gevrRack[GUNRIGHT].dry && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0);

        /* the gun hand touching the belt (as when dual wielding) chambers a waiting magazine: no rack */
        rackSeat(item, size, 0);
        gripHeld[0] = 0; gevrHandReloadTick();
        at(0, 20, -20, 40);
        belt(1, 13.1f); gevrHandReloadTick();
        belt(1, 0); gevrHandReloadTick();
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == size && s_gevrRack[GUNRIGHT].pending == 0);
        assert(reserve == 50 - size);
        gevrHandReloadTick();
        assert(!s_gevrRack[GUNRIGHT].dry);

        /* ... with nothing left in the reserve too: the last magazine is not lost, nor the gun put away for it */
        rackSeat(item, size, 0);
        reserve = 0;
        gripHeld[0] = 0; gevrHandReloadTick();
        assert(gevrReloadCarriesRounds(GUNRIGHT) && gevrReloadReservedRounds(1) == size);
        at(0, 20, -20, 40);
        belt(1, 13.1f); gevrHandReloadTick();
        belt(1, 0); gevrHandReloadTick();
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == size && reserve == 0);

        /* put away unracked and drawn again: still waiting, the same rounds */
        rackSeat(item, size, 0);
        gripHeld[0] = 0; gevrHandReloadTick();
        assert(gevrReloadStow(GUNRIGHT, item));
        assert(s_gevrRack[GUNRIGHT].pending == 0 && gevrReloadStoredRounds(GUNRIGHT, item) == size);
        assert(gevrReloadReservedRounds(1) == size && reserve == 50 - size);
        assert(gevrReloadDraw(GUNRIGHT));
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0 && s_gevrRack[GUNRIGHT].pending == size);
        assert(gevrReloadReservedRounds(1) == size);
        gevrHandReloadTick();
        assert(gevrReloadLabel(GUNRIGHT) == label);

        /* hand reload turned off with one waiting: it is simply loaded */
        VrManualReloading = 0; gevrHandReloadTick(); VrManualReloading = 1;
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == size && s_gevrRack[GUNRIGHT].pending == 0);
    }

    /* the AR33, the RC-P90 and the sniper rifle have bolt handles too */
    {
        static const int bolts[] = { ITEM_M16, ITEM_FNP90, ITEM_SNIPERRIFLE, ITEM_SKORPION, ITEM_SPECTRE, ITEM_AK47 };
        for (unsigned n = 0; n < sizeof(bolts) / sizeof(bolts[0]); n++) {
            rackSeat(bolts[n], 30, 0);
            assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0 && s_gevrRack[GUNRIGHT].pending == 30);
            assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_BOLT);
        }
    }
    /* the Cougar's cylinder is a magazine with nothing to rack: dry or not, seated it is ready */
    rackSeat(ITEM_RUGER, 6, 0);
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 6 && s_gevrRack[GUNRIGHT].pending == 0);
    assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_SEATED);
    stats.MagSize = magSize;
    VrGexGuns = 1;
}
/*
 * The shotguns (user, 2026-11-21: "we need to make the regular shotgun require
 * a pump after each shot"; the automatic one cycles itself): the pump shotgun
 * is pumped for every shot, the off hand sliding on the fore-end it holds the
 * gun by; the automatic shotgun is pumped once it has run dry, as a magazine
 * gun is racked.
 */
/* the off hand at the shells the shotgun carries (here 10 cm over the gun hand's grip) */
static void saddleHand(void)
{
    const float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100.0f;

    memcpy(beanFoot, poses[1], sizeof(beanFoot));
    beanFoot[1] += 10 * cm;
    beanPoints |= 1;
    memcpy(poses[0], beanFoot, sizeof(beanFoot));
}

static void pumpShell(void)
{
    const float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100.0f;

    at(0, 20, -20, 40);
    gripHeld[0] = 0; gevrHandReloadTick();
    saddleHand(); gripHeld[0] = 1; gevrHandReloadTick();   /* from the shells the gun carries */
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND && s_gevrGexHeldRounds == 1);
    beanPoints = 3;
    memset(wellPoint, 0, sizeof(wellPoint));
    memset(heldPoint, 0, sizeof(heldPoint));
    heldPoint[1] = 20 * cm; gevrHandReloadTick();
    heldPoint[1] = 0; gevrHandReloadTick();
    assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN);
    gripHeld[0] = 0; gevrHandReloadTick();
}

/* the off hand at the pump (the guess: 2 cm up, 28 ahead of the grip), pulled back by `back` cm */
static void pumpHand(float back)
{
    const float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100.0f;

    memcpy(poses[0], poses[1], sizeof(poses[0]));
    poses[0][1] += 2.0f * cm;
    poses[0][2] += (-28.0f + back) * cm;
}

static void pumpStart(int item, int loaded)
{
    reset();
    VrGexGuns = 0; beanOn = 1; beanMag[GUNRIGHT] = 1; beanKind = 1 | 4;
    stats.MagSize = 5;
    player.hands[GUNRIGHT].weapon = item;
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = loaded;
    at(1, 20, 20, 40);
    at(0, 20, -20, 40);
    gevrHandReloadTick();
}

static void pumpReload(void)
{
    const int magSize = stats.MagSize;
    const float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100.0f;

    /* The automatic shotgun: shots need nothing; dry, the shells put in wait for its pump. */
    pumpStart(ITEM_AUTOSHOT, 3);
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 2; gevrHandReloadTick();   /* a shot */
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 2 && s_gevrRack[GUNRIGHT].pending == 0);
    assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);
    pumpShell();
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 3 && s_gevrRack[GUNRIGHT].pending == 0 && reserve == 49);
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 0; gevrHandReloadTick();   /* fired dry */
    pumpShell(); pumpShell();
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0 && s_gevrRack[GUNRIGHT].pending == 2 && reserve == 47);
    /* a pump on its fore-end, as the other shotgun's (user: not "pull bolt") */
    assert(gevrReloadReservedRounds(1) == 2 && gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_PUMP);
    {
        /* the saddle still gives shells for the room left, the waiting ones counted; the
         * belt has none (the hip's holster is its own) */
        int rounds = 0, hover = 0;
        float ring[3];
        belt(0, 0);
        assert(gevrReloadBeltRound(0, &rounds, &hover) < 0 && !gevrReloadBeltOffers(0) && !gevrGexClaimsOffHand());
        saddleHand();
        assert(gevrGexClaimsOffHand() && gevrReloadSaddleShown(GUNRIGHT, ring, &hover) && hover);
        /* a fresh grip at the shells is for a shell, though the fore-end is in reach and a pump is due */
        foreEnd = 1; gripHeld[0] = 1; gevrHandReloadTick();
        assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_INHAND && !s_gevrRack[GUNRIGHT].held);
        foreEnd = 0; gripHeld[0] = 0; gevrHandReloadTick();     /* let go: back to the reserve */
        assert(s_gevrGexMag[GUNRIGHT] == GEVR_GEXMAG_IN && s_gevrRack[GUNRIGHT].pending == 2 && reserve == 47);
        /* the empty gun's ring shows from anywhere */
        at(0, 20, -20, 40);
        assert(gevrReloadSaddleShown(GUNRIGHT, ring, &hover) && !hover);
    }
    pumpShell(); pumpShell(); pumpShell();
    assert(s_gevrRack[GUNRIGHT].pending == 5 && reserve == 44);
    {
        int hover = 0;
        float ring[3];
        saddleHand();
        assert(!gevrGexClaimsOffHand() && !gevrReloadSaddleShown(GUNRIGHT, ring, &hover));   /* full */
    }
    pumpHand(0);
    assert(!gevrGexClaimsOffHand());   /* the pump is no claim on the off hand */
    gripHeld[0] = 1; gevrHandReloadTick();
    assert(s_gevrRack[GUNRIGHT].held);
    pumpHand(GEVR_RACK_PUMP_CM + 0.5f); gevrHandReloadTick();
    pumpHand(1.0f); gevrHandReloadTick();
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 5 && s_gevrRack[GUNRIGHT].pending == 0 && reserve == 44);
    assert(lastSound == GUN_RIFLECOCK_SFX && !s_gevrGexGripSpent);
    gripHeld[0] = 0; gevrHandReloadTick();
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 4; gevrHandReloadTick();   /* and fires on, unpumped */
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 4 && s_gevrRack[GUNRIGHT].pending == 0);

    /* The pump shotgun, drawn loaded: the first shell is ready. */
    pumpStart(ITEM_SHOTGUN, 5);
    assert(s_gevrRack[GUNRIGHT].pending == 0 && gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);
    /* a shot: the four left wait for the pump, and the trigger finds the gun empty */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 4; gevrHandReloadTick();
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0 && s_gevrRack[GUNRIGHT].pending == 4);
    assert(gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_PUMP && gevrReloadReservedRounds(1) == 4 && reserve == 50);
    assert(gevrReloadCarriesRounds(GUNRIGHT));
    pumpHand(0);
    assert(!gevrGexClaimsOffHand());
    /*
     * Held by the fore-end, away from the pump's fitted place, the grip down
     * since before the shot (the two-handed hold has let go of the gun for the
     * pump: test_two_hand.py): that hand has the pump with no fresh squeeze,
     * from where it is. Back the throw and forward again.
     */
    pumpStart(ITEM_SHOTGUN, 5);
    at(0, 20, -20, 100);   /* 32 cm ahead of the pump's place */
    foreEnd = 1; gripHeld[0] = 1; gevrHandReloadTick();
    assert(!s_gevrRack[GUNRIGHT].held);   /* nothing to pump yet */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 4; gevrHandReloadTick();
    assert(s_gevrRack[GUNRIGHT].pending == 4 && s_gevrRack[GUNRIGHT].held);
    poses[0][2] += (GEVR_RACK_PUMP_CM - 0.5f) * cm; gevrHandReloadTick();
    assert(!s_gevrRack[GUNRIGHT].cocked);
    poses[0][2] += 1.0f * cm; gevrHandReloadTick();
    assert(s_gevrRack[GUNRIGHT].cocked && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0);
    poses[0][2] -= GEVR_RACK_PUMP_CM * cm; gevrHandReloadTick();
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 4 && s_gevrRack[GUNRIGHT].pending == 0);
    /* that grip is the two-handed hold's again at once: not spent */
    assert(!s_gevrGexGripSpent && lastSound == GUN_RIFLECOCK_SFX && gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_NONE);
    /* the next shot, the next pump, the grip never let go */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 3; gevrHandReloadTick();
    assert(s_gevrRack[GUNRIGHT].pending == 3 && s_gevrRack[GUNRIGHT].held);
    poses[0][2] += (GEVR_RACK_PUMP_CM + 0.5f) * cm; gevrHandReloadTick();
    poses[0][2] -= GEVR_RACK_PUMP_CM * cm; gevrHandReloadTick();
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 3 && s_gevrRack[GUNRIGHT].pending == 0);
    /* a hand off the fore-end and away from the pump has nothing */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 2; foreEnd = 0;
    at(0, 20, -20, 100); gevrHandReloadTick();
    assert(s_gevrRack[GUNRIGHT].pending == 2 && !s_gevrRack[GUNRIGHT].held);
    foreEnd = 0; gripHeld[0] = 0; gevrHandReloadTick();

    /* at the pump's own place a grip runs it too, held or fresh */
    pumpHand(0); gripHeld[0] = 1; gevrHandReloadTick();
    assert(s_gevrRack[GUNRIGHT].held);
    pumpHand(GEVR_RACK_PUMP_CM + 0.5f); gevrHandReloadTick();
    gripHeld[0] = 0; gevrHandReloadTick();
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 2 && s_gevrRack[GUNRIGHT].pending == 0);

    /* shells put in between shots wait with the rest; none is lost */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 1; gevrHandReloadTick();
    assert(s_gevrRack[GUNRIGHT].pending == 1);
    pumpShell(); pumpShell();
    assert(s_gevrRack[GUNRIGHT].pending == 3 && player.hands[GUNRIGHT].weapon_ammo_in_magazine == 0 && reserve == 48);
    /* the last shell fired leaves it dry; a shell then waits as well */
    pumpHand(0); gripHeld[0] = 1; gevrHandReloadTick();
    pumpHand(GEVR_RACK_PUMP_CM + 0.5f); gevrHandReloadTick();
    gripHeld[0] = 0; gevrHandReloadTick();
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 3);
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 0; s_gevrRack[GUNRIGHT].last = 1; gevrHandReloadTick();
    assert(s_gevrRack[GUNRIGHT].dry && s_gevrRack[GUNRIGHT].pending == 0);
    pumpShell();
    assert(s_gevrRack[GUNRIGHT].pending == 1 && gevrReloadLabel(GUNRIGHT) == GEVR_RELOAD_LABEL_PUMP);
    /* the gun touched to the belt loads and chambers it, as it always has (dual wielding) */
    at(0, 20, -20, 40);
    belt(1, 13.1f); gevrHandReloadTick();
    belt(1, 0); gevrHandReloadTick();
    assert(s_gevrRack[GUNRIGHT].pending == 0 && player.hands[GUNRIGHT].weapon_ammo_in_magazine > 0);
    assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine + reserve == 48);   /* 55 at the start, 7 fired: none lost */

    /*
     * Gun fit sets where a gun is racked: the off hand there, the left grip
     * (gevrRackFitSet). The ring and the grab follow it; the guess no longer
     * takes it. Left-handed, the same hand places mirror.
     */
    {
        /* the KF7's handle is on its right, a hand ahead of the grip (the owner's own fit, baked in) */
        float ring[3];
        int held;
        const float side = ((VrLeftHandedMode != 0) ? -1.0f : 1.0f);

        rackSeat(ITEM_AK47, 30, 0);
        gripHeld[0] = 0; gevrHandReloadTick();
        assert(gevrRackPointShown(GUNRIGHT, ring, &held));
        assert(fabsf(ring[0] - poses[1][0] - side * 7.0f * cm) < 1e-3f && fabsf(ring[1] - poses[1][1] - 3.6f * cm) < 1e-3f
            && fabsf(ring[2] - poses[1][2] + 17.2f * cm) < 1e-3f);
        /* ... and the AR33's, nearer the grip */
        rackSeat(ITEM_M16, 30, 0);
        gripHeld[0] = 0; gevrHandReloadTick();
        assert(gevrRackPointShown(GUNRIGHT, ring, &held));
        assert(fabsf(ring[0] - poses[1][0] - side * 5.7f * cm) < 1e-3f && fabsf(ring[1] - poses[1][1] - 3.6f * cm) < 1e-3f
            && fabsf(ring[2] - poses[1][2] + 9.8f * cm) < 1e-3f);
    }
    rackSeat(ITEM_UZI, 30, 0);
    gripHeld[0] = 0; gevrHandReloadTick();
    {
        float ring[3], dist;
        int held;
        const float side = ((VrLeftHandedMode != 0) ? -1.0f : 1.0f);

        assert(gevrRackPointShown(GUNRIGHT, ring, &held) && !held);
        assert(fabsf(ring[1] - poses[1][1] - 6 * cm) < 1e-3f && fabsf(ring[2] - poses[1][2] + 6 * cm) < 1e-3f);
        assert(strcmp(gevrRackFitDistance(&dist), "THE BOLT HANDLE") == 0);
        memcpy(poses[0], poses[1], sizeof(poses[0]));
        poses[0][0] += 4 * cm; poses[0][1] += 3 * cm; poses[0][2] -= 22 * cm;
        gevrRackFitSet();
        assert(fabsf(VrRackFit[gevrFitLook(GUNRIGHT)][ITEM_UZI][0] - side * 4) < 1e-3f && fabsf(VrRackFit[gevrFitLook(GUNRIGHT)][ITEM_UZI][1] - 3) < 1e-3f
            && fabsf(VrRackFit[gevrFitLook(GUNRIGHT)][ITEM_UZI][2] + 22) < 1e-3f);
        assert(gevrRackPointShown(GUNRIGHT, ring, &held));
        assert(fabsf(ring[0] - poses[0][0]) < 1e-3f && fabsf(ring[1] - poses[0][1]) < 1e-3f && fabsf(ring[2] - poses[0][2]) < 1e-3f);
        assert(gevrRackFitDistance(&dist) && dist < 0.01f);
        rackHand(0, 0);                       /* the old guess, 17 cm from the fitted place */
        assert(!gevrGexClaimsOffHand());
        memcpy(poses[0], ring, sizeof(ring));
        assert(gevrGexClaimsOffHand());
        gripHeld[0] = 1; gevrHandReloadTick();
        assert(s_gevrRack[GUNRIGHT].held && gevrRackPointShown(GUNRIGHT, ring, &held) && held);
        poses[0][2] += (GEVR_RACK_BOLT_CM + 0.2f) * cm; gevrHandReloadTick();
        gripHeld[0] = 0; gevrHandReloadTick();
        assert(player.hands[GUNRIGHT].weapon_ammo_in_magazine == 30);
        assert(!gevrRackPointShown(GUNRIGHT, ring, &held));   /* racked: no ring */
        memset(VrRackFit, 0, sizeof(VrRackFit));
        /* a gun with nothing to rack has no place to set */
        player.hands[GUNRIGHT].weapon = ITEM_RUGER;
        assert(gevrRackFitDistance(&dist) == NULL);
        gevrRackFitSet();
        assert(VrRackFit[gevrFitLook(GUNRIGHT)][ITEM_RUGER][1] == 0.0f);
    }
    stats.MagSize = magSize;
    beanKind = 0;
    VrGexGuns = 1;
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
        reloadLabels();
        requiredGripUse();
        torsoReloadBelt();
        reversedReloads();
        reloadRoleChanges();
        klobbPullFromEitherSlot();
        reloadGrabFitMirroring();
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
    beanPp7Reload();
    rackReload();
    pumpReload();
    {
        /* the fitted place mirrors with the hands */
        const int lefty = VrLeftHandedMode;
        VrLeftHandedMode = !lefty;
        rackReload();
        pumpReload();
        VrLeftHandedMode = lefty;
    }
    /* every physical magazine swap above told the mods' weapons area */
    assert(modReloadStarts > 0 && modReloadEnds > 0);
    puts("PASS: calibrated reloads, PP7 fresh-grip ownership, physical seating, partial/fresh magazines, ammo conservation, handedness, belt and lifecycle, Bean's PP7 magazine, the rack, the pump, the rack's fitted place and the shotguns' own shells");
    return 0;
}
