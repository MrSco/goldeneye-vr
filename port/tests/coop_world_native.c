/* Mission effects with the game's records and serializer; rendering is stubbed. */
#include "net/netenet.h"
#include "net_core.h"
#include "net_coop.h"
#include "aicommands2.h"
#include "game/explosion.h"
#define MAX_WEAPON_SLOTS 30
#define MAX_AMMO_CRATES 20
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define COOP_LOG(...) ((void)0)
#define NET_LOG(...) ((void)0)
#define COOP_MISSION_CHECK_US 250000ull
#define COOP_MISSION_REFRESH_US 2000000ull
#define PLAYER_TICKEXPLODE_FACTOR 15
#define RANDOMGETNEXT_F32() 0.5f

static int host = 1, active = 1, local = 2;
static NetState s_state = NET_STATE_INGAME;
static int s_local_slot = 2, s_host_slot = 2;
static NetMatchConfig config = {.stage = LEVELID_CONTROL};
static u32 epoch = 123;
s32 g_GlobalTimer = 600;
static u8 packet[1024];
static unsigned packet_size, sends, snapshots;
static unsigned char saved[512][1024];
static unsigned saved_sizes[512];
static int saved_count;
stagesetup g_CurrentSetup;
WeaponObjRecord g_WeaponSlots[MAX_WEAPON_SLOTS];
AmmoCrateRecord g_AmmoCrates[MAX_AMMO_CRATES];
static struct { ObjectRecord glass, tinted, prop; PropDefHeaderRecord end; } setup;
static PropRecord glass_prop, tinted_prop, object_prop, monitor_prop;
static MonitorObjRecord monitor;
static struct ModelRoData_BoundingBoxRecord bounds;
static int shards, freed, damage_calls, monitor_calls, audio_calls, music_calls, moves;
static f32 last_damage, fog;
static Mtxf identity;
static PadRecord pads[1];
static BoundPadRecord boundpads[1];
static int g_MaxNumRooms = 100;
static StandTile floor_tile;
struct player { PropRecord *prop; };
static struct player player;
static PropRecord player_prop;
struct player *g_CurrentPlayer = &player;
struct player *g_playerPointers[MAX_PLAYER_COUNT];
static s32 g_PlayerTickExplodeCreatePosition, g_PlayerInvincible;
static s32 g_gevrNetExplosionRx;
static ObjectRecord *g_gevrExplodingObj;
s_explosiontype g_ExplosionTypes[32];
static int created, last_owner, guard_explosive, guard_blast;
static PropRecord *active_prop;
static s32 clock_drawn_flag, clock_enable;
static f32 clock_time;
static s32 g_SurroundBondWithExplosionsFlag, g_SurroundBondWithExplosionsTicks;
static f32 toxic_gas_sound_timer, gasTimeToFullOpacity;
static s32 activate_gas_sound_timer;
static u32 gasDoesDamageFlag;
static coord3d gasLeakSource;
static sfxRecord sfx_related[8];

