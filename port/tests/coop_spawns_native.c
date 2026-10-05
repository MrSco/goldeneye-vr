#include "net_core.h"
#include "net_coop.h"
#include "net_protocol.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/player.h"
#include "game/model.h"
#include "game/stan.h"
#include "aicommands2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* INSERT_TYPES */
#define SLOTS 96
#define GUARDS 40
typedef struct {
    ChrRecord slots[SLOTS];
    PropRecord props[SLOTS];
    Model models[SLOTS];
    coord3d positions[SLOTS];
    f32 yaw[SLOTS];
    u8 guns[SLOTS][2];
    CoopSent sent[COOP_MAX_SLOTS];
    CoopPuppet puppet[COOP_MAX_SLOTS];
    s16 local_of[COOP_MAX_SLOTS], host_of[COOP_MAX_SLOTS];
    bool spawned[COOP_MAX_SLOTS];
    s32 spawn_flags[COOP_MAX_SLOTS];
    struct { bool valid; } ai[COOP_MAX_SLOTS];
    CoopSpawn pending[COOP_PENDING_SPAWNS];
    int pending_count, allocations;
    bool await_remap;
    u64 last_send;
    u32 sequence;
} World;
static World worlds[4], *world;
#define s_sent (world->sent)
#define s_puppet (world->puppet)
#define s_local_of (world->local_of)
#define s_host_of (world->host_of)
#define s_spawned (world->spawned)
#define s_spawn_flags (world->spawn_flags)
#define s_ai (world->ai)
#define s_pending_spawn (world->pending)
#define s_pending_spawns (world->pending_count)
#define s_await_remap (world->await_remap)
#define s_last_send_us (world->last_send)
#define s_state_seq (world->sequence)
#define COOP_LOG(...) ((void)0)

ChrRecord *g_ChrSlots, *g_ActiveChrs;
s32 g_NumChrSlots=SLOTS, g_ActiveChrsCount, g_GlobalTimer, g_ClockTimer;
struct player *g_CurrentPlayer;
int VrFastReinforcements;
static bool host_fast;
static u64 now=1000000;
static ModelAnimation animation;
static StandTile tile;
static struct { u8 data[1100]; u32 size; int reliable; } packets[1024];
static int packet_count;
void sysLogPrintf(int level,const char *format,...) {}

bool netIsActive(void) { return true; }
bool netIsHost(void) { return world==&worlds[0]; }
int netCoopActive(void) { return 1; }
int gevrCoopHostGuards(void) { return netIsHost(); }
int gevrCoopFastReinforcements(void) { return host_fast; }
int netGetLocalSlot(void) { return (int)(world-worlds); }
s32 getPlayerCount(void) { return 4; }
s32 gevrBodyRetireForAi(ChrRecord *chr) { return FALSE; } /* online corpses use ordinary cleanup */
u64 sysGetMicroseconds(void) { return now; }
static void coopHostMission(u64 t) {}
static void coopHostDropIn(u64 t) {}
static void coopCinemaHostTick(u64 t) {}

