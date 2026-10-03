/* Production state functions with the socket and game clock stubbed. */
#include "net/netenet.h"
#undef s_host /* Winsock's in_addr byte-field macro */
static void fixtureDamage(uint8_t,uint8_t,uint8_t,float,float,float);
#include "../src/net/net_core.c"
#define EXPORT __declspec(dllexport)
struct player_data g_playerPlayerData[MAX_PLAYER_COUNT];
s32 g_gameOverFlag, D_80048394;
struct player *g_playerPointers[MAX_PLAYER_COUNT];
s32 startpadcount=8;
static unsigned char sent_data[1024];   /* an eight-player match snapshot is 715 bytes */
static size_t sent_size;
s32 get_cur_playernum(void) { return s_local_slot; }
static int respawn_calls;
void set_cur_player(s32 slot) { (void)slot; }
void mp_respawn_handler_net(s32 pad,float theta) { (void)pad;(void)theta;respawn_calls++; }

int bondinvHasInvItem(ITEM_IDS item) { (void)item; return 0; }
int VrMpStage,VrMpWeaponSet,VrMpChr,VrMpScenario,VrMpLength,VrMpHealth;
int VrMpDual,VrMpLoadouts,VrMpNextRound,VrMpCustom[4],VrMpLoadout[4],VrMpVoiceMode,VrMpFriendlyFire,VrMpFunFlags,VrMpGunSize,VrMpMaxPlayers;
int VrHostEqualization=1,VrHostLatencyCapMs=50;
unsigned VrMpFavStages,VrMpFavSets;
char VrPlayerName[16]="Test";
static uint64_t clock_us=10000000;
static int voice_resets;
static struct player hit_players[GEVR_MAX_PLAYERS];
static ENetPeer hit_peers[GEVR_MAX_PLAYERS];
static int damage_count,damage_target[2048],kill_on_damage;
static uint64_t damage_time[2048];
static void fixtureDamage(uint8_t target,uint8_t attacker,uint8_t weapon,float dmg,float vx,float vz) {
    (void)attacker;(void)weapon;(void)dmg;(void)vx;(void)vz;
    damage_target[damage_count]=target;damage_time[damage_count++]=clock_us;
    if(kill_on_damage) { hit_players[target].bonddead=1;s_hit_death_us[target]=clock_us; }
}
uint32_t enet_time_get(void) { return (uint32_t)(clock_us/1000); }
static ObjectRecord test_ammo;
static PropRecord test_ammo_prop;
static int ammo_applied;
WeaponObjRecord g_WeaponSlots[MAX_WEAPON_SLOTS];
AmmoCrateRecord g_AmmoCrates[MAX_AMMO_CRATES];
ObjectRecord *setupGetPtrToCommandByIndex(s32 index) { return index==29 ? &test_ammo : NULL; }
int gevrAmmoNetworked(ObjectRecord *obj) { return obj && obj==&test_ammo; }
ObjectRecord *gevrAmmoAt(int ordinal,int *index) { if(ordinal) return NULL;*index=29;return &test_ammo; }
void gevrAmmoApplyTransform(ObjectRecord *obj,const coord3d *p,const coord3d *runtime,const Mtxf *m,int regen,int enabled,int moving,const coord3d *speed,const Mtxf *rotation) {
    (void)enabled;(void)moving;(void)speed;(void)rotation;ammo_applied++;
    obj->prop->pos=*p;obj->runtime_pos=*runtime;obj->mtx=*m;obj->prop->timetoregen=regen;
}
void sysLogPrintf(s32 level,const char *fmt,...) { (void)level;(void)fmt; }
u64 sysGetMicroseconds(void) { return clock_us; }
void vrSettingsSave(void) {}
void netVoiceForgetSlot(uint8_t slot) { (void)slot;voice_resets++; }
ENetPacket *enet_packet_create(const void *data,size_t size,uint32_t flags) { (void)flags;if(size<=sizeof(sent_data)) { memcpy(sent_data,data,size);sent_size=size; }return NULL; }
int enet_peer_send(ENetPeer *peer,uint8_t channel,ENetPacket *packet) { (void)peer;(void)channel;(void)packet;return 0; }
int enet_address_get_ip(const ENetAddress *address,char *buffer,size_t size) { (void)address;snprintf(buffer,size,"127.0.0.1");return 0; }
static void fixture(int scenario) {
    memset(&s_round,0,sizeof(s_round)); memset(g_playerPlayerData,0,sizeof(g_playerPlayerData));
    s_state=NET_STATE_HOSTING_LOBBY;s_local_slot=s_host_slot=0;netResetLobbyState();
    s_lobby_state.config.scenario=scenario; s_lobby_state.config.stage=34;
    s_lobby_state.config.voice_mode=NET_VOICE_PROXIMITY; s_phase=NET_PHASE_WAITING;
    s_server_peer=NULL;s_host=NULL;s_match_ended=false;s_next_round_at_us=0;s_countdown_end_us=0;voice_resets=0;
    s_round_reset_loading=false;s_waiting_for_match_snapshot=false;
    memset(g_playerPointers,0,sizeof(g_playerPointers));memset(s_client_peers,0,sizeof(s_client_peers));
    for(int i=0;i<4;i++) {s_lobby_state.slots[i].connected=1;s_lobby_state.slots[i].ready=1;s_lobby_state.slots[i].loaded=1;}
}
EXPORT int test_core_teams(void) {
    fixture(5);
    if(netTeamRosterReady()) return 1;
    if(!netSetSlotTeam(0,0) || s_lobby_state.slots[0].ready) return 2;
    if(!netSetSlotTeam(1,0) || netSetSlotTeam(2,0)) return 3;
    if(!netSetSlotTeam(2,1) || !netSetSlotTeam(3,1) || !netTeamRosterReady()) return 4;
    if(netSetSlotTeam(1,255)) return 5;
    netLobbySetReady(true);if(!s_lobby_state.slots[0].ready) return 11;
    netSetSlotTeam(0,NET_TEAM_NONE);netLobbySetReady(true);
    if(s_lobby_state.slots[0].ready) return 12;
    netSetSlotTeam(0,NET_TEAM_RED);netLobbySetReady(true);
    netLatchRoundSettings();s_state=NET_STATE_INGAME;s_phase=NET_PHASE_IN_PROGRESS;
    for(int i=0;i<4;i++) s_lobby_state.slots[i].ready=1;
    if(!netVoiceSameGroup(0,1) || !netVoiceSameGroup(0,2) ||
       netVoiceModeForPair(0,1)!=NET_VOICE_COUCH || netVoiceModeForPair(0,2)!=NET_VOICE_PROXIMITY) return 6;
    if(!netSetSlotTeam(1,NET_TEAM_NONE) || s_round.team[1] != 0 || s_lobby_state.slots[1].ready || !s_lobby_state.slots[1].loaded) return 7;
    if(!netVoiceSameGroup(0,1) || netTeamRosterReady() || netVoiceModeForPair(0,1)!=NET_VOICE_COUCH) return 8;
    s_lobby_state.slots[0].eliminated=1;s_lobby_state.slots[2].spectator=1;
    if(!netVoiceSameGroup(0,2) || netVoiceSameGroup(0,1)) return 9;
    voice_resets=0;netHostRoundEnded();if(!netVoiceSameGroup(0,1) || voice_resets!=GEVR_MAX_PLAYERS) return 10;
    return 0;
}
EXPORT int test_core_live_voice(void) {
    fixture(0);netLatchRoundSettings();s_state=NET_STATE_INGAME;s_phase=NET_PHASE_IN_PROGRESS;
    NetMatchConfig c=s_lobby_state.config;c.voice_mode=1;c.stage=31;netLobbySetConfig(&c);
    if(s_round.config.voice_mode != 1 || s_round.config.stage != 34 || s_lobby_state.config.stage != 31 || voice_resets != GEVR_MAX_PLAYERS) return 1;
    c.voice_mode=2;netLobbySetConfig(&c);if(s_lobby_state.config.voice_mode != 1) return 2;
    u8 raw[128];struct netbuf b={.data=raw,.size=sizeof(raw)};NetRoundSettings restored;
    s_round.team[1]=1;s_round.departed_score[1]=-3;
    netbufStartWrite(&b);netWriteRoundSettings(&b);netbufStartReadData(&b,raw,b.wp);
    if(!netReadRoundSettings(&b,&restored) || netbufReadLeft(&b) || restored.team[1]!=1 || restored.departed_score[1]!=-3 || restored.config.voice_mode!=1) return 3;
    unsigned length=b.rp;
    for(unsigned n=0;n<length;n++) {
        netbufStartReadData(&b,raw,n);if(netReadRoundSettings(&b,&restored)) return 4;
    }
    return 0;
}
EXPORT int test_core_friendly_fire(void) {
    fixture(5);for(int i=0;i<4;i++)s_lobby_state.slots[i].team=i/2;
    netLatchRoundSettings();s_state=NET_STATE_INGAME;s_phase=NET_PHASE_IN_PROGRESS;
    NetMatchConfig c=s_lobby_state.config;c.friendly_fire=0;netLobbySetConfig(&c);
    if(netDamageAllowed(0,1) || !netDamageAllowed(0,2) || !netDamageAllowed(0,0) || !netDamageAllowed(-1,0)) return 1;
    s_lobby_state.slots[1].team=NET_TEAM_BLUE; // Pending choice cannot change active damage rules.
    if(netDamageAllowed(0,1)) return 2;
    c.friendly_fire=2;netLobbySetConfig(&c);if(s_round.config.friendly_fire) return 3;
    c.friendly_fire=1;netLobbySetConfig(&c);if(!netDamageAllowed(0,1)) return 4;
    u8 raw[128];struct netbuf b={.data=raw,.size=sizeof(raw)};NetRoundSettings r;
    netbufStartWrite(&b);netWriteRoundSettings(&b);netbufStartReadData(&b,raw,b.wp);
    if(!netReadRoundSettings(&b,&r) || !r.config.friendly_fire || r.team[1]!=NET_TEAM_RED) return 5;
    s_local_slot=2;ENetPeer peer={0};netHostLost(&peer);
    if(!s_round.config.friendly_fire || s_round.team[1]!=NET_TEAM_RED) return 6;
    return 0;
}
EXPORT int test_core_ammo_packets(void) {
    fixture(0);netLatchRoundSettings();s_state=NET_STATE_INGAME;
    memset(&test_ammo,0,sizeof(test_ammo));memset(&test_ammo_prop,0,sizeof(test_ammo_prop));
    test_ammo.type=PROPDEF_AMMO;test_ammo.prop=&test_ammo_prop;test_ammo_prop.obj=&test_ammo;
    test_ammo_prop.pos.x=500;test_ammo_prop.timetoregen=1200;
    ENetPeer peer={0};netSendAmmoState(&peer);
    struct netbuf b;netbufStartReadData(&b,sent_data,sent_size);
    if(netbufReadU32(&b)!=GEVR_NET_MAGIC || netbufReadU16(&b)!=GEVR_NET_VERSION || netbufReadU8(&b)!=NET_MSG_AMMO_STATE) return 1;
    netbufReadU8(&b);ammo_applied=0;test_ammo_prop.pos.x=0;
    if(!netApplyAmmoPacket(&b) || ammo_applied!=1 || test_ammo_prop.pos.x!=500 || test_ammo_prop.timetoregen!=1200) return 2;
    // Reject wrong map, truncated frames, and a whole batch if any record is invalid.
    u8 raw[256];b=(struct netbuf){.data=raw,.size=sizeof(raw)};
    NetAmmoState a={0};a.index=29;a.enabled=1;a.pos.x=777;
    netbufStartWrite(&b);netbufWriteU8(&b,34);netbufWriteU32(&b,s_round.world_epoch);netbufWriteU8(&b,2);netbufWriteAmmoState(&b,&a);
    a.pos.y=NAN;netbufWriteAmmoState(&b,&a);unsigned len=b.wp;
    ammo_applied=0;netbufStartReadData(&b,raw,len);
    if(netApplyAmmoPacket(&b) || ammo_applied) return 3;
    raw[0]=31;netbufStartReadData(&b,raw,len);if(netApplyAmmoPacket(&b) || ammo_applied) return 4;
    raw[0]=34;netbufStartReadData(&b,raw,len-1);if(netApplyAmmoPacket(&b) || ammo_applied) return 5;
    // A valid packet from the preceding round of the same stage is stale.
    s_round.world_epoch++;netbufStartReadData(&b,sent_data+8,sent_size-8);
    if(netApplyAmmoPacket(&b) || ammo_applied) return 6;
    return 0;
}
EXPORT int test_core_scores_after_departure(void) {
    fixture(5);for(int i=0;i<4;i++) s_lobby_state.slots[i].team=i/2;
    netLatchRoundSettings();s_state=NET_STATE_INGAME;s_phase=NET_PHASE_IN_PROGRESS;
    g_playerPlayerData[0].kill_counts[2]=3;g_playerPlayerData[1].kill_counts[0]=1;
    if(netTeamScore(0)!=2) return 1;
    netForgetPlayerScore(2);s_lobby_state.slots[2].connected=0;if(netTeamScore(0)!=2) return 2;
    netForgetPlayerScore(0);s_lobby_state.slots[0].connected=0;if(netTeamScore(0)!=2) return 3;
    return 0;
}
EXPORT int test_core_latency_and_migration(void) {
    fixture(0);s_state=NET_STATE_CLIENT_LOBBY;s_host_slot=0;s_local_slot=2;
    s_lobby_received_us=clock_us;s_lobby_state.slots[1].ping_ms=42;
    if(netGetSlotPing(0)!=0 || netGetSlotPing(1)!=42) return 1;
    clock_us+=5000001;if(netGetSlotPing(1)!=-1) return 2;
    s_lobby_received_us=clock_us;s_state=NET_STATE_INGAME;netLatchRoundSettings();
    s_lobby_state.slots[1].eliminated=1;s_round.config.voice_mode=1;
    ENetPeer peer={0};netHostLost(&peer);
    if(s_host_slot!=1 || s_state!=NET_STATE_MIGRATING || netGetSlotPing(1)!=-1 || s_round.config.voice_mode!=1 || !s_lobby_state.slots[1].eliminated) return 3;
    for(int i=0;i<4;i++) if(s_lobby_state.slots[i].ping_ms!=NET_PING_UNKNOWN) return 4;
    return 0;
}