bool netIsHost(void) { return host; }
bool netIsActive(void) { return active; }
int netCoopActive(void) { return active; }
int gevrCoopHostGuards(void) { return active && host; }
int gevrCoopPuppets(void) { return active && !host; }
int netGetLocalSlot(void) { return local; }
u32 netGetWorldEpoch(void) { return epoch; }
const NetMatchConfig *netGetActiveMatchConfig(void) { return &config; }
bool netSlotOwned(int slot) { return slot == local; }
int gevrNetOwnsSlot(int slot) { return slot == local; }
bool netSlotOccupied(int slot) { return slot >= 0 && slot < MAX_PLAYER_COUNT; }
s32 gevrCoopGuardExplosive(ObjectRecord *obj) { (void)obj; return guard_explosive; }
s32 gevrCoopGuardBlastNow(void) { return guard_blast; }
void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; (void)fmt; }
static void capture(const void *data, unsigned size) { assert(size <= sizeof(packet)); memcpy(packet, data, size); packet_size = size; sends++; }
void netCoopBroadcast(const u8 *data, u32 size, int reliable) { assert(reliable); capture(data, size); }
void netCoopSendTo(int slot, const u8 *data, u32 size) { assert(slot == 1); capture(data, size); saved_sizes[saved_count] = size; memcpy(saved[saved_count++], data, size); }
static void netBroadcastBuf(struct netbuf *b, int channel, int flags, void *peer) { (void)channel; (void)flags; (void)peer; capture(b->data, b->wp); }
static void netBroadcastPacket(const void *data, unsigned size, int channel, int flags, void *peer) { (void)channel; (void)flags; (void)peer; capture(data, size); }
ENetPacket *enet_packet_create(const void *data, size_t size, u32 flags) { (void)flags; capture(data, size); snapshots++; return NULL; }
int enet_peer_send(ENetPeer *peer, u8 channel, ENetPacket *p) { (void)peer; (void)channel; (void)p; return 0; }
static bool netAcceptPlayerEvent(ENetPeer *peer, int slot) { (void)peer; return slot != local; }
bool netSlotIsSpectator(int slot) { (void)slot; return false; }
s32 sizepropdef(PropDefHeaderRecord *def) { return def->type == PROPDEF_END ? 1 : sizeof(ObjectRecord) / sizeof(*def); }
void *setupGetPtrToCommandByIndex(s32 index) { if (index == 0) return &setup.glass; if (index == 1) return &setup.tinted; if (index == 2) return &setup.prop; return NULL; }
ObjectRecord *objFindByTagId(s32 tag) { return tag == 7 ? &setup.glass : tag == 8 ? &setup.prop : tag == 9 ? (ObjectRecord *)&monitor : NULL; }
void chrpropEnable(PropRecord *p) { p->flags |= PROPFLAG_ENABLED; }
void chrpropDisable(PropRecord *p) { p->flags &= ~PROPFLAG_ENABLED; }
void objFreePermanently(ObjectRecord *obj, s32 freeprop) { (void)freeprop; freed++; obj->prop = NULL; }
static void netTakeSpecialItem(int slot, int item) { (void)slot; (void)item; }
void propExecuteTickOperation(PropRecord *p, TICKOP op) { (void)p; (void)op; }
void gevrNetDoorApply(PropRecord *p, PropRecord *by, s32 state) { (void)p; (void)by; (void)state; }
void doorUpdateBbox(DoorRecord *door) { (void)door; }
void doorBuildClippedVertices(DoorRecord *door) { (void)door; }
void explosionClearBulletImpactRoomByFlag(PropRecord *p, s8 flag) { (void)p; (void)flag; }
struct ModelRoData_BoundingBoxRecord *chrobjGetBboxFromObjectRecord(ObjectRecord *obj) { (void)obj; return &bounds; }
void sub_GAME_7F0A1DA0(f32 *a, f32 *b, f32 *c, f32 *d, f32 e, f32 f, f32 g, f32 h, f32 i, f32 j) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j; shards++; }
s32 objGetDestroyedLevel(ObjectRecord *obj) { return !!(obj->state & PROPSTATE_DESTROYED); }
void sub_GAME_7F04DCB4(ObjectRecord *obj);
void objApplyDamage(ObjectRecord *obj, f32 damage, coord3d *pos, s32 item, s32 owner) { (void)pos;(void)item; assert(owner == -1); damage_calls++; last_damage = damage; if(obj->type == PROPDEF_GLASS) sub_GAME_7F04DCB4(obj); else obj->state |= PROPSTATE_DESTROYED; }
void matrix_4x4_set_basis_and_position_target(Mtxf *m, f32 a, f32 b, f32 c, f32 d, f32 e, f32 f, f32 g, f32 h, f32 i) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i; *m = identity; }
void matrix_scalar_multiply(f32 scale, Mtxf *m) { (void)scale;(void)m; }
void sub_GAME_7F04088C(ObjectRecord *obj, coord3d *pos, Mtxf *m, StandTile *stan, coord3d *runtime) { obj->prop->pos = *pos; obj->runtime_pos = *runtime; obj->mtx = *m; obj->prop->stan = stan; moves++; }
void setupUpdateObjectRoomPosition(ObjectRecord *obj) { (void)obj; }
void monitorSetImageByNum(MonitorRecord *mon, s32 bank) { mon->offset = bank; monitor_calls++; }
void fogSwitchToSolosky2(f32 fraction) { fog = fraction; }
void sub_GAME_7F03FDA8(PropRecord *p) { (void)p; }
void projectileSetSticky(PropRecord *p) { (void)p; }
void matrix_4x4_set_identity(Mtxf *m) { *m = identity; }
void audioPlayFromProp(s32 channel, s32 sound) { (void)channel;(void)sound; audio_calls++; }
void sub_GAME_7F0349BC(s32 channel) { (void)channel; audio_calls++; }
void audioPlayFromProp2(s32 slot) { (void)slot; audio_calls++; }
s16 sub_GAME_7F0539B8(f32 volume) { return (s16)volume; }
void musicPlaySlot(s32 slot, s32 pause, s32 duration) { (void)slot;(void)pause;(void)duration; music_calls++; }
void musicStopSlot(s32 slot) { (void)slot; music_calls++; }
PropRecord *chrpropGetActiveTail(void) { return active_prop; }
StandTile *stanFindTileBelowPos(coord3d *pos, StandTile *stan, f32 *y) { (void)pos;(void)stan; *y = 0; return &floor_tile; }
s32 getTileRoom(StandTile *stan) { (void)stan; return 1; }
u32 randomGetNext(void) { return 1; }
static s32 gevrIsExplosiveItem(s32 item) { return item == ITEM_GRENADE; }
s32 chraiitemsize(u8 *list, s32 offset);
int netPlayersWereTicked(void) { return 1; }
void ai(PropDefHeaderRecord *entity, PROP_TYPE type);

