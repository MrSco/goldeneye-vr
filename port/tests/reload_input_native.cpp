#include "gevr_reload_input.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#define GEVR
#define ANDROID
#define B_BUTTON 0x4000
#define TRUE 1
enum { GUNRIGHT, GUNLEFT, ITEM_UNARMED = 0 };
using s32 = int;
using GUNHAND = int;
struct Action { bool isActive; float currentState; };
struct Controller {
    Action menu, select, grip_force, grip_value, grip_click, button_a, button_b,
           button_x, button_y, thumbstick_click, trackpad_click, trackpad_touch;
} gControllerStates[2];
int VrLeftHandedMode, VrSwapJoysticks, VrPlayMode = 1;
bool gIsValveIndex;
struct Hand { int weapon, weapon_current_animation; };
struct player { int bonddead, field_D0; Hand hands[2]; } players[8];
player *g_playerPointers[8], *g_CurrentPlayer = &players[0];
static GevrReloadInput s_gevrReloadInput;
int slot, localSlot, stage = 33, pauseOpen, locked, spectator, downed;
bool connected, reloadGameplay = true, s_menuHeld, interact;
int interactionCount;
int bossGetStageNum() { return stage; }
int gevrNativePauseOpen() { return pauseOpen; }
int gevrSpectating() { return spectator; }
int gevrCoopLocalDowned() { return downed; }
bool netIsActive() { return connected; }
int netGetLocalSlot() { return localSlot; }
int get_cur_playernum() { return slot; }
int lvlGetControlsLockedFlag() { return locked; }
int disablePlayerActionsWhenPausedOrInMpMenu() { return !pauseOpen; }
int getCurrentPlayerWeaponId(int h) { return g_CurrentPlayer->hands[h].weapon; }
int get_ammo_type_for_weapon(int weapon) { return weapon; }
int bond_pressed_reload_activate() { return g_CurrentPlayer->field_D0; }
bool bond_interact_object() { interactionCount++; return !interact; }
int gevrManualReloadOn(int) { return 0; }   /* Hand reload (WIP) off: B/Y reload as before */
struct Pad { unsigned button; } pad, *npad = &pad;
int idx;
void vr_log(const char *, ...) {}
/* INSERT_BUTTONS */
/* INSERT_BRIDGE */
/* INSERT_RELOAD */
void poll() {
    /* INSERT_POLL */
}
void move(int legacyTap = 0, bool tankConsumes = false) {
    struct { int btap; } moveData = {legacyTap};
    /* INSERT_MOVEMENT */
    g_CurrentPlayer->field_D0 = moveData.btap && !tankConsumes;
}
void act() {
    /* INSERT_ACTION */
}
void tick() { move(); act(); }
void buttons(int b, int y) {
    gControllerStates[1].button_b.currentState = b;
    gControllerStates[0].button_y.currentState = y;
    poll();
}
void clearGuns() { for (auto &h : g_CurrentPlayer->hands) h.weapon_current_animation = 0; }
int main() {
    for (int i = 0; i < 8; i++) g_playerPointers[i] = &players[i];
    players[0].hands[0].weapon = players[0].hands[1].weapon = 1;
    buttons(0,0);
    buttons(1,0); tick();
    assert(players[0].hands[0].weapon_current_animation == 9 && !players[0].hands[1].weapon_current_animation);
    clearGuns(); buttons(1,1); tick(); // second edge while first button remains down
    assert(!players[0].hands[0].weapon_current_animation && players[0].hands[1].weapon_current_animation == 9);
    clearGuns(); buttons(1,1); tick(); assert(!players[0].hands[1].weapon_current_animation);
    buttons(0,0); buttons(1,1); tick();
    assert(players[0].hands[0].weapon_current_animation == 9 && players[0].hands[1].weapon_current_animation == 9);
    for (int lefty = 0; lefty < 2; lefty++) {
        VrLeftHandedMode = lefty; buttons(0,0); clearGuns(); buttons(1,0); tick();
        assert(players[0].hands[lefty ? 1 : 0].weapon_current_animation == 9);
        assert(!players[0].hands[lefty ? 0 : 1].weapon_current_animation);
        buttons(0,0); clearGuns(); players[0].hands[1].weapon = ITEM_UNARMED;
        buttons(0,1); tick(); assert(players[0].hands[0].weapon_current_animation == 9);
        players[0].hands[1].weapon = 1;
        buttons(0,0); clearGuns(); s_menuHeld = true; buttons(1,0); move(1); act();
        assert(!players[0].hands[0].weapon_current_animation && !players[0].hands[1].weapon_current_animation);
        s_menuHeld = false; buttons(1,0); tick(); assert(!gevrVrReloadPressedMask());
    }
    VrLeftHandedMode = 0; buttons(0,0); clearGuns();
    buttons(1,0); buttons(0,0); tick(); // a press survives release before the game tick
    assert(players[0].hands[0].weapon_current_animation == 9);
    buttons(0,0); clearGuns(); interact = true; buttons(0,1); tick();
    assert(!players[0].hands[1].weapon_current_animation && !gevrVrReloadPressedMask()); interact = false;
    buttons(0,0); buttons(1,0); move(0,true); act(); assert(!gevrVrReloadPressedMask());
    clearGuns(); buttons(0,0); pauseOpen = 1; buttons(1,0); pauseOpen = 0;
    buttons(1,0); move(1); act(); assert(!players[0].hands[0].weapon_current_animation);
    buttons(0,0); reloadGameplay = false; buttons(0,1); reloadGameplay = true;
    buttons(0,1); move(1); act(); assert(!players[0].hands[1].weapon_current_animation);
    buttons(0,0); stage++; buttons(1,0); move(1); act(); assert(!players[0].hands[0].weapon_current_animation);
    buttons(0,0); VrPlayMode = 0; buttons(0,0); buttons(0,1); tick();
    assert(players[0].hands[1].weapon_current_animation == 9);
    clearGuns(); buttons(0,0); move(1); act(); // keyboard/gamepad retains both-hand reload
    assert(players[0].hands[0].weapon_current_animation == 9 && players[0].hands[1].weapon_current_animation == 9);
    connected = true; localSlot = idx = slot = 7; g_CurrentPlayer = &players[7];
    players[7].hands[0].weapon = players[7].hands[1].weapon = 1;
    buttons(0,0); buttons(1,0);
    slot = 3; g_CurrentPlayer = &players[3]; move(); act(); assert(gevrVrReloadPressedMask() == 1);
    slot = 7; g_CurrentPlayer = &players[7]; tick(); assert(players[7].hands[0].weapon_current_animation == 9);
    puts("PASS: production button mapping, queued independent reloads, activation priority, chords, lifecycle and local slots");
}