EXPORT int test_core_late_join_snapshot(void) {
    fixture(5);for(int i=0;i<4;i++) s_lobby_state.slots[i].team=i/2;
    netLatchRoundSettings();s_state=NET_STATE_INGAME;s_phase=NET_PHASE_IN_PROGRESS;
    s_lobby_state.config.voice_mode=s_round.config.voice_mode=NET_VOICE_COUCH;
    s_lobby_state.slots[1].team=NET_TEAM_NONE; // Pending change leaves the active team intact.
    s_lobby_state.slots[2].eliminated=1;s_lobby_state.slots[2].ping_ms=83;
    g_playerPlayerData[2].order_out_in_yolt=1;s_round.departed_score[0]=-2;
    netSendMatchSnapshot(NULL);
    struct netbuf b;NetRoundSettings r;NetMatchConfig pending;
    netbufStartReadData(&b,sent_data,sent_size);
    if(netbufReadU32(&b)!=GEVR_NET_MAGIC || netbufReadU16(&b)!=GEVR_NET_VERSION || netbufReadU8(&b)!=NET_MSG_MATCH_SNAPSHOT) return 1;
    netbufReadU8(&b);netbufReadU8(&b);netbufReadU32(&b);
    if(!netReadRoundSettings(&b,&r)) return 2;
    netbufReadMatchConfig(&b,&pending);
    if(r.team[1]!=NET_TEAM_RED || r.departed_score[0]!=-2 || r.config.voice_mode!=1 || pending.voice_mode!=1) return 3;
    for(int i=0;i<4;i++) {
        int team=netbufReadU8(&b), eliminated=netbufReadU8(&b),ping=netbufReadU16(&b),order=netbufReadU8(&b);
        if(i==1 && team!=NET_TEAM_NONE) return 4;
        if(i==2 && (!eliminated || ping!=83 || order!=1)) return 5;
    }
    return b.error ? 6 : 0;
}

