/* Run the real Horde adapter against synthetic Archives pads and actors. */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/player.h"
#include "game/bondhead.h"
#include "game/bondinv.h"
#include "game/gun.h"
#include "game/objecthandler.h"
#include "game/loadobjectmodel.h"
#include "game/stan.h"
#include "net_horde.h"
#include "game/gevr_ally.h"
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
static int path_clear, door_opens, ammo_full;
static int supply_amount[AMMOTYPE_MAX];
static DoorRecord test_door;static PropRecord door_prop;
static int door_blocking;
static int announced, supplies, armed, host=1,free_override=-1, allyStops;
void gevrAllyStop(void) { allyStops++; }
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
static int owned_items[ITEM_IDS_MAX];
int bondinvHasInvItem(ITEM_IDS item) {return item>0 && item<ITEM_IDS_MAX && owned_items[item];}
int bondinvAddInvItem(ITEM_IDS item) {if(item>0 && item<ITEM_IDS_MAX)owned_items[item]=1;return 1;}
s32 currentPlayerGetAmmoCount(AMMOTYPE ammo) {(void)ammo;return ammo_full?999:0;}
s32 get_max_ammo_for_type(s32 ammo) {(void)ammo;return 999;}
void give_cur_player_ammo(s32 ammo,s32 count) {assert(ammo>0 && ammo<AMMOTYPE_MAX && count>0);supplies++;supply_amount[ammo]+=count;}
void hudmsgTopShow(char *text) {(void)text;}
static int shown,live_shown;
void hudmsgBottomShow(char *text) {(void)text;shown++;}
void gevrHudmsgBottomLive(char *text) {(void)text;live_shown++;}
/* station markers: uncollectable objects, counted and placed */
static ObjectRecord markers[128];static PropRecord marker_props[128];static Model marker_models[128];
static int marker_count, marker_rooms;
ObjectRecord *create_new_item_instance_of_model(PROP model,s32 item) {
    (void)model;(void)item;assert(marker_count<128);
    ObjectRecord *o=&markers[marker_count];memset(o,0,sizeof(*o));
    o->prop=&marker_props[marker_count];o->model=&marker_models[marker_count];o->model->scale=1;
    marker_count++;return o;
}
void setupUpdateObjectRoomPosition(ObjectRecord *obj) {(void)obj;marker_rooms++;}
static int marker_activations[128];
void chrpropActivate(PropRecord *p) {int i=p-marker_props;assert(i>=0 && i<128);assert(!marker_activations[i]++);}
void chrpropEnable(PropRecord *p) {(void)p;}
void chrpropDisable(PropRecord *p) {(void)p;}
WeaponStats *get_ptr_item_statistics(ITEM_IDS item) {
    static WeaponStats stats;stats.AmmoType=AMMO_9MM;stats.MagSize=7;
    if(item==ITEM_SKORPION){stats.AmmoType=AMMO_9MM_2;stats.MagSize=20;}
    if(item==ITEM_AK47){stats.AmmoType=AMMO_RIFLE;stats.MagSize=30;}
    if(item==ITEM_SHOTGUN){stats.AmmoType=AMMO_SHOTGUN;stats.MagSize=5;}
    if(item==ITEM_RUGER){stats.AmmoType=AMMO_MAGNUM;stats.MagSize=6;}
    if(item==ITEM_ROCKETLAUNCH){stats.AmmoType=AMMO_ROCKETS;stats.MagSize=1;}
    return &stats;
}
int gevrCoopGuardTarget(ChrRecord *chr) {(void)chr;return 0;}
PropRecord *sub_GAME_7F0B1410(StandTile *t,f32 x,f32 z,f32 xx,f32 zz,s32 mask)
{(void)t;(void)x;(void)z;(void)xx;(void)zz;(void)mask;return door_blocking?&door_prop:NULL;}
void doorsChooseSwingDirection(PropRecord *p,DoorRecord *d) {(void)p;(void)d;}
void doorActivate(DoorRecord *d,DOORSTATE state) {assert(state==DOORSTATE_OPENING);d->openstate=state;door_opens++;}
void modelSetScale(Model *m,f32 s) {m->scale=s;}
void matrix_4x4_set_position_and_rotation_around_y(f32 *pos,f32 a,Mtxf *m) {(void)a;memset(m,0,sizeof(*m));m->m[3][0]=pos[0];m->m[3][1]=pos[1];m->m[3][2]=pos[2];}
void matrix_scalar_multiply(f32 s,f32 *m) {(void)s;(void)m;}
StandTile *stanFindTileBelowPos(coord3d *pos,u8 *rooms,f32 *y) {(void)rooms;*y=pos->y;return &tile;}
void sysLogPrintf(s32 level,const char *fmt,...) {(void)level;(void)fmt;}
void netHordeSend(int slot) {(void)slot;}
void netHordeAdmitPlayers(void) {}
/* 2026-10-17: held-open doors, the two-gun list, additive ammo, the start wave */
s32 g_GlobalTimer;
unsigned netHordeStartWave(void) {return 1;}
bool doorIsPadlockFree(DoorRecord *d) {(void)d;return 1;}
void doorUpdateBbox(DoorRecord *d) {(void)d;}
void doorActivatePortal(DoorRecord *d) {(void)d;}
void door7F053B10(DoorRecord *d) {(void)d;}
void netSendHostDoorState(ObjectRecord *d,s32 state) {(void)d;(void)state;}
s32 check_cur_player_ammo_amount_in_inventory(AMMOTYPE ammo) {(void)ammo;return 0;}
InvItem *bondinvGetDualWeapon(ITEM_IDS right,ITEM_IDS left) {(void)right;(void)left;return NULL;}
void bondinvRemoveItem(InvItem *item) {(void)item;}
void bondinvRemoveItemByID(ITEM_IDS item) {if(item>0 && item<ITEM_IDS_MAX)owned_items[item]=0;}
ITEM_IDS getCurrentPlayerWeaponId(GUNHAND hand) {(void)hand;return ITEM_WPPK;}
void gunRequestHandWeaponChange(enum GUNHAND hand,s32 next,s32 dir) {(void)hand;(void)next;(void)dir;}
void netHordeRequestAction(unsigned action,unsigned item) {gevrHordeHandleAction(0,action,item);}
void chrStopFiring(ChrRecord *chr) {(void)chr;}
AIRecord *ailistFindById(s32 id) {(void)id;static AIRecord list;return &list;}
s32 sizepropdef(PropDefHeaderRecord *p) {assert(p->type==PROPDEF_GUARD);return sizeof(GuardRecord)/4;}
s32 chrGetNumFree(void) {
    if(free_override>=0)return free_override;
    int n=0;for(int i=0;i<HORDE_GUARD_SLOTS;i++)if(!actors[i].model)n++;return n;
}
waypoint *chrlvStanPathRelated(coord3d *p,StandTile *t) {(void)p;(void)t;return (waypoint *)1;}
s32 waypointFindRoute(waypoint *a,waypoint *b,waypoint **route,s32 length) {(void)a;(void)b;(void)route;(void)length;return 2;}
s32 stanTestLineUnobstructed(StandTile **stan,f32 x,f32 z,f32 nx,f32 nz,int mask,f32 ymin,f32 ymax,f32 a,f32 b) {(void)stan;(void)x;(void)z;(void)nx;(void)nz;(void)mask;(void)ymin;(void)ymax;(void)a;(void)b;return path_clear;}
PropRecord *chrGiveWeapon(ChrRecord *chr,s32 model,ITEM_IDS item,s32 flags) {
    (void)model;(void)item;assert(flags&PROPFLAG_AIUNDROPPABLE);armed++;return chr->prop;
}
PropRecord *chrSpawnAtCoord(s32 body,s32 head,coord3d *pos,StandTile *stan,f32 angle,AIRecord *ai,s32 flags) {
    (void)head;(void)angle;(void)ai;(void)flags;
    for(int i=0;i<HORDE_GUARD_SLOTS;i++)if(!actors[i].model) {
        assert(pos->x>900); /* the player is at the near pad */
        ChrRecord *chr=&actors[i];memset(chr,0,sizeof(*chr));
        chr->model=&models[i];chr->prop=&props[i];chr->prop->chr=chr;chr->prop->pos=*pos;chr->prop->stan=stan;
        chr->actiontype=ACT_STAND;chr->bodynum=body;chr->headnum=1;return chr->prop;
    }
    return NULL;
}
void gevrCoopChrSpawned(ChrRecord *chr,AIRecord *ai,s32 flags) {
    (void)ai;(void)flags;assert(gevrHordeGuardId(chr) && armed>announced);announced++;
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
    assert(gevrHordeState()->credits[0]==HORDE_START_CREDITS);
    for(int i=0;i<60;i++)gevrHordeTick(15);
    assert(announced==1 && supplies==0 && gevrHordeState()->remaining==5);
    /* the preparation countdown updates each second; the wave is announced once */
    assert(live_shown>=2 && shown==1);
    /* the box and six wall buys stand in the world and cannot be picked up */
    assert(marker_count==7);
    for(int i=0;i<7;i++) assert(markers[i].flags&PROPFLAG_UNCOLLECTABLE);
    int rooms=marker_rooms;gevrHordeTick(1);assert(marker_rooms==rooms); /* turning only, no room churn */
    unsigned remaining=gevrHordeState()->remaining;
    free_override=0;for(int i=0;i<20;i++)gevrHordeTick(15);
    assert(announced==1 && gevrHordeState()->remaining==remaining);free_override=-1;
    for(int i=0;i<40;i++)gevrHordeTick(15);
    assert(announced==6 && gevrHordeState()->alive==6);
    HordeState lagged=*gevrHordeState();
    lagged.revision++;lagged.alive--;lagged.remaining++;lagged.next_id--;
    gevrHordeApplyState(&lagged);gevrHordeBecameHost();
    assert(gevrHordeState()->alive==6 && !gevrHordeState()->remaining && gevrHordeState()->next_id==7);
    /* Non-lethal hit awards +10 pts; lethal headshot awards +100 pts once. */
    actors[0].damage=1.0f;gevrHordeNotifyGuardHit(&actors[0],0,HIT_CHEST);
    assert(gevrHordeState()->credits[0]==HORDE_START_CREDITS+HORDE_HIT_CREDITS);
    actors[0].damage=10.0f;gevrHordeNotifyGuardHit(&actors[0],0,HIT_HEAD);
    actors[0].actiontype=ACT_DIE;gevrHordeTick(1);gevrHordeTick(1);
    assert(gevrHordeState()->kills==1 && gevrHordeState()->alive==5);
    assert(gevrHordeState()->credits[0]==HORDE_START_CREDITS+HORDE_HIT_CREDITS+HORDE_HEADSHOT_CREDITS);
    /* One crate per actual death, including repeated lethal notifications.
     * Despawns give none. Pickup is shared, proximity checked and idempotent. */
    HordeState loot=*gevrHordeState();
    assert(loot.ammo[0].id==gevrHordeGuardId(&actors[0]));
    assert(loot.ammo[0].ticks>0 && !loot.ammo[1].id);
    assert(!gevrHordeHandleAction(0,HORDE_ACT_AMMO_PICKUP,loot.ammo[0].id));
    player_prop.pos=(coord3d){.x=loot.ammo[0].pos[0],.y=loot.ammo[0].pos[1]+160,.z=loot.ammo[0].pos[2]};
    assert(!gevrHordeHandleAction(0,HORDE_ACT_AMMO_PICKUP,loot.ammo[0].id)); /* closed door */
    path_clear=1;
    assert(gevrHordeHandleAction(0,HORDE_ACT_AMMO_PICKUP,loot.ammo[0].id));
    assert(!gevrHordeHandleAction(0,HORDE_ACT_AMMO_PICKUP,loot.ammo[0].id));
    assert(gevrHordeState()->ammo_grants[0]==1);
    owned_items[ITEM_WPPK]=1;
    player_prop.pos=(coord3d){0};path_clear=0;
    gevrHordeTick(1);assert(supplies==1);
    gevrHordeTick(1);assert(supplies==1 && supply_amount[AMMO_9MM]==16); /* snapshot grant applied once */
    /* Horde requests nearby route-blocking doors without re-triggering one
     * already opening, and never opens a distant door. */
    door_prop.type=PROP_TYPE_DOOR;door_prop.door=&test_door;
    door_prop.pos=actors[1].prop->pos;test_door.frac=1;test_door.openPosition=0;
    door_blocking=1;
    for(int i=0;i<61;i++)gevrHordeTick(1);
    assert(door_opens>=1);int opened=door_opens;
    for(int i=0;i<61;i++)gevrHordeTick(1);
    assert(door_opens==opened);door_blocking=0;
    /* Walk to pad 2 (Wall Buy) and buy Klobb (500 pts), then walk to pad 1 (Mystery Box). */
    player_prop.pos.x=4000;
    assert(gevrHordeOnUse() && gevrHordeState()->grant_seq[0]==1);
    gevrHordeTick(1);assert(owned_items[ITEM_SKORPION]);
    /* An unexplained despawn is replaced, never rewarded as a kill. */
    player_prop.pos.x=0;
    actors[1].model=NULL;gevrHordeTick(15);
    assert(gevrHordeState()->kills==1);
    for(int i=0;i<20;i++)gevrHordeTick(15);
    assert(announced==7 && gevrHordeState()->alive==5);
    for(int i=0;i<HORDE_GUARD_SLOTS;i++)if(actors[i].model)actors[i].actiontype=ACT_DEAD;
    gevrHordeTick(1);assert(gevrHordeState()->phase==HORDE_INTERMISSION && gevrHordeState()->completed==1);
    assert(supplies==2); /* enemy pickup plus one magazine on buying a new weapon; no wave refill */
    int ammo_before=supplies;
    for(int i=0;i<30;i++)gevrHordeTick(1);
    assert(supplies==ammo_before); /* no intermission resupply */
    /* Mystery Box roll and claim at pad 1 once player has >= 950 pts. */
    HordeState rich=*gevrHordeState();rich.revision++;rich.credits[0]=2000;
    gevrHordeApplyState(&rich);
    player_prop.pos.x=2000;
    assert(gevrHordeOnUse() && gevrHordeState()->box_state==HORDE_BOX_ROLLING);
    for(int i=0;i<12;i++)gevrHordeTick(15);
    assert(gevrHordeState()->box_state==HORDE_BOX_READY);
    assert(gevrHordeOnUse() && gevrHordeState()->box_state==HORDE_BOX_IDLE);
    gevrHordeTick(1);assert(gevrHordeState()->grant_seq[0]==2);
    player_prop.pos.x=0;
    gevrHordeDefeat();HordeState ended=*gevrHordeState();gevrHordeTick(15);
    assert(!memcmp(&ended,gevrHordeState(),sizeof(ended)));
    /* Synthetic 45-minute run: repeated deaths/replacement must recycle the
     * same actor storage and keep every live wave under its concurrent cap. */
    int previous_markers=marker_count;
    memset(actors,0,sizeof(actors));gevrHordeStageLoaded();
    for(int frame=0;frame<45*60*4;frame++) {
        gevrHordeTick(15);
        assert(gevrHordeState()->alive<=hordeCapacity(1));
        for(int i=0;i<HORDE_GUARD_SLOTS;i++)if(actors[i].model) {
            if(actors[i].hidden&CHRHIDDEN_REMOVE)actors[i].model=NULL;
            else if(frame%3==0 && actors[i].actiontype!=ACT_DEAD) {
                /* every other death is the player's (paid), the rest no one's (not paid) */
                if(i%2==0) gevrHordeNotifyGuardHit(&actors[i],0,HIT_PELVIS);
                actors[i].actiontype=ACT_DEAD;
            }
        }
    }
    assert(gevrHordeState()->completed>30 && gevrHordeState()->kills>1000 && gevrHordeState()->credits[0]>20000
        && gevrHordeState()->credits[0]<(HORDE_START_CREDITS+gevrHordeState()->kills*HORDE_KILL_CREDITS));
    assert(marker_count-previous_markers<=7+HORDE_AMMO_DROPS); /* fixed object pool over 45 minutes */
    for(unsigned i=0;i<HORDE_AMMO_DROPS;i++) assert(gevrHordeState()->ammo[i].ticks<=HORDE_AMMO_LIFETIME);
    /* A friendly companion has a network identity but no wave identity. It
     * cannot increase quotas/kills, keep a wave alive or survive all-down defeat. */
    memset(actors,0,sizeof(actors));gevrHordeStageLoaded();
    actors[63].model=&models[63];actors[63].prop=&props[63];actors[63].actiontype=ACT_STAND;
    gevrHordeSetGuardId(&actors[63],GEVR_ALLY_HORDE_ID);
    assert(!gevrHordeGuardId(&actors[63]));
    gevrHordeBecameHost();assert(gevrHordeState()->alive==0 && gevrHordeState()->next_id==1);
    actors[63].actiontype=ACT_DEAD;gevrHordeTick(15);
    assert(!gevrHordeState()->kills && !gevrHordeState()->alive);
    actors[63].actiontype=ACT_STAND;gevrHordeDefeat();assert(gevrHordeDefeated() && allyStops==2);
    /* Owned ammo pools grant once even with multiple guns sharing them.
     * Explosive ammo arrives once per four shared crates, not per kill. */
    owned_items[ITEM_WPPK]=owned_items[ITEM_TT33]=owned_items[ITEM_AK47]=owned_items[ITEM_SHOTGUN]=1;
    owned_items[ITEM_RUGER]=owned_items[ITEM_ROCKETLAUNCH]=1;
    int before_ammo[AMMOTYPE_MAX];memcpy(before_ammo,supply_amount,sizeof(before_ammo));
    HordeState ammo_state=*gevrHordeState();ammo_state.revision++;ammo_state.ammo_grants[0]=4;
    gevrHordeApplyState(&ammo_state);gevrHordeTick(1);
    assert(supply_amount[AMMO_9MM]-before_ammo[AMMO_9MM]==64);
    assert(supply_amount[AMMO_RIFLE]-before_ammo[AMMO_RIFLE]==48);
    assert(supply_amount[AMMO_SHOTGUN]-before_ammo[AMMO_SHOTGUN]==16);
    assert(supply_amount[AMMO_MAGNUM]-before_ammo[AMMO_MAGNUM]==12);
    assert(supply_amount[AMMO_ROCKETS]-before_ammo[AMMO_ROCKETS]==1);
    /* Full players leave crates for teammates. */
    ammo_state=*gevrHordeState();ammo_state.revision++;ammo_state.phase=HORDE_PREPARE;
    ammo_state.ammo[0]=(HordeAmmoDrop){99999,100,{player_prop.pos.x,0,player_prop.pos.z}};
    gevrHordeApplyState(&ammo_state);ammo_full=1;path_clear=1;
    gevrHordeTick(1);assert(gevrHordeState()->ammo[0].id==99999);
    ammo_full=0;gevrHordeTick(1);assert(!gevrHordeState()->ammo[0].id);
    puts("PASS: production Horde setup, safe pads, armed spawn publication, hit/headshot points, Mystery Box, Wall Buys, slot pressure, takeover reconciliation, death idempotency, despawn replacement, bounded shared ammo drops, doorway recovery, defeat and the companion kept out of wave accounting");
    puts("PASS: 45 simulated minutes of wave progression with bounded/recycled actors");
}
