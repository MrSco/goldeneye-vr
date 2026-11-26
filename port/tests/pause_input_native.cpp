#include "gevr_pause_input.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static void check(bool ok,const char* message) { if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);} }
enum { START_BUTTON=0x1000, Z_TRIG=0x2000, R_TRIG=0x10 };
struct TestPlayer { int mpmenuon; } players[8],*g_playerPointers[8];
struct TestPad { int button,stick_x,stick_y,rstick_x,rstick_y; };
static int online,coop,inMenus,localSlot,idx,trigger,g_gevrWatchGesturePending;
static int gevrVrTriggerDown[2];
static float gevrTurnAxis;
static int netIsActive() { return online; }
static int netCoopActive() { return coop; }
/* mpmenu.c: a game screen (Select Character, Select Stage) up in place of the window */
static int panelOpen,panelCancels;
static int gevrMenuPanelCancel(int slot) { if(!panelOpen || slot!=localSlot)return 0; panelOpen=0; panelCancels++; return 1; }
/* mpmenu.c: in a deathmatch warmup Menu opens the Multiplayer Options folder, itself a game screen */
static int warmup,folderOpens;
static int gevrMpOptionsStartPressed(int slot) {
    if(!warmup || coop || slot!=localSlot)return 0;
    players[slot].mpmenuon=1;panelOpen=1;folderOpens++;return 1;
}
static int gevrNativePauseOpen() { return online && !inMenus && players[localSlot].mpmenuon && !panelOpen; }
static int get_button_state(int,const char*) { return trigger; }
static TestPad poll(int start) {
    TestPad pad{(start?START_BUTTON:0)|(trigger?Z_TRIG:0),1,1,1,1};
    TestPad* npad=&pad;
    gevrVrTriggerDown[0]=gevrVrTriggerDown[1]=trigger;
    gevrTurnAxis=1;
    /* INSERT_PAUSE_INPUT */
    return pad;
}
int main() {
    for(int i=0;i<8;i++)g_playerPointers[i]=&players[i];
    for(int mode=0;mode<2;mode++)for(int slot:{0,3,7}) {
        if(mode && slot>=4)continue;
        online=0;trigger=0;inMenus=0;poll(0);
        online=1;coop=mode;localSlot=idx=slot;players[slot].mpmenuon=0;
        for(int cycle=0;cycle<3;cycle++) {
            g_gevrWatchGesturePending=1;
            auto pad=poll(1);
            check(players[slot].mpmenuon,"Start opens the native menu");
            check(!g_gevrWatchGesturePending,"opening consumes the watch gesture request");
            check(!(pad.button&START_BUTTON),"opening pulse is consumed before the legacy menu");
            poll(1);check(players[slot].mpmenuon,"held opening pulse does not toggle twice");
            poll(0);
            pad=poll(1);check(!players[slot].mpmenuon,"Start closes the native menu");
            check(!(pad.button&START_BUTTON),"closing pulse is consumed before the legacy menu");
            pad=poll(1);check(!players[slot].mpmenuon && !(pad.button&START_BUTTON),"remaining closing pulse cannot reopen the menu");
            poll(0);
        }
        trigger=1;poll(1);poll(0);
        auto pad=poll(1);
        check(!players[slot].mpmenuon && !(pad.button&(Z_TRIG|R_TRIG)),"Start resume blocks a held trigger");
        check(!gevrVrTriggerDown[0] && !gevrVrTriggerDown[1],"resume suppresses tracked-hand fire");
        poll(1);trigger=0;poll(0);trigger=1;
        check(poll(0).button&Z_TRIG,"a fresh squeeze after release can fire");
        trigger=0;poll(1);players[slot].mpmenuon=0;
        pad=poll(1);check(!players[slot].mpmenuon && !(pad.button&START_BUTTON),"pointer Resume cannot leak the held Start pulse");
        poll(0);
    }
    /* the headset's Menu with the game's Select Character or Select Stage up: leaves it and closes the menu */
    online=1;coop=0;inMenus=0;trigger=0;localSlot=idx=0;poll(0);
    players[0].mpmenuon=1;panelOpen=1;panelCancels=0;
    {
        auto pad=poll(1);
        check(panelCancels==1 && !panelOpen,"Start leaves the game screen unchanged");
        check(!players[0].mpmenuon,"Start with a game screen up closes the menu instead of reopening the window");
        check(!(pad.button&START_BUTTON),"the screen never sees Start as a choice");
        poll(1);check(!players[0].mpmenuon && panelCancels==1,"held Start does nothing more");
        poll(0);pad=poll(1);check(players[0].mpmenuon && panelCancels==1,"the next Start opens the window as before");
        poll(0);poll(1);poll(0);
    }
    /* a deathmatch warmup: Menu opens the folder (a game screen), the next Menu leaves it and closes the menu */
    online=1;coop=0;inMenus=0;trigger=0;localSlot=idx=0;players[0].mpmenuon=0;panelOpen=0;panelCancels=0;warmup=1;folderOpens=0;poll(0);
    {
        auto pad=poll(1);
        check(folderOpens==1 && players[0].mpmenuon && panelOpen,"warmup Start opens the folder");
        check(!gevrNativePauseOpen() && !(pad.button&START_BUTTON),"the folder, not the window, and Start is consumed");
        poll(1);check(folderOpens==1 && panelOpen,"held Start opens it once");
        poll(0);poll(1);check(panelCancels==1 && !panelOpen && !players[0].mpmenuon,"the next Start leaves the folder and closes the menu");
        poll(0);warmup=0;
    }
    online=0;check(poll(1).button&START_BUTTON,"solo Start still reaches the watch");
    online=1;inMenus=1;check(poll(1).button&START_BUTTON,"title Start keeps its normal behavior");
    puts("PASS: production Start routing toggles deathmatch/co-op once, consumes full pulses, leaves a game screen and preserves trigger release");
}