EXPORT int test_core_fun(void) {
    fixture(5);for(int i=0;i<4;i++)s_lobby_state.slots[i].team=i/2;
    s_max_players=4;
    gevrNetConfigSet(CFG_FUN_FLAGS,7);gevrNetConfigSet(CFG_GUN_SIZE,1);
    if(s_lobby_state.config.fun_flags!=7 || VrMpFunFlags!=7 || VrMpGunSize!=1) return 1;
    gevrNetConfigSet(CFG_FUN_FLAGS,8);gevrNetConfigSet(CFG_GUN_SIZE,3);
    if(s_lobby_state.config.fun_flags!=7 || s_lobby_state.config.gun_size!=1) return 2;
    netLatchRoundSettings();s_state=NET_STATE_INGAME;s_phase=NET_PHASE_IN_PROGRESS;
    gevrNetConfigSet(CFG_FUN_FLAGS,2);gevrNetConfigSet(CFG_GUN_SIZE,2);
    if(netActiveFunFlags()!=7 || !netActiveLineMode() || netActiveGunSize()!=1 || s_round.team[1]!=NET_TEAM_RED) return 3;
    s_state=NET_STATE_CLIENT_LOBBY;gevrNetConfigSet(CFG_FUN_FLAGS,0);
    if(s_lobby_state.config.fun_flags!=2) return 4;
    s_state=NET_STATE_INGAME;
    netSendMatchSnapshot(NULL);
    struct netbuf b;NetRoundSettings active;NetMatchConfig pending;
    netbufStartReadData(&b,sent_data,sent_size);
    netbufReadU32(&b);netbufReadU16(&b);netbufReadU8(&b);netbufReadU8(&b);netbufReadU8(&b);netbufReadU32(&b);
    if(!netReadRoundSettings(&b,&active)) return 5;
    netbufReadMatchConfig(&b,&pending);
    if(b.error || active.config.fun_flags!=7 || active.config.gun_size!=1 || pending.fun_flags!=2 || pending.gun_size!=2) return 6;
    s_local_slot=1;ENetPeer peer={0};netHostLost(&peer);
    if(s_host_slot!=1 || netActiveFunFlags()!=7 || netActiveGunSize()!=1 || s_lobby_state.config.fun_flags!=2) return 7;
    s_state=NET_STATE_HOSTING_LOBBY;netLatchRoundSettings();netLatchRoundSettings();
    if(netActiveFunFlags()!=2 || netActiveLineMode() || netActiveGunSize()!=2 || s_round.team[1]!=NET_TEAM_RED) return 8;
    return 0;
}
EXPORT int test_core_launch_consent(void) {
    fixture(7);s_max_players=4;
    for(int i=1;i<4;i++)s_lobby_state.slots[i].connected=0;
    s_lobby_state.slots[0].ready=0;
    if(!netLobbyCanLaunch() || netRoundRosterReady()) return 1;
    NetMatchConfig c=s_lobby_state.config;c.scenario=0;netLobbySetConfig(&c);
    if(!netLobbyCanLaunch() || !netRoundRosterReady() || s_lobby_state.slots[0].team!=NET_TEAM_NONE) return 2;
    s_lobby_state.slots[1].connected=1;s_lobby_state.slots[1].ready=0;
    if(netLobbyCanLaunch()) return 3;
    s_lobby_state.slots[1].ready=1;
    c.voice_mode=1;c.friendly_fire=0;netLobbySetConfig(&c);
    if(!netLobbyCanLaunch()) return 4;
    c.fun_flags=NET_FUN_DK;netLobbySetConfig(&c);
    if(netLobbyCanLaunch() || s_lobby_state.slots[1].ready) return 5;
    s_lobby_state.slots[1].ready=1;c.scenario=7;netLobbySetConfig(&c);
    if(netLobbyCanLaunch() || s_lobby_state.slots[1].team!=NET_TEAM_NONE) return 6;
    s_lobby_state.slots[1].ready=1;
    if(!netLobbyCanLaunch() || netRoundRosterReady()) return 7;
    netSetSlotTeam(0,NET_TEAM_RED);netSetSlotTeam(1,NET_TEAM_BLUE);
    s_lobby_state.slots[1].ready=1;s_lobby_state.slots[2].connected=1;
    netSetSlotTeam(2,NET_TEAM_RED);s_lobby_state.slots[2].ready=1;
    if(!netRoundRosterReady()) return 8;
    netLatchRoundSettings();s_state=NET_STATE_INGAME;s_phase=NET_PHASE_WARMUP;
    netHostStartRoundNow();if(!s_next_round_at_us) return 9;
    c=s_lobby_state.config;c.health=6;netLobbySetConfig(&c);
    if(s_next_round_at_us || s_round.config.health==6 || s_lobby_state.slots[1].loaded!=1) return 10;
    netHostStartRoundNow();if(s_next_round_at_us) return 11;
    s_lobby_state.slots[1].ready=s_lobby_state.slots[2].ready=1;
    netHostStartRoundNow();if(!s_next_round_at_us) return 12;
    s_host_slot=s_local_slot=2;s_lobby_state.slots[2].ready=0;s_lobby_state.slots[0].ready=1;
    if(!netLobbyCanLaunch()) return 13;
    s_lobby_state.slots[0].ready=0;if(netLobbyCanLaunch()) return 14;
    return 0;
}

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static void hitFixture(void) {
    fixture(0);netLatchRoundSettings();s_max_players=4;s_state=NET_STATE_INGAME;s_phase=NET_PHASE_IN_PROGRESS;
    clock_us=10000000;damage_count=kill_on_damage=0;VrHostEqualization=1;VrHostLatencyCapMs=50;
    memset(hit_players,0,sizeof(hit_players));memset(hit_peers,0,sizeof(hit_peers));
    for(int i=0;i<4;i++) {
        g_playerPointers[i]=&hit_players[i];s_client_peers[i]=&hit_peers[i];
        hit_peers[i].state=ENET_PEER_STATE_CONNECTED;hit_peers[i].lastReceiveTime=enet_time_get();
        hit_peers[i].roundTripTime=i==1?20:i==2?60:180;hit_peers[i].data=(void*)(intptr_t)(i+1);
        netClockSample(&s_clock_sync[i],clock_us-2000,clock_us-1000,clock_us-1000,clock_us);
    }
}
static int queued(void) {int n=0;for(int i=0;i<NET_HOST_HIT_CAPACITY;i++)n+=s_host_hits[i].active;return n;}
static void shoot(int target) {netSendHitReport(target,ITEM_WPPK,0,1,2,3,.5f);}
EXPORT int test_core_host_delay(void) {
    hitFixture();shoot(3);shoot(2);shoot(1);
    CHECK(!damage_count && queued()==3);
    for(int ms=5;ms<=50;ms+=5) {clock_us=10000000+ms*1000;netDrainHostHits();}
    CHECK(damage_count==3 && !queued());
    CHECK(damage_target[0]==1 && damage_time[0]==10010000);
    CHECK(damage_target[1]==2 && damage_time[1]==10030000);
    CHECK(damage_target[2]==3 && damage_time[2]==10050000);
    CHECK(netGetSlotHostDelayMs(0)==0 && netGetSlotHostDelayMs(-1)==0);
    char text[128];netHostEqualizationText(text,sizeof(text));CHECK(strstr(text,"P2=10ms") && strstr(text,"P4=50ms"));
    hit_peers[1].lastReceiveTime=0;CHECK(netGetSlotHostDelayMs(1)==0);shoot(1);CHECK(damage_count==4);
    hit_peers[2].lastReceiveTime=enet_time_get()-5001;CHECK(netGetSlotHostDelayMs(2)==0);
    hit_peers[3].state=ENET_PEER_STATE_DISCONNECTED;CHECK(netGetSlotHostDelayMs(3)==0);
    hitFixture();shoot(1);netSetHostEqualization(0,50);netDrainHostHits();CHECK(damage_count==1 && !queued());
    shoot(2);CHECK(damage_count==2);netSetHostEqualization(1,80);shoot(3);
    clock_us+=40000;netSetHostEqualization(1,40);netDrainHostHits();CHECK(damage_count==3 && !queued());
    netSetHostEqualization(1,500);unsigned cap;CHECK(netGetHostEqualization(&cap) && cap==80);
    s_host_slot=1;netSetHostEqualization(0,0);CHECK(netGetHostEqualization(&cap) && cap==80);
    // Incoming pre-death lethal shot resolves before a queued host shot; both trade.
    hitFixture();kill_on_damage=1;shoot(2);clock_us+=30000;
    NetHitReport trade={s_combat_epoch,clock_us-30000,s_hit_life[2],s_hit_life[0],1,1,0,ITEM_WPPK,0,1,2,3,1};
    unsigned u;CHECK(netAcceptHit(2,&trade,&u));netProcessHitReport(2,&trade,u);netDrainHostHits();
    CHECK(damage_count==2 && hit_players[0].bonddead && hit_players[2].bonddead);
    kill_on_damage=0;clock_us+=150000;
    trade.shot_us=10030000;trade.hit_id=2;CHECK(netAcceptHit(2,&trade,&u));netProcessHitReport(2,&trade,u);CHECK(damage_count==3);
    trade.shot_us=clock_us;trade.hit_id=3;CHECK(!netAcceptHit(2,&trade,&u)); // Fired after death, even inside arrival grace.
    trade.weapon=ITEM_GRENADE;CHECK(netAcceptHit(2,&trade,&u));netProcessHitReport(2,&trade,u);CHECK(damage_count==4);
    hitFixture();shoot(99);netSendHitReport(1,ITEM_WPPK,0,NAN,0,0,1);netSendHitReport(1,ITEM_WPPK,0,0,0,0,-1);
    CHECK(!queued() && !damage_count);
    s_phase=NET_PHASE_WAITING;shoot(1);CHECK(!queued());
    s_phase=NET_PHASE_WARMUP;shoot(1);clock_us+=10000;netDrainHostHits();CHECK(damage_count==1);
    return 0;
}
EXPORT int test_core_host_delay_lifecycle(void) {
    hitFixture();for(int i=0;i<NET_HOST_HIT_CAPACITY+20;i++)shoot(1);
    CHECK(queued()==NET_HOST_HIT_CAPACITY && damage_count==20 && s_hit_overflow_warned);
    clock_us+=10000;netDrainHostHits();CHECK(damage_count==NET_HOST_HIT_CAPACITY+20 && !queued());
    hitFixture();shoot(1);netInvalidateHitSlot(1);clock_us+=10000;netDrainHostHits();CHECK(!damage_count && !queued());
    shoot(2);netInvalidateHitSlot(0);clock_us+=30000;netDrainHostHits();CHECK(!damage_count);
    shoot(1);netForgetPlayerScore(1);clock_us+=10000;netDrainHostHits();CHECK(!damage_count);
    shoot(2);netTransitionRoundPhase(NET_PHASE_WARMUP);clock_us+=30000;netDrainHostHits();CHECK(!damage_count);
    shoot(1);netResetLobbyState();clock_us+=10000;netDrainHostHits();CHECK(!damage_count);
    hitFixture();shoot(1);s_local_slot=1;ENetPeer oldhost={0};netHostLost(&oldhost);
    CHECK(!queued());netDrainHostHits();CHECK(!damage_count);
    hitFixture();s_round.config.scenario=5;s_round.config.friendly_fire=0;
    s_round.team[0]=s_round.team[1]=NET_TEAM_RED;shoot(1);CHECK(!queued());
    s_round.team[2]=NET_TEAM_BLUE;shoot(2);CHECK(queued()==1);
    s_round.team[2]=NET_TEAM_RED;clock_us+=30000;netDrainHostHits();CHECK(!damage_count && !queued());
    return 0;
}
EXPORT int test_core_score_boundary(void) {
    hitFixture();s_phase=NET_PHASE_WARMUP;
    for(int i=0;i<4;i++) {
        g_playerPlayerData[i].kill_count=7;g_playerPlayerData[i].killed_gg_owner_count=3;
        g_playerPlayerData[i].gevr_score_bank=5;g_playerPlayerData[i].flag_counter=2;g_playerPlayerData[i].order_out_in_yolt=1;
        for(int j=0;j<4;j++)g_playerPlayerData[i].kill_counts[j]=4;
    }
    s_round.departed_score[0]=10;s_round.departed_score[1]=-3;shoot(1);
    s_round_reset_loading=s_start_after_load=true;netReadyProgress();
    CHECK(s_phase==NET_PHASE_IN_PROGRESS && !s_round_reset_loading && !queued());
    for(int i=0;i<4;i++) {
        CHECK(!g_playerPlayerData[i].kill_count && !g_playerPlayerData[i].killed_gg_owner_count);
        CHECK(!g_playerPlayerData[i].gevr_score_bank && !g_playerPlayerData[i].flag_counter && !g_playerPlayerData[i].order_out_in_yolt);
        for(int j=0;j<4;j++)CHECK(!g_playerPlayerData[i].kill_counts[j]);
    }
    CHECK(!s_round.departed_score[0] && !s_round.departed_score[1]);
    g_playerPlayerData[0].kill_counts[1]=8;netTransitionRoundPhase(NET_PHASE_IN_PROGRESS);
    CHECK(g_playerPlayerData[0].kill_counts[1]==8);
    s_phase=NET_PHASE_WARMUP;s_waiting_for_match_snapshot=true;netTransitionRoundPhase(NET_PHASE_IN_PROGRESS);
    CHECK(g_playerPlayerData[0].kill_counts[1]==8);
    return 0;
}

