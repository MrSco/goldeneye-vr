/* Run the real Horde adapter against synthetic Archives pads and actors. */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/chr.h"
#include "game/player.h"
#include "game/bondhead.h"
#include "game/bondinv.h"
#include "game/gun.h"
#include "game/objecthandler.h"
#include "game/loadobjectmodel.h"
#include "game/stan.h"
#include "net_horde.h"
#include <assert.h>
#include <string.h>

struct stagesetup g_CurrentSetup;
static ChrRecord actors[HORDE_GUARD_SLOTS];
ChrRecord *g_ChrSlots=actors;
s32 g_NumChrSlots=HORDE_GUARD_SLOTS;
static Model models[HORDE_GUARD_SLOTS];
static PropRecord props[HORDE_GUARD_SLOTS],player_prop;
static struct player local;
struct player *g_playerPointers[MAX_PLAYER_COUNT],*g_CurrentPlayer=&local;
static StandTile tile;
static int announced, supplies, armed, host=1,free_override=-1;
int netHordeActive(void) {return 1;}
int netHordeRunning(void) {return 1;}
s32 netIsHost(void) {return host;}
s32 netGetLocalSlot(void) {return 0;}
unsigned netGetRandomSeed(void) {return 123;}
int netMpPlayerCount(int fallback) {return 1;}
int netLobbySlotConnected(int slot) {return slot==0;}
int netPlayerIsSpectator(int slot) {return 0;}
int gevrCoopDowned(int slot) {return 0;}
s32 get_cur_playernum(void) {return 0;}
void set_cur_player(s32 slot) {assert(slot==0);}
int bondinvAddInvItem(ITEM_IDS item) {return 1;}
void give_cur_player_ammo(s32 ammo,s32 count) {assert(count==120);supplies++;}
void hudmsgTopShow(char *text) {}
static int shown,live_shown;
void hudmsgBottomShow(char *text) {shown++;}
void gevrHudmsgBottomLive(char *text) {live_shown++;}
void netHordeSend(int slot) {}
void netHordeAdmitPlayers(void) {}
void chrStopFiring(ChrRecord *chr) {}
AIRecord *ailistFindById(s32 id) {static AIRecord list;return &list;}
s32 sizepropdef(PropDefHeaderRecord *p) {assert(p->type==PROPDEF_GUARD);return sizeof(GuardRecord)/4;}
s32 chrGetNumFree(void) {
    if(free_override>=0)return free_override;
    int n=0;for(int i=0;i<HORDE_GUARD_SLOTS;i++)if(!actors[i].model)n++;return n;
}
waypoint *chrlvStanPathRelated(coord3d *p,StandTile *t) {return (waypoint *)1;}
s32 waypointFindRoute(waypoint *a,waypoint *b,waypoint **route,s32 length) {return 2;}
s32 stanTestLineUnobstructed(StandTile **stan,f32 x,f32 z,f32 nx,f32 nz,int mask,f32 ymin,f32 ymax,f32 a,f32 b) {return FALSE;}
PropRecord *chrGiveWeapon(ChrRecord *chr,s32 model,ITEM_IDS item,s32 flags) {
    assert(flags&PROPFLAG_AIUNDROPPABLE);armed++;return chr->prop;
}
PropRecord *chrSpawnAtCoord(s32 body,s32 head,coord3d *pos,StandTile *stan,f32 angle,AIRecord *ai,s32 flags) {
    for(int i=0;i<HORDE_GUARD_SLOTS;i++)if(!actors[i].model) {
        assert(pos->x>900); /* the player is at the near pad */
        ChrRecord *chr=&actors[i];memset(chr,0,sizeof(*chr));
        chr->model=&models[i];chr->prop=&props[i];chr->prop->chr=chr;chr->prop->pos=*pos;chr->prop->stan=stan;
        chr->actiontype=ACT_STAND;chr->bodynum=body;chr->headnum=1;return chr->prop;
    }
    return NULL;
}
void gevrCoopChrSpawned(ChrRecord *chr,AIRecord *ai,s32 flags) {
    assert(gevrHordeGuardId(chr) && armed>announced);announced++;
}
int main(void)
{
    PadRecord pads[4]={0};for(int i=0;i<3;i++){pads[i].plink="pad";pads[i].stan=&tile;pads[i].pos.x=i*2000;}
    struct {GuardRecord guards[3];PropDefHeaderRecord end;} setup={0};
    for(int i=0;i<3;i++){setup.guards[i].type=PROPDEF_GUARD;setup.guards[i].PadID=i;setup.guards[i].BodyID=1;}
    setup.end.type=PROPDEF_END;
    SetupIntroSpawn original[2]={{INTROTYPE_SPAWN,0,0},{INTROTYPE_END,0,0}};
    g_CurrentSetup.propDefs=(void *)&setup;g_CurrentSetup.pads=pads;g_CurrentSetup.intro=(void *)original;
    gevrHordePrepareSetup();
    assert(gevrHordeSkipProp(PROPDEF_GUARD) && gevrHordeSkipProp(PROPDEF_OBJECTIVE_START));
    assert(!gevrHordeSkipProp(PROPDEF_DOOR));
    assert(((SetupIntroSpawn *)g_CurrentSetup.intro)->index==0);
    local.prop=&player_prop;player_prop.stan=&tile;g_playerPointers[0]=&local;
    gevrHordeStageLoaded();
    for(int i=0;i<60;i++)gevrHordeTick(15);
    assert(announced==1 && supplies==3 && gevrHordeState()->remaining==5);
    /* the preparation countdown updates each second; the wave is announced once */
    assert(live_shown>=2 && shown==1);
    unsigned remaining=gevrHordeState()->remaining;
    free_override=0;for(int i=0;i<20;i++)gevrHordeTick(15);
    assert(announced==1 && gevrHordeState()->remaining==remaining);free_override=-1;
    for(int i=0;i<40;i++)gevrHordeTick(15);
    assert(announced==6 && gevrHordeState()->alive==6);
    HordeState lagged=*gevrHordeState();
    lagged.revision++;lagged.alive--;lagged.remaining++;lagged.next_id--;
    gevrHordeApplyState(&lagged);gevrHordeBecameHost();
    assert(gevrHordeState()->alive==6 && !gevrHordeState()->remaining && gevrHordeState()->next_id==7);
    actors[0].actiontype=ACT_DIE;gevrHordeTick(1);gevrHordeTick(1);
    assert(gevrHordeState()->kills==1 && gevrHordeState()->alive==5);
    /* An unexplained despawn is replaced, never rewarded as a kill. */
    actors[1].model=NULL;gevrHordeTick(15);
    assert(gevrHordeState()->kills==1);
    for(int i=0;i<20;i++)gevrHordeTick(15);
    assert(announced==7 && gevrHordeState()->alive==5);
    for(int i=0;i<HORDE_GUARD_SLOTS;i++)if(actors[i].model)actors[i].actiontype=ACT_DEAD;
    gevrHordeTick(1);assert(gevrHordeState()->phase==HORDE_INTERMISSION && gevrHordeState()->completed==1);
    assert(supplies==6);
    gevrHordeDefeat();HordeState ended=*gevrHordeState();gevrHordeTick(15);
    assert(!memcmp(&ended,gevrHordeState(),sizeof(ended)));
    /* Synthetic 45-minute run: repeated deaths/replacement must recycle the
     * same actor storage and keep every live wave under its concurrent cap. */
    memset(actors,0,sizeof(actors));gevrHordeStageLoaded();
    for(int frame=0;frame<45*60*4;frame++) {
        gevrHordeTick(15);
        assert(gevrHordeState()->alive<=hordeCapacity(1));
        for(int i=0;i<HORDE_GUARD_SLOTS;i++)if(actors[i].model) {
            if(actors[i].hidden&CHRHIDDEN_REMOVE)actors[i].model=NULL;
            else if(frame%3==0 && actors[i].actiontype!=ACT_DEAD)actors[i].actiontype=ACT_DEAD;
        }
    }
    assert(gevrHordeState()->completed>30 && gevrHordeState()->kills>1000);
    puts("PASS: production Horde setup, safe pads, armed spawn publication, slot pressure, takeover reconciliation, death idempotency, despawn replacement, resupply and defeat");
    puts("PASS: 45 simulated minutes of wave progression with bounded/recycled actors");
}