static void select_world(int index) { world=&worlds[index];g_ChrSlots=world->slots; }
static int model_slot(Model *model) { return (int)(model-world->models); }
static ChrRecord *make_chr(int slot,int id)
{
    ChrRecord *chr=&world->slots[slot];memset(chr,0,sizeof(*chr));
    chr->chrnum=id;chr->model=&world->models[slot];chr->prop=&world->props[slot];
    chr->prop->chr=chr;chr->prop->type=PROP_TYPE_CHR;chr->prop->stan=&tile;
    chr->bodynum=1;chr->headnum=2;chr->fadealpha=255;chr->maxdamage=100;
    chr->actiontype=ACT_STAND;chr->model->anim=&animation;chr->model->speed=1;
    return chr;
}
static void reset_world(int index,int parent_slot)
{
    select_world(index);memset(world,0,sizeof(*world));
    for(int i=0;i<COOP_MAX_SLOTS;i++)s_local_of[i]=s_host_of[i]=-1;
    ChrRecord *parent=make_chr(parent_slot,5);parent->chrflags=CHRFLAG_CLONE;
    s_local_of[0]=parent_slot;s_host_of[parent_slot]=0;
}
s32 chrGetNumFree(void) { int n=0;for(int i=0;i<SLOTS;i++)if(!g_ChrSlots[i].model)n++;return n; }
PropRecord *chrAllocate(Model *header,coord3d *pos,f32 angle,StandTile *stan,AIRecord *list)
{
    for(int i=0;i<SLOTS;i++)if(!g_ChrSlots[i].model) {
        ChrRecord *chr=make_chr(i,5000+i);chr->prop->pos=*pos;world->positions[i]=*pos;
        world->yaw[i]=angle;chr->ailist=list;world->allocations++;return chr->prop;
    }
    assert(!"allocator called with no free slot");return NULL;
}
Model *retrieve_header_for_body_and_head(s32 body,s32 head,u32 flags) { return &world->models[0]; }
StandTile *stanFindTileBelowPos(coord3d *pos,u8 *rooms,f32 *height) { *height=0;return &tile; }
void chrpropActivateThisFrame(PropRecord *prop) {}
void chrpropEnable(PropRecord *prop) {}
void chrStopFiring(ChrRecord *chr) {}
void chrlvMergeKneelToStand(ChrRecord *chr,f32 merge) {}
void chrDetectRooms(ChrRecord *chr) {}
void chrUpdateAnim(ChrRecord *chr,s32 ticks) {}
void modelTickAnim(Model *model,s32 ticks,s32 update) {}
void modelSetAnimation(Model *model,ModelAnimation *anim,s32 flip,f32 frame,f32 speed,f32 merge) {
    model->anim=anim;model->gunhand=flip;model->animframe1=frame;model->speed=speed;
}
void modelSetAnimSpeed(Model *model,f32 speed,f32 merge) { model->speed=speed; }
void getsuboffset(Model *model,coord3d *pos) { *pos=world->positions[model_slot(model)]; }
void setsuboffset(Model *model,coord3d *pos) { world->positions[model_slot(model)]=*pos; }
f32 getsubroty(Model *model) { return world->yaw[model_slot(model)]; }
void setsubroty(Model *model,f32 angle) { world->yaw[model_slot(model)]=angle; }
void subcalcpos(Model *model) {}
s32 walkTilesBetweenPoints_NoCallback(StandTile **stan,f32 x,f32 z,f32 nx,f32 nz) { return TRUE; }
void sub_GAME_7F02BFE4(ChrRecord *chr,s32 hand,s32 firing) {}
void chrSetFiring(ChrRecord *chr,s32 hand,s32 firing) {}
static u16 coopAnimId(const ModelAnimation *anim) { return anim ? 1 : NET_CHR_NO_ANIM; }
static ModelAnimation *coopAnimById(u16 id) { return id==1 ? &animation : NULL; }
static u8 coopWeaponOf(ChrRecord *chr,s32 hand) { return world->guns[chr-g_ChrSlots][hand]; }
static void coopSyncHand(ChrRecord *chr,s32 hand,u8 item,bool dying) { world->guns[chr-g_ChrSlots][hand]=item; }
PropRecord *chrGetEquippedWeaponProp(ChrRecord *chr,GUNHAND hand) { return NULL; }
PropRecord *chrGiveWeapon(ChrRecord *chr,s32 model,ITEM_IDS item,s32 flags) { return NULL; }
void propweaponSetDual(WeaponObjRecord *left,WeaponObjRecord *right) {}
PropRecord *hatCreateForChr(ChrRecord *chr,s32 model,u32 flags) { return NULL; }
AIRecord *ailistFindById(s32 id) { return NULL; }
s32 chraiGetAIListID(AIRecord *list,s32 *global) { return -1; }
static s32 chraiGoToLabel(void *list,s32 offset,u8 label) { return 1000+label; }
static void coopWriteSpawn(struct netbuf *,ChrRecord *,s32,AIRecord *,s32);
void netCoopBroadcast(const unsigned char *data,unsigned int size,int reliable) {
    assert(netIsHost() && packet_count<1024 && size<=1100);
    memcpy(packets[packet_count].data,data,size);packets[packet_count].size=size;
    packets[packet_count++].reliable=reliable;
}

/* INSERT_LOOKUPS */
/* INSERT_NETWORK */