static void receiveClockBody(int slot,ENetPeer *peer,NetClockExchange c) {
    u8 raw[64];struct netbuf b={.data=raw,.size=sizeof(raw)};
    netbufStartWrite(&b);netbufWriteClock(&b,&c);netbufStartReadData(&b,raw,b.wp);netReceiveClock(peer,slot,&b);
}
EXPORT int test_core_clock_exchange(void) {
    hitFixture();memset(&s_clock_sync[1],0,sizeof(s_clock_sync[1]));netClockTick();
    NetClockExchange c=s_clock_pending[1];CHECK(c.nonce && c.epoch==s_combat_epoch);
    c.reply=1;c.t1=c.t0+100000000+30000;c.t2=c.t1+1000;clock_us=c.t0+61000;
    receiveClockBody(1,&hit_peers[2],c);CHECK(!netClockBest(&s_clock_sync[1],clock_us));
    c.nonce++;receiveClockBody(1,&hit_peers[1],c);CHECK(!netClockBest(&s_clock_sync[1],clock_us));
    c.nonce--;receiveClockBody(1,&hit_peers[1],c);CHECK(netClockBest(&s_clock_sync[1],clock_us));
    uint64_t mapped;unsigned uncertainty;
    CHECK(netMapShotTime(1,c.t1-10000,&mapped,&uncertainty) && mapped==c.t0+20000 && uncertainty==30000);
    CHECK(!s_clock_pending[1].nonce);
    unsigned cursor=s_clock_sync[1].cursor;receiveClockBody(1,&hit_peers[1],c);CHECK(s_clock_sync[1].cursor==cursor);
    c.epoch++;receiveClockBody(1,&hit_peers[1],c);CHECK(s_clock_sync[1].cursor==cursor);
    s_local_slot=1;s_host_slot=0;s_server_peer=&hit_peers[0];s_state=NET_STATE_INGAME;
    clock_us=110100000;c=(NetClockExchange){.epoch=s_combat_epoch,.nonce=123,.t0=10100000};
    sent_size=0;receiveClockBody(0,&hit_peers[2],c);CHECK(!sent_size);
    receiveClockBody(0,s_server_peer,c);CHECK(sent_size==8+NET_CLOCK_EXCHANGE_BYTES);
    struct netbuf b;netbufStartReadData(&b,sent_data+8,sent_size-8);NetClockExchange reply;
    CHECK(netbufReadClock(&b,&reply) && reply.reply && reply.nonce==123 && reply.t0==10100000 && reply.t1==clock_us && reply.t2==clock_us);
    hitFixture();memset(s_clock_sync,0,sizeof(s_clock_sync));netClockTick();c=s_clock_pending[1];
    clock_us+=600000;netClockTick();CHECK(s_clock_pending[1].nonce==c.nonce); // Do not overwrite a slow pending probe.
    clock_us+=500001;netClockTick();CHECK(s_clock_pending[1].nonce!=c.nonce);
    return 0;
}
static void receiveHitBody(int shooter,NetHitReport h) {
    u8 raw[128];struct netbuf b={.data=raw,.size=sizeof(raw)};
    netbufStartWrite(&b);netbufWriteHitReport(&b,&h);netbufStartReadData(&b,raw,b.wp);netReceiveHitReport(shooter,&b);
}
static NetHitReport remoteHit(int shooter,int target,unsigned id,uint64_t stamp) {
    NetHitReport h={s_combat_epoch,stamp,s_hit_life[shooter],s_hit_life[target],id,id,target,ITEM_WPPK,0,1,2,3,1};return h;
}
EXPORT int test_core_timed_hits(void) {
    hitFixture();netClockSample(&s_clock_sync[1],clock_us-2000,clock_us-1000+100000000,clock_us-1000+100000000,clock_us);
    // Remove equal-RTT old sample so the test represents one consistent client clock.
    memset(&s_clock_sync[1],0,sizeof(s_clock_sync[1]));
    CHECK(netClockSample(&s_clock_sync[1],clock_us-2000,clock_us-1000+100000000,clock_us-1000+100000000,clock_us));
    NetHitReport h=remoteHit(1,0,1,clock_us+100000000-1000);
    receiveHitBody(1,h);CHECK(damage_count==1);receiveHitBody(1,h);CHECK(damage_count==1);
    h.hit_id=2;receiveHitBody(1,h);CHECK(damage_count==2); // A second pellet keeps the shot ID but has its own hit ID.
    h.hit_id=3;h.target_life++;receiveHitBody(1,h);CHECK(damage_count==2);h.target_life--;
    h.shooter_life++;receiveHitBody(1,h);CHECK(damage_count==2);h.shooter_life--;
    h.epoch++;receiveHitBody(1,h);CHECK(damage_count==2);h.epoch--;
    h.shot_us=clock_us+100000000-500001;receiveHitBody(1,h);CHECK(damage_count==2);
    h.shot_us=clock_us+100000000+51001;receiveHitBody(1,h);CHECK(damage_count==2);
    h.shot_us=UINT64_MAX;receiveHitBody(1,h);CHECK(damage_count==2);
    h.shot_us=clock_us+100000000-1000;h.damage=NAN;receiveHitBody(1,h);CHECK(damage_count==2);h.damage=1;
    s_hit_death_us[1]=clock_us-10000;receiveHitBody(1,h);CHECK(damage_count==2); // Shot actually happened after death.
    h.shot_us=s_hit_death_us[1]+100000000-1000;receiveHitBody(1,h);CHECK(damage_count==3);
    h.hit_id=4;h.shot_us=s_hit_death_us[1]+100000000-150001;receiveHitBody(1,h);CHECK(damage_count==3);
    h.weapon=ITEM_GRENADE;h.shot_us=clock_us+100000000;receiveHitBody(1,h);CHECK(damage_count==4);
    uint32_t previous=s_hit_life[0];CHECK(netAcceptRespawn(0,s_combat_epoch,previous,netNextLife(previous)));
    h.hit_id=5;receiveHitBody(1,h);CHECK(damage_count==4); // Old target-life damage cannot hit its respawn.
    h.target_life=s_hit_life[0];receiveHitBody(1,h);CHECK(damage_count==5);
    CHECK(!netAcceptRespawn(0,s_combat_epoch,previous,netNextLife(previous)));
    CHECK(!netAcceptRespawn(0,s_combat_epoch+1,s_hit_life[0],netNextLife(s_hit_life[0])));
    CHECK(!netAcceptRespawn(0,s_combat_epoch,s_hit_life[0],s_hit_life[0]+2));
    hitFixture();netBeginLocalShot();NetHitReport a=netMakeLocalHit(1,ITEM_SHOTGUN,0,1,2,3,1);
    clock_us+=1000;NetHitReport b=netMakeLocalHit(1,ITEM_SHOTGUN,0,1,2,3,1);netEndLocalShot();
    CHECK(a.shot_id==b.shot_id && a.shot_us==b.shot_us && a.hit_id!=b.hit_id);
    NetHitReport c=netMakeLocalHit(1,ITEM_WPPK,0,1,2,3,1);CHECK(c.shot_id!=a.shot_id && c.shot_us==clock_us);
    shoot(1);CHECK(queued()==1);CHECK(netAcceptRespawn(1,s_combat_epoch,s_hit_life[1],netNextLife(s_hit_life[1])));
    clock_us+=20000;netDrainHostHits();CHECK(!damage_count && !queued());
    // Owner death uses synchronized time and life; older movement cannot reset it.
    struct netplayermove m={0};m.epoch=s_combat_epoch;m.life_id=s_hit_life[2];m.tick=5;m.dead=1;m.death_us=clock_us-5000;
    netObserveHitMove(2,&m);CHECK(s_hit_death_us[2]==clock_us-5000);
    m.dead=0;m.tick=6;netObserveHitMove(2,&m);CHECK(s_hit_death_us[2]);
    CHECK(netAcceptRespawn(2,s_combat_epoch,s_hit_life[2],netNextLife(s_hit_life[2])));CHECK(!s_hit_death_us[2]);
    m.tick=7;m.dead=1;netObserveHitMove(2,&m);CHECK(!s_hit_death_us[2]); // Movement still belongs to old life.
    return 0;
}
EXPORT int test_core_combat_identity(void) {
    hitFixture();s_hit_life[1]=7;s_hit_life[2]=9;
    u8 raw[128];struct netbuf b={.data=raw,.size=sizeof(raw)};netbufStartWrite(&b);netWriteRoundSettings(&b);
    unsigned len=b.wp;NetRoundSettings r;netbufStartReadData(&b,raw,len);CHECK(netReadRoundSettings(&b,&r));
    CHECK(r.combat_epoch==s_combat_epoch && r.life[1]==7 && r.life[2]==9);
    for(unsigned n=0;n<len;n++){netbufStartReadData(&b,raw,n);CHECK(!netReadRoundSettings(&b,&r));}
    netbufStartReadData(&b,raw,len);CHECK(netReadRoundSettings(&b,&r));
    s_hit_life[0]=2;netImportCombatIdentity(&r);CHECK(s_hit_life[0]==2); // A lagging snapshot cannot revert local respawn.
    r.life[2]=10;netImportCombatIdentity(&r);CHECK(s_hit_life[2]==10);
    uint64_t epoch=s_combat_epoch;netResetCombatEpoch();CHECK(s_combat_epoch!=epoch && s_hit_life[1]==1);
    netImportCombatIdentity(&r);CHECK(s_combat_epoch==epoch && s_hit_life[1]==7);
    shoot(1);CHECK(queued());s_state=NET_STATE_MIGRATING;netDrainHostHits();CHECK(!queued());
    s_state=NET_STATE_INGAME;s_host_slot=s_local_slot=1;netResetCombatEpoch();CHECK(s_combat_epoch!=epoch);
    CHECK(!netClockBest(&s_clock_sync[2],clock_us));
    NetHitReport h=remoteHit(2,0,1,clock_us);h.epoch=epoch;unsigned uncertainty;
    CHECK(!netAcceptHit(2,&h,&uncertainty));
    return 0;
}