/* INSERT_HEADER */
/* INSERT_EFFECTS */
/* INSERT_OBJECTS */
/* INSERT_GLASS */
/* INSERT_SEND_BLAST */

void explosionCreate(PropRecord *arg0, coord3d *target_pos, StandTile *stan, s16 explosion_type, s32 arg4, s32 player, u8 *rooms, s32 arg7)
{
    s_explosiontype *sp44 = &g_ExplosionTypes[explosion_type];
    (void)stan;
    /* INSERT_CREATE_BLAST */
    created++; last_owner = player;
}
/* INSERT_RECEIVE_BLAST */
/* INSERT_TICK_BLAST */

void ai(PropDefHeaderRecord *entity, PROP_TYPE type)
{
    assert(type == PROP_TYPE_CHR);
    AIRecord *AiListp = ((ChrRecord *)entity)->ailist;
    s32 Offset = 0;
    gevrCoopScriptEffect(AiListp, Offset);
    switch (AiListp->cmd) {
        /* INSERT_AI_CASES */
        default: assert(0);
    }
    assert(AiListp[Offset].cmd == AI_EndList);
}

s32 chraiitemsize(u8 *list, s32 offset)
{
    switch(list[offset]) {
        case AI_DestroyObject: case AI_ObjectRocketLaunch: case AI_SfxStopChannel: case AI_MusicStopSlot: return 2;
        case AI_MoveObject: case AI_TvChangeScreenBank: case AI_SfxPlay: case AI_MusicPlaySlot: return 4;
        case AI_SetObjectFlags: case AI_UnsetObjectFlags: case AI_SetObjectFlags2: case AI_UnsetObjectFlags2:
        case AI_SfxSetChannelVolume: case AI_SfxFadeChannelVolume: case AI_SfxEmitFromPad: return 6;
        case AI_SfxEmitFromObject: return 5;
        default: return 1;
    }
}