PropRecord *chrSpawnAtChr(ChrRecord *self,s32 body,s32 head,s32 id,AIRecord *list,s32 flags)
{
    assert(netIsHost());
    if(chrGetNumFree()<3)return NULL;
    PropRecord *prop=chrAllocate(NULL,&self->prop->pos,0,&tile,list);
    gevrCoopChrSpawned(prop->chr,list,flags);return prop;
}
static s32 run_ai(ChrRecord *ChrEntityp,void *AiListp)
{
    s32 Offset=0;switch(*(u8 *)AiListp) {
        /* INSERT_AI_EXISTENCE */
        /* INSERT_AI_CLONE */
    }return Offset;
}
static bool gone(ChrRecord *parent) {
    AiIFChrDoesNotExistRecord ai={.cmd=AI_IFChrDoesNotExist,.CHR_NUM=(u8)CHR_CLONE,.GOTOLABEL=7};
    return run_ai(parent,&ai)==1007;
}
static void spawn(ChrRecord *parent) {
    AiTRYCloningChrRecord ai={.cmd=AI_TRYCloningChr,.CHR_NUM=(u8)CHR_SELF,.AI_LIST_ID=0,.GOTOLABEL=7};
    assert(run_ai(parent,&ai)==1007);
}
static void receive_all(int client)
{
    select_world(client);
    for(int i=0;i<packet_count;i++) {
        struct netbuf b;netbufStartReadData(&b,packets[i].data,packets[i].size);
        assert(netbufReadU32(&b)==GEVR_NET_MAGIC && netbufReadU16(&b)==GEVR_NET_VERSION);
        u8 type=netbufReadU8(&b);assert(netbufReadU8(&b)==0);
        if(type==NET_MSG_CHR_SPAWN) { assert(packets[i].reliable);coopReceiveSpawn(&b); }
        else { assert(type==NET_MSG_CHR_STATE && !packets[i].reliable);coopReceiveStates(&b); }
        assert(!b.error);
    }
}
static void compare(int client)
{
    select_world(client);assert(world->allocations==GUARDS);
    for(int h=1;h<=GUARDS;h++) {
        ChrRecord *chr=coopLocalChr(h), *original=&worlds[0].slots[h];assert(chr && chr->model);
        assert(chr->chrnum==original->chrnum && netCoopHostSlotOf(chr-g_ChrSlots)==h);
        netCoopPuppetTick(chr,1);
        assert(chr->prop->pos.x==original->prop->pos.x && chr->prop->pos.z==original->prop->pos.z);
        assert(chr->fadealpha==original->fadealpha && chr->damage==original->damage);
        assert(chr->model->anim==original->model->anim && chr->model->animframe1==original->model->animframe1);
        assert(world->guns[chr-g_ChrSlots][0]==worlds[0].guns[h][0]);
    }
}
int main(void)
{
    reset_world(0,0);reset_world(1,0);reset_world(2,7);reset_world(3,3);
    select_world(1);for(int i=1;i<SLOTS;i++)make_chr(i,20000+i); /* old corpses still occupying slots */
    select_world(0);host_fast=false;ChrRecord *parent=&g_ChrSlots[0];spawn(parent);assert(!gone(parent));
    host_fast=true;
    for(int i=1;i<GUARDS;i++) { assert(gone(parent));spawn(parent); }
    assert(chrFindById(parent,(u8)CHR_CLONE)==&g_ChrSlots[GUARDS]);
    host_fast=false;assert(!gone(parent)); /* host's live Off choice stops further reinforcements */
    for(int c=1;c<4;c++)receive_all(c);
    select_world(1);assert(s_pending_spawns==GUARDS && world->allocations==0);
    for(int i=1;i<SLOTS;i++)g_ChrSlots[i].model=NULL;
    coopRetrySpawns();assert(s_pending_spawns==0 && world->allocations==GUARDS);
    select_world(0);packet_count=0;
    for(int h=1;h<=GUARDS;h++) {
        ChrRecord *chr=&g_ChrSlots[h];chr->prop->pos=(coord3d){1000+h*10,0,800+h*10};
        chr->model->animframe1=20+h;chr->fadealpha=200;chr->damage=25;world->guns[h][0]=ITEM_SNIPERRIFLE;
    }
    netCoopHostTick();assert(packet_count>=2); /* burst crosses the 24-state packet boundary */
    for(int c=1;c<4;c++) {receive_all(c);compare(c);}
    /* A late join starts with a different local layout and gets the final roster IDs. */
    reset_world(3,5);select_world(0);packet_count=0;
    for(int h=1;h<=GUARDS;h++)gevrCoopChrIdentityChanged(&g_ChrSlots[h]);
    now+=2000000;netCoopHostTick();receive_all(3);compare(3);
    receive_all(3);compare(3); /* repeated roster/identity records never duplicate guards */
    puts("PASS: 40 host AI reinforcements on three clients, final IDs, distinct slot maps, reliable spawns, deferred burst, state/animation/damage, late join and duplicate roster");
    return 0;
}