static void receiveDamageBody(ENetPeer *peer,int shooter,NetHitReport h,int extra) {
    u8 raw[128];struct netbuf b={.data=raw,.size=sizeof(raw)};netbufStartWrite(&b);netbufWriteHitReport(&b,&h);
    if(extra)netbufWriteU8(&b,0);netbufStartReadData(&b,raw,b.wp);netReceiveDamageEvent(peer,shooter,&b);
}
static void receiveRespawnBody(ENetPeer *peer,int slot,uint64_t epoch,uint32_t previous,uint32_t next) {
    u8 raw[64];struct netbuf b={.data=raw,.size=sizeof(raw)};netbufStartWrite(&b);
    netbufWriteU8(&b,1);netbufWriteF32(&b,90);netbufWriteU64(&b,epoch);netbufWriteU32(&b,previous);netbufWriteU32(&b,next);
    netbufStartReadData(&b,raw,b.wp);netReceiveRespawn(peer,slot,&b,raw,b.size);
}
EXPORT int test_core_owner_packets(void) {
    hitFixture();s_local_slot=1;s_host_slot=0;s_server_peer=&hit_peers[0];
    NetHitReport h=remoteHit(2,1,1,clock_us);receiveDamageBody(&hit_peers[2],2,h,0);CHECK(!damage_count);
    receiveDamageBody(s_server_peer,2,h,1);CHECK(!damage_count);
    receiveDamageBody(s_server_peer,2,h,0);CHECK(damage_count==1);
    uint32_t previous=s_hit_life[1];CHECK(netAcceptRespawn(1,s_combat_epoch,previous,netNextLife(previous)));
    receiveDamageBody(s_server_peer,2,h,0);CHECK(damage_count==1);
    h.target_life=s_hit_life[1];receiveDamageBody(s_server_peer,2,h,0);CHECK(damage_count==2);
    h.epoch++;receiveDamageBody(s_server_peer,2,h,0);CHECK(damage_count==2);
    // A client polling host queues must retain its original owner death time.
    struct netplayermove m={0};m.dead=1;m.tick=1;netSendLocalPlayerMove(&m);uint64_t death=s_local_death_us;CHECK(death);
    clock_us+=20000;netDrainHostHits();netSendLocalPlayerMove(&m);CHECK(s_local_death_us==death);
    hitFixture();static PropRecord prop;hit_players[1].prop=&prop;respawn_calls=0;
    previous=s_hit_life[1];s_remote_moves[1].dead=1;s_remote_moves[1].death_us=clock_us;s_remote_active[1]=true;
    receiveRespawnBody(&hit_peers[2],1,s_combat_epoch,previous,netNextLife(previous));CHECK(!respawn_calls);
    receiveRespawnBody(&hit_peers[1],1,s_combat_epoch+1,previous,netNextLife(previous));CHECK(!respawn_calls);
    receiveRespawnBody(&hit_peers[1],1,s_combat_epoch,previous,previous+2);CHECK(!respawn_calls);
    receiveRespawnBody(&hit_peers[1],1,s_combat_epoch,previous,netNextLife(previous));CHECK(respawn_calls==1);
    CHECK(s_hit_life[1]==netNextLife(previous) && !s_remote_moves[1].dead && !s_remote_moves[1].death_us && !s_remote_active[1]);
    receiveRespawnBody(&hit_peers[1],1,s_combat_epoch,previous,netNextLife(previous));CHECK(respawn_calls==1);
    hitFixture();s_round_reset_loading=s_start_after_load=true;memset(&s_clock_sync[1],0,sizeof(s_clock_sync[1]));
    netReadyProgress();CHECK(s_round_reset_loading);
    netClockTick();NetClockExchange c=s_clock_pending[1];c.reply=1;c.t1=c.t0+1000;c.t2=c.t1;clock_us+=2000;
    receiveClockBody(1,&hit_peers[1],c);CHECK(!s_round_reset_loading && s_phase==NET_PHASE_IN_PROGRESS);
    return 0;
}