static void receive_world(const u8 *data, unsigned size, ENetPeer *peer, ENetPeer *s_server_peer)
{
    struct netbuf buf;
    netbufStartReadData(&buf, data, size);
    netbufReadU32(&buf); netbufReadU16(&buf);
    u8 type = netbufReadU8(&buf), slot_id = netbufReadU8(&buf);
    switch(type) {
        /* INSERT_RECEIVE_OBJECT */
        /* INSERT_RECEIVE_SNAPSHOT */
    }
}
static void receive_clock(void) { struct netbuf b; netbufStartReadData(&b,packet+8,packet_size-8); coopReceiveClock(&b); }
static void receive_script(const u8 *data, unsigned size) { struct netbuf b; netbufStartReadData(&b,data+8,size-8); coopReceiveScript(&b); }
static void init_objects(void)
{
    memset(&setup, 0, sizeof(setup));
    memset(&glass_prop, 0, sizeof(glass_prop));
    memset(&tinted_prop, 0, sizeof(tinted_prop));
    memset(&object_prop, 0, sizeof(object_prop));
    setup.glass.type = PROPDEF_GLASS; setup.glass.prop = &glass_prop;
    setup.tinted.type = PROPDEF_TINTED_GLASS; setup.tinted.prop = &tinted_prop;
    setup.prop.type = PROPDEF_PROP; setup.prop.prop = &object_prop;
    setup.end.type = PROPDEF_END;
    setup.glass.damage = 1000;
    glass_prop.type = tinted_prop.type = object_prop.type = PROP_TYPE_OBJ;
    glass_prop.flags = tinted_prop.flags = PROPFLAG_ENABLED;
    g_CurrentSetup.propDefs = (PropDefHeaderRecord *)&setup;
    g_CurrentSetup.pads = pads; g_CurrentSetup.boundpads = boundpads;
    memset(&monitor,0,sizeof(monitor)); monitor.type=PROPDEF_MONITOR; monitor.prop=&monitor_prop;
    shards = damage_calls = freed = 0;
}
static void test_clock(void)
{
    host=1; coopClockReset(); clock_time = 3600; clock_enable = 1; clock_drawn_flag = 8;
    g_SurroundBondWithExplosionsFlag = 1; g_SurroundBondWithExplosionsTicks = 630;
    toxic_gas_sound_timer=1800; gasTimeToFullOpacity=3600; gasDoesDamageFlag=1; activate_gas_sound_timer=1;
    gasLeakSource=(coord3d){12,34,56};
    unsigned before=sends;
    coopHostClock(1000000); assert(sends == before+1 && packet[6] == NET_MSG_COOP_CLOCK);
    host=0; clock_time=0; clock_enable=0; clock_drawn_flag=1|4|8;
    toxic_gas_sound_timer=0; g_GlobalTimer=900;
    receive_clock();
    assert(clock_time==3600 && clock_enable==1 && clock_drawn_flag==(4|8));
    assert(toxic_gas_sound_timer==1800 && gasDoesDamageFlag && activate_gas_sound_timer);
    assert(gasLeakSource.x==12 && g_SurroundBondWithExplosionsTicks==930);
    epoch++; clock_time=77; receive_clock(); assert(clock_time==77); epoch--;
    config.stage++; receive_clock(); assert(clock_time==77); config.stage--;
    packet_size--; receive_clock(); assert(clock_time==77); packet_size++;
    float bad=NAN; memcpy(packet+13,&bad,4); receive_clock(); assert(clock_time==77);
    host=1; clock_time=900; clock_enable=0; clock_drawn_flag=1|16; toxic_gas_sound_timer=0;
    activate_gas_sound_timer=0; g_SurroundBondWithExplosionsFlag=0;
    coopHostClock(2000000); host=0; clock_drawn_flag=8; receive_clock();
    assert(clock_time==900 && !clock_enable && clock_drawn_flag==(1|8|16));
    host=1; before=sends; coopHostClock(2100000); assert(sends==before);
    coopClockReset(); coopHostClock(3000000); assert(sends==before+1); /* drop-in refresh */
}
static void test_glass(void)
{
    init_objects(); host=1; local=s_local_slot=2;
    sub_GAME_7F04DCB4(&setup.glass); assert(shards==1 && packet[6]==NET_MSG_OBJECT_STATE);
    u8 event[64]; unsigned len=packet_size; memcpy(event,packet,len);
    init_objects(); host=0; local=1;
    receive_world(event,len,NULL,NULL); assert(shards==1 && (setup.glass.state & PROPSTATE_DESTROYED));
    receive_world(event,len,NULL,NULL); assert(shards==1); /* already shattered */
    init_objects(); event[7]=3; receive_world(event,len,NULL,NULL); assert(shards==0); /* not host */
    event[7]=2; host=1; receive_world(event,len,NULL,NULL); assert(shards==0); /* clients can't ask host */
    host=1; local=s_local_slot=2; sub_GAME_7F04DCB4(&setup.glass); objFreePermanently(&setup.glass,1);
    netSendWorldSnapshot(NULL); u8 snap[512]; unsigned snap_len=packet_size; memcpy(snap,packet,snap_len);
    init_objects(); host=0; local=1; receive_world(snap,snap_len,NULL,NULL);
    assert(setup.glass.prop==NULL && shards==0 && setup.tinted.prop!=NULL);
    init_objects(); host=1; local=s_local_slot=2; sub_GAME_7F04DCB4(&setup.tinted); netSendWorldSnapshot(NULL);
    snap_len=packet_size; memcpy(snap,packet,snap_len); init_objects(); host=0; local=1; receive_world(snap,snap_len,NULL,NULL);
    assert((setup.tinted.runtime_bitflags & RUNTIMEBITFLAG_REMOVE) && (setup.tinted.state & PROPSTATE_DESTROYED));
    assert(shards==0);
}
static u8 mission[] = {
    AI_SetObjectFlags2,7,0,0,0,4,
    AI_UnsetObjectFlags2,7,0,0,0,4,
    AI_DestroyObject,7,
    AI_MoveObject,8,0,0,
    AI_SfxPlay,0,1,0,
    AI_MusicPlaySlot,0,0,1,
    AI_SwitchSky,
    AI_TvChangeScreenBank,9,0,2,
    AI_SetObjectFlags,7,0,0,0,8,
    AI_UnsetObjectFlags,7,0,0,0,8,
    AI_EndList
};
static AIListRecord lists[2];
static void test_scripts(void)
{
    init_objects(); lists[0].ID=0x400; lists[0].ailist=(AIRecord *)mission; lists[1].ailist=NULL;
    g_CurrentSetup.ailists=lists; coopScriptReset(); host=1; local=s_local_slot=2;
    gevrCoopScriptEffect((AIRecord *)mission,0); assert(packet[6]==NET_MSG_COOP_SCRIPT);
    u8 event[64]; unsigned len=packet_size; memcpy(event,packet,len); host=0;
    receive_script(event,len); assert(setup.glass.flags2==4);
    /* The instruction boundary, whitelist, stage, epoch and entire packet are validated. */
    event[15]=1; receive_script(event,len); assert(setup.glass.flags2==4); event[15]=0;
    event[8]++; setup.glass.flags2=0; receive_script(event,len); assert(!setup.glass.flags2); event[8]--;
    epoch++; receive_script(event,len); assert(!setup.glass.flags2); epoch--;
    receive_script(event,len-1); assert(!setup.glass.flags2);
    host=1; gevrCoopScriptEffect((AIRecord *)mission,6); host=0; receive_script(packet,packet_size); assert(!setup.glass.flags2);
    host=1; gevrCoopScriptEffect((AIRecord *)mission,12); host=0; receive_script(packet,packet_size);
    assert(damage_calls==1 && shards==1 && last_damage>0); receive_script(packet,packet_size); assert(damage_calls==1);
    pads[0].pos=(coord3d){100,200,300};
    host=1; gevrCoopScriptEffect((AIRecord *)mission,14); host=0; receive_script(packet,packet_size);
    assert(moves==1 && setup.prop.prop->pos.x==100);
    host=1; gevrCoopScriptEffect((AIRecord *)mission,18); host=0; receive_script(packet,packet_size); assert(audio_calls==1);
    host=1; gevrCoopScriptEffect((AIRecord *)mission,22); host=0; receive_script(packet,packet_size); assert(music_calls==1);
    host=1; gevrCoopScriptEffect((AIRecord *)mission,26); host=0; receive_script(packet,packet_size); assert(fog==1);
    host=1; gevrCoopScriptEffect((AIRecord *)mission,27); host=0; receive_script(packet,packet_size);
    assert(monitor_calls==1 && monitor.Monitor.offset==2);
    host=1; gevrCoopScriptEffect((AIRecord *)mission,31); host=0; receive_script(packet,packet_size); assert(setup.glass.flags==8);
    host=1; gevrCoopScriptEffect((AIRecord *)mission,37); host=0; receive_script(packet,packet_size); assert(setup.glass.flags==0);
    assert(s_script_count==8); /* durable effects only; audio excluded */
    host=1; gevrCoopScriptEffect((AIRecord *)mission,0); memcpy(event,packet,packet_size); host=0;
    event[15]=43; receive_script(event,packet_size); assert(s_script_count==8); /* EndList rejected */
    event[15]=0; event[13]++; receive_script(event,packet_size); assert(s_script_count==8); /* unknown list */
    host=1; gevrCoopScriptEffect((AIRecord *)mission,0); /* repeated instruction moves to the end */
    assert(s_script_count==8 && s_script_history[7].offset==0);
    saved_count=0; coopSendScriptSnapshot(1); assert(saved_count==8);
    init_objects(); coopScriptReset(); host=0;
    for(int i=0;i<saved_count;i++) receive_script(saved[i],saved_sizes[i]);
    assert((setup.glass.state & PROPSTATE_DESTROYED) && setup.glass.flags2==4);
    assert(setup.prop.prop->pos.x==100 && audio_calls==1 && music_calls==1);
    coopScriptReset(); assert(s_script_count==0);
    for (int i=0;i<600;i++) coopRememberScript(0x400,(u16)i);
    assert(s_script_count==COOP_SCRIPT_HISTORY_MAX && s_script_history[0].offset==88);
    coopRememberScript(0x400,500); assert(s_script_history[511].offset==500);
    coopScriptReset();
}
static void test_blasts(void)
{
    player.prop=&player_prop; player_prop.stan=&floor_tile; player_prop.rooms[0]=1; player_prop.rooms[1]=0xff;
    for(int i=0;i<MAX_PLAYER_COUNT;i++) g_playerPointers[i]=&player;
    g_ExplosionTypes[EXPLOSION_DEF_PLAYER].damage=1;
    g_SurroundBondWithExplosionsFlag=1; g_SurroundBondWithExplosionsTicks=0; g_GlobalTimer=1000;
    host=0; local=1; unsigned before=sends; created=0;
    bondviewPlayerTickExplode(); assert(!created && sends==before);
    host=1; local=s_local_slot=2; bondviewPlayerTickExplode();
    assert(created==1 && last_owner==-1 && sends==before+1 && (packet[10]&4));
    host=0; local=1; before=sends;
    explosionCreate(NULL,&player_prop.pos,&floor_tile,EXPLOSION_DEF_PLAYER,0,-1,player_prop.rooms,0);
    assert(created==1 && sends==before); /* replay updates objects without duplicating their blast */
    WeaponObjRecord *grenade=&g_WeaponSlots[0]; PropRecord grenade_prop={0};
    grenade_prop.type=PROP_TYPE_WEAPON; grenade_prop.weapon=grenade; grenade->prop=&grenade_prop;
    grenade->weaponnum=ITEM_GRENADE; grenade->runtime_bitflags=RUNTIME_OWNER_BITS(2);
    active_prop=&grenade_prop; int old_freed=freed;
    gevrNetExplosionReceive(2,EXPLOSION_DEF_PLAYER,&player_prop.pos,1,0,0,1);
    assert(created==2 && last_owner==-1 && sends==before && freed==old_freed);
    gevrNetExplosionReceive(2,EXPLOSION_DEF_PLAYER,&player_prop.pos,1,0,0,0);
    assert(created==3 && last_owner==2 && sends==before && freed==old_freed+1);
    active=0; created=0; g_SurroundBondWithExplosionsTicks=0;
    bondviewPlayerTickExplode(); assert(created==1 && last_owner==0 && sends==before); /* solo */
}
int main(void)
{
    test_clock(); test_glass(); test_scripts(); test_blasts();
    puts("PASS: countdown/gas, script validation and drop-in replay, glass events/snapshots, world blast ownership and deduplication");
    return 0;
}
