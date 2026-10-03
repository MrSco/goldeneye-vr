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
static int gevrNativePauseOpen() { return online && !inMenus && players[localSlot].mpmenuon; }
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
    online=0;check(poll(1).button&START_BUTTON,"solo Start still reaches the watch");
    online=1;inMenus=1;check(poll(1).button&START_BUTTON,"title Start keeps its normal behavior");
    puts("PASS: production Start routing toggles deathmatch/co-op once, consumes full pulses and preserves trigger release");
}