/* Issue #88: slots 4..7 through the packets that grew with the player count,
 * the team rules, damage, voice, start pads and the combat epoch. */
/* The host's player count: any stage, never below who is connected, a team
 * scenario's own size, and in a match from the next load. */
EXPORT int test_core_player_count(void) {
    fixture(0);   /* hosting the lobby, slots 0..3 connected */
    NetMatchConfig c=s_lobby_state.config;
    CHECK(c.max_players==4);
    c.stage=32;c.max_players=8;netLobbySetConfig(&c);   /* eight on Egypt */
    CHECK(s_lobby_state.config.max_players==8 && s_max_players==8 && netGetMaxPlayers()==8);
    CHECK(netLobbyMinPlayers()==4);
    c.max_players=3;netLobbySetConfig(&c);CHECK(s_lobby_state.config.max_players==8);
    /* a departed player's gap at slot 3, a player in slot 5: six or more */
    s_lobby_state.slots[3].connected=0;s_lobby_state.slots[5].connected=1;
    CHECK(netLobbyMinPlayers()==6);
    c.max_players=5;netLobbySetConfig(&c);CHECK(s_lobby_state.config.max_players==8);
    c.max_players=6;netLobbySetConfig(&c);CHECK(s_lobby_state.config.max_players==6 && s_max_players==6);
    /* 2v2 takes its four, its slots stay open to slot 5; 2v1 cannot take four */
    c.scenario=5;netLobbySetConfig(&c);
    CHECK(s_lobby_state.config.scenario==5 && netGetMaxPlayers()==4 && s_max_players==6);
    c.scenario=7;netLobbySetConfig(&c);CHECK(s_lobby_state.config.scenario==5);
    /* in a match the count changes at the next load */
    c.scenario=0;c.max_players=8;netLobbySetConfig(&c);
    netLatchRoundSettings();s_state=NET_STATE_INGAME;
    c.max_players=7;netLobbySetConfig(&c);
    CHECK(s_lobby_state.config.max_players==7 && s_max_players==8 && netGetMaxPlayers()==8);
    netLatchRoundSettings();CHECK(s_max_players==7 && netGetMaxPlayers()==7);
    /* the lobby page's row, saved for the next session */
    gevrNetConfigSet(CFG_MAX_PLAYERS,6);CHECK(s_lobby_state.config.max_players==6 && VrMpMaxPlayers==6);
    gevrNetConfigSet(CFG_MAX_PLAYERS,9);CHECK(s_lobby_state.config.max_players==6);
    s_state=NET_STATE_HOSTING_LOBBY;
    return 0;
}

