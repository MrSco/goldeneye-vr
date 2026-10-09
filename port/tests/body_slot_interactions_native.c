#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gevr_bodyslot.h"
typedef int s32;
typedef float f32;
#define TRUE 1
#define FALSE 0
#define sysLogPrintf(...) ((void)0)
#define GUNLEFT 1
#define GUNRIGHT 0
#define ITEM_UNARMED 0
#define ITEM_FIST 1
#define ITEM_GRENADE 20
#define ITEM_KNIFE 21
#define GUN_ANIM_STATE_IDLE 0
#define GUN_ANIM_STATE_TRIGGER_PRESS 1
#define GUN_ANIM_STATE_GRENADE_RECOVER 2
#define GUN_ANIM_STATE_GRENADE_THROW 3
#define GEVR_ACTION_SLOT_HOVER 1004
#define GEVR_ACTION_SLOT_TAKE 1005
#define GEVR_ACTION_SLOT_DENY 1006
#define BS_SETTLE_SPEED 0.35f
#define BT_DWELL 0
#define GEVR_GRIP_PEND_POLLS 30
#define GEVR_THROW_BUFFER_SIZE 6
struct coord3d { float x, y, z; };
struct hand { int weapon_ammo_in_magazine, weapon_action_state, item; };
struct Player { int bonddead, watch_animation_state, mpmenuon; struct hand hands[2]; } player;
static struct Player *g_CurrentPlayer = &player;
typedef struct { int slot, pick, gripHeld, quiet; float ms, settledMs; GevrBodyStick stick; } GevrBodyHand;
static GevrBodyHand s_bodyHand[2];
static GevrBodyMru s_bodyMru[GEVR_BODY_SLOTS];
static int s_bodyLive = 1, VrGestureHolster, VrGestureGripUse, VrGesturePickup, VrGestureMineGrab;
int VrMotionThrowing = 1, g_gevrStereo = 1;
static int g_PlayerIsInTank, s_gevrGripGesture[2], s_gevrGripPendAge[2];
static float s_bodyTune[] = {350};
float vr_ctrl_velocity_play[2][3], vr_head_velocity_play[3], vr_ctrl_quat_play[2][4];
float VrMotionThrowPitch, VrMotionThrowGazeAssist, VrMotionThrowStrength;
int g_gevrMotionThrowActive[2];
struct coord3d g_gevrMotionThrowVel[2];
static int s_gevrThrowWindup[2], s_gevrGripArmed[2], s_gevrThrowSpent[2], s_gevrTrackingLostFrames[2];
static float s_gevrGripPeak[2];
static int s_gevrThrowVelHead[2], s_gevrThrowVelCount[2];
static struct coord3d s_gevrThrowVelBuffer[2][GEVR_THROW_BUFFER_SIZE];
static int changes, throws, steps, pressDecisions, lastChoice;
static float squeeze[2];
static int online, localSlot, currentSlot;
static int netIsActive(void) { return online; }
static int netGetLocalSlot(void) { return localSlot; }
static int get_cur_playernum(void) { return currentSlot; }
static int gevrShotCtrl(int hand) { return hand == GUNRIGHT ? 1 : 0; }
static int getCurrentPlayerWeaponId(int hand) { return player.hands[hand].item; }
static int gevrBodyHandSelected(int hand) { return getCurrentPlayerWeaponId(hand); }
int gevrIsThrowable(int item) { return item == ITEM_GRENADE || item == ITEM_KNIFE; }
static int gevrReloadBeltHover(int ctrl) { (void)ctrl; return 0; }
static int getPlayerCount(void) { return 1; }
static int get_scenario(void) { return 0; }
static int bondinvIsAliveWithFlag(void) { return 0; }
static void gevrBodyPickName(int hand, int pick, char *out, int size) { (void)hand; (void)pick; snprintf(out, size, "item"); }
static void gevrBodyBuzz(int ctrl, int event) { (void)ctrl; (void)event; }
static void gunRequestHandWeaponChange(int hand, int pick, int dir) { (void)hand; (void)dir; changes++; lastChoice = pick; }
static int gevrBodyItemKept(int item) { (void)item; return 0; }
static int gevrBodySlotForItem(int item, int hand) { (void)item; (void)hand; return 0; }
static void gevrBodyStep(int ctrl, int step, const char *how) { (void)ctrl; (void)step; (void)how; steps++; }
static int gevrHandReloadActive(void) { return 0; }
static int gevrBodySlotsOn(void) { return s_bodyLive; }
static void gevrGestureTuneRead(void) {}
void gevrBodySlotsTick(void) {} /* Test positions and settled times are supplied explicitly. */
float get_analog_value(int ctrl, const char *name) { (void)name; return squeeze[ctrl]; }
int gevrDualWielding(void) { return 1; }
int vr_haptics_ready(void) { return 0; }
int trigger_haptic_vibration_c(int ctrl, float a, float d) { (void)ctrl; (void)a; (void)d; return 0; }
void generate_player_thrown_grenade(int hand) { (void)hand; }
void generate_player_thrown_knife(int hand) { (void)hand; }
void generate_player_thrown_object(int hand) { (void)hand; }
int gevrBodySlotGrip(int ctrl);
static int gevrGripGestureTry(int ctrl) { pressDecisions++; return gevrBodySlotGrip(ctrl); }
static int stereoplay = 1, fitting, grips[2], gripWas[2], gripTaken[2], VrSwapJoysticks;
typedef struct { float x, y; } XrVector2f;
static XrVector2f left, right;
static float dt = 16;
/* PRODUCTION */
static void frame(int ctrl, float value) {
    squeeze[ctrl] = value;
    grips[ctrl] = value >= 0.5f;
    poll();
}
static void reset(int item) {
    memset(&player, 0, sizeof(player));
    memset(s_bodyHand, 0, sizeof(s_bodyHand));
    memset(s_gevrGripGesture, 0, sizeof(s_gevrGripGesture));
    memset(gripWas, 0, sizeof(gripWas)); memset(grips, 0, sizeof(grips)); memset(squeeze, 0, sizeof(squeeze));
    memset(s_gevrThrowWindup, 0, sizeof(s_gevrThrowWindup)); memset(s_gevrGripArmed, 0, sizeof(s_gevrGripArmed));
    memset(s_gevrThrowSpent, 0, sizeof(s_gevrThrowSpent));
    s_bodyHand[0].slot = s_bodyHand[1].slot = -1;
    changes = throws = pressDecisions = 0;
    VrMotionThrowing = s_bodyLive = 1;
    for (int hand = 0; hand < 2; hand++) { player.hands[hand].item = item; player.hands[hand].weapon_ammo_in_magazine = 5; }
}
int main(void) {
    for (int ctrl = 0; ctrl < 2; ctrl++) {
        int hand = ctrl ? GUNRIGHT : GUNLEFT;
        for (int item = ITEM_GRENADE; item <= ITEM_KNIFE; item++) {
            reset(item);
            /* Press away from a slot: wait for arbitration before winding up. */
            frame(ctrl, 0.9f); gevrMotionThrowTick(hand);
            assert(!s_gevrThrowWindup[hand]);
            gevrGripGestureTick(); gevrMotionThrowTick(hand);
            assert(s_gevrThrowWindup[hand] && changes == 0);
            /* Passing/pausing at the shoulder during the same squeeze cannot grab. */
            s_bodyHand[ctrl].slot = GEVR_BS_BACK_GUN; s_bodyHand[ctrl].pick = 7; s_bodyHand[ctrl].settledMs = 1000;
            frame(ctrl, 0.4f); gevrGripGestureTick(); gevrMotionThrowTick(hand);
            assert(throws == 1 && changes == 0 && pressDecisions == 1);
            frame(ctrl, 0.9f); gevrGripGestureTick(); gevrMotionThrowTick(hand);
            assert(throws == 1 && changes == 0 && !s_gevrThrowWindup[hand]);
            frame(ctrl, 0); gevrMotionThrowTick(hand);
            /* Now release fully, settle and squeeze: just one weapon change. */
            frame(ctrl, 0.9f); gevrMotionThrowTick(hand); gevrGripGestureTick(); gevrMotionThrowTick(hand);
            assert(changes == 1 && lastChoice == 7 && throws == 1);
            frame(ctrl, 0.4f); gevrGripGestureTick(); gevrMotionThrowTick(hand);
            frame(ctrl, 0.9f); gevrGripGestureTick(); gevrMotionThrowTick(hand);
            assert(changes == 1 && throws == 1 && gevrBodySlotHoldsGrip(ctrl));
            frame(ctrl, 0); assert(!gevrBodySlotHoldsGrip(ctrl));
            /* An insufficient pause at the shoulder still chooses throwing. */
            reset(item); s_bodyHand[ctrl].slot = GEVR_BS_BACK_GUN; s_bodyHand[ctrl].pick = 7; s_bodyHand[ctrl].settledMs = 349;
            frame(ctrl, 0.9f); gevrGripGestureTick(); gevrMotionThrowTick(hand);
            assert(changes == 0 && s_gevrThrowWindup[hand]);
        }
        /* Guns/gadgets need no pause, nor do throwables with motion throwing off. */
        for (int item = 7; item <= 8; item++) {
            reset(item); s_bodyHand[ctrl].slot = GEVR_BS_BACK_GUN; s_bodyHand[ctrl].pick = 9;
            frame(ctrl, 0.9f); gevrGripGestureTick(); assert(changes == 1);
        }
        reset(ITEM_GRENADE); VrMotionThrowing = 0;
        s_bodyHand[ctrl].slot = GEVR_BS_BACK_GUN; s_bodyHand[ctrl].pick = 9;
        frame(ctrl, 0.9f); gevrGripGestureTick(); assert(changes == 1);
    }
    /* The actual poll clears both axes of the correct logical stick. */
    for (VrSwapJoysticks = 0; VrSwapJoysticks < 2; VrSwapJoysticks++) {
        for (int ctrl = 0; ctrl < 2; ctrl++) {
            reset(7); steps = 0; s_bodyHand[ctrl].slot = GEVR_BS_HIP_GUN;
            left = right = (XrVector2f){0, 0}; sticks();
            XrVector2f *own = ((ctrl == 0) == (VrSwapJoysticks == 0)) ? &left : &right;
            XrVector2f *other = own == &left ? &right : &left;
            *own = (XrVector2f){0.8f, 0.6f}; *other = (XrVector2f){0.1f, 0.9f}; sticks();
            assert(steps == 1 && own->x == 0 && own->y == 0 && other->y == 0.9f);
            s_bodyHand[ctrl].slot = -1; *own = (XrVector2f){0, 0.8f}; sticks(); assert(own->y == 0);
            *own = (XrVector2f){0, 0}; sticks();
            *own = (XrVector2f){0, 0.8f}; sticks(); assert(own->y == 0.8f);
        }
    }
    puts("PASS: body-slot grip ownership, throwable-only pause, partial release, diagonal stick capture");
    return 0;
}