static ENetHost eight_host;
EXPORT int test_core_eight_slots(void) {
    fixture(NET_SCENARIO_4V4);
    for(int i=4;i<GEVR_MAX_PLAYERS;i++) {s_lobby_state.slots[i].connected=1;s_lobby_state.slots[i].ready=1;s_lobby_state.slots[i].loaded=1;}
    CHECK(netGetConnectedPlayerCount()==GEVR_MAX_PLAYERS);
    /* four a side, and no fifth */
    for(int i=0;i<4;i++) CHECK(netSetSlotTeam(i,NET_TEAM_RED));
    CHECK(!netTeamRosterReady());
    for(int i=4;i<7;i++) CHECK(netSetSlotTeam(i,NET_TEAM_BLUE));
    CHECK(!netSetSlotTeam(7,NET_TEAM_RED));
    CHECK(netSetSlotTeam(7,NET_TEAM_BLUE) && netTeamRosterReady() && netGetSlotTeam(7)==NET_TEAM_BLUE);
    netLatchRoundSettings();s_state=NET_STATE_INGAME;s_phase=NET_PHASE_IN_PROGRESS;s_round.config.friendly_fire=0;
    CHECK(netDamageAllowed(7,0) && !netDamageAllowed(7,6) && !netDamageAllowed(0,GEVR_MAX_PLAYERS));
    CHECK(netVoiceSameGroup(0,7) && netVoiceModeForPair(4,7)==NET_VOICE_COUCH && netVoiceModeForPair(0,7)==NET_VOICE_PROXIMITY);
    g_playerPlayerData[7].kill_counts[0]=2;g_playerPlayerData[4].kill_counts[5]=1;
    CHECK(netTeamScore(NET_TEAM_BLUE)==1 && netTeamScore(NET_TEAM_RED)==0);

    /* the late-join snapshot: 67 + 49N + 4N^2 bytes, past the old 512 */
    s_lobby_state.slots[7].eliminated=1;s_lobby_state.slots[7].ping_ms=77;g_playerPlayerData[7].order_out_in_yolt=GEVR_MAX_PLAYERS;
    g_playerPlayerData[6].kill_counts[7]=5;g_playerPlayerData[7].gevr_score_bank=9;
    sent_size=0;netSendMatchSnapshot(NULL);
    CHECK(sent_size==67+49*GEVR_MAX_PLAYERS+4*GEVR_MAX_PLAYERS*GEVR_MAX_PLAYERS && sent_size>512);
    struct netbuf b;NetRoundSettings r;NetMatchConfig pending;
    netbufStartReadData(&b,sent_data,sent_size);
    netbufReadU32(&b);netbufReadU16(&b);netbufReadU8(&b);netbufReadU8(&b);netbufReadU8(&b);netbufReadU32(&b);
    CHECK(netReadRoundSettings(&b,&r) && r.team[7]==NET_TEAM_BLUE);
    netbufReadMatchConfig(&b,&pending);CHECK(pending.scenario==NET_SCENARIO_4V4);
    for(int i=0;i<GEVR_MAX_PLAYERS;i++) {
        int team=netbufReadU8(&b),eliminated=netbufReadU8(&b),ping=netbufReadU16(&b),order=netbufReadU8(&b);
        if(i==7) CHECK(team==NET_TEAM_BLUE && eliminated && ping==77 && order==GEVR_MAX_PLAYERS);
    }
    for(int i=0;i<GEVR_MAX_PLAYERS;i++) {
        u32 bank=netbufReadU32(&b);
        for(int j=0;j<GEVR_MAX_PLAYERS;j++) {u32 k=netbufReadU32(&b);if(i==6&&j==7) CHECK(k==5);}
        if(i==7) CHECK(bank==9);
    }
    for(int i=0;i<GEVR_MAX_PLAYERS;i++) {netbufReadU8(&b);for(int j=0;j<7;j++)netbufReadF32(&b);netbufReadU8(&b);}
    CHECK(!b.error && !netbufReadLeft(&b));

    /* a ballot carries every slot's vote: 9 + N bytes, past the old 16 */
    s_host=&eight_host;s_client_peers[7]=&hit_peers[7];
    netClearVotes(-1);s_vote[NET_BALLOT_STAGE][7]=3;
    sent_size=0;netBroadcastVotes(NET_BALLOT_STAGE);
    CHECK(sent_size==9+GEVR_MAX_PLAYERS && sent_data[sent_size-1]==3);
    s_host=NULL;s_client_peers[7]=NULL;

    /* start pads: past the pads a slot shares one, and stands beside it */
    CHECK(netStartPadShare(3,5)==0 && netStartPadShare(5,5)==1 && netStartPadShare(7,5)==1);
    CHECK(netStartPadShare(7,3)==2 && netStartPadShare(7,0)==0 && netStartPadShare(0,1)==0);
    for(int slot=0;slot<GEVR_MAX_PLAYERS;slot++) CHECK(netStartPad(slot,GEVR_MAX_PLAYERS)>=0 && netStartPad(slot,GEVR_MAX_PLAYERS)<GEVR_MAX_PLAYERS);
    for(int a=0;a<GEVR_MAX_PLAYERS;a++) for(int c=a+1;c<GEVR_MAX_PLAYERS;c++) CHECK(netStartPad(a,GEVR_MAX_PLAYERS)!=netStartPad(c,GEVR_MAX_PLAYERS));

    /* the combat epoch keeps host slot 7 apart from the clock */
    s_host_slot=7;netResetCombatEpoch();CHECK((s_combat_epoch&15)==8);
    uint64_t epoch=s_combat_epoch;netResetCombatEpoch();CHECK(s_combat_epoch!=epoch && (s_combat_epoch&15)==8);
    s_host_slot=0;
    return 0;
}
