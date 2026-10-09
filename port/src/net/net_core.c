#ifdef ntohl
#undef ntohl
#endif
#ifdef ntohs
#undef ntohs
#endif

#include "net/netenet.h"
#include "net_core.h"
#include "net_game.h"
#include "game/mpmenu.h"
#include "net_ice.h"
#include "net/netbuf.h"
#include "net_voice.h"
#include "net_objects.h"
#include "net_coop.h"
#include "net_timing.h"
#include "bondconstants.h"
#include "boss.h"
#include "game/front.h"
#include "game/mp_weapon.h"
#include "game/bondinv.h"
#include "game/player.h"
#include "game/front.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "game/chrai.h"
#include "game/loadobjectmodel.h"
#include "game/propobj.h"
#include "music.h"   /* g_musicSfxBufferPtr: the hit heard at a copy (netApplyDamage) */
#include "snd.h"
#include "system.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/*
 * Through the game's log (system.c sysLogPrintf): logcat under the app's tag
 * and the gevr.log file the launcher's "Send debug log" bundles. As a
 * separate "GEVR-Net" logcat tag these lines were missing from a tester's
 * bundle (2026-09-30): the bundle's logcat tail is 1500 lines, which the
 * Quest's own services fill in under a minute.
 */
#define NET_LOG(...) sysLogPrintf(LOG_NOTE, "net: " __VA_ARGS__)
#define NET_ERR(...) sysLogPrintf(LOG_ERROR, "net: " __VA_ARGS__)

static bool s_initialized = false;
static NetState s_state = NET_STATE_OFFLINE;
static NetPhase s_phase = NET_PHASE_WAITING;
static int s_max_players = GEVR_MAX_PLAYERS;
static bool s_round_reset_pending = false;
static bool s_round_reset_loading = false;
static bool s_start_after_load = false;
static bool s_lobby_open = false;
static bool s_warmup_started = false, s_start_requested = false, s_vote_requested = false;
static uint64_t s_warmup_end_us = 0;
#define NET_WARMUP_SECONDS 120
enum { NET_NOTICE_READY = 1, NET_NOTICE_VOTE = 2, NET_NOTICE_WARMUP = 4 };
static bool s_match_ended = false;
static uint64_t s_results_deadline_us = 0;
static uint64_t s_next_round_at_us = 0;
static ENetHost *s_host = NULL;
static ENetPeer *s_server_peer = NULL; /* Used when we are a client */
static ENetPeer *s_client_peers[GEVR_MAX_PLAYERS]; /* Host peer-to-slot map. */
static bool s_client_can_be_kicked[GEVR_MAX_PLAYERS];
static uint8_t s_client_caps[GEVR_MAX_PLAYERS];        /* NET_CLIENT_CAP_*, as the guest last said */
static bool s_client_caps_known[GEVR_MAX_PLAYERS];     /* ... and whether it has said */
static ENetVirtualSendCallback s_virtual_send = NULL;
static ENetVirtualReceiveCallback s_virtual_receive = NULL;
static void *s_virtual_context = NULL;

void netSetVirtualTransport(ENetVirtualSendCallback sendCallback,
                            ENetVirtualReceiveCallback receiveCallback, void *context) {
    s_virtual_send = sendCallback;
    s_virtual_receive = receiveCallback;
    s_virtual_context = context;
    if (s_host) enet_host_set_virtual_transport(s_host, sendCallback, receiveCallback, context);
}
static int s_local_slot = 0;
static int s_host_slot = 0;   /* the host's slot: 0, or the elected one after a migration */

/*
 * Host migration: the host gone mid-match, the lowest remaining slot serves
 * the same match (netHostLost). What the clients keep for it: the internet
 * lobby and its owner token, the LAN beacon's name (NET_MSG_LOBBY_HANDOFF).
 */
static char s_game_name[GEVR_MAX_NAME_LEN] = "";
static char s_lobby_code[16] = "";
static char s_lobby_token[96] = "";
static uint8_t s_lobby_max_players = GEVR_MAX_PLAYERS;
static bool s_takeover_pending = false;      /* elected: the launcher glue sets the transport, then netHostTakeOver */
static bool s_rejoining = false;             /* a client between hosts: its slot and match are kept */
static uint64_t s_migrate_deadline_us = 0;   /* a client gives up on the new host at this time */
static char s_old_host_ip[64] = "";          /* the old host's LAN beacon may linger: skipped */
static uint64_t s_slot_grace_us[GEVR_MAX_PLAYERS];   /* new host: a slot kept for its player until this time */
static uint64_t s_slot_heard_us[GEVR_MAX_PLAYERS];   /* last application packet from this remote slot */
static uint64_t s_host_heard_us;                     /* client: last application packet from the host */
#define NET_SILENCE_TIMEOUT_US (30ull * 1000000ull)
static void netBroadcastAllVotes(void);
static void netClearVotes(int slot);
static void netBroadcastVotes(int kind);
static int netBallotSize(int kind);
/* Each slot's vote on each ballot (NET_BALLOT_*): a stage index or a weapon set, -1 none */
static int8_t s_vote[NET_BALLOT_COUNT][GEVR_MAX_PLAYERS];
static void netSendLobbyHandoffTo(ENetPeer *peer);
static void netMigrationGiveUpNow(void);
static bool s_coop_ended;                /* co-op: this mission has ended (every headset; netCoopApplyEnd) */
static void netCoopApplyEnd(bool success);

static uint8_t s_preferred_chr_id = 0;

void netSetPreferredCharacter(uint8_t chr_id) {
    if (chr_id < netCharacterCount()) s_preferred_chr_id = chr_id;
}

/* The host's ENet peers: every client slot, plus room for a player back on a
 * new connection while the old one drains and for a joiner being turned away. */
#define NET_HOST_PEERS (GEVR_MAX_PLAYERS + 1)

/* Remote player state cache */
static struct netplayermove s_remote_moves[GEVR_MAX_PLAYERS];
static int8_t s_last_attacker[GEVR_MAX_PLAYERS] = { [0 ... GEVR_MAX_PLAYERS - 1] = -1 };   /* per target: who last damaged it here */

int netLastAttacker(int slot) {
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS) return slot;
    return s_last_attacker[slot] >= 0 ? s_last_attacker[slot] : slot;
}

#define NET_HOST_HIT_CAPACITY 256
extern int VrHostEqualization, VrHostLatencyCapMs;
typedef struct {
    bool active;
    uint8_t shooter;
    unsigned delay_ms;
    uint64_t queued_us,order;
    NetHitReport report; /* shot_us has been mapped to host time before queuing */
} QueuedHostHit;
static QueuedHostHit s_host_hits[NET_HOST_HIT_CAPACITY];
static uint64_t s_combat_epoch,s_hit_death_us[GEVR_MAX_PLAYERS],s_hit_order,s_local_death_us[GEVR_MAX_PLAYERS];
static uint32_t s_hit_life[GEVR_MAX_PLAYERS],s_last_hit_id[GEVR_MAX_PLAYERS];
/* s_local_*: per owned slot, the local player's and (the host) its bots' (netSlotOwned) */
static uint32_t s_hit_move_tick[GEVR_MAX_PLAYERS],s_local_shot_id[GEVR_MAX_PLAYERS],s_local_hit_id[GEVR_MAX_PLAYERS];
static bool s_hit_move_seen[GEVR_MAX_PLAYERS];
static bool s_hit_overflow_warned,s_local_shot_active[GEVR_MAX_PLAYERS];
static unsigned s_host_hit_cursor;
static NetHitReport s_local_shot[GEVR_MAX_PLAYERS];
static NetClockSync s_clock_sync[GEVR_MAX_PLAYERS];
static NetClockExchange s_clock_pending[GEVR_MAX_PLAYERS];
static uint64_t s_clock_probe_us[GEVR_MAX_PLAYERS];
static uint32_t s_clock_nonce;
static void netClearHostHits(void);
static void netInvalidateHitSlot(int slot);
static void netDrainHostHits(void);
static void netReadyProgress(void);
static void netBroadcastRoundNotice(void);
static void netTryHostStartRequest(void);

/* Every headset's damage application, logged with the target's accounting (playtest 2026-09-30) */
static void netApplyDamage(uint8_t target, uint8_t attacker, uint8_t weapon, float dmg, float vx, float vz) {
    extern s32 s_gevrExplosionDamage;
    if (!netDamageAllowed(attacker, target)) return;
    struct player *pl = g_playerPointers[target];
    s32 prev = get_cur_playernum();
    f32 h0 = pl->bondhealth, a0 = pl->bondarmour;
    s_last_attacker[target] = (int8_t)attacker;
    if (!netSlotOwned(target)) {
        /*
         * A copy takes no damage of its own: its health is its owner's
         * (protocol 10) and it dies when its owner reports dead
         * (net_player_sync.c), credited to the last attacker recorded here.
         * Applied locally, the same event killed a copy on one headset and
         * not the owner on its own (the owner's damage-flash gate), and the
         * kill was counted where the owner never died: the client won 5-0
         * on its screen, 4-0 on the host's (match 2026-09-30, 15:04). The
         * hit is still heard from where the copy stands.
         */
        if (pl->prop && !pl->bonddead)
            chrobjSndCreatePostEventDamage(sndPlaySfx(g_musicSfxBufferPtr, BOND_GET_HIT1_SFX, 0), &pl->prop->pos);
        NET_LOG("damage: player %d took %.2f from %d (weapon %d): a copy, its owner decides", target, dmg, attacker, weapon);
        return;
    }
    set_cur_player(target);
    s_gevrExplosionDamage = (weapon == ITEM_GRENADE || weapon == ITEM_GRENADELAUNCH || weapon == ITEM_ROCKETLAUNCH || weapon == ITEM_PROXIMITYMINE || weapon == ITEM_TIMEDMINE || weapon == ITEM_REMOTEMINE || weapon == ITEM_TANKSHELLS);
    record_damage_kills(dmg, vx, vz, attacker, 1);
    s_gevrExplosionDamage = 0;
    set_cur_player(prev);
    if (pl->bonddead && !s_hit_death_us[target]) s_hit_death_us[target] = sysGetMicroseconds();
    if (pl->bonddead && !s_local_death_us[target]) s_local_death_us[target] = s_hit_death_us[target];
    NET_LOG("damage: player %d took %.2f from %d (weapon %d): health %.2f -> %.2f, armour %.2f -> %.2f%s%s",
            target, dmg, attacker, weapon, h0, pl->bondhealth, a0, pl->bondarmour,
            pl->bonddead ? ", dead" : "", target == s_local_slot ? " (me)" : "");
}
static NetMsgPlayerState s_remote_players[GEVR_MAX_PLAYERS];
static bool s_remote_active[GEVR_MAX_PLAYERS];
static bool s_waiting_for_match_snapshot = false;
static bool s_stage_ready_sent = false;
static uint64_t s_last_stage_ready_us = 0;

/* Current Lobby State */
static NetMsgLobbyState s_lobby_state;
/* Frozen at match load; pending lobby edits must never change a late join's map or kit. */
typedef struct {
    NetMatchConfig config;
    uint32_t world_epoch; /* Reject delayed crate updates after reloading the same map. */
    uint64_t combat_epoch; /* Snapshot envelope: dynamic life identity is separate from frozen teams/kits. */
    uint32_t life[GEVR_MAX_PLAYERS];
    uint8_t character[GEVR_MAX_PLAYERS];
    uint8_t loadout[GEVR_MAX_PLAYERS][4];
    uint8_t team[GEVR_MAX_PLAYERS];
    int32_t departed_score[2];
} NetRoundSettings;
static NetRoundSettings s_round;
extern int VrMpStage, VrMpWeaponSet, VrMpChr, VrMpScenario, VrMpLength, VrMpHealth;
extern int VrMpDual, VrMpLoadouts, VrMpNextRound, VrMpCustom[4], VrMpLoadout[4];
extern int VrMpMovementSpeed;
extern int VrMpVoiceMode, VrMpFriendlyFire, VrMpFunFlags, VrMpGunSize, VrMpMaxPlayers;
extern int VrMpBotMode, VrMpBotCount, VrMpBotDifficulty;
extern int VrCoopFastReinforcements;
extern unsigned VrMpFavStages, VrMpFavSets;
extern void vrSettingsSave(void);

static void netInvalidateHitSlot(int slot)
{
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS) return;
    s_hit_death_us[slot] = 0;
    s_last_hit_id[slot]=0;
    s_hit_move_seen[slot]=false;
    s_local_death_us[slot]=0;s_local_shot_active[slot]=false;s_local_shot_id[slot]=s_local_hit_id[slot]=0;
    for (int i=0;i<NET_HOST_HIT_CAPACITY;i++)
        if (s_host_hits[i].active && (s_host_hits[i].report.target == slot || s_host_hits[i].shooter == slot)) s_host_hits[i].active = false;
}

static void netClearHostHits(void)
{
    memset(s_host_hits,0,sizeof(s_host_hits));
    for (int i=0;i<GEVR_MAX_PLAYERS;i++) s_hit_death_us[i]=0;
    memset(s_local_death_us,0,sizeof(s_local_death_us));memset(s_local_shot_active,0,sizeof(s_local_shot_active));
    memset(s_hit_move_seen,0,sizeof(s_hit_move_seen));
    s_hit_overflow_warned=false;
    s_host_hit_cursor=0;
}

void netSetHostEqualization(int enabled, unsigned cap_ms)
{
    if (netIsActive() && !netIsHost()) return;
    VrHostEqualization = enabled != 0;
    VrHostLatencyCapMs = cap_ms > 80 ? 80 : (int)cap_ms;
    vrSettingsSave();
}
int netGetHostEqualization(unsigned *cap_ms)
{
    if (cap_ms) *cap_ms = VrHostLatencyCapMs < 0 ? 0 : VrHostLatencyCapMs > 80 ? 80 : VrHostLatencyCapMs;
    return VrHostEqualization != 0;
}
static int netMapShotTime(int slot,uint64_t stamp,uint64_t *mapped,unsigned *uncertainty)
{
    uint64_t now=sysGetMicroseconds();
    if (netSlotOwned(slot)) { *mapped=stamp;*uncertainty=0;return stamp && stamp<=INT64_MAX; }
    return slot>=0 && slot<GEVR_MAX_PLAYERS && netClockMap(&s_clock_sync[slot],stamp,now,mapped,uncertainty);
}
static void netObserveHitMove(int slot,const struct netplayermove *move)
{
    if (slot<0 || slot>=GEVR_MAX_PLAYERS || move->epoch!=s_combat_epoch || move->life_id!=s_hit_life[slot]) return;
    if (s_hit_move_seen[slot] && (int32_t)(move->tick-s_hit_move_tick[slot])<=0) return;
    s_hit_move_seen[slot]=true;s_hit_move_tick[slot]=move->tick;
    if (!move->dead) return; /* Only an explicit new life can clear an observed death. */
    uint64_t death;unsigned uncertainty;
    if (move->death_us && netMapShotTime(slot,move->death_us,&death,&uncertainty)
        && death<=sysGetMicroseconds()+NET_SHOT_FUTURE_US+uncertainty && !s_hit_death_us[slot])
        s_hit_death_us[slot]=death>sysGetMicroseconds()?sysGetMicroseconds():death;
}
static void netResetCombatEpoch(void)
{
    uint64_t next=(sysGetMicroseconds()<<4)|(uint64_t)(s_host_slot+1);   /* slot+1 fits four bits */
    if (next==s_combat_epoch) next+=16;
    s_combat_epoch=next;
    NET_LOG("Combat clock epoch: %llu",(unsigned long long)s_combat_epoch);
    netClearHostHits();
    memset(s_clock_sync,0,sizeof(s_clock_sync));memset(s_clock_pending,0,sizeof(s_clock_pending));
    memset(s_clock_probe_us,0,sizeof(s_clock_probe_us));memset(s_last_hit_id,0,sizeof(s_last_hit_id));
    memset(s_local_shot_id,0,sizeof(s_local_shot_id));memset(s_local_hit_id,0,sizeof(s_local_hit_id));
    for(int i=0;i<GEVR_MAX_PLAYERS;i++)s_hit_life[i]=1;
}
static void netWriteCombatIdentity(struct netbuf *b)
{
    netbufWriteU64(b,s_combat_epoch);
    for(int i=0;i<GEVR_MAX_PLAYERS;i++)netbufWriteU32(b,s_hit_life[i]);
}
static int netReadCombatIdentity(struct netbuf *b,NetRoundSettings *r)
{
    r->combat_epoch=netbufReadU64(b);
    for(int i=0;i<GEVR_MAX_PLAYERS;i++) {r->life[i]=netbufReadU32(b);if(!r->life[i])return 0;}
    return !b->error && r->combat_epoch;
}
static void netImportCombatIdentity(const NetRoundSettings *r)
{
    bool changed=r->combat_epoch!=s_combat_epoch;
    if(changed) {
        netClearHostHits();s_combat_epoch=r->combat_epoch;
        memset(s_clock_sync,0,sizeof(s_clock_sync));memset(s_clock_pending,0,sizeof(s_clock_pending));
        memset(s_clock_probe_us,0,sizeof(s_clock_probe_us));
    }
    for(int i=0;i<GEVR_MAX_PLAYERS;i++)
        if(changed || (int32_t)(r->life[i]-s_hit_life[i])>0) {netInvalidateHitSlot(i);s_hit_life[i]=r->life[i];}
}
static void netSendClockTo(ENetPeer *peer,const NetClockExchange *c)
{
    u8 raw[64];struct netbuf b={.data=raw,.size=sizeof(raw)};netbufStartWrite(&b);
    netbufWriteU32(&b,GEVR_NET_MAGIC);netbufWriteU16(&b,GEVR_NET_VERSION);
    netbufWriteU8(&b,NET_MSG_CLOCK);netbufWriteU8(&b,(uint8_t)s_local_slot);netbufWriteClock(&b,c);
    if (!b.error) enet_peer_send(peer,NET_CHAN_RELIABLE,enet_packet_create(b.data,b.wp,ENET_PACKET_FLAG_RELIABLE));
}
static void netClockTick(void)
{
    if(!netIsHost() || !s_combat_epoch) return;
    uint64_t now=sysGetMicroseconds();
    for(int i=0;i<GEVR_MAX_PLAYERS;i++) {
        ENetPeer *peer=s_client_peers[i];
        if(i==s_local_slot || !peer || peer->state!=ENET_PEER_STATE_CONNECTED)continue;
        if(s_clock_pending[i].nonce && now-s_clock_pending[i].t0<=NET_CLOCK_MAX_RTT_US)continue;
        uint64_t interval=netClockBest(&s_clock_sync[i],now)?2000000:250000;
        if(s_clock_probe_us[i] && now-s_clock_probe_us[i]<interval)continue;
        s_clock_probe_us[i]=now;
        if(!++s_clock_nonce)++s_clock_nonce;
        s_clock_pending[i]=(NetClockExchange){.epoch=s_combat_epoch,.nonce=s_clock_nonce,.t0=now};
        netSendClockTo(peer,&s_clock_pending[i]);
    }
}
static void netReceiveClock(ENetPeer *peer,int slot,struct netbuf *b)
{
    NetClockExchange c;uint64_t received=sysGetMicroseconds();
    if(!netbufReadClock(b,&c) || netbufReadLeft(b) || c.epoch!=s_combat_epoch) return;
    if(netIsHost()) {
        if(!c.reply || slot<0 || slot>=GEVR_MAX_PLAYERS || s_client_peers[slot]!=peer
            || (int)(intptr_t)peer->data-1!=slot || c.nonce!=s_clock_pending[slot].nonce
            || c.t0!=s_clock_pending[slot].t0 || c.epoch!=s_clock_pending[slot].epoch)return;
        bool was_synced=netClockBest(&s_clock_sync[slot],received)!=NULL;
        if(netClockSample(&s_clock_sync[slot],c.t0,c.t1,c.t2,received)) {
            if(!was_synced) {
                const NetClockSample *sample=netClockBest(&s_clock_sync[slot],received);
                NET_LOG("Clock synced: slot %d offset %lldus RTT %lluus",slot,(long long)sample->offset_us,(unsigned long long)sample->rtt_us);
            }
            s_clock_pending[slot].nonce=0;
            if(s_state==NET_STATE_INGAME)netReadyProgress();
        }
    } else if(peer==s_server_peer && slot==s_host_slot && !c.reply) {
        c.reply=1;c.t1=received;c.t2=sysGetMicroseconds();netSendClockTo(peer,&c);
    }
}

static unsigned netHostBaseDelayMs(int slot)
{
    if (!netIsHost() || slot < 0 || slot >= GEVR_MAX_PLAYERS || slot == s_local_slot) return 0;
    ENetPeer *peer = s_client_peers[slot];
    if (!peer || peer->state != ENET_PEER_STATE_CONNECTED ||
        netLatencyValue(peer->roundTripTime, peer->lastReceiveTime, enet_time_get()) == NET_PING_UNKNOWN) return 0;
    return peer->roundTripTime / 2;
}
unsigned netGetSlotHostDelayMs(int slot)
{
    unsigned cap=0, delay=netHostBaseDelayMs(slot);
    if (!netGetHostEqualization(&cap)) return 0;
    return delay < cap ? delay : cap;
}
void netHostEqualizationText(char *text, unsigned size)
{
    if (!size) return;
    text[0]=0;
    if (!netIsHost()) return;
    unsigned cap; int on=netGetHostEqualization(&cap);
    int used=snprintf(text,size,"HOST EQ: %s CAP %ums",on ? "ON" : "OFF",cap);
    for (int i=0;i<GEVR_MAX_PLAYERS && used>=0 && (unsigned)used<size;i++)
        if (i!=s_local_slot && s_lobby_state.slots[i].connected)
            used += snprintf(text+used,size-used," P%d=%ums",i+1,netGetSlotHostDelayMs(i));
}

/* Reset at the actual warmup -> match boundary, after every player has loaded.
 * A late join's in-progress snapshot must retain the existing round scores. */
static void netTransitionRoundPhase(NetPhase phase)
{
    if (phase == NET_PHASE_IN_PROGRESS && s_phase == NET_PHASE_WARMUP && !s_waiting_for_match_snapshot)
    {
        for (int i=0;i<GEVR_MAX_PLAYERS;i++)
        {
            g_playerPlayerData[i].kill_count=0;
            g_playerPlayerData[i].killed_gg_owner_count=0;
            g_playerPlayerData[i].gevr_score_bank=0;
            g_playerPlayerData[i].flag_counter=0;
            g_playerPlayerData[i].order_out_in_yolt=0;
            memset(g_playerPlayerData[i].kill_counts,0,sizeof(g_playerPlayerData[i].kill_counts));
        }
        s_round.departed_score[0]=s_round.departed_score[1]=0;
    }
    if (phase != s_phase) netClearHostHits();
    s_phase=phase;
}

static bool netValidConfig(const NetMatchConfig *c) { return netMatchConfigValid(c) != 0; }

/* The slots a match opens: the host's count, or a team scenario's size when
 * that is larger. Joins stop at netConfigMaxPlayers (netGetMaxPlayers), so a
 * team match keeps working with a gap a departed player left in the slots. */
static int netConfigSlots(const NetMatchConfig *c) {
    int players = netConfigMaxPlayers(c);
    /* a co-op party: the campaign's four, never the deathmatch count riding along (#94) */
    if (c->mode == NET_MODE_COOP) return players;
    return c->max_players > players ? c->max_players : players;
}

static bool netUpdateBots(bool next_round);
static void netLatchRoundSettings(void) {
    netUpdateBots(true);   /* the next round's bots, before its roster is frozen */
    s_round.config = s_lobby_state.config;
    if (s_round.config.mode == NET_MODE_COOP) {
        /* the solo mission's rules, not the host's deathmatch settings riding
         * along: no spawn kits, a second gun of a kind pairs as in solo (#94) */
        s_round.config.scenario = SCENARIO_NORMAL;
        s_round.config.loadouts = 0;
        s_round.config.dual_wield = NET_DUAL_DOUBLES;
        s_round.config.game_length = 0;
    }
    s_round.departed_score[0] = s_round.departed_score[1] = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        s_round.team[i] = s_lobby_state.slots[i].team;
        s_round.character[i] = s_lobby_state.slots[i].chr_id;
        memcpy(s_round.loadout[i], s_lobby_state.slots[i].loadout, 4);
    }
    s_max_players = netConfigSlots(&s_round.config);
    s_lobby_max_players = (uint8_t)s_max_players;
}

static void netWriteRoundSettings(struct netbuf *buf) {
    netWriteCombatIdentity(buf);
    netbufWriteMatchConfig(buf, &s_round.config);
    netbufWriteU32(buf, s_round.world_epoch);
    for (int t=0;t<2;t++) netbufWriteU32(buf, (uint32_t)s_round.departed_score[t]);
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        netbufWriteU8(buf, s_round.team[i]);
        netbufWriteU8(buf, s_round.character[i]);
        for (int k = 0; k < 4; k++) netbufWriteU8(buf, s_round.loadout[i][k]);
    }
}

static bool netReadRoundSettings(struct netbuf *buf, NetRoundSettings *r) {
    if(!netReadCombatIdentity(buf,r))return false;
    netbufReadMatchConfig(buf, &r->config);
    r->world_epoch = netbufReadU32(buf);
    for (int t=0;t<2;t++) r->departed_score[t] = (int32_t)netbufReadU32(buf);
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        r->team[i] = netbufReadU8(buf);
        if (r->team[i] > NET_TEAM_NONE) return false;
        r->character[i] = netbufReadU8(buf);
        if (r->character[i] >= netCharacterCount()) return false;
        for (int k = 0; k < 4; k++) {
            r->loadout[i][k] = netbufReadU8(buf);
            if (r->loadout[i][k] && netItemIndexOf(r->loadout[i][k]) < 0) return false;
        }
    }
    return !buf->error && netValidConfig(&r->config);
}

int netActiveDualWield(void) { return netIsActive() ? s_round.config.dual_wield : 0; }
int netActiveLoadoutItem(int slot, int k) {
    return netIsActive() && s_round.config.loadouts && slot >= 0 && slot < GEVR_MAX_PLAYERS &&
           k >= 0 && k < 4 && netItemIndexOf(s_round.loadout[slot][k]) >= 0 ? s_round.loadout[slot][k] : 0;
}
const NetMatchConfig *netGetActiveMatchConfig(void) { return &s_round.config; }
int netDamageAllowed(int attacker, int target) {
    if (!netIsActive()) return 1;
    if (target < 0 || target >= GEVR_MAX_PLAYERS || attacker >= GEVR_MAX_PLAYERS) return 0;
    if (attacker < 0) return 1; /* Environmental damage has no player attacker. */
    /* co-op: the guards are the enemy; a teammate hurts only with friendly fire on */
    if (s_round.config.mode == NET_MODE_COOP) return attacker == target || s_round.config.friendly_fire;
    return netTeamDamageAllowed(s_round.config.scenario, s_round.config.friendly_fire,
        attacker == target, s_round.team[attacker], s_round.team[target]);
}
/* Every stage takes the host's player count (net_match.c), so any listed stage
 * can be picked, voted for or rotated to whoever is in the lobby. */
int netStageEligible(int idx) {
    return idx >= 0 && idx < netStageCount();
}
static char s_slot_app_version[GEVR_MAX_PLAYERS][32];
static char s_local_app_version[32] = "";

extern char VrPlayerName[];   /* port/vr/vr_settings_defaults.c: the launcher's "Your name" */

/*
 * A name off the wire, as the lobby keeps it: printable ASCII (the game's font
 * draws it over the player's head, and '|' separates the lobby service's
 * fields), at most the launcher's 15 characters, trimmed, never empty. Read no
 * further than end: the sender's terminator isn't trusted.
 */
static void netCleanName(char *dst, const char *src, const char *end)
{
    int n = 0;
    for (const char *c = src; c && c < end && *c && n < 15; c++) {
        if (*c >= 0x20 && *c <= 0x7e && *c != '|' && (n > 0 || *c != ' ')) dst[n++] = *c;
    }
    while (n > 0 && dst[n - 1] == ' ') n--;
    dst[n] = '\0';
    if (n == 0) snprintf(dst, GEVR_MAX_NAME_LEN, "Player");
}

/* Host: a joining player's name, numbered when it's someone else's already ("Agent 2"). */
static void netSetJoinerName(int slot, const char *src, const char *end)
{
    char clean[GEVR_MAX_NAME_LEN];
    char *dst = s_lobby_state.slots[slot].name;
    netCleanName(clean, src, end);
    snprintf(dst, GEVR_MAX_NAME_LEN, "%s", clean);
    for (int k = 2; k <= GEVR_MAX_PLAYERS + 1; k++) {
        bool taken = false;
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
            if (i != slot && s_lobby_state.slots[i].connected
                && strcasecmp(s_lobby_state.slots[i].name, dst) == 0) taken = true;
        }
        if (!taken) return;
        snprintf(dst, GEVR_MAX_NAME_LEN, "%.13s %d", clean, k);
    }
}

static uint32_t s_rng_seed = 0;

uint32_t netGetRandomSeed(void) {
    return s_rng_seed;
}

/* ENet Peer to slot mapping on host */
extern s32 D_80048394;
extern s32 D_800483A8;
static void netBroadcastBuf(struct netbuf *buf, uint8_t channel, uint32_t flags, ENetPeer *except);
static void netHostDropSlot(int slot, ENetPeer *stale);
static void netHostLost(ENetPeer *peer);

/*
 * A player leaves: the kills against them go to their killers' score bank
 * (gevr_score_bank, counted in the points and the awards with the
 * per-victim table). The bank was kill_count until 2026-09-30, which the
 * game's kill message also increments on every kill, so every kill scored
 * twice (three kills read as six, the cap of five was skipped).
 */
static void netForgetPlayerScore(int slot) {
    netInvalidateHitSlot(slot);
    if(netIsHost() && slot>=0 && slot<GEVR_MAX_PLAYERS) {if(!++s_hit_life[slot])++s_hit_life[slot];memset(&s_clock_sync[slot],0,sizeof(s_clock_sync[slot]));s_clock_probe_us[slot]=0;s_clock_pending[slot].nonce=0;}
    if (slot >= 0 && slot < GEVR_MAX_PLAYERS) s_hit_move_seen[slot]=false;
    if (s_state != NET_STATE_INGAME || slot < 0 || slot >= GEVR_MAX_PLAYERS) return;
    if (netScenarioHasTeams(s_round.config.scenario) && s_round.team[slot] < 2) {
        int points = g_playerPlayerData[slot].gevr_score_bank;
        for (int v=0;v<GEVR_MAX_PLAYERS;v++)
            points += netTeamKillPoints(s_round.team[slot], s_round.team[v], g_playerPlayerData[slot].kill_counts[v]);
        s_round.departed_score[s_round.team[slot]] += points;
    }
    for (int shooter = 0; shooter < GEVR_MAX_PLAYERS; shooter++) {
        if (shooter != slot) {
            g_playerPlayerData[shooter].gevr_score_bank += netScenarioHasTeams(s_round.config.scenario) ?
                netTeamKillPoints(s_round.team[shooter], s_round.team[slot], g_playerPlayerData[shooter].kill_counts[slot]) :
                g_playerPlayerData[shooter].kill_counts[slot];
            g_playerPlayerData[shooter].kill_counts[slot] = 0;
        }
    }
    memset(g_playerPlayerData[slot].kill_counts, 0, sizeof(g_playerPlayerData[slot].kill_counts));
    g_playerPlayerData[slot].gevr_score_bank = 0;
    g_playerPlayerData[slot].kill_count = 0;
}

static void netResetLobbyState(void) {
    netClearHostHits();
    if(netIsHost())netResetCombatEpoch();
    else {s_combat_epoch=0;memset(s_hit_life,0,sizeof(s_hit_life));memset(s_clock_sync,0,sizeof(s_clock_sync));}
    memset(&s_lobby_state, 0, sizeof(s_lobby_state));
    s_lobby_state.header.magic = GEVR_NET_MAGIC;
    s_lobby_state.header.version = GEVR_NET_VERSION;
    s_lobby_state.header.msg_type = NET_MSG_LOBBY_STATE;
    /* the defaults; the host's launcher page sets its own before hosting */
    s_lobby_state.config.stage = 27;            /* Bunker II */
    s_lobby_state.config.scenario = 0;          /* Normal */
    s_lobby_state.config.weapon_set = 4;        /* Power Weapons */
    s_lobby_state.config.game_length = 2;       /* 10 minutes */
    s_lobby_state.config.friendly_fire = 1;
    s_lobby_state.config.health = 5;            /* Normal */
    s_lobby_state.config.max_players = 4;       /* the host picks 2..8 */
    s_lobby_state.config.custom_set[0] = ITEM_TT33;   /* the Rockets set's guns, until the host picks */
    s_lobby_state.config.custom_set[1] = ITEM_SKORPION;
    s_lobby_state.config.custom_set[2] = ITEM_AK47;
    s_lobby_state.config.custom_set[3] = ITEM_ROCKETLAUNCH;
    s_lobby_state.config.bot_count = 3;
    s_lobby_state.config.bot_difficulty = NET_BOT_NORMAL;
    netLatchRoundSettings();
    s_lobby_open = false;
    s_warmup_started = s_start_requested = s_vote_requested = false;
    s_warmup_end_us = 0;
    memset(s_client_can_be_kicked, 0, sizeof(s_client_can_be_kicked));
    memset(s_client_caps, 0, sizeof(s_client_caps));
    memset(s_client_caps_known, 0, sizeof(s_client_caps_known));
    s_match_ended = false;
    s_results_deadline_us = 0;
    s_lobby_code[0] = '\0';
    s_lobby_token[0] = '\0';
    s_game_name[0] = '\0';
    s_lobby_max_players = GEVR_MAX_PLAYERS;
    s_host_heard_us = 0;
    
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        s_remote_active[i] = false;
        s_client_peers[i] = NULL;
        s_slot_app_version[i][0] = '\0';
        s_slot_grace_us[i] = 0;
        s_slot_heard_us[i] = 0;
        s_lobby_state.slots[i].team = s_round.team[i] = NET_TEAM_NONE;
        s_lobby_state.slots[i].ping_ms = NET_PING_UNKNOWN;
        memset(&s_remote_moves[i], 0, sizeof(s_remote_moves[i]));
        memset(&s_remote_players[i], 0, sizeof(s_remote_players[i]));
    }
}

bool netInit(void) {
    netClearVotes(-1);   /* -1 is "no vote"; zero would be the first stage (see netLobbyHostLaunchMatch) */
    if (s_initialized) return true;
    
    if (enet_initialize() != 0) {
        NET_ERR("Failed to initialize ENet!");
        return false;
    }
    
    s_initialized = true;
    s_state = NET_STATE_OFFLINE;
    netResetLobbyState();
    NET_LOG("Network subsystem initialized successfully.");
    return true;
}

void netShutdown(void) {
    if (!s_initialized) return;
    
    netDisconnect();
    netIceStop();
    enet_deinitialize();
    s_initialized = false;
    s_state = NET_STATE_OFFLINE;
    NET_LOG("Network subsystem shutdown.");
}

bool netHostStart(uint16_t port) {
    if (!s_initialized && !netInit()) return false;
    netDisconnect();
    
    ENetAddress address;
    enet_address_set_ip(&address, "0.0.0.0");
    address.port = port ? port : GEVR_DEFAULT_PORT;
    
    s_host = enet_host_create(&address, NET_HOST_PEERS, NET_CHAN_MAX, 0, 0, 0);
    if (!s_host) {
        NET_ERR("Failed to create ENet host on port %d!", address.port);
        return false;
    }
    enet_host_set_virtual_transport(s_host, s_virtual_send, s_virtual_receive, s_virtual_context);
    
    s_state = NET_STATE_HOSTING_LOBBY;
    s_phase = NET_PHASE_WAITING;
    s_max_players = GEVR_MAX_PLAYERS;
    s_host_slot = 0;
    s_round_reset_pending = false;
    s_round_reset_loading = false;
    s_start_after_load = false;
    s_next_round_at_us = 0;
    s_local_slot = 0;
    netResetLobbyState();
    
    /* Host occupies slot 0 */
    s_lobby_state.slots[0].connected = 1;
    s_lobby_state.slots[0].ready = 1;
    s_lobby_state.slots[0].chr_id = 0; /* James Bond */
    netCleanName(s_lobby_state.slots[0].name, VrPlayerName, VrPlayerName + strlen(VrPlayerName));
    strncpy(s_slot_app_version[0], s_local_app_version, sizeof(s_slot_app_version[0]) - 1);
    
    player_char[0] = 0;
    
    NET_LOG("Multiplayer server hosted on port %d", address.port);
    return true;
}

bool netConnect(const char *host_addr, uint16_t port) {
    if (!s_initialized && !netInit()) return false;
    /* To the new host after the old one left (netHostLost): the slot, the
     * lobby and the match stay; only the connection is new. */
    const bool rejoin = s_state == NET_STATE_MIGRATING;
    if (rejoin) {
        if (s_host) {
            enet_host_destroy(s_host);
            s_host = NULL;
        }
        s_server_peer = NULL;
    } else {
        netDisconnect();
    }

    s_host = enet_host_create(NULL, 1, NET_CHAN_MAX, 0, 0, 0);
    if (!s_host) {
        NET_ERR("Failed to create client ENet host!");
        return false;
    }
    enet_host_set_virtual_transport(s_host, s_virtual_send, s_virtual_receive, s_virtual_context);
    
    ENetAddress address;
    enet_address_set_ip(&address, host_addr ? host_addr : "127.0.0.1");
    address.port = port ? port : GEVR_DEFAULT_PORT;
    
    s_server_peer = enet_host_connect(s_host, &address, NET_CHAN_MAX, 0);
    if (!s_server_peer) {
        NET_ERR("Failed to initiate connection to %s:%d!", host_addr, address.port);
        enet_host_destroy(s_host);
        s_host = NULL;
        return false;
    }
    enet_peer_timeout(s_server_peer, 32, 3000, 6000); /* a vanished host is noticed in seconds */
    
    if (!rejoin) {
        s_state = NET_STATE_CONNECTING;
        s_local_slot = -1;
        netResetLobbyState();
    }

    NET_LOG("Connecting to %s:%d...%s", host_addr, address.port, rejoin ? " (rejoining as slot)" : "");
    return true;
}

void netDisconnect(void) {
    netClearHostHits();s_combat_epoch=0;memset(s_clock_sync,0,sizeof(s_clock_sync));
    netVoiceReset();
    s_waiting_for_match_snapshot = false;
    s_stage_ready_sent = false;
    if (!s_host) return;
    
    if (s_server_peer) {
        enet_peer_disconnect_now(s_server_peer, 0);
        s_server_peer = NULL;
    }
    
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        if (s_client_peers[i]) {
            enet_peer_disconnect_now(s_client_peers[i], 0);
            s_client_peers[i] = NULL;
        }
    }
    
    enet_host_flush(s_host);
    enet_host_destroy(s_host);
    s_host = NULL;
    
    s_state = NET_STATE_OFFLINE;
    s_phase = NET_PHASE_WAITING;
    s_round_reset_pending = false;
    s_round_reset_loading = false;
    s_start_after_load = false;
    s_next_round_at_us = 0;
    s_local_slot = 0;
    s_host_slot = 0;
    s_takeover_pending = false;
    s_rejoining = false;
    s_migrate_deadline_us = 0;
    netResetLobbyState();
    NET_LOG("Disconnected and reset network state.");
}

NetState netGetState(void) {
    return s_state;
}

bool netIsActive(void) {
    return s_state != NET_STATE_OFFLINE;
}

bool netIsHost(void) {
    return s_state == NET_STATE_HOSTING_LOBBY || (s_state == NET_STATE_INGAME && s_local_slot == s_host_slot);
}

int netGetLocalSlot(void) {
    return s_local_slot;
}

/*
 * The stage and weapons as the game lists show them (the lobby service's
 * entry, the LAN beacon): one byte each, so a co-op game lists its mission's
 * LEVELID with the top bit set (NET_LOBBY_COOP_STAGE) and its difficulty in
 * the weapons' place. Level ids stay under 0x80.
 */
static const NetMatchConfig *netListedConfig(void) {
    return s_state == NET_STATE_INGAME || s_state == NET_STATE_MIGRATING ? &s_round.config : &s_lobby_state.config;
}

uint8_t netGetLobbyStage(void) {
    const NetMatchConfig *c = netListedConfig();
    return c->mode == NET_MODE_COOP ? (uint8_t)(NET_LOBBY_COOP_STAGE | c->stage) : c->stage;
}

uint8_t netGetLobbyWeaponSet(void) {
    const NetMatchConfig *c = netListedConfig();
    return c->mode == NET_MODE_COOP ? c->difficulty : c->weapon_set;
}

int netGetConnectedPlayerCount(void) {
    int count = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        if (s_lobby_state.slots[i].connected) count++;
    }
    return count;
}

/*
 * Bots: player slots the host runs with AI input (gevr_bot.c). In the
 * roster, the scores and the start gates they are players; joins, the LAN
 * beacon and the host election count the humans.
 */
bool netSlotIsBot(int slot) {
    return slot >= 0 && slot < GEVR_MAX_PLAYERS && s_lobby_state.slots[slot].connected && s_lobby_state.slots[slot].is_bot;
}

/* This headset decides the slot's life and sends its events: its own, and
 * on the host the bots. During a host change no one owns the bots. */
bool netSlotOwned(int slot) {
    return slot >= 0 && slot < GEVR_MAX_PLAYERS && (slot == s_local_slot || (netIsHost() && netSlotIsBot(slot)));
}
int gevrNetOwnsSlot(int slot) { return netSlotOwned(slot); }

/* The owned player ticking now, whose events go out; -1 for a copy */
static int netActingSlot(void) {
    int slot = get_cur_playernum();
    return netSlotOwned(slot) ? slot : -1;
}

int netGetHumanPlayerCount(void) {
    int count = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        if (s_lobby_state.slots[i].connected && !s_lobby_state.slots[i].is_bot) count++;
    return count;
}

/* A bot leaves: its kills are kept as a departure's (netForgetPlayerScore) */
static void netReleaseBotSlot(int slot) {
    netForgetPlayerScore(slot);
    s_remote_active[slot] = false;
    memset(&s_lobby_state.slots[slot], 0, sizeof(NetLobbySlot));
    s_lobby_state.slots[slot].team = NET_TEAM_NONE;
    s_lobby_state.slots[slot].ping_ms = NET_PING_UNKNOWN;
}

/* A bot takes an empty slot: one of the eight leads no one plays, or the
 * body the loaded stage already has for the slot (a warmup refill) */
static void netAddBot(int slot, bool in_level) {
    NetLobbySlot *s = &s_lobby_state.slots[slot];
    bool taken[64] = { false };
    int chr = in_level ? s_round.character[slot] : -1;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        if (s_lobby_state.slots[i].connected && s_lobby_state.slots[i].chr_id < 64) taken[s_lobby_state.slots[i].chr_id] = true;
    for (int n = 0; chr < 0 && netBotCharacter(n) >= 0; n++)
        if (!taken[netBotCharacter(n)]) chr = netBotCharacter(n);
    for (int k = 0; chr < 0 && k < netCharacterCount() && k < 64; k++)
        if (!taken[k]) chr = k;
    if (chr < 0 || chr >= netCharacterCount()) chr = 0;
    memset(s, 0, sizeof(*s));
    s->connected = s->ready = s->is_bot = 1;
    s->loaded = in_level;
    s->chr_id = (uint8_t)chr;
    s->team = NET_TEAM_NONE;
    s->ping_ms = NET_PING_UNKNOWN;
    memcpy(s->loadout, s_lobby_state.config.custom_set, 4);
    snprintf(s->name, sizeof(s->name), "%s (Bot)", netCharacterName(chr));
    s_client_peers[slot] = NULL;
    s_remote_active[slot] = false;
    s_slot_grace_us[slot] = 0;
    s_slot_heard_us[slot] = 0;
    s_slot_app_version[slot][0] = '\0';
}

/*
 * The host's bots follow the config: Fill takes every place no human holds,
 * Fixed keeps bot_count of them, inside the match's player count, and in a
 * team scenario they fill the sides the humans leave open. A round in
 * progress keeps its players (a joiner still takes a bot's place,
 * netBotToReplace); next_round is the latch for the round about to load.
 * Returns whether the roster changed; the caller broadcasts it.
 */
static bool netUpdateBots(bool next_round) {
    if (!netIsHost() || s_state == NET_STATE_MIGRATING) return false;
    if (!next_round && s_state == NET_STATE_INGAME && s_phase == NET_PHASE_IN_PROGRESS) return false;
    const NetMatchConfig *c = &s_lobby_state.config;
    const bool in_level = !next_round && s_state == NET_STATE_INGAME && !s_round_reset_loading;
    int slots = netConfigSlots(c);
    if (in_level && slots > s_max_players) slots = s_max_players;   /* the loaded stage has these players */
    if (slots > GEVR_MAX_PLAYERS) slots = GEVR_MAX_PLAYERS;
    int room = netConfigMaxPlayers(c) - netGetHumanPlayerCount(), target = 0;
    if (c->mode != NET_MODE_COOP && c->bot_mode != NET_BOT_OFF)
        target = c->bot_mode == NET_BOT_FILL || c->bot_count > room ? room : c->bot_count;
    if (target < 0) target = 0;
    bool changed = false;
    int bots = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        if (!netSlotIsBot(i)) continue;
        if (i >= slots) { netReleaseBotSlot(i); changed = true; }
        else bots++;
    }
    for (int i = GEVR_MAX_PLAYERS - 1; i >= 0 && bots > target; i--)
        if (netSlotIsBot(i)) { netReleaseBotSlot(i); bots--; changed = true; }
    for (int i = 0; i < slots && bots < target; i++) {
        if (i == s_host_slot || s_lobby_state.slots[i].connected) continue;
        netAddBot(i, in_level);
        bots++;
        changed = true;
    }
    /* The sides: humans first, a bot keeps its side while it has room, the rest balance */
    const bool teams = c->mode != NET_MODE_COOP && netScenarioHasTeams(c->scenario);
    int room_on[2] = { 0, 0 };
    if (teams) {
        room_on[0] = netTeamCapacity(c->scenario, 0);
        room_on[1] = netTeamCapacity(c->scenario, 1);
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
            if (s_lobby_state.slots[i].connected && !s_lobby_state.slots[i].is_bot && s_lobby_state.slots[i].team < 2)
                room_on[s_lobby_state.slots[i].team]--;
    }
    bool placed[GEVR_MAX_PLAYERS] = { false };
    for (int i = 0; teams && i < GEVR_MAX_PLAYERS; i++) {
        uint8_t t = s_lobby_state.slots[i].team;
        if (netSlotIsBot(i) && t < 2 && room_on[t] > 0) { room_on[t]--; placed[i] = true; }
    }
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        if (!netSlotIsBot(i)) continue;
        s_lobby_state.slots[i].ready = 1;   /* a bot consents to everything */
        if (placed[i]) continue;
        uint8_t t = NET_TEAM_NONE;
        if (teams && (room_on[0] > 0 || room_on[1] > 0)) t = room_on[0] >= room_on[1] ? 0 : 1;
        if (t < 2) room_on[t]--;
        if (s_lobby_state.slots[i].team != t) { s_lobby_state.slots[i].team = t; changed = true; }
    }
    if (changed) NET_LOG("bots: %d of %d (%s, %s)", bots, target, netBotModeName(c->bot_mode), netBotDifficultyName(c->bot_difficulty));
    return changed;
}

/* A bot's points, as the scoreboard counts them: kills, less suicides */
static int netBotPoints(int slot) {
    int points = g_playerPlayerData[slot].gevr_score_bank;
    for (int j = 0; j < GEVR_MAX_PLAYERS; j++)
        points += j == slot ? -g_playerPlayerData[slot].kill_counts[j] : g_playerPlayerData[slot].kill_counts[j];
    return points;
}

/* The bot a joiner replaces when every place is taken: in a team match one
 * from the side with more bots, then the lowest score (the later slot on a tie) */
static int netBotToReplace(void) {
    const bool ingame = s_state == NET_STATE_INGAME;
    int bots_on[2] = { 0, 0 }, side = -1, best = -1, best_points = 0;
    for (int i = 0; i < s_max_players; i++) {
        int t = ingame ? s_round.team[i] : s_lobby_state.slots[i].team;
        if (netSlotIsBot(i) && t < 2) bots_on[t]++;
    }
    if (netScenarioHasTeams((ingame ? &s_round.config : &s_lobby_state.config)->scenario) && bots_on[0] + bots_on[1] > 0)
        side = bots_on[1] > bots_on[0] ? 1 : 0;
    for (int i = 0; i < s_max_players; i++) {
        int t = ingame ? s_round.team[i] : s_lobby_state.slots[i].team;
        if (!netSlotIsBot(i) || i == s_host_slot || (side >= 0 && t != side)) continue;
        int points = netBotPoints(i);
        if (best < 0 || points <= best_points) { best = i; best_points = points; }
    }
    return best;
}

/* Menus need the connection roster, including loading players and spectators.
 * netSlotOccupied remains the separate list of characters in the world. */
int netLobbySlotConnected(int slot) {
    return slot >= 0 && slot < GEVR_MAX_PLAYERS && s_lobby_state.slots[slot].connected;
}

/* The fewest players the host can pick now: every connected slot must stay
 * inside the count (netLobbySetConfig refuses less), at least two. */
int netLobbyMinPlayers(void) {
    int min = 2;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        if (s_lobby_state.slots[i].connected && !s_lobby_state.slots[i].is_bot && i + 1 > min) min = i + 1;
    return min;
}

/* The players who may be in the match: the loaded round's count in a match,
 * the host's current choice in the lobby (LAN beacon, internet lobby, roster). */
int netGetMaxPlayers(void) {
    const bool ingame = s_state == NET_STATE_INGAME || s_state == NET_STATE_MIGRATING;
    return netConfigMaxPlayers(ingame ? &s_round.config : &s_lobby_state.config);
}

NetPhase netGetPhase(void) {
    return s_phase;
}

int netPlayerInRound(int slot) {
    return slot >= 0 && slot < s_max_players && s_lobby_state.slots[slot].connected && !netSlotIsSpectator(slot);
}

bool netSlotOccupied(int slot) {
    return slot >= 0 && slot < s_max_players &&
           (slot == s_local_slot || (s_lobby_state.slots[slot].connected &&
            (s_state != NET_STATE_INGAME ||
             (s_lobby_state.slots[slot].loaded && !s_lobby_state.slots[slot].spectator))));
}

/* The stage a round reset loads: the config's, the party's menus being the title stage (co-op, #94) */
int netRoundLoadStage(void) {
    if (s_round.config.mode == NET_MODE_COOP && s_round.config.stage == NET_COOP_FRONT_STAGE) return LEVELID_TITLE;
    return s_round.config.stage;
}

bool netTakeRoundReset(void) {
    bool pending = s_round_reset_pending;
    s_round_reset_pending = false;
    /* The stage is about to reload and the players' structs with it: world
     * events wait for the new stage's first tick (netPlayersWereTicked). */
    if (pending) netPlayersTickedReset();
    return pending;
}

static void netBroadcastRoundPhase(NetPhase phase) {
    u8 raw[16];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_ROUND_PHASE);
    netbufWriteU8(&buf, 0);
    netbufWriteU8(&buf, (uint8_t)phase);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    netTransitionRoundPhase(phase);
    for (int i=0;i<GEVR_MAX_PLAYERS;i++) netVoiceForgetSlot((uint8_t)i);
}

const NetMsgLobbyState *netGetLobbyState(void) {
    return &s_lobby_state;
}

const char *netGetSlotName(int slot) {
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS || !s_lobby_state.slots[slot].connected) return NULL;
    return s_lobby_state.slots[slot].name;
}

static void netBroadcastPacket(const void *data, size_t size, uint8_t channel, uint32_t flags, ENetPeer *except) {
    if (!s_host) return;
    
    if (netIsHost()) {
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
            if (s_client_peers[i] && s_client_peers[i] != except) {
                ENetPacket *packet = enet_packet_create(data, size, flags);
                enet_peer_send(s_client_peers[i], channel, packet);
            }
        }
    } else if (s_server_peer && s_server_peer != except) {
        ENetPacket *packet = enet_packet_create(data, size, flags);
        enet_peer_send(s_server_peer, channel, packet);
    }
}

void netSetLocalAppVersion(const char *version) {
    if (!version) return;
    strncpy(s_local_app_version, version, sizeof(s_local_app_version) - 1);
    s_local_app_version[sizeof(s_local_app_version) - 1] = '\0';
}

const char *netGetSlotAppVersion(int slot) {
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS || !s_lobby_state.slots[slot].connected) return NULL;
    return s_slot_app_version[slot];
}

static void netSendLocalAppVersion(ENetPeer *target_peer) {
    if (!s_local_app_version[0]) return;
    u8 raw[64];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_APP_VERSION);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteStr(&buf, s_local_app_version);
    ENetPacket *packet = enet_packet_create(buf.data, buf.wp, ENET_PACKET_FLAG_RELIABLE);
    if (target_peer) {
        enet_peer_send(target_peer, NET_CHAN_RELIABLE, packet);
    } else {
        netBroadcastPacket(buf.data, buf.wp, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    }
}

/* Helper to broadcast a netbuf */
static void netBroadcastBuf(struct netbuf *buf, uint8_t channel, uint32_t flags, ENetPeer *except) {
    if (!buf || buf->error || buf->wp == 0) return;
    netBroadcastPacket(buf->data, buf->wp, channel, flags, except);
}

static void netSendClientCaps(void) {
    u8 raw[9];struct netbuf buf={.data=raw,.size=sizeof(raw)};
    netbufStartWrite(&buf);
    netbufWriteU32(&buf,GEVR_NET_MAGIC);netbufWriteU16(&buf,GEVR_NET_VERSION);
    netbufWriteU8(&buf,NET_MSG_CLIENT_CAPS);netbufWriteU8(&buf,(uint8_t)s_local_slot);
    netbufWriteU8(&buf,NET_CLIENT_CAP_KICK|NET_CLIENT_CAP_NO_RADAR);
    netBroadcastBuf(&buf,NET_CHAN_RELIABLE,ENET_PACKET_FLAG_RELIABLE,NULL);
}

static void netReceiveClientCaps(ENetPeer *peer, int slot, struct netbuf *buf, size_t size) {
    if (!netIsHost() || !peer || size!=9 || !netLobbySlotConnected(slot) ||
        s_client_peers[slot]!=peer || (int)(intptr_t)peer->data-1!=slot) return;
    unsigned caps=netbufReadU8(buf);
    if (buf->error || netbufReadLeft(buf)) return;
    s_client_can_be_kicked[slot]=(caps & NET_CLIENT_CAP_KICK)!=0;
    s_client_caps[slot]=(uint8_t)caps;
    s_client_caps_known[slot]=true;
}

/*
 * No radar (protocol 19). Protocol 19 already turns v0.4.11 and older away;
 * this is the rule's own guarantee: while it is pending or live, the host
 * removes a guest that has said it does not know it, which would reject the
 * config and sit in a lobby it cannot read.
 */
static int netHostRemoveOldForNoRadar(void) {
    int removed = 0;
    if (!netIsHost() || (s_state != NET_STATE_INGAME && s_state != NET_STATE_HOSTING_LOBBY)) return 0;
    if (!((s_lobby_state.config.fun_flags | s_round.config.fun_flags) & NET_FUN_NO_RADAR)) return 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        if (i == s_local_slot || !s_client_caps_known[i] || (s_client_caps[i] & NET_CLIENT_CAP_NO_RADAR)) continue;
        if (!netHostKickPlayer(i)) continue;
        NET_LOG("Slot %d runs an app without No radar: removed while the rule is on", i);
        removed++;
    }
    if (removed && s_state == NET_STATE_INGAME) {
        extern void hudmsgTopShow(char *mess);
        hudmsgTopShow("NO RADAR: AN OLDER APP WAS REMOVED");
    }
    return removed;
}

static uint64_t s_lobby_received_us, s_last_latency_us;
static void netBroadcastLobbyState(void) {
    u8 raw[512];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_LOBBY_STATE);
    netbufWriteU8(&buf, 0);
    netbufWriteMatchConfig(&buf, &s_lobby_state.config);
    netWriteRoundSettings(&buf);
    netbufWriteU8(&buf, s_lobby_open);
    netbufWriteU8(&buf, s_start_after_load);
    netbufWriteU8(&buf, s_lobby_state.countdown_secs);
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        netbufWriteU8(&buf, s_lobby_state.slots[i].connected);
        netbufWriteU8(&buf, s_lobby_state.slots[i].ready);
        netbufWriteU8(&buf, s_lobby_state.slots[i].loaded);
        netbufWriteU8(&buf, s_lobby_state.slots[i].chr_id);
        netbufWriteU8(&buf, s_lobby_state.slots[i].spectator);
        netbufWriteU8(&buf, s_lobby_state.slots[i].team);
        netbufWriteU8(&buf, s_lobby_state.slots[i].eliminated);
        netbufWriteU8(&buf, s_lobby_state.slots[i].is_bot);
        netbufWriteU16(&buf, s_lobby_state.slots[i].ping_ms);
        for (int k = 0; k < 4; k++) netbufWriteU8(&buf, s_lobby_state.slots[i].loadout[k]);
        netbufWriteStr(&buf, s_lobby_state.slots[i].name);
    }
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

static void netTakeSpecialItem(int slot, int item);

static void netSendMatchSnapshot(ENetPeer *peer) {
    u8 raw[1024];   /* 79 + 49N + 4N^2 bytes: 727 at eight players */
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_MATCH_SNAPSHOT);
    netbufWriteU8(&buf, 0);
    netbufWriteU8(&buf, s_match_ended || g_gameOverFlag != 0);
    netbufWriteU32(&buf, (u32)D_80048394);
    netWriteRoundSettings(&buf);
    netbufWriteMatchConfig(&buf, &s_lobby_state.config);
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        netbufWriteU8(&buf, s_lobby_state.slots[i].team);
        netbufWriteU8(&buf, s_lobby_state.slots[i].eliminated);
        netbufWriteU16(&buf, s_lobby_state.slots[i].ping_ms);
        netbufWriteU8(&buf, (uint8_t)g_playerPlayerData[i].order_out_in_yolt);
    }
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        netbufWriteU32(&buf, (u32)g_playerPlayerData[i].gevr_score_bank);
        for (int j = 0; j < GEVR_MAX_PLAYERS; j++)
            netbufWriteU32(&buf, (u32)g_playerPlayerData[i].kill_counts[j]);
    }
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        struct player *pl = g_playerPointers[i];
        netbufWriteU8(&buf, pl && netSlotOccupied(i) ? 1 : 0);
        netbufWriteF32(&buf, pl && pl->prop ? pl->prop->pos.x : 0);
        netbufWriteF32(&buf, pl && pl->prop ? pl->prop->pos.y : 0);
        netbufWriteF32(&buf, pl && pl->prop ? pl->prop->pos.z : 0);
        netbufWriteF32(&buf, pl ? pl->vv_theta : 0);
        netbufWriteF32(&buf, pl ? pl->vv_verta : 0);
        netbufWriteF32(&buf, pl ? pl->bondhealth : 0);
        netbufWriteF32(&buf, pl ? pl->bondarmour : 0);
        uint8_t special = 0;
        if (pl && netPlayerInRound(i)) {
            int previous = get_cur_playernum();
            set_cur_player(i);
            if (bondinvHasInvItem(ITEM_TOKEN)) special |= 1;
            if (bondinvHasInvItem(ITEM_GOLDENGUN)) special |= 2;
            set_cur_player(previous);
        }
        netbufWriteU8(&buf, special);
    }
    if (buf.error) { NET_LOG("Match snapshot does not fit its buffer"); return; }
    ENetPacket *packet = enet_packet_create(buf.data, buf.wp, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, NET_CHAN_RELIABLE, packet);
}

static bool netSnapshotObjectType(int type) {
    switch (type) {
        case PROPDEF_DOOR:
        case PROPDEF_PROP:
        case PROPDEF_KEY:
        case PROPDEF_MAGAZINE:
        case PROPDEF_COLLECTABLE:
        case PROPDEF_AMMO:
        case PROPDEF_ARMOUR:
            return true;
        default: return false;
    }
}

/* The world-object index both headsets share: 0x9000+ ammo crates, 0x8000+
 * multiplayer weapon slots, else the setup command index (the snapshot's). */
static ObjectRecord *netObjectByIndex(u16 index, u8 type) {
    ObjectRecord *obj = NULL;
    if (index >= 0x9000 && index < 0x9000 + MAX_AMMO_CRATES)
        obj = (ObjectRecord *)&g_AmmoCrates[index - 0x9000];
    else if (index >= 0x8000 && index < 0x8000 + MAX_WEAPON_SLOTS)
        obj = (ObjectRecord *)&g_WeaponSlots[index - 0x8000];
    else if (index < 0x8000 && netSnapshotObjectType(type))
        obj = setupGetPtrToCommandByIndex(index);
    if (!obj || obj->type != type) return NULL;
    return obj;
}

static int netObjectIndex(ObjectRecord *target) {
    if (!target) return -1;
    for (int slot = 0; slot < MAX_AMMO_CRATES; slot++)
        if ((ObjectRecord *)&g_AmmoCrates[slot] == target) return 0x9000 + slot;
    for (int slot = 0; slot < MAX_WEAPON_SLOTS; slot++)
        if ((ObjectRecord *)&g_WeaponSlots[slot] == target) return 0x8000 + slot;
    PropDefHeaderRecord *def = g_CurrentSetup.propDefs;
    if (!def) return -1;
    for (int index = 0; def->type != PROPDEF_END && index < 0x8000; index++) {
        if ((ObjectRecord *)def == target) return index;
        def += sizepropdef(def);
    }
    return -1;
}

void netSendAmmoImpulse(ObjectRecord *obj, const coord3d *dir) {
    if (s_state != NET_STATE_INGAME || !gevrAmmoNetworked(obj) || netLocalIsSpectator()) return;
    if (netIsHost()) { gevrAmmoImpulseWorld(obj, dir); return; }
    int index = netObjectIndex(obj);
    if (index < 0 || index >= 0x8000) return;
    u8 raw[32]; struct netbuf b = {.data=raw,.size=sizeof(raw)};
    netbufStartWrite(&b);
    netbufWriteU32(&b,GEVR_NET_MAGIC);netbufWriteU16(&b,GEVR_NET_VERSION);
    netbufWriteU8(&b,NET_MSG_AMMO_IMPULSE);netbufWriteU8(&b,s_local_slot);
    netbufWriteU8(&b,s_round.config.stage);netbufWriteU32(&b,s_round.world_epoch);netbufWriteU16(&b,index);
    for(int i=0;i<3;i++) netbufWriteF32(&b,dir->f[i]);
    netBroadcastBuf(&b,NET_CHAN_RELIABLE,ENET_PACKET_FLAG_RELIABLE,NULL);
}

/* At 20 Hz the host sends each setup crate's physical state, including
 * velocity for migration. A loaded late joiner also gets a reliable copy. */
static void netSendAmmoState(ENetPeer *peer) {
    u8 raw[512]; struct netbuf b = {.data=raw,.size=sizeof(raw)};
    for(int ordinal=0;;) {
        netbufStartWrite(&b);
        netbufWriteU32(&b,GEVR_NET_MAGIC);netbufWriteU16(&b,GEVR_NET_VERSION);
        netbufWriteU8(&b,NET_MSG_AMMO_STATE);netbufWriteU8(&b,s_host_slot);
        netbufWriteU8(&b,s_round.config.stage);netbufWriteU32(&b,s_round.world_epoch);netbufWriteU8(&b,0);
        int count=0,index; ObjectRecord *obj;
        while(count < 4 && (obj=gevrAmmoAt(ordinal++,&index))) {
            if (!obj->prop) continue;
            NetAmmoState s = {0};
            s.index=index;s.pos=obj->prop->pos;s.runtime_pos=obj->runtime_pos;s.mtx=obj->mtx;
            s.regen=obj->prop->timetoregen;s.enabled=!!(obj->prop->flags&PROPFLAG_ENABLED);
            if ((obj->runtime_bitflags&RUNTIMEBITFLAG_HASPROJECTILE) && obj->projectile && !s.regen) {
                s.moving=1;s.speed=obj->projectile->speed;s.rotation=obj->projectile->mtx;
            }
            if (!netAmmoStateValid(&s)) continue;
            netbufWriteAmmoState(&b,&s);count++;
        }
        if (!count) break;
        raw[13]=count;
        if (peer) {
            ENetPacket *packet=enet_packet_create(raw,b.wp,ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(peer,NET_CHAN_RELIABLE,packet);
        } else netBroadcastBuf(&b,NET_CHAN_PLAYER_STATE,0,NULL);
        if (count < 4) break;
    }
}

static bool netApplyAmmoPacket(struct netbuf *b) {
    int stage=netbufReadU8(b);uint32_t epoch=netbufReadU32(b);int count=netbufReadU8(b);
    if (stage != s_round.config.stage || epoch != s_round.world_epoch || count < 1 || count > 4 ||
        netbufReadLeft(b) != count*NET_AMMO_STATE_BYTES) return false;
    NetAmmoState states[4];
    /* Validate the entire batch before mutating any world object. */
    for(int i=0;i<count;i++) if (!netbufReadAmmoState(b,&states[i])) return false;
    for(int i=0;i<count;i++) {
        NetAmmoState *s=&states[i]; ObjectRecord *obj=netObjectByIndex(s->index,PROPDEF_AMMO);
        if (gevrAmmoNetworked(obj)) gevrAmmoApplyTransform(obj,&s->pos,&s->runtime_pos,&s->mtx,
            s->regen,s->enabled,s->moving,&s->speed,&s->rotation);
    }
    return true;
}

static void netSendObjectEvent(int slot, ObjectRecord *obj, uint8_t action, int8_t value) {
    if (s_state != NET_STATE_INGAME || slot < 0 || !obj) return;
    int index = netObjectIndex(obj);
    /* Only the setup's objects are the same on every headset. The
     * g_WeaponSlots / g_AmmoCrates pools (0x8000+, 0x9000+) are recycled per
     * headset for held models, projectiles and dropped guns, so a slot
     * number names a different object on each one. */
    if (index < 0 || index >= 0x8000) return;

    u8 raw[24];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_OBJECT_STATE);
    netbufWriteU8(&buf, (uint8_t)slot);
    netbufWriteU16(&buf, (uint16_t)index);
    netbufWriteU8(&buf, (uint8_t)obj->type);
    netbufWriteU8(&buf, action);
    netbufWriteS8(&buf, value);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    NET_LOG("object tx: index 0x%04x type %d action %d value %d", index, obj->type, action, value);
}

/* chrprop.c propsTickPlayer: the local player collected this (Perfect Dark
 * port-net SVC_PROP_PICKUP carries the tick operation the same way). */
void netSendObjectPickup(ObjectRecord *obj, s32 tickop) {
    if (!netIsActive()) return;
    netSendObjectEvent(netActingSlot(), obj, NET_OBJECT_PICKUP, (int8_t)tickop);
}

/* propobj.c propdoorInteract: the local player opened or closed this door
 * (SVC_PROP_DOOR sends the door's new mode). */
void netSendDoorState(ObjectRecord *door, s32 state) {
    if (!netIsActive()) return;
    netSendObjectEvent(netActingSlot(), door, NET_OBJECT_DOOR, (int8_t)state);
}

/* Co-op (#94), the host: a door its guards, scripts or timers moved, and a
 * script's lock (propobj.c doorActivate, chrai.c AI_DoorSetLock): the same
 * on every headset. A player's own door still goes as netSendDoorState. */
void netSendHostDoorState(ObjectRecord *door, s32 state) {
    if (!netIsHost() || !netCoopActive() || !netPlayersWereTicked()) return;
    if (state != DOORSTATE_OPENING && state != DOORSTATE_CLOSING && state != DOORSTATE_WAITING) return;
    netSendObjectEvent(s_local_slot, door, NET_OBJECT_DOOR, (int8_t)state);
}

void netSendHostDoorLock(ObjectRecord *door) {
    if (!netIsHost() || !netCoopActive() || !netPlayersWereTicked() || !door || door->type != PROPDEF_DOOR) return;
    netSendObjectEvent(s_local_slot, door, NET_OBJECT_DOOR_LOCK, (int8_t)(uint8_t)((DoorRecord *)door)->keyflags);
}

/* chrprop.c: the local player now holds the flag or the Golden Gun, from the
 * setup or a dropped one. The others put it in that copy's inventory
 * (netTakeSpecialItem), so the holder rules (lv.c) and the drop on death run
 * on every headset. A pool object has no shared identity, so the item is
 * the message. */
void netSendSpecialTaken(s32 item) {
    int slot = netActingSlot();
    if (s_state != NET_STATE_INGAME || slot < 0) return;
    if (item != ITEM_GOLDENGUN && item != ITEM_TOKEN) return;
    u8 raw[24];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_OBJECT_STATE);
    netbufWriteU8(&buf, (uint8_t)slot);
    netbufWriteU16(&buf, 0);
    netbufWriteU8(&buf, 0);
    netbufWriteU8(&buf, NET_OBJECT_SPECIAL_TAKEN);
    netbufWriteS8(&buf, (int8_t)(uint8_t)item);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    NET_LOG("special item tx: item %d", item);
}

/* The dropped one it took, if that is what it was, still lies here: the
 * nearest such prop goes. */
static void netTakeSpecialItem(int slot, int item) {
    struct player *pl = slot >= 0 && slot < GEVR_MAX_PLAYERS ? g_playerPointers[slot] : NULL;
    if (!pl || !pl->prop || (item != ITEM_GOLDENGUN && item != ITEM_TOKEN)) return;
    s32 prev = get_cur_playernum();
    set_cur_player(slot);
    bondinvAddInvItem((ITEM_IDS)item);
    set_cur_player(prev);
    PropRecord *closest = NULL;
    f32 best = 300.0f * 300.0f;
    for (PropRecord *prop = chrpropGetActiveTail(); prop != NULL; prop = prop->prev) {
        if (prop->type != PROP_TYPE_WEAPON || prop->weapon == NULL || !(prop->flags & PROPFLAG_ENABLED)) continue;
        if (prop->weapon->weaponnum != item) continue;
        f32 dx = prop->pos.x - pl->prop->pos.x;
        f32 dy = prop->pos.y - pl->prop->pos.y;
        f32 dz = prop->pos.z - pl->prop->pos.z;
        f32 d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < best) {
            best = d2;
            closest = prop;
        }
    }
    if (closest) objFreePermanently((ObjectRecord *)closest->weapon, TRUE);
    NET_LOG("special item rx: slot %d took item %d%s", slot, item, closest ? ", a dropped one" : "");
}

static void netSendWorldSnapshot(ENetPeer *peer) {
    PropDefHeaderRecord *def = g_CurrentSetup.propDefs;
    if (!def) return;
    u8 raw[512];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    int count = 0;
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_WORLD_SNAPSHOT);
    netbufWriteU8(&buf, 0);
    netbufWriteU8(&buf, 0); /* record count, filled before sending */
    /* Setup objects only (source 0): the weapon and ammo-crate pools are
     * recycled per headset, so their slot numbers name different objects on
     * the joiner (see netSendObjectEvent). */
    for (int source = 0; source < 1; source++) {
        int limit = source == 1 ? MAX_WEAPON_SLOTS : MAX_AMMO_CRATES;
        for (int slot = 0; source == 0 ? (def->type != PROPDEF_END && slot < 0x8000) :
             slot < limit; slot++) {
            ObjectRecord *obj;
            u16 index;
            if (source == 0) {
                obj = (ObjectRecord *)def;
                index = (u16)slot;
                def += sizepropdef(def);
                if (!netSnapshotObjectType(obj->type)) continue;
            } else if (source == 1) {
                obj = (ObjectRecord *)&g_WeaponSlots[slot];
                index = (u16)(0x8000 + slot);
            } else {
                obj = (ObjectRecord *)&g_AmmoCrates[slot];
                index = (u16)(0x9000 + slot);
            }
            netbufWriteU16(&buf, index);
            netbufWriteU8(&buf, (u8)obj->type);
            netbufWriteU8(&buf, obj->prop ? 1 : 0);
            netbufWriteU8(&buf, obj->prop && (obj->prop->flags & PROPFLAG_ENABLED) ? 1 : 0);
            netbufWriteU32(&buf, obj->runtime_bitflags &
                           (RUNTIMEBITFLAG_REMOVE | RUNTIMEBITFLAG_DESTROYED | RUNTIMEBITFLAG_BEENOPENED));
            if (source == 0 && obj->type == PROPDEF_DOOR) {
                DoorRecord *door = (DoorRecord *)obj;
                netbufWriteF32(&buf, door->openPosition);
                netbufWriteU8(&buf, (u8)door->openstate);
            } else {
                netbufWriteF32(&buf, 0);
                netbufWriteU8(&buf, 0);
            }
            netbufWriteU32(&buf, obj->prop ? (u32)obj->prop->timetoregen : 0);
            count++;
            if (count == 24) {
                if (!buf.error) {
                    raw[8] = (u8)count;
                    ENetPacket *packet = enet_packet_create(raw, buf.wp, ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(peer, NET_CHAN_RELIABLE, packet);
                }
                netbufStartWrite(&buf);
                netbufWriteU32(&buf, GEVR_NET_MAGIC);
                netbufWriteU16(&buf, GEVR_NET_VERSION);
                netbufWriteU8(&buf, NET_MSG_WORLD_SNAPSHOT);
                netbufWriteU8(&buf, 0);
                netbufWriteU8(&buf, 0);
                count = 0;
            }
        }
    }
    if (count && !buf.error) {
        raw[8] = (u8)count;
        ENetPacket *packet = enet_packet_create(raw, buf.wp, ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, NET_CHAN_RELIABLE, packet);
    }
}

static bool netCombatClocksReady(void) {
    uint64_t now=sysGetMicroseconds();
    for(int i=0;i<GEVR_MAX_PLAYERS;i++)
        if(i!=s_local_slot && netSlotOccupied(i) && s_client_peers[i]
            && s_client_peers[i]->state==ENET_PEER_STATE_CONNECTED && !netClockBest(&s_clock_sync[i],now))return false;
    return true;
}
static bool netAllLoaded(void) {
    for (int i = 0; i < s_max_players; i++)
        if (s_lobby_state.slots[i].connected && !s_lobby_state.slots[i].loaded) return false;
    return true;
}

/* The countdown to the next round, as every headset shows it
 * (net_player_sync.c): the host schedules the round reset and tells the
 * clients when it falls. 0 cancels. */
static uint64_t s_countdown_end_us = 0;
static bool s_stage_fade_in = false;

static void netSendCountdown(uint32_t ms) {
    u8 raw[16];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_COUNTDOWN);
    netbufWriteU8(&buf, 0);
    netbufWriteU32(&buf, ms);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

static void netScheduleRound(uint32_t secs) {
    s_next_round_at_us = sysGetMicroseconds() + (uint64_t)secs * 1000000;
    s_countdown_end_us = s_next_round_at_us;
    netSendCountdown(secs * 1000);
    NET_LOG("Next round in %u s", secs);
}

static void netCancelRound(void) {
    if (!s_next_round_at_us && !s_countdown_end_us) return;
    s_next_round_at_us = 0;
    s_countdown_end_us = 0;
    netSendCountdown(0);
    NET_LOG("Next round countdown cancelled");
}

int netCountdownSecondsLeft(void) {
    uint64_t now = sysGetMicroseconds();
    return s_countdown_end_us > now ? (int)((s_countdown_end_us - now + 999999) / 1000000) : 0;
}

uint64_t netGetCountdownEndUs(void) {
    return s_countdown_end_us;
}

int netWarmupSecondsLeft(void) {
    uint64_t now = sysGetMicroseconds();
    return s_phase == NET_PHASE_WARMUP && s_warmup_end_us > now ?
        (int)((s_warmup_end_us - now + 999999) / 1000000) : 0;
}

int netHostStartRequested(void) { return s_start_requested; }

static void netBroadcastRoundNotice(void) {
    if (!netIsHost() || s_state != NET_STATE_INGAME) return;
    u8 raw[13];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_ROUND_NOTICE);
    netbufWriteU8(&buf, (uint8_t)s_host_slot);
    netbufWriteU8(&buf, (s_start_requested ? NET_NOTICE_READY : 0) |
        (s_vote_requested ? NET_NOTICE_VOTE : 0) | (s_warmup_end_us ? NET_NOTICE_WARMUP : 0));
    uint64_t now = sysGetMicroseconds();
    netbufWriteU32(&buf, s_warmup_end_us > now ? (uint32_t)((s_warmup_end_us - now + 999) / 1000) : 0);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

static void netReceiveRoundNotice(ENetPeer *peer, struct netbuf *buf, size_t size) {
    if (!peer || netIsHost() || peer != s_server_peer || s_state != NET_STATE_INGAME || size != 13) return;
    unsigned flags = netbufReadU8(buf);
    uint32_t ms = netbufReadU32(buf);
    if (buf->error || netbufReadLeft(buf) || (flags & ~7u) || ms > NET_WARMUP_SECONDS * 1000u ||
        (!(flags & NET_NOTICE_WARMUP) && ms)) return;
    s_start_requested = (flags & NET_NOTICE_READY) != 0;
    s_vote_requested = (flags & NET_NOTICE_VOTE) != 0;
    s_warmup_started = (flags & NET_NOTICE_WARMUP) != 0;
    s_warmup_end_us = s_warmup_started ? sysGetMicroseconds() + (uint64_t)ms * 1000 : 0;
}

void netHostRequestVotes(void) {
    if (!netIsHost() || s_state != NET_STATE_INGAME || s_round.config.mode == NET_MODE_COOP ||
        s_lobby_state.config.mode == NET_MODE_COOP ||
        s_lobby_state.config.next_round != NET_NEXT_VOTE || s_round_reset_loading || s_countdown_end_us) return;
    s_vote_requested = true;
    netBroadcastRoundNotice();
}

void netRoundNoticeText(char *text, unsigned size) {
    if (!text || !size) return;
    text[0] = 0;
    if (s_state != NET_STATE_INGAME || s_round.config.mode == NET_MODE_COOP ||
        s_local_slot < 0 || s_local_slot >= GEVR_MAX_PLAYERS || s_countdown_end_us || s_round_reset_loading) return;
    bool need_ready = s_start_requested && s_local_slot != s_host_slot && !netLocalReady();
    bool need_team = need_ready && s_lobby_state.config.mode != NET_MODE_COOP &&
        netScenarioHasTeams(s_lobby_state.config.scenario) &&
        s_lobby_state.slots[s_local_slot].team == NET_TEAM_NONE;
    bool need_vote = s_vote_requested && s_lobby_state.config.next_round == NET_NEXT_VOTE &&
        (netGetVote(NET_BALLOT_STAGE, s_local_slot) < 0 || netGetVote(NET_BALLOT_WEAPONS, s_local_slot) < 0);
    if (need_team) snprintf(text,size,"HOST REQUESTS READY - OPEN MENU: CHOOSE TEAM / READY UP");
    else if (need_ready && need_vote) snprintf(text,size,"HOST REQUESTS READY / VOTES - OPEN MENU TO JOIN IN");
    else if (need_ready) snprintf(text,size,"HOST REQUESTS READY - OPEN MENU: READY UP");
    else if (need_vote) snprintf(text,size,"HOST REQUESTS VOTES - OPEN MENU: NEXT MAP / WEAPONS");
    else if (s_start_requested) snprintf(text,size,"WAITING FOR PLAYERS TO READY UP / LOAD");
    else if (s_phase == NET_PHASE_WARMUP) {
        int seconds = netWarmupSecondsLeft();
        if (seconds > 0) snprintf(text,size,"WARMUP %d:%02d - HOST CAN START EARLY",seconds/60,seconds%60);
        else snprintf(text,size,"WARMUP - WAITING FOR HOST / PLAYERS");
    }
}

/* bondview_r.c: the start pad of a slot at stage load, the slot's entry in a
 * permutation of the pads drawn from the match seed, so every headset puts
 * every player on the same pad and no two players share one. */
int netStartPad(int slot, int padcount) {
    int order[64];
    uint32_t x = s_rng_seed ^ 0x5bd1e995u;
    int n = padcount > 64 ? 64 : padcount;
    if (n <= 0) return 0;
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = n - 1; i > 0; i--) {
        x = x * 1664525u + 1013904223u;
        int j = (int)((x >> 16) % (uint32_t)(i + 1));
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    if (slot < 0) slot = 0;
    return order[slot % n];
}

/* The slots before this one that netStartPad gave its pad: a stage with fewer
 * start pads than players hands them out again (bondview_r.c then stands the
 * player beside the pad). 0 when the pad is the slot's own. */
int netStartPadShare(int slot, int padcount) {
    int n = padcount > 64 ? 64 : padcount;
    return n > 0 && slot > 0 ? slot / n : 0;
}

/* One fade from black per online stage load (net_player_sync.c). */
bool netTakeStageFadeIn(void) {
    bool pending = s_stage_fade_in;
    s_stage_fade_in = false;
    return pending;
}

static void netResolveVotes(void);
static bool s_rotate_next;   /* the next round follows a finished match: rotate (see netResolveVotes) */

static void netBeginRoundReset(bool start) {
    netResetCombatEpoch();
    u8 raw[256];   /* 45 + 10N bytes: 125 at eight players */
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netCancelRound();
    s_round.world_epoch++;
    s_results_deadline_us = 0;
    s_match_ended = false;
    s_start_after_load = start;
    s_lobby_open = !start;
    s_warmup_started = s_start_requested = s_vote_requested = false;
    s_warmup_end_us = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        if (s_lobby_state.slots[i].connected) s_lobby_state.slots[i].loaded = 0;
        s_lobby_state.slots[i].spectator = 0;
        s_lobby_state.slots[i].eliminated = 0;
        s_remote_active[i] = false;
        netVoiceForgetSlot((uint8_t)i);
    }
    netBroadcastLobbyState();
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_ROUND_RESET);
    netbufWriteU8(&buf, (uint8_t)s_host_slot);
    netWriteRoundSettings(&buf);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    netBroadcastRoundPhase(NET_PHASE_WARMUP);
    s_round_reset_pending = true;
    s_round_reset_loading = true;
    netBroadcastRoundNotice();
}

void netHostRoundEnded(void) {
    if (!netIsHost() || s_state != NET_STATE_INGAME || s_phase != NET_PHASE_IN_PROGRESS || s_match_ended) return;
    s_match_ended = true;
    for (int i=0;i<GEVR_MAX_PLAYERS;i++) netVoiceForgetSlot((uint8_t)i);
    s_results_deadline_us = sysGetMicroseconds() + 30000000ull;
    u8 raw[8];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_MATCH_END);
    netbufWriteU8(&buf, (uint8_t)s_host_slot);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    /* Readiness belongs to the next match, including connected spectators. */
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) s_lobby_state.slots[i].ready = 0;
    s_start_requested = true;
    s_vote_requested = s_lobby_state.config.next_round == NET_NEXT_VOTE;
    netBroadcastLobbyState();
    netBroadcastRoundNotice();
}

void netHostContinue(void) {
    if (!netIsHost() || s_state != NET_STATE_INGAME || !s_match_ended || s_next_round_at_us || s_round_reset_loading) return;
    s_results_deadline_us = 0;
    s_start_requested = true;
    s_vote_requested = s_lobby_state.config.next_round == NET_NEXT_VOTE;
    netBroadcastRoundNotice();
    netTryHostStartRequest();
}

void netHostReturnToLobby(void) {
    if (!netIsHost() || s_state != NET_STATE_INGAME || s_round_reset_loading) return;
    if (s_countdown_end_us || s_start_requested) {
        netCancelRound();
        s_start_requested = s_vote_requested = false;
        s_results_deadline_us = s_warmup_end_us = 0;
        /* Keep practicing until the host requests a start again. */
        s_warmup_started = true;
        netBroadcastRoundNotice();
        return;
    }
    NET_LOG("host: lobby");
    /* Keep the active stage and kits. Pending choices wait for START MATCH. */
    netBeginRoundReset(false);
}

void netHostStartRoundNow(void) {
    if (!netHostCanStartRound()) return;
    if (netGetConnectedPlayerCount() == 1) {
        /* Apply the host's pending choices without needing a second player or
         * a complete team roster. Stay joinable in warmup after this load. */
        s_rotate_next = false;
        netClearVotes(-1);
        netLatchRoundSettings();
        netBeginRoundReset(false);
        return;
    }
    s_results_deadline_us = 0;
    s_start_requested = true;
    s_vote_requested = s_lobby_state.config.next_round == NET_NEXT_VOTE;
    netBroadcastRoundNotice();
    netTryHostStartRequest();
}

int netHostCanStartRound(void) {
    if (!netIsHost() || s_state != NET_STATE_INGAME || s_next_round_at_us || s_round_reset_loading
        || s_round.config.mode == NET_MODE_COOP || s_lobby_state.config.mode == NET_MODE_COOP) return 0;
    /* An unready/loading player must not disable the request button. */
    return 1;
}

static bool netWarmupSettingsChanged(void) {
    NetMatchConfig active = s_round.config, pending = s_lobby_state.config;
    active.next_round = pending.next_round = 0;
    active.voice_mode = pending.voice_mode = 0;
    active.friendly_fire = pending.friendly_fire = 0;
    if (memcmp(&active, &pending, sizeof(active))) return true;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) if (netLobbySlotConnected(i)) {
        if (s_lobby_state.slots[i].spectator || s_round.team[i] != s_lobby_state.slots[i].team ||
            s_round.character[i] != s_lobby_state.slots[i].chr_id ||
            (pending.loadouts && memcmp(s_round.loadout[i], s_lobby_state.slots[i].loadout, 4))) return true;
    }
    return false;
}

static void netTryHostStartRequest(void) {
    if (!netIsHost() || !s_start_requested || s_next_round_at_us || s_round_reset_loading ||
        s_round.config.mode == NET_MODE_COOP || s_lobby_state.config.mode == NET_MODE_COOP || netGetConnectedPlayerCount() < 2 ||
        !netRoundRosterReady() || !netAllLoaded() || !netCombatClocksReady()) return;
    s_rotate_next = s_match_ended;
    netResolveVotes();
    if (s_match_ended || s_phase != NET_PHASE_WARMUP || netWarmupSettingsChanged()) {
        /* Load the chosen next map into warmup before starting its countdown. */
        netLatchRoundSettings();
        netBeginRoundReset(false);
    } else {
        s_start_requested = s_vote_requested = false;
        s_warmup_end_us = 0;
        netScheduleRound(10);
        netBroadcastRoundNotice();
    }
}

static void netReadyProgress(void) {
    if (!netIsHost() || !netAllLoaded() || !netCombatClocksReady()) return;
    if (s_round.config.mode == NET_MODE_COOP) {
        /* the next mission (netCoopTick) is under way once everyone has loaded it, alone too */
        if (s_round_reset_loading) {
            s_round_reset_loading = false;
            s_start_after_load = false;
            s_lobby_open = false;
            netBroadcastRoundPhase(NET_PHASE_IN_PROGRESS);
            netBroadcastLobbyState();
        }
        return;
    }
    if (s_round_reset_loading) {
        s_round_reset_loading = false;
        if (s_start_after_load && netGetConnectedPlayerCount() >= 2 && netRoundRosterReady()) {
            s_start_after_load = false;
            s_lobby_open = false;
            netBroadcastRoundPhase(NET_PHASE_IN_PROGRESS);
        } else s_lobby_open = true;
        netBroadcastLobbyState();
    }
    if (s_phase == NET_PHASE_WARMUP && !s_warmup_started && !s_next_round_at_us) {
        s_warmup_started = true;
        s_warmup_end_us = sysGetMicroseconds() + NET_WARMUP_SECONDS * 1000000ull;
        s_lobby_open = true;
        netBroadcastRoundNotice();
    }
}

void netStageLoaded(void) {
    s_coop_ended = false;
    netCoopMenuReset();
    netSpectatorReset();
    netPlayersTickedReset(); /* events queued through the load are for the old stage */
    /* Spectators must acknowledge their load too: otherwise they block the
     * next round forever and never receive their in-progress snapshot. */
    if (s_state != NET_STATE_INGAME || s_local_slot < 0) return;
    s_stage_fade_in = true;
    s_lobby_state.slots[s_local_slot].loaded = 1;
    if (!netIsHost()) {
        s_stage_ready_sent = true;
        s_last_stage_ready_us = sysGetMicroseconds();
        u8 raw[24];
        struct netbuf buf = { .data = raw, .size = sizeof(raw) };
        netbufStartWrite(&buf);
        netbufWriteU32(&buf, GEVR_NET_MAGIC);
        netbufWriteU16(&buf, GEVR_NET_VERSION);
        netbufWriteU8(&buf, NET_MSG_STAGE_READY);
        netbufWriteU8(&buf, (uint8_t)s_local_slot);
        struct player *pl = g_playerPointers[s_local_slot];
        netbufWriteF32(&buf, pl && pl->prop ? pl->prop->pos.x : 0);
        netbufWriteF32(&buf, pl && pl->prop ? pl->prop->pos.y : 0);
        netbufWriteF32(&buf, pl && pl->prop ? pl->prop->pos.z : 0);
        netbufWriteF32(&buf, pl ? pl->vv_theta : 0);
        netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    } else {
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
            if (netSlotIsBot(i)) s_lobby_state.slots[i].loaded = 1;
        netBroadcastLobbyState();
        netReadyProgress();
    }
}

void netLobbySetReady(bool ready) {
    if (s_local_slot < 0 || s_local_slot >= GEVR_MAX_PLAYERS) return;
    if (ready && s_lobby_state.config.mode != NET_MODE_COOP && netScenarioHasTeams(s_lobby_state.config.scenario) &&
        s_lobby_state.slots[s_local_slot].team == NET_TEAM_NONE &&
        !(netIsHost() && netGetConnectedPlayerCount() == 1)) return;
    s_lobby_state.slots[s_local_slot].ready = ready ? 1 : 0;
    
    u8 raw[64];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_LOBBY_READY);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteU8(&buf, ready ? 1 : 0);
    
    if (netIsHost()) {
        if (!ready) netCancelRound();
        netBroadcastLobbyState();
    } else if (s_server_peer) {
        netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    }
}

/* Next-round consent also belongs to players already in the loaded stage. */
static void netReceiveLobbyReady(ENetPeer *peer, int slot_id, struct netbuf *buf, size_t size) {
    if (!netIsHost() || !peer) return;
    int slot = (int)(intptr_t)peer->data - 1;
    uint8_t ready = netbufReadU8(buf);
    if (slot < 0 || slot >= s_max_players || s_client_peers[slot] != peer
        || !s_lobby_state.slots[slot].connected || buf->error || netbufReadLeft(buf)
        || size != 9 || slot_id != slot || ready > 1) return;
    if (ready && s_lobby_state.config.mode != NET_MODE_COOP
        && netScenarioHasTeams(s_lobby_state.config.scenario) && s_lobby_state.slots[slot].team >= 2) return;
    s_lobby_state.slots[slot].ready = ready;
    if (!ready) netCancelRound();
    netBroadcastLobbyState();
}

void netLobbySetCharacter(uint8_t chr_id) {
    if (s_local_slot < 0 || s_local_slot >= GEVR_MAX_PLAYERS) return;
    if (chr_id >= netCharacterCount()) return;
    s_lobby_state.slots[s_local_slot].chr_id = chr_id;
    VrMpChr = chr_id;
    s_preferred_chr_id = chr_id;
    vrSettingsSave();
    
    if (netIsHost()) {
        netBroadcastLobbyState();
    } else if (s_server_peer) {
        u8 raw[64];
        struct netbuf buf = { .data = raw, .size = sizeof(raw) };
        netbufStartWrite(&buf);
        netbufWriteU32(&buf, GEVR_NET_MAGIC);
        netbufWriteU16(&buf, GEVR_NET_VERSION);
        netbufWriteU8(&buf, NET_MSG_LOBBY_CHARACTER);
        netbufWriteU8(&buf, (uint8_t)s_local_slot);
        netbufWriteU8(&buf, chr_id);
        netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    }
}

void netLobbySetLoadout(const uint8_t items[4]) {
    if (s_local_slot < 0 || s_local_slot >= GEVR_MAX_PLAYERS || !items) return;
    for (int k = 0; k < 4; k++) if (netItemIndexOf(items[k]) < 0) return;
    memcpy(s_lobby_state.slots[s_local_slot].loadout, items, 4);
    for (int k = 0; k < 4; k++) VrMpLoadout[k] = items[k];
    vrSettingsSave();
    if (netIsHost()) {
        netBroadcastLobbyState();
        return;
    }
    if (!s_server_peer) return;
    u8 raw[16];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_LOBBY_LOADOUT);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    for (int k = 0; k < 4; k++) netbufWriteU8(&buf, items[k]);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

const NetMatchConfig *netGetMatchConfig(void) {
    return &s_lobby_state.config;
}

/* The host's settings (the launcher's pages, the game's lobby page): kept
 * in the lobby state and told to everyone; they take effect at the next load. */
void netLobbySetConfig(const NetMatchConfig *config) {
    if (!netIsHost() || !config || !netValidConfig(config)) return;
    /* Fewer players than are connected (or a slot past the new count) is refused. */
    int cap = netConfigSlots(config);
    for (int i=cap;i<GEVR_MAX_PLAYERS;i++) if (s_lobby_state.slots[i].connected && !s_lobby_state.slots[i].is_bot) return;
    if (netGetHumanPlayerCount() > netConfigMaxPlayers(config)) return;
    NetMatchConfig oldRound = s_lobby_state.config, newRound = *config;
    oldRound.voice_mode = newRound.voice_mode = 0;
    oldRound.movement_speed = newRound.movement_speed = 0;
    oldRound.friendly_fire = newRound.friendly_fire = 0;
    if (oldRound.mode == NET_MODE_COOP && newRound.mode == NET_MODE_COOP) {
        oldRound.fun_flags &= ~NET_COOP_FAST_REINFORCEMENTS;
        newRound.fun_flags &= ~NET_COOP_FAST_REINFORCEMENTS;
    }
    if (memcmp(&oldRound, &newRound, sizeof(oldRound)) != 0) {
        for (int i=0;i<GEVR_MAX_PLAYERS;i++) s_lobby_state.slots[i].ready = 0;
        netCancelRound();
    }
    if (config->scenario != s_lobby_state.config.scenario)
        for (int i=0;i<GEVR_MAX_PLAYERS;i++) s_lobby_state.slots[i].team = NET_TEAM_NONE;
    if (config->voice_mode != s_lobby_state.config.voice_mode) {
        s_round.config.voice_mode = config->voice_mode;
        for (int i=0;i<GEVR_MAX_PLAYERS;i++) netVoiceForgetSlot((uint8_t)i);
    }
    s_round.config.friendly_fire = config->friendly_fire;
    s_round.config.movement_speed = config->movement_speed;
    if (s_round.config.mode == NET_MODE_COOP && config->mode == NET_MODE_COOP)
        s_round.config.fun_flags = (s_round.config.fun_flags & ~NET_COOP_FAST_REINFORCEMENTS)
            | (config->fun_flags & NET_COOP_FAST_REINFORCEMENTS);
    s_lobby_state.config = *config;
    if (s_state != NET_STATE_INGAME && s_state != NET_STATE_MIGRATING) {
        s_max_players = cap;
        s_lobby_max_players = (uint8_t)cap;
    }
    netUpdateBots(false);
    netBroadcastLobbyState();
}

/* Online with a co-op mission loaded or loading: the game's code asks this
 * where retail reads "two or more players" as a deathmatch (gevrCoopActive). */
int netCoopActive(void) {
    /* the stage loaded, not the config's: a mission keeps its rules until the menus replace it */
    return netCoopSession() && bossGetStageNum() != LEVELID_TITLE;
}
int gevrCoopActive(void) { return netCoopActive(); }

/* Co-op (#94): a number every headset shares for this mission (the launch's seed, the round's epoch): the intro's camera */
unsigned int gevrCoopIntroSeed(void) {
    uint32_t x = s_rng_seed ^ (s_round.world_epoch * 2654435761u);
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
    return x;
}

/* A co-op party, in its menus or a mission: the session lasts through the title stage */
int netCoopSession(void) {
    return (s_state == NET_STATE_INGAME || s_state == NET_STATE_MIGRATING) && s_round.config.mode == NET_MODE_COOP;
}
int gevrCoopSession(void) { return netCoopSession(); }

int gevrCoopFastReinforcements(void) {
    return netCoopSession() && (s_round.config.fun_flags & NET_COOP_FAST_REINFORCEMENTS) != 0;
}

/*
 * The party's tally, for the statistics page (front.c): each player's guard
 * kills and hits as the host counted them. A teammate's hits on the host's
 * guards are the host's to count (net_coop.c coopApplyHit); its shots fired
 * are its own.
 */
#define NET_COOP_TALLY_REGS 6   /* shot_count[1..6]: head, body, limb, gun, hat, object */
static bool s_coop_tally_valid;
static bool s_coop_tally_in[GEVR_MAX_PLAYERS];
static uint16_t s_coop_tally_kills[GEVR_MAX_PLAYERS];
static uint16_t s_coop_tally_hits[GEVR_MAX_PLAYERS][NET_COOP_TALLY_REGS];
static char s_coop_tally_name[GEVR_MAX_PLAYERS][16];

static void netCoopTallyName(int slot) {
    const char *name = netGetSlotName(slot);
    snprintf(s_coop_tally_name[slot], sizeof(s_coop_tally_name[slot]), "%s", name && name[0] ? name : "Player");
}

/* The host: every player's count, as the mission ends */
static void netCoopTallyCollect(void) {
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        const struct player_data *d = &g_playerPlayerData[i];
        s_coop_tally_in[i] = netSlotOccupied(i) && !netSlotIsSpectator(i);
        s_coop_tally_kills[i] = (uint16_t)(d->kill_count < 0 ? 0 : d->kill_count > 0xFFFF ? 0xFFFF : d->kill_count);
        for (int r = 0; r < NET_COOP_TALLY_REGS; r++) {
            s32 v = d->shot_count[r + 1];
            s_coop_tally_hits[i][r] = (uint16_t)(v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : v);
        }
        if (s_coop_tally_in[i]) netCoopTallyName(i);
    }
    s_coop_tally_valid = true;
}

/* Every headset: this player's kills and hits as the host counted them */
static void netCoopTallyApplyLocal(void) {
    int me = s_local_slot;
    if (!s_coop_tally_valid || netIsHost() || me < 0 || me >= GEVR_MAX_PLAYERS) return;
    g_playerPlayerData[me].kill_count = s_coop_tally_kills[me];
    for (int r = 0; r < NET_COOP_TALLY_REGS; r++) g_playerPlayerData[me].shot_count[r + 1] = s_coop_tally_hits[me][r];
}

int gevrCoopTallyCount(void) { return s_coop_tally_valid && netCoopSession() ? GEVR_MAX_PLAYERS : 0; }
int gevrCoopTallyPlayed(int slot) { return s_coop_tally_valid && slot >= 0 && slot < GEVR_MAX_PLAYERS && s_coop_tally_in[slot]; }
int gevrCoopTallyKills(int slot) { return gevrCoopTallyPlayed(slot) ? s_coop_tally_kills[slot] : 0; }
const char *gevrCoopTallyName(int slot) { return gevrCoopTallyPlayed(slot) ? s_coop_tally_name[slot] : ""; }

/*
 * Every headset, back in the menus: the debrief and statistics pages read
 * player one's stats (front.c), on a teammate's headset the host's copy:
 * this headset's own player's take their place.
 */
static void netCoopStatsToPlayerOne(void) {
    int me = s_local_slot;
    if (me <= 0 || me >= GEVR_MAX_PLAYERS) return;
    g_playerPlayerData[0] = g_playerPlayerData[me];
    array_favweapon[0][0] = array_favweapon[me][0];
    array_favweapon[0][1] = array_favweapon[me][1];
}

/*
 * A co-op mission, as the solo game starts one (file.c set_solo_and_ptr_briefing,
 * front.c init_menu0B_runstage) but with a player struct for every slot: the
 * game mode stays GAMEMODE_MULTI, which is what sizes the players
 * (front.c get_selected_num_players, boss.c), and the code that reads two or
 * more players as a deathmatch asks gevrCoopActive() instead.
 */
static void netApplyCoopConfig(const NetMatchConfig *c) {
    extern void init_mp_options_for_scenario(s32 numplayers);
    extern void do_extended_cast_display(s32 arg0);
    extern s32 g_StageNum;
    gevrCoopPrepareSaveFolder();
    s_max_players = NET_COOP_MAX_PLAYERS;
    s_lobby_max_players = NET_COOP_MAX_PLAYERS;
    if (c->stage == NET_COOP_FRONT_STAGE) {
        /*
         * The party's menus: the solo front end on every headset, each player
         * with their own save folder (front.c). The first time in, the folder
         * screen; back from a mission, the debrief its launch chose.
         */
        gamemode = GAMEMODE_SOLO;
        selected_num_players = 1;
        g_StageNum = LEVELID_TITLE;
        if (menu_update == MENU_INVALID && current_menu == MENU_INVALID)
            menu_update = MENU_FILE_SELECT;
        if (menu_update == MENU_MISSION_FAILED)
            netCoopStatsToPlayerOne();
        NET_LOG("co-op config applied: the party's menus");
        return;
    }
    gamemode = GAMEMODE_MULTI;
    selected_num_players = NET_COOP_MAX_PLAYERS;
    s_coop_tally_valid = false;   /* the statistics page's tally is the next end's */
    g_StageNum = c->stage;
    selected_stage = c->stage;
    /* the mission's folder entry (the save's index): the co-op table is in the solo mission order */
    if (netCoopMissionIndexOf(c->stage) >= 0)
        briefingpage = pull_and_display_text_for_folder_a0(netCoopMissionIndexOf(c->stage));
    selected_difficulty = (DIFFICULTY)(c->difficulty < NET_DIFFICULTY_COUNT ? c->difficulty : DIFFICULTY_AGENT);
    lvlSetSelectedDifficulty(selected_difficulty);
    /* back in the menus afterwards, the debrief (Cuba: the cast), as front.c's MENU_RUN_STAGE picks it */
    if (c->stage == NET_COOP_CUBA_STAGE) {
        do_extended_cast_display(TRUE);
        menu_update = MENU_DISPLAY_CAST;
    } else {
        menu_update = MENU_MISSION_FAILED;
    }
    init_mp_options_for_scenario(selected_num_players);
    reset_mp_options_for_scenario(SCENARIO_NORMAL);
    game_length = 0;   /* no time or point limit: the mission ends the round */
    for (int p = 0; p < GEVR_MAX_PLAYERS; p++) {
        player_char[p] = s_round.character[p] < netCharacterCount() ? s_round.character[p] : p;
        player_handicap[p] = 5;   /* normal: the difficulty sets the damage */
    }
    NET_LOG("co-op config applied: mission %s (level %d) difficulty %s",
            c->stage == NET_COOP_CUBA_STAGE ? "Cuba" : netCoopMissionName(netCoopMissionIndexOf(c->stage)),
            c->stage, netDifficultyName(c->difficulty));
}

_Static_assert(NET_WEAPON_SET_CUSTOM == MP_WEAPON_SET_CUSTOM, "the custom set's index must agree between net_match.h and mp_weapon.h");

/*
 * Every headset, before each stage load (the launcher's launch, boss.c's
 * round reset): GoldenEye's match globals from the lobby's config. The game
 * reads them at load only (lv.c lvlStageLoad the limits and handicaps,
 * prop.c the weapon pads), never mid-round, so this is the one place they
 * change. The scenario's own rules come first (front.c: YOLT is last one
 * standing, the Golden Gun scenario has its own set), the host's choices after.
 */
void netApplyMatchConfig(void) {
    extern void init_mp_options_for_scenario(s32 numplayers);
    extern s32 g_StageNum;
    const NetMatchConfig *c = &s_round.config;
    if (c->mode == NET_MODE_COOP) {
        netApplyCoopConfig(c);
        return;
    }
    int cap = netConfigSlots(c);

    gamemode = GAMEMODE_MULTI;
    s_max_players = cap;
    s_lobby_max_players = (uint8_t)cap;
    selected_num_players = cap;   /* a slot for every player who may join */
    g_StageNum = c->stage;
    init_mp_options_for_scenario(selected_num_players);
    reset_mp_options_for_scenario((MPSCENARIOS)netGameScenario(c->scenario < netScenarioCount() ? c->scenario : 0));
    if (c->scenario == SCENARIO_YOLT) {
        game_length = 7;   /* last one standing, as reset_mp_options_for_scenario forces */
    } else {
        int limit = c->scenario == SCENARIO_TLD ? 3 : 6;   /* the game's own caps per scenario */
        game_length = c->game_length <= limit ? c->game_length : 2;
    }
    if (c->scenario != SCENARIO_MWTGG) {   /* the Golden Gun scenario keeps its set */
        if (c->weapon_set == NET_WEAPON_SET_CUSTOM) {
            mpBuildCustomWeaponSet(c->custom_set);
            setMPWeaponSet(MP_WEAPON_SET_CUSTOM);
        } else {
            setMPWeaponSet(c->weapon_set < NET_WEAPON_SET_CUSTOM ? c->weapon_set : 4);
        }
    }
    for (int p = 0; p < GEVR_MAX_PLAYERS; p++) {
        player_char[p] = s_round.character[p] < netCharacterCount() ? s_round.character[p] : p;
        player_handicap[p] = c->health < netHealthCount() ? c->health : 5;
        if (netScenarioHasTeams(c->scenario)) set_players_team_or_scenario_item_flag(p, s_round.team[p] == NET_TEAM_BLUE ? 1 : 0);
    }
    NET_LOG("config applied: stage %d scenario %d set %d length %d health %d dual %d loadouts %d next %d",
            c->stage, c->scenario, getMPWeaponSet(), game_length, c->health, c->dual_wield, c->loadouts, c->next_round);
}

/* Connected players in the round: a spectator (a late join, watching until
 * the next round) is not in it. */
int netGetPlayingCount(void) {
    int count = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        if (s_lobby_state.slots[i].connected && !s_lobby_state.slots[i].spectator) count++;
    return count;
}

/* The game's player_count online: the humans in the round, not the stage's
 * slots (lv.c's YOLT end, mpmenu.c's Golden Gun bonus). */
int netMpPlayerCount(int fallback) {
    return s_state == NET_STATE_INGAME || s_state == NET_STATE_MIGRATING ? netGetPlayingCount() : fallback;
}

bool netSlotIsSpectator(int slot) {
    return slot >= 0 && slot < GEVR_MAX_PLAYERS && s_lobby_state.slots[slot].spectator != 0;
}

int netPlayerIsSpectator(int slot) { return netIsActive() && netSlotIsSpectator(slot); }
int gevrSpectating(void) { return netLocalIsSpectator() && get_cur_playernum() == s_local_slot; }
int netTeamRosterReady(void) {
    if (s_lobby_state.config.mode == NET_MODE_COOP) return 1;
    uint8_t connected[GEVR_MAX_PLAYERS], team[GEVR_MAX_PLAYERS];
    for (int i=0;i<GEVR_MAX_PLAYERS;i++) { connected[i]=s_lobby_state.slots[i].connected; team[i]=s_lobby_state.slots[i].team; }
    return netTeamRosterComplete(s_lobby_state.config.scenario, GEVR_MAX_PLAYERS, connected, team);
}
int netGetSlotTeam(int slot) { return slot >= 0 && slot < GEVR_MAX_PLAYERS ? s_lobby_state.slots[slot].team : NET_TEAM_NONE; }
int netGetSlotPing(int slot) {
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS || !s_lobby_state.slots[slot].connected || s_state == NET_STATE_MIGRATING) return -1;
    if (slot == s_host_slot) return 0;
    if (!netIsHost() && sysGetMicroseconds() - s_lobby_received_us > 5000000) return -1;
    return s_lobby_state.slots[slot].ping_ms == NET_PING_UNKNOWN ? -1 : s_lobby_state.slots[slot].ping_ms;
}
int netVoiceSlotSpectating(int slot) {
    return slot >= 0 && slot < GEVR_MAX_PLAYERS && (s_lobby_state.slots[slot].spectator || s_lobby_state.slots[slot].eliminated);
}
int netVoiceModeForPair(int a, int b) {
    if (a < 0 || b < 0 || a >= GEVR_MAX_PLAYERS || b >= GEVR_MAX_PLAYERS) return NET_VOICE_PROXIMITY;
    if (s_phase != NET_PHASE_IN_PROGRESS || s_match_ended ||
        netVoiceSlotSpectating(a) || netVoiceSlotSpectating(b)) return NET_VOICE_COUCH;
    return netVoicePairMode(s_round.config.scenario, s_round.config.voice_mode, s_round.team[a], s_round.team[b]);
}
int netVoiceSameGroup(int a, int b) {
    if (a < 0 || b < 0 || a >= GEVR_MAX_PLAYERS || b >= GEVR_MAX_PLAYERS || !s_lobby_state.slots[a].connected || !s_lobby_state.slots[b].connected) return 0;
    return netVoiceGroupsMatch(s_state == NET_STATE_INGAME && s_phase == NET_PHASE_IN_PROGRESS && !s_match_ended,
        s_round.config.scenario, netVoiceSlotSpectating(a), netVoiceSlotSpectating(b), s_round.team[a], s_round.team[b]);
}
int netTeamScore(int team) {
    if (team < 0 || team > 1) return 0;
    int points = s_round.departed_score[team];
    for (int i=0;i<GEVR_MAX_PLAYERS;i++) if (netPlayerInRound(i) && s_round.team[i] == team) {
        points += g_playerPlayerData[i].gevr_score_bank;
        for (int j=0;j<GEVR_MAX_PLAYERS;j++) points += netTeamKillPoints(team, s_round.team[j], g_playerPlayerData[i].kill_counts[j]);
    }
    return points;
}
static bool netSetSlotTeam(int slot, uint8_t team) {
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS || !s_lobby_state.slots[slot].connected || team > NET_TEAM_NONE || !netScenarioHasTeams(s_lobby_state.config.scenario)) return false;
    if (team < 2) {
        int count=0;
        for (int i=0;i<GEVR_MAX_PLAYERS;i++) if (i != slot && s_lobby_state.slots[i].connected && !s_lobby_state.slots[i].is_bot && s_lobby_state.slots[i].team == team) count++;
        if (count >= netTeamCapacity(s_lobby_state.config.scenario, team)) return false;
    }
    if (s_lobby_state.slots[slot].team == team) return true;
    s_lobby_state.slots[slot].team = team;
    s_lobby_state.slots[slot].ready = 0; // Next-round consent never changes the loaded active player.
    netCancelRound();
    netUpdateBots(false);   /* a bot on the side picked moves over */
    netBroadcastLobbyState();
    return true;
}
void netLobbySetTeam(uint8_t team) {
    if (netIsHost()) { netSetSlotTeam(s_local_slot, team); return; }
    if (!s_server_peer || team > NET_TEAM_NONE) return;
    u8 raw[9]; struct netbuf buf = { .data=raw, .size=sizeof(raw) };
    netbufStartWrite(&buf); netbufWriteU32(&buf, GEVR_NET_MAGIC); netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_LOBBY_TEAM); netbufWriteU8(&buf, (uint8_t)s_local_slot); netbufWriteU8(&buf, team);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

bool netLocalIsSpectator(void) {
    return netIsActive() && netSlotIsSpectator(s_local_slot);
}

int gevrNetConfigGet(int field) {
    const NetMatchConfig *c = &s_lobby_state.config;
    switch (field) {
    case CFG_STAGE: return c->stage;
    case CFG_SCENARIO: return c->scenario;
    case CFG_WEAPON_SET: return c->weapon_set;
    case CFG_GAME_LENGTH: return c->game_length;
    case CFG_HEALTH: return c->health;
    case CFG_DUAL_WIELD: return c->dual_wield;
    case CFG_LOADOUTS: return c->loadouts;
    case CFG_NEXT_ROUND: return c->next_round;
    case CFG_FRIENDLY_FIRE: return c->friendly_fire;
    case CFG_VOICE_MODE: return c->voice_mode;
    case CFG_FUN_FLAGS: return c->fun_flags;
    case CFG_GUN_SIZE: return c->gun_size;
    case CFG_MAX_PLAYERS: return c->max_players;
    case CFG_FAST_REINFORCEMENTS: return c->mode == NET_MODE_COOP && (c->fun_flags & NET_COOP_FAST_REINFORCEMENTS) != 0;
    case CFG_BOT_MODE: return c->bot_mode;
    case CFG_BOT_COUNT: return c->bot_count;
    case CFG_BOT_DIFFICULTY: return c->bot_difficulty;
    case CFG_MOVEMENT_SPEED: return c->movement_speed;
    default: return field >= CFG_CUSTOM0 && field <= CFG_CUSTOM3 ? c->custom_set[field-CFG_CUSTOM0] : 0;
    }
}
void gevrNetConfigSet(int field, int value) {
    if (!netIsHost() || value < 0 || value > 255) return;
    NetMatchConfig c = s_lobby_state.config;
    switch (field) {
    case CFG_STAGE: if (!netStageEligible(value)) return; c.stage = netStage(value)->level_id; break;
    case CFG_SCENARIO:
        c.scenario = value;
        if (value == SCENARIO_MWTGG) c.weapon_set = 13;
        if (value == SCENARIO_LTK) c.weapon_set = 1;
        if (value == SCENARIO_YOLT) c.game_length = 7;
        else if (c.game_length > (value == SCENARIO_TLD ? 3 : 6)) c.game_length = 2;
        break;
    case CFG_WEAPON_SET: if (c.scenario == SCENARIO_MWTGG) return; c.weapon_set = value; break;
    case CFG_GAME_LENGTH: c.game_length = value; break;
    case CFG_HEALTH: c.health = value; break;
    case CFG_DUAL_WIELD: c.dual_wield = value; break;
    case CFG_LOADOUTS: c.loadouts = value; break;
    case CFG_NEXT_ROUND: c.next_round = value; break;
    case CFG_FRIENDLY_FIRE: c.friendly_fire = value; break;
    case CFG_VOICE_MODE: c.voice_mode = value; break;
    case CFG_FUN_FLAGS:
        if (value & ~NET_FUN_MASK) return;
        c.fun_flags = value | (c.mode == NET_MODE_COOP ? c.fun_flags & NET_COOP_FAST_REINFORCEMENTS : 0);
        break;
    case CFG_GUN_SIZE: c.gun_size = value; break;
    case CFG_MAX_PLAYERS: c.max_players = value; break;
    case CFG_FAST_REINFORCEMENTS:
        if (c.mode != NET_MODE_COOP || value > 1) return;
        c.fun_flags = (c.fun_flags & ~NET_COOP_FAST_REINFORCEMENTS) | (value ? NET_COOP_FAST_REINFORCEMENTS : 0);
        break;
    case CFG_BOT_MODE: if (!gevrNetBotRowsEditable()) return; c.bot_mode = value; break;
    case CFG_BOT_COUNT: if (!gevrNetBotRowsEditable() || value < 1) return; c.bot_count = value; break;
    case CFG_BOT_DIFFICULTY: if (!gevrNetBotRowsEditable()) return; c.bot_difficulty = value; break;
    case CFG_MOVEMENT_SPEED: c.movement_speed = value; break;
    default: if (field < CFG_CUSTOM0 || field > CFG_CUSTOM3) return; c.custom_set[field-CFG_CUSTOM0] = value; break;
    }
    if (!netValidConfig(&c)) return;
    netLobbySetConfig(&c);
    c = s_lobby_state.config; // Persist the accepted host settings.
    VrMpStage=c.stage; VrMpScenario=c.scenario; VrMpWeaponSet=c.weapon_set;
    VrMpLength=c.game_length; VrMpHealth=c.health; VrMpDual=c.dual_wield;
    VrMpLoadouts=c.loadouts; VrMpNextRound=c.next_round; VrMpVoiceMode=s_lobby_state.config.voice_mode; VrMpFriendlyFire=c.friendly_fire; VrMpFunFlags=c.fun_flags & NET_FUN_MASK; VrMpGunSize=c.gun_size; VrMpMaxPlayers=c.max_players;
    VrMpMovementSpeed=c.movement_speed;
    VrMpBotMode=c.bot_mode; VrMpBotCount=c.bot_count; VrMpBotDifficulty=c.bot_difficulty;
    if (c.mode == NET_MODE_COOP) VrCoopFastReinforcements = (c.fun_flags & NET_COOP_FAST_REINFORCEMENTS) != 0;
    for (int k=0;k<4;k++) VrMpCustom[k]=c.custom_set[k];
    vrSettingsSave();
}
int gevrNetSlotIsBot(int slot) { return netSlotIsBot(slot); }
/* A bot's foes: everyone else, or the other side in a team round */
int gevrNetBotFoes(int a, int b) {
    if (a == b || a < 0 || b < 0 || a >= GEVR_MAX_PLAYERS || b >= GEVR_MAX_PLAYERS) return 0;
    return !netScenarioHasTeams(s_round.config.scenario) || s_round.team[a] != s_round.team[b];
}
int gevrNetBotDifficulty(void) { return s_round.config.bot_difficulty < NET_BOT_DIFF_COUNT ? s_round.config.bot_difficulty : NET_BOT_NORMAL; }
/* Bots change between rounds: the lobby and the warmup, never a round in progress */
int gevrNetBotRowsEditable(void) {
    return netIsHost() && s_lobby_state.config.mode != NET_MODE_COOP &&
           !(s_state == NET_STATE_INGAME && s_phase == NET_PHASE_IN_PROGRESS);
}
int gevrNetSlotChr(int slot) { return slot >= 0 && slot < GEVR_MAX_PLAYERS ? s_lobby_state.slots[slot].chr_id : 0; }
int gevrNetSlotLoadout(int slot, int k) { return slot >= 0 && slot < GEVR_MAX_PLAYERS && k >= 0 && k < 4 ? s_lobby_state.slots[slot].loadout[k] : 0; }
unsigned char gevrNetItemAt(int idx) { return netItem(idx)->item; }

static void netSendMatchStartTo(ENetPeer *peer) {
    u8 raw[256];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_START_MATCH);
    netbufWriteU8(&buf, 0);
    netWriteRoundSettings(&buf);
    netbufWriteU32(&buf, s_rng_seed);
    netbufWriteU8(&buf, (uint8_t)netGetConnectedPlayerCount());
    netbufWriteU8(&buf, (uint8_t)s_phase);
    if (peer) {
        ENetPacket *packet = enet_packet_create(buf.data, buf.wp, ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, NET_CHAN_RELIABLE, packet);
    } else {
        netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    }
}

/* Launch enters warmup. The complete team roster is enforced separately
 * before a real round starts. The host consents by pressing Launch. */
int netLocalReady(void) { return s_local_slot >= 0 && s_local_slot < GEVR_MAX_PLAYERS && s_lobby_state.slots[s_local_slot].ready; }
void gevrNetSetReady(int ready) { netLobbySetReady(ready != 0); }
int netRoundRosterReady(void) { return netLobbyCanLaunch() && netTeamRosterReady(); }
int netGetHostSlot(void) { return s_host_slot; }
int netLobbyCanLaunch(void) {
    if (!netIsHost()) return 0;
    int connected = 0;
    for (int i=0;i<GEVR_MAX_PLAYERS;i++) if (s_lobby_state.slots[i].connected) {
        if (i >= s_max_players || (i != s_host_slot && !s_lobby_state.slots[i].ready && !s_lobby_state.slots[i].is_bot)) return 0;
        connected++;
    }
    return connected > 0;
}
int netActiveFunFlags(void) { return s_round.config.fun_flags; }
int netActiveLineMode(void) { return netIsActive() && (s_round.config.fun_flags & NET_FUN_LINE) != 0; }
int netActiveGunSize(void) { return s_round.config.gun_size; }
int netActiveMovementSpeed(void) { return s_round.config.movement_speed; }

bool netLobbyHostLaunchMatch(void) {
    if (!netLobbyCanLaunch()) return false;
    
    s_rng_seed = (uint32_t)(sysGetMicroseconds());
    if (s_rng_seed == 0) s_rng_seed = 0x12345678;
    s_round.world_epoch = s_rng_seed;
    
    extern void randomSetSeed(u32);
    randomSetSeed(s_rng_seed);
    
    netLatchRoundSettings();
    /*
     * No votes yet. The ballots are zero from the start, and zero is a vote
     * for the first stage and set: the warmup join's countdown tallied
     * "Facility, Power Weapons" from every connected slot and the match
     * left the host's Bunker II for it (playtest 2026-09-30, "round
     * settings ... ballot stage 0 set 4").
     */
    netClearVotes(-1);
    s_lobby_open = false;
    /* a co-op mission is under way from the load: the party plays as it arrives (#94) */
    s_phase = s_round.config.mode == NET_MODE_COOP ? NET_PHASE_IN_PROGRESS : NET_PHASE_WARMUP;
    netSendMatchStartTo(NULL);
    s_state = NET_STATE_INGAME;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        if (s_lobby_state.slots[i].connected) s_lobby_state.slots[i].loaded = 0;
    NET_LOG("Match launched! Stage: %d, Players: %d, Seed: 0x%08X", s_lobby_state.config.stage, netGetConnectedPlayerCount(), s_rng_seed);
    return true;
}

/* An owned player's move: the local player's, or the host's bot's */
void netSendOwnedMove(int slot, const struct netplayermove *input) {
    if(!netSlotOwned(slot) || !s_combat_epoch)return;
    struct netplayermove local=*input;local.epoch=s_combat_epoch;local.life_id=s_hit_life[slot];
    local.clock_us=sysGetMicroseconds();
    if(local.dead && !s_local_death_us[slot])s_local_death_us[slot]=local.clock_us;
    local.death_us=local.dead?s_local_death_us[slot]:0;
    const struct netplayermove *move=&local;
    if (netIsHost()) netObserveHitMove(slot,move);
    if (s_state != NET_STATE_INGAME || (slot == s_local_slot && netLocalIsSpectator())) return;
    
    u8 raw[256];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_PLAYER_STATE);
    netbufWriteU8(&buf, (uint8_t)slot);
    netbufWritePlayerMove(&buf, move);

    netBroadcastBuf(&buf, NET_CHAN_PLAYER_STATE, ENET_PACKET_FLAG_UNSEQUENCED, NULL);
}
void netSendLocalPlayerMove(const struct netplayermove *input) { netSendOwnedMove(s_local_slot, input); }

void netSendLocalPlayerState(const NetMsgPlayerState *state) {
    if (s_state != NET_STATE_INGAME || s_local_slot < 0 || netLocalIsSpectator()) return;
    
    struct netplayermove m;
    memset(&m, 0, sizeof(m));
    m.tick = state->sequence;
    m.ucmd = state->is_firing ? UCMD_FIRE : 0;
    if (state->stance == 1) m.ucmd |= UCMD_DUCK;
    m.angles[0] = state->head_yaw;
    m.angles[1] = state->head_pitch;
    m.weaponnum = state->weapon_id;
    m.crouchpos = state->stance;
    m.pos.x = state->pos_x;
    m.pos.y = state->pos_y;
    m.pos.z = state->pos_z;
    m.handpos.x = state->hand_x;
    m.handpos.y = state->hand_y;
    m.handpos.z = state->hand_z;
    m.handrot.x = state->hand_pitch;
    m.handrot.y = state->hand_yaw;
    m.handrot.z = state->hand_roll;
    
    netSendLocalPlayerMove(&m);
}

const struct netplayermove *netGetRemotePlayerMove(int slot_id) {
    if (slot_id < 0 || slot_id >= GEVR_MAX_PLAYERS) return NULL;
    return &s_remote_moves[slot_id];
}

const NetMsgPlayerState *netGetRemotePlayerState(int slot_id) {
    if (slot_id < 0 || slot_id >= GEVR_MAX_PLAYERS) return NULL;
    return &s_remote_players[slot_id];
}

bool netIsRemotePlayerActive(int slot_id) {
    if (slot_id < 0 || slot_id >= GEVR_MAX_PLAYERS || netSlotOwned(slot_id)) return false;
    return s_remote_active[slot_id];
}

/* The remote player's right gun barrel in world space, when its owner aims
 * from a tracked controller (bondview2.c gevrStereoShot for a copy). */
int netRemoteWeapon(int slot, int hand) {
    if (!netIsRemotePlayerActive(slot) || !netSlotOccupied(slot) || hand < 0 || hand > 1) return ITEM_UNARMED;
    if (hand == GUNLEFT && !netActiveDualWield()) return ITEM_UNARMED;
    int item = hand == GUNLEFT ? s_remote_moves[slot].weaponnum_left : s_remote_moves[slot].weaponnum;
    return item >= ITEM_UNARMED && item < ITEM_IDS_MAX ? item : ITEM_UNARMED;
}
int netRemoteTrigger(int slot, int hand) {
    return netIsRemotePlayerActive(slot) && netSlotOccupied(slot) && hand >= 0 && hand <= 1 &&
           (hand != GUNLEFT || netActiveDualWield()) &&
           (s_remote_moves[slot].ucmd & (hand == GUNLEFT ? UCMD_FIRE_LEFT : UCMD_FIRE)) != 0;
}
int netGetRemoteAim(int slot_id, int hand, coord3d *origin, coord3d *dir) {
    if (!netIsRemotePlayerActive(slot_id) || hand < 0 || hand > 1 || !origin || !dir) return 0;
    const struct netplayermove *m = &s_remote_moves[slot_id];
    if (!(m->ucmd & (hand == GUNLEFT ? UCMD_AIMVALID_LEFT : UCMD_AIMVALID))) return 0;
    const coord3d *aim = hand == GUNLEFT ? &m->aimdir_l : &m->aimdir;
    const coord3d *pos = hand == GUNLEFT ? &m->aimorigin_l : &m->aimorigin;
    float len = sqrtf(aim->x * aim->x + aim->y * aim->y + aim->z * aim->z);
    if (!(len > 0.0001f) || !isfinite(len) || !isfinite(pos->x) || !isfinite(pos->y) || !isfinite(pos->z)) return 0;
    *origin = *pos;
    dir->x = aim->x / len; dir->y = aim->y / len; dir->z = aim->z / len;
    return 1;
}

static bool netExplosiveWeapon(int weapon)
{
    return weapon==ITEM_GRENADE || weapon==ITEM_GRENADELAUNCH || weapon==ITEM_ROCKETLAUNCH
        || weapon==ITEM_PROXIMITYMINE || weapon==ITEM_TIMEDMINE || weapon==ITEM_REMOTEMINE || weapon==ITEM_TANKSHELLS;
}
static bool netHitReportAllowed(int shooter,const NetHitReport *h,unsigned uncertainty)
{
    if (!netIsHost() || s_state!=NET_STATE_INGAME || s_round_reset_loading || s_match_ended
        || (s_phase!=NET_PHASE_IN_PROGRESS && s_phase!=NET_PHASE_WARMUP)
        || !netSlotOccupied(shooter) || !netSlotOccupied(h->target)
        || h->epoch!=s_combat_epoch || h->shooter_life!=s_hit_life[shooter] || h->target_life!=s_hit_life[h->target]
        || !h->hit_id || !h->shot_id || h->weapon>=ITEM_IDS_MAX || !isfinite(h->damage) || h->damage<=0
        || !isfinite(h->hx) || !isfinite(h->hy) || !isfinite(h->hz)
        || !netShotTimeValid(h->shot_us,sysGetMicroseconds(),uncertainty) || !netDamageAllowed(shooter,h->target))return false;
    struct player *pl=g_playerPointers[shooter];
    if(netSlotOwned(shooter) && pl && pl->bonddead && !s_hit_death_us[shooter])s_hit_death_us[shooter]=sysGetMicroseconds();
    /* Blast time is impact time; a grenade thrown before death remains dangerous. */
    return netExplosiveWeapon(h->weapon) || netShotTradeValid(h->shot_us,s_hit_death_us[shooter],uncertainty);
}
static void netProcessHitReport(int shooter,const NetHitReport *h,unsigned uncertainty)
{
    if(!netHitReportAllowed(shooter,h,uncertainty))return;
    NET_LOG("Hit: P%d life %u shot %u hit %u -> P%d life %u age %lluus",shooter+1,h->shooter_life,h->shot_id,h->hit_id,h->target+1,h->target_life,
        (unsigned long long)(sysGetMicroseconds()>h->shot_us?sysGetMicroseconds()-h->shot_us:0));
    u8 raw[128];struct netbuf b={.data=raw,.size=sizeof(raw)};netbufStartWrite(&b);
    netbufWriteU32(&b,GEVR_NET_MAGIC);netbufWriteU16(&b,GEVR_NET_VERSION);
    netbufWriteU8(&b,NET_MSG_DAMAGE_EVENT);netbufWriteU8(&b,(uint8_t)shooter);
    netbufWriteHitReport(&b,h);
    netBroadcastBuf(&b,NET_CHAN_RELIABLE,ENET_PACKET_FLAG_RELIABLE,NULL);
    if(g_playerPointers[h->target])netApplyDamage(h->target,shooter,h->weapon,h->damage,h->hx,h->hz);
}
static bool netAcceptHit(int shooter,NetHitReport *h,unsigned *uncertainty)
{
    if(shooter<0 || shooter>=GEVR_MAX_PLAYERS)return false;
    uint64_t mapped;
    if(!netMapShotTime(shooter,h->shot_us,&mapped,uncertainty))return false;
    h->shot_us=mapped;
    if(!netHitReportAllowed(shooter,h,*uncertainty)
        || (s_last_hit_id[shooter] && (int32_t)(h->hit_id-s_last_hit_id[shooter])<=0))return false;
    s_last_hit_id[shooter]=h->hit_id;
    if(h->shot_us>sysGetMicroseconds())h->shot_us=sysGetMicroseconds();
    return true;
}
static void netReceiveHitReport(int shooter,struct netbuf *b)
{
    NetHitReport h;unsigned uncertainty;
    if(netbufReadHitReport(b,&h) && !netbufReadLeft(b) && netAcceptHit(shooter,&h,&uncertainty))
        netProcessHitReport(shooter,&h,uncertainty);
}
static void netExecuteHostHit(QueuedHostHit *hit)
{
    if(!hit->active)return;
    hit->active=false;netProcessHitReport(hit->shooter,&hit->report,0);
}
static void netQueueHostHit(int shooter,NetHitReport h)
{
    unsigned uncertainty;
    if(!netAcceptHit(shooter,&h,&uncertainty))return;
    unsigned delay=netGetSlotHostDelayMs(h.target);
    if(!delay) {netProcessHitReport(shooter,&h,0);return;}
    int free_slot=-1,oldest=0;
    for(int n=0;n<NET_HOST_HIT_CAPACITY;n++) {
        int i=(s_host_hit_cursor+n)%NET_HOST_HIT_CAPACITY;
        if(!s_host_hits[i].active) {free_slot=i;break;}
        if(s_host_hits[i].order<s_host_hits[oldest].order)oldest=i;
    }
    if(free_slot<0) {
        if(!s_hit_overflow_warned) {NET_LOG("Host hit queue full: dispatching oldest early");s_hit_overflow_warned=true;}
        netExecuteHostHit(&s_host_hits[oldest]);free_slot=oldest;
    }
    s_host_hit_cursor=(free_slot+1)%NET_HOST_HIT_CAPACITY;
    s_host_hits[free_slot]=(QueuedHostHit){true,(uint8_t)shooter,netHostBaseDelayMs(h.target),sysGetMicroseconds(),++s_hit_order,h};
}
static void netDrainHostHits(void)
{
    if(s_state!=NET_STATE_INGAME) {netClearHostHits();return;}
    if(!netIsHost()) return;
    uint64_t now=sysGetMicroseconds();unsigned cap;int on=netGetHostEqualization(&cap);
    for(;;) {
        int next=-1;uint64_t first=UINT64_MAX;
        for(int i=0;i<NET_HOST_HIT_CAPACITY;i++)if(s_host_hits[i].active) {
            unsigned delay=on?(s_host_hits[i].delay_ms<cap?s_host_hits[i].delay_ms:cap):0;
            uint64_t due=s_host_hits[i].queued_us+(uint64_t)delay*1000;
            if(due<=now && (next<0 || due<first || (due==first && s_host_hits[i].order<s_host_hits[next].order))) {next=i;first=due;}
        }
        if(next<0)break;
        netExecuteHostHit(&s_host_hits[next]);
    }
}
void netBeginLocalShot(void)
{
    int slot=netActingSlot();
    if(!s_combat_epoch || slot<0)return;
    if(!++s_local_shot_id[slot])++s_local_shot_id[slot];
    s_local_shot[slot]=(NetHitReport){.epoch=s_combat_epoch,.shot_us=sysGetMicroseconds(),.shooter_life=s_hit_life[slot],.shot_id=s_local_shot_id[slot]};
    s_local_shot_active[slot]=true;
}
void netEndLocalShot(void) {int slot=netActingSlot();if(slot>=0)s_local_shot_active[slot]=false;}
static NetHitReport netMakeLocalHit(int slot,uint8_t target,uint8_t weapon,uint8_t part,float hx,float hy,float hz,float damage)
{
    NetHitReport h=s_local_shot[slot];
    if(!s_local_shot_active[slot] || netExplosiveWeapon(weapon)) {
        if(!++s_local_shot_id[slot])++s_local_shot_id[slot];
        h=(NetHitReport){.epoch=s_combat_epoch,.shot_us=sysGetMicroseconds(),.shooter_life=s_hit_life[slot],.shot_id=s_local_shot_id[slot]};
    }
    if(!++s_local_hit_id[slot])++s_local_hit_id[slot];
    h.hit_id=s_local_hit_id[slot];h.target=target;h.target_life=target<GEVR_MAX_PLAYERS?s_hit_life[target]:0;
    h.weapon=weapon;h.part=part;h.hx=hx;h.hy=hy;h.hz=hz;h.damage=damage;
    return h;
}
void netSendWorldHitReport(uint8_t target,uint8_t weapon,float x,float y,float z,float damage)
{
    if(s_state!=NET_STATE_INGAME || !netIsHost() || target>=GEVR_MAX_PLAYERS)return;
    NetHitReport h=netMakeLocalHit(s_local_slot,target,weapon,0,x,y,z,damage);
    h.shooter_life=s_hit_life[target];
    /* Environmental blasts have no remote clock or player attacker. */
    netProcessHitReport(target,&h,0);
}
/* An owned player's hit on another player: a bot's (the host) or the local player's */
void netSendHitReportAs(int shooter,uint8_t target,uint8_t weapon,uint8_t part,float x,float y,float z,float damage)
{
    if(s_state!=NET_STATE_INGAME || !s_combat_epoch || !netSlotOwned(shooter))return;
    NetHitReport h=netMakeLocalHit(shooter,target,weapon,part,x,y,z,damage);
    if(netIsHost()) {netQueueHostHit(shooter,h);return;}
    u8 raw[128];struct netbuf b={.data=raw,.size=sizeof(raw)};netbufStartWrite(&b);
    netbufWriteU32(&b,GEVR_NET_MAGIC);netbufWriteU16(&b,GEVR_NET_VERSION);
    netbufWriteU8(&b,NET_MSG_HIT_REPORT);netbufWriteU8(&b,(uint8_t)shooter);netbufWriteHitReport(&b,&h);
    netBroadcastBuf(&b,NET_CHAN_RELIABLE,ENET_PACKET_FLAG_RELIABLE,NULL);
}
/* The player ticking now, when it is owned */
void netSendHitReport(uint8_t target,uint8_t weapon,uint8_t part,float x,float y,float z,float damage)
{
    int slot=netActingSlot();
    if(slot>=0) netSendHitReportAs(slot,target,weapon,part,x,y,z,damage);
}
static uint32_t netNextLife(uint32_t life) {return life==UINT32_MAX?1:life+1;}
static bool netAcceptRespawn(int slot,uint64_t epoch,uint32_t previous,uint32_t next)
{
    if(slot<0 || slot>=GEVR_MAX_PLAYERS || epoch!=s_combat_epoch || previous!=s_hit_life[slot] || next!=netNextLife(previous))return false;
    netInvalidateHitSlot(slot);s_hit_life[slot]=next;return true;
}
void netSendRespawnEvent(uint8_t pad_index,float theta)
{
    int slot=netActingSlot();
    if(s_state!=NET_STATE_INGAME || slot<0 || !s_combat_epoch || pad_index>=startpadcount)return;
    uint32_t previous=s_hit_life[slot],next=netNextLife(previous);
    netAcceptRespawn(slot,s_combat_epoch,previous,next);
    u8 raw[40];struct netbuf b={.data=raw,.size=sizeof(raw)};netbufStartWrite(&b);
    netbufWriteU32(&b,GEVR_NET_MAGIC);netbufWriteU16(&b,GEVR_NET_VERSION);
    netbufWriteU8(&b,NET_MSG_RESPAWN);netbufWriteU8(&b,(uint8_t)slot);
    netbufWriteU8(&b,pad_index);netbufWriteF32(&b,theta);netbufWriteU64(&b,s_combat_epoch);
    netbufWriteU32(&b,previous);netbufWriteU32(&b,next);
    netBroadcastBuf(&b,NET_CHAN_RELIABLE,ENET_PACKET_FLAG_RELIABLE,NULL);
}

void netSendFireEvent(uint8_t weapon_id) {
    if (s_state != NET_STATE_INGAME) return;
    
    u8 raw[64];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_FIRE_EVENT);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteU8(&buf, weapon_id);
    
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

/* gun.c gevrNetProjectile: the local player's thrown or launched projectile */
void netSendProjectile(s32 kind, s32 hand, s32 item, const coord3d *pos, const coord3d *vel,
                       const f32 *rot9, const coord3d *extra, s32 cooktimer) {
    int slot = netActingSlot();
    if (s_state != NET_STATE_INGAME || slot < 0 || !pos || !vel || !rot9) return;

    u8 raw[128];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    coord3d zero = { 0 };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_PROJECTILE);
    netbufWriteU8(&buf, (uint8_t)slot);
    netbufWriteU8(&buf, (uint8_t)kind);
    netbufWriteU8(&buf, (uint8_t)hand);
    netbufWriteU8(&buf, (uint8_t)item);
    netbufWriteU32(&buf, (uint32_t)cooktimer);
    netbufWriteCoord(&buf, pos);
    netbufWriteCoord(&buf, vel);
    for (int i = 0; i < 9; i++) netbufWriteF32(&buf, rot9[i]);
    netbufWriteCoord(&buf, extra ? extra : &zero);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    NET_LOG("projectile tx: kind %d item %d at %.0f,%.0f,%.0f", (int)kind, (int)item, pos->x, pos->y, pos->z);
}

/* explosion.c explosionCreate: a damaging explosion an owned player caused
 * (the local player, or the host's bot); -1 is the world's, sent as the host's */
void netSendExplosionAs(s32 owner, s32 type, const coord3d *pos, const u8 *rooms, s32 ground, s32 flag8) {
    int slot = owner < 0 ? s_local_slot : owner;
    if (s_state != NET_STATE_INGAME || !netSlotOwned(slot) || !pos) return;

    u8 raw[48];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_EXPLOSION);
    netbufWriteU8(&buf, (uint8_t)slot);
    netbufWriteU8(&buf, (uint8_t)type);
    netbufWriteU8(&buf, rooms ? rooms[0] : 0xff);
    netbufWriteU8(&buf, (uint8_t)((ground ? 1 : 0) | (flag8 ? 2 : 0)));
    netbufWriteCoord(&buf, pos);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    NET_LOG("explosion tx: type %d at %.0f,%.0f,%.0f", (int)type, pos->x, pos->y, pos->z);
}
void netSendExplosion(s32 type, const coord3d *pos, const u8 *rooms, s32 ground, s32 flag8) {
    netSendExplosionAs(-1, type, pos, rooms, ground, flag8);
}

/* A world event from player slot: sent by that player's own headset, relayed
 * by the host, applied only while the level's players are live. */
static bool netAcceptPlayerEvent(ENetPeer *peer, int slot) {
    if (s_state != NET_STATE_INGAME || slot < 0 || slot >= s_max_players || slot == s_local_slot ||
        !s_lobby_state.slots[slot].connected) return false;
    if (netIsHost()) {
        if (slot == s_host_slot || s_client_peers[slot] != peer || ((int)(intptr_t)peer->data - 1) != slot) return false;
    } else if (peer != s_server_peer) {
        return false;
    }
    return true;
}

static bool netCoordFinite(const coord3d *c) {
    return isfinite(c->x) && isfinite(c->y) && isfinite(c->z) &&
           fabsf(c->x) < 1.0e6f && fabsf(c->y) < 1.0e6f && fabsf(c->z) < 1.0e6f;
}

void netSendVoipChunk(uint32_t sequence, const uint8_t *opus_data, uint16_t size) {
    if ((s_state != NET_STATE_INGAME && s_state != NET_STATE_HOSTING_LOBBY &&
         s_state != NET_STATE_CLIENT_LOBBY) || s_local_slot < 0 || !opus_data ||
        size == 0 || size > GEVR_VOIP_MAX_BYTES) return;
    
    u8 raw[256];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_VOIP_FRAME);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteU32(&buf, sequence);
    netbufWriteU16(&buf, size);
    netbufWriteData(&buf, opus_data, size);
    
    if (netIsHost()) {
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++) if (s_client_peers[i] && netVoiceSameGroup(i, s_local_slot))
            enet_peer_send(s_client_peers[i], NET_CHAN_VOIP, enet_packet_create(buf.data, buf.wp, ENET_PACKET_FLAG_UNSEQUENCED));
    } else netBroadcastBuf(&buf, NET_CHAN_VOIP, ENET_PACKET_FLAG_UNSEQUENCED, NULL);
}

static void netReceiveDamageEvent(ENetPeer *peer,int slot,struct netbuf *b)
{
    NetHitReport h;
    if(!netIsHost() && peer==s_server_peer && s_state==NET_STATE_INGAME
        && (s_phase==NET_PHASE_IN_PROGRESS || s_phase==NET_PHASE_WARMUP)
        && slot>=0 && slot<GEVR_MAX_PLAYERS && netbufReadHitReport(b,&h) && !netbufReadLeft(b)
        && h.epoch==s_combat_epoch && h.target_life==s_hit_life[h.target]
        && netSlotOccupied(h.target) && g_playerPointers[h.target])
        netApplyDamage(h.target,slot,h.weapon,h.damage,h.hx,h.hz);

}
static void netReceiveRespawn(ENetPeer *peer,int slot,struct netbuf *b,const u8 *data,size_t size)
{
    if (netSlotIsSpectator(slot)) return;
    uint8_t pad_index = netbufReadU8(b);
    float theta = netbufReadF32(b);
    uint64_t epoch=netbufReadU64(b);uint32_t previous_life=netbufReadU32(b),next_life=netbufReadU32(b);
    /* Applied whether or not this headset saw the player die: the
     * respawn resets health, armour and position, so it also brings
     * a copy that drifted back in line with its owner. */
    if (s_state != NET_STATE_INGAME || b->error || netbufReadLeft(b) || !isfinite(theta) ||
        slot < 0 || slot >= GEVR_MAX_PLAYERS || slot == s_local_slot ||
        pad_index >= startpadcount || g_playerPointers[slot] == NULL ||
        g_playerPointers[slot]->prop == NULL) return;
    if (netIsHost()) {
        if (slot == s_host_slot || s_client_peers[slot] != peer ||
            ((int)(intptr_t)peer->data - 1) != slot) return;
    } else if (peer != s_server_peer) {
        return;
    }

    s32 prev = get_cur_playernum();
    if(!netAcceptRespawn(slot,epoch,previous_life,next_life))return;
    set_cur_player(slot);
    mp_respawn_handler_net(pad_index, theta);
    s_remote_moves[slot].pos = g_playerPointers[slot]->prop->pos;
    s_remote_moves[slot].angles[0] = theta;
    /* A transform from the old life must not restore death or health. */
    s_remote_moves[slot].epoch = epoch;
    s_remote_moves[slot].life_id = next_life;
    s_remote_moves[slot].death_us = 0;
    s_remote_moves[slot].dead = 0;
    s_remote_moves[slot].health = g_playerPointers[slot]->bondhealth;
    s_remote_moves[slot].armour = g_playerPointers[slot]->bondarmour;
    s_remote_active[slot] = false;
    set_cur_player(prev);

    if (netIsHost()) {
        netBroadcastPacket(data, size, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, peer);
    }

}

/*
 * A joiner's slot (HELLO). A player back after a host migration names its
 * slot, kept for it (s_slot_grace_us); otherwise the first free one. Slot 0
 * is the first host's: after a migration it stays empty. With every place
 * taken a bot gives its own up, in any phase: the joiner drops in at once,
 * on the bot's side. -1: full.
 */
static int netJoinSlot(int previous, const char *clean, bool *replacing_bot, uint8_t *bot_team) {
    int assigned = -1;
    *replacing_bot = false;
    *bot_team = NET_TEAM_NONE;
    if (previous >= 0 && previous < s_max_players && previous != s_host_slot &&
        s_lobby_state.slots[previous].connected && s_client_peers[previous] == NULL &&
        s_slot_grace_us[previous] && strcasecmp(s_lobby_state.slots[previous].name, clean) == 0) {
        assigned = previous;
        NET_LOG("%s is back in slot %d after the host change", clean, previous);
    }
    for (int i = 0; assigned < 0 && i < s_max_players; i++) {
        if (i != s_host_slot && !s_lobby_state.slots[i].connected) {
            assigned = i;
        }
    }
    if (assigned >= 0 && (assigned == previous || netGetConnectedPlayerCount() < netGetMaxPlayers())) return assigned;
    int bot = netGetHumanPlayerCount() < netGetMaxPlayers() ? netBotToReplace() : -1;
    if (bot < 0) return -1;
    *replacing_bot = true;
    *bot_team = s_state == NET_STATE_INGAME ? s_round.team[bot] : s_lobby_state.slots[bot].team;
    NET_LOG("%s takes the place of %s (slot %d)", clean, s_lobby_state.slots[bot].name, bot);
    netReleaseBotSlot(bot);
    return bot;
}

static void netHandlePacket(ENetPeer *peer, const uint8_t *data, size_t size) {
    if (size < 8) return;
    
    struct netbuf buf;
    netbufStartReadData(&buf, data, (u32)size);
    
    const uint32_t magic = netbufReadU32(&buf);
    const uint16_t version = netbufReadU16(&buf);
    const uint8_t msg_type = netbufReadU8(&buf);
    const uint8_t slot_id = netbufReadU8(&buf);
    
    if (magic != GEVR_NET_MAGIC || version != GEVR_NET_VERSION) {
        NET_ERR("Ignored packet with invalid magic/version: %08x v%d", magic, version);
        if (magic == GEVR_NET_MAGIC && version != GEVR_NET_VERSION)
            enet_peer_disconnect(peer, 0);
        return;
    }
    /* Pose packets keep an idle player alive. A frozen or force-quit headset stops here. */
    {
        uint64_t heard = sysGetMicroseconds();
        if (netIsHost()) {
            int from = (int)(intptr_t)peer->data - 1;
            if (from >= 0 && from < GEVR_MAX_PLAYERS && s_client_peers[from] == peer)
                s_slot_heard_us[from] = heard;
        } else if (peer == s_server_peer) {
            s_host_heard_us = heard;
        }
    }
    
    switch (msg_type) {
        case NET_MSG_CLOCK: netReceiveClock(peer,slot_id,&buf);break;
        case NET_MSG_HELLO: {
            if (!netIsHost()) break;
            if (peer->data) break;
            char *name = netbufReadStr(&buf);
            const char *name_end = (const char *)buf.data + buf.rp;
            uint8_t requested_chr = netbufReadU8(&buf);
            if (buf.error) break;
            if (requested_chr >= netCharacterCount()) requested_chr = 0;

            /* The same name back on a new connection: the earlier one is dead
             * (a crash, a quit without a goodbye) and its slot is theirs again. */
            char clean[GEVR_MAX_NAME_LEN];
            netCleanName(clean, name, name_end);
            for (int i = 0; i < s_max_players; i++) {
                if (s_lobby_state.slots[i].connected && s_client_peers[i] && s_client_peers[i] != peer &&
                    strcasecmp(s_lobby_state.slots[i].name, clean) == 0) {
                    NET_LOG("%s is back on a new connection; slot %d's earlier one dropped", clean, i);
                    netHostDropSlot(i, s_client_peers[i]);
                }
            }

            int previous = netbufReadLeft(&buf) >= 1 ? netbufReadU8(&buf) : 0xFF;
            bool replacing_bot = false;
            uint8_t bot_team = NET_TEAM_NONE;
            int assigned = netJoinSlot(previous, clean, &replacing_bot, &bot_team);

            if (assigned < 0) {
                NET_ERR("Rejecting connection: lobby full");
                enet_peer_disconnect(peer, 0);
                break;
            }
            
            s_client_peers[assigned] = peer;
            s_client_can_be_kicked[assigned] = false;
            s_client_caps[assigned] = 0;
            s_client_caps_known[assigned] = false;
            peer->data = (void *)(intptr_t)(assigned + 1);
            
            s_lobby_state.slots[assigned].connected = 1;
            s_lobby_state.slots[assigned].ready = 0;
            s_lobby_state.slots[assigned].loaded = 0;
            s_lobby_state.slots[assigned].chr_id = requested_chr;
            if (!s_slot_grace_us[assigned]) {
                s_lobby_state.slots[assigned].team = bot_team;
                s_lobby_state.slots[assigned].eliminated = 0;
                s_lobby_state.slots[assigned].ping_ms = NET_PING_UNKNOWN;
                s_round.team[assigned] = bot_team;
                /* a deathmatch's late joiner watches until the next round; a co-op one, or one taking a bot's place, drops in (#94) */
                s_lobby_state.slots[assigned].spectator = s_state == NET_STATE_INGAME && s_phase == NET_PHASE_IN_PROGRESS &&
                    s_round.config.mode != NET_MODE_COOP && !replacing_bot;
                /* a bot's place in a loaded stage keeps its body until the next load */
                if (!replacing_bot || s_state != NET_STATE_INGAME) s_round.character[assigned] = requested_chr;
                memset(s_round.loadout[assigned], 0, 4);
            }
            if (assigned != previous) memset(s_lobby_state.slots[assigned].loadout, 0, 4);   /* a returning player keeps its guns */
            netSetJoinerName(assigned, name, name_end);
            
            /* Send welcome to client */
            u8 wraw[128];   /* 35 + 4N bytes: 67 at eight players */
            struct netbuf wbuf = { .data = wraw, .size = sizeof(wraw) };
            netbufStartWrite(&wbuf);
            netbufWriteU32(&wbuf, GEVR_NET_MAGIC);
            netbufWriteU16(&wbuf, GEVR_NET_VERSION);
            netbufWriteU8(&wbuf, NET_MSG_WELCOME);
            netbufWriteU8(&wbuf, 0);
            netbufWriteU8(&wbuf, (uint8_t)assigned);
            netbufWriteMatchConfig(&wbuf, s_state == NET_STATE_INGAME ? &s_round.config : &s_lobby_state.config);
            netbufWriteU8(&wbuf, (uint8_t)s_host_slot);
            netWriteCombatIdentity(&wbuf);
            if (wbuf.error) { NET_ERR("WELCOME does not fit its buffer; turning %s away", clean); netHostDropSlot(assigned, peer); break; }

            ENetPacket *wp = enet_packet_create(wbuf.data, wbuf.wp, ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(peer, NET_CHAN_RELIABLE, wp);
            
            /* Send host's app version and known peer versions to the new client */
            netSendLocalAppVersion(peer);
            for (int k = 0; k < s_max_players; k++) {
                if (k != assigned && s_lobby_state.slots[k].connected && s_slot_app_version[k][0]) {
                    u8 vraw[64];
                    struct netbuf vbuf = { .data = vraw, .size = sizeof(vraw) };
                    netbufStartWrite(&vbuf);
                    netbufWriteU32(&vbuf, GEVR_NET_MAGIC);
                    netbufWriteU16(&vbuf, GEVR_NET_VERSION);
                    netbufWriteU8(&vbuf, NET_MSG_APP_VERSION);
                    netbufWriteU8(&vbuf, (uint8_t)k);
                    netbufWriteStr(&vbuf, s_slot_app_version[k]);
                    ENetPacket *vp = enet_packet_create(vbuf.data, vbuf.wp, ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(peer, NET_CHAN_RELIABLE, vp);
                }
            }
            
            netUpdateBots(false);
            netBroadcastLobbyState();
            if (s_state == NET_STATE_INGAME) netSendMatchStartTo(peer);
            netSendLobbyHandoffTo(peer);
            netBroadcastAllVotes();
            netBroadcastRoundNotice();
            NET_LOG("Assigned player '%s' to slot %d", s_lobby_state.slots[assigned].name, assigned);
            break;
        }
        case NET_MSG_WELCOME: {
            if (netIsHost() || peer != s_server_peer ||
                (s_state != NET_STATE_CONNECTING && s_state != NET_STATE_MIGRATING)) break;
            const bool rejoin = s_state == NET_STATE_MIGRATING;
            int slot = netbufReadU8(&buf);
            if (slot < 0 || slot >= GEVR_MAX_PLAYERS || (rejoin && slot != s_local_slot)) {
                /* a rejoin under another slot would move the player's own struct: not mid-level */
                NET_ERR("Welcomed as slot %d%s; leaving", slot, rejoin ? " after a host change" : "");
                enet_peer_disconnect(peer, 0);
                if (rejoin) netMigrationGiveUpNow();
                break;
            }
            s_local_slot = slot;
            netbufReadMatchConfig(&buf, &s_lobby_state.config);
            if (netbufReadLeft(&buf) >= 1) {
                int host = netbufReadU8(&buf);
                if (host >= 0 && host < GEVR_MAX_PLAYERS && host != s_local_slot) s_host_slot = host;
            }
            NetRoundSettings identity;
            if(!netReadCombatIdentity(&buf,&identity) || netbufReadLeft(&buf))break;
            netImportCombatIdentity(&identity);
            s_state = NET_STATE_CLIENT_LOBBY;
            netSendClientCaps();
            strncpy(s_slot_app_version[s_local_slot], s_local_app_version, sizeof(s_slot_app_version[0]) - 1);
            netSendLocalAppVersion(s_server_peer);
            uint8_t saved_items[4];
            for (int k = 0; k < 4; k++) saved_items[k] = netItemIndexOf(VrMpLoadout[k]) >= 0 ? VrMpLoadout[k] : netItem(0)->item;
            netLobbySetLoadout(saved_items);
            NET_LOG("Connected! Assigned local slot: %d, stage: %d", s_local_slot, s_lobby_state.config.stage);
            break;
        }
        case NET_MSG_LOBBY_STATE: {
            if (netIsHost() || peer != s_server_peer) break;
            NetMsgLobbyState next = s_lobby_state;
            NetRoundSettings round;
            netbufReadMatchConfig(&buf, &next.config);
            if (!netReadRoundSettings(&buf, &round) || !netValidConfig(&next.config)) break;
            bool lobby_open = netbufReadU8(&buf) != 0;
            bool start_after_load = netbufReadU8(&buf) != 0;
            next.countdown_secs = netbufReadU8(&buf);
            bool valid = true;
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                NetLobbySlot *slot = &next.slots[i];
                slot->connected = netbufReadU8(&buf);
                slot->ready = netbufReadU8(&buf);
                slot->loaded = netbufReadU8(&buf);
                slot->chr_id = netbufReadU8(&buf);
                slot->spectator = netbufReadU8(&buf);
                slot->team = netbufReadU8(&buf);
                slot->eliminated = netbufReadU8(&buf);
                slot->is_bot = netbufReadU8(&buf);
                slot->ping_ms = netbufReadU16(&buf);
                if (slot->connected > 1 || slot->ready > 1 || slot->loaded > 1 || slot->spectator > 1 || slot->eliminated > 1 || slot->is_bot > 1 || slot->team > NET_TEAM_NONE || slot->chr_id >= netCharacterCount()) valid = false;
                for (int k = 0; k < 4; k++) {
                    slot->loadout[k] = netbufReadU8(&buf);
                    if (slot->loadout[k] && netItemIndexOf(slot->loadout[k]) < 0) valid = false;
                }
                char *name = netbufReadStr(&buf);
                if (name) netCleanName(slot->name, name, (const char *)buf.data + buf.rp);
            }
            if (buf.error || netbufReadLeft(&buf) || !valid) break;
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                if (s_lobby_state.slots[i].spectator != next.slots[i].spectator ||
                    (s_lobby_state.slots[i].connected && !next.slots[i].connected)) netVoiceForgetSlot((uint8_t)i);
                if (s_lobby_state.slots[i].connected && !next.slots[i].connected) {
                    netForgetPlayerScore(i);
                    s_slot_app_version[i][0] = 0;
                    s_remote_active[i] = false;
                }
            }
            if (s_local_slot >= 0 && s_lobby_state.slots[s_local_slot].spectator != next.slots[s_local_slot].spectator)
                for (int i = 0; i < GEVR_MAX_PLAYERS; i++) netVoiceForgetSlot((uint8_t)i);
            bool voice_changed = s_round.config.voice_mode != round.config.voice_mode;
            for (int i=0;i<GEVR_MAX_PLAYERS;i++) if (s_lobby_state.slots[i].eliminated != next.slots[i].eliminated || s_round.team[i] != round.team[i]) voice_changed = true;
            if (voice_changed) for (int i=0;i<GEVR_MAX_PLAYERS;i++) netVoiceForgetSlot((uint8_t)i);
            s_lobby_received_us = sysGetMicroseconds();
            s_lobby_state = next;
            s_round = round;netImportCombatIdentity(&round);
            s_lobby_open = lobby_open;
            s_start_after_load = start_after_load;
            break;
        }
        case NET_MSG_LOBBY_READY: {
            netReceiveLobbyReady(peer, slot_id, &buf, size);
            break;
        }
        case NET_MSG_CLIENT_CAPS:
            netReceiveClientCaps(peer, slot_id, &buf, size);
            break;
        case NET_MSG_LOBBY_TEAM: {
            int slot = (int)(intptr_t)peer->data - 1;
            uint8_t team = netbufReadU8(&buf);
            if (netIsHost() && !buf.error && size == 9 && slot >= 0 && slot < GEVR_MAX_PLAYERS &&
                slot_id == slot && s_client_peers[slot] == peer) netSetSlotTeam(slot, team);
            break;
        }
        case NET_MSG_LOBBY_CHARACTER: {
            /* in the lobby, and in a match: the body changes at the next load */
            if (!netIsHost()) break;
            int slot = ((int)(intptr_t)peer->data - 1);
            /* netLobbySetCharacter sends the slot in the header, then one byte. */
            uint8_t chr_id = netbufReadU8(&buf);
            if (!buf.error && size == 9 && slot >= 0 && slot < s_max_players &&
                slot_id == slot && chr_id < netCharacterCount() &&
                s_client_peers[slot] == peer) {
                s_lobby_state.slots[slot].chr_id = chr_id;
                
                netBroadcastLobbyState();
            }
            break;
        }
        case NET_MSG_START_MATCH: {
            if (netIsHost() || peer != s_server_peer || s_state != NET_STATE_CLIENT_LOBBY) break;
            NetRoundSettings round;
            if (!netReadRoundSettings(&buf, &round)) break;
            uint32_t seed = netbufReadU32(&buf);
            uint8_t num_players = netbufReadU8(&buf);
            uint8_t phase = netbufReadU8(&buf);
            if (buf.error || netbufReadLeft(&buf) || phase > NET_PHASE_IN_PROGRESS) break;
            s_round = round;netImportCombatIdentity(&round);
            s_max_players = netConfigSlots(&round.config);
            s_rng_seed = seed;
            extern void randomSetSeed(u32);
            randomSetSeed(s_rng_seed);
            (void)num_players;
            s_state = NET_STATE_INGAME;
            /* A late joiner stays in warmup until the reliable match snapshot arrives. */
            s_phase = phase == NET_PHASE_IN_PROGRESS ? NET_PHASE_WARMUP : (NetPhase)phase;
            s_waiting_for_match_snapshot = phase == NET_PHASE_IN_PROGRESS;
            s_stage_ready_sent = false;
            s_lobby_state.slots[s_local_slot].loaded = 0;
            NET_LOG("Starting match on stage %d! Seed: 0x%08X", s_lobby_state.config.stage, s_rng_seed);
            if (s_rejoining) {
                /* Back with the new host: the stage is loaded already, so this
                 * is the load's end (STAGE_READY, then the snapshots) - unless
                 * the map changed meanwhile, which is a reload. */
                s_rejoining = false;
                if ((int)s_round.config.stage == (int)bossGetStageNum()) {
                    netStageLoaded();
                    s_stage_fade_in = false;
                } else {
                    s_round_reset_pending = true;
                }
            }
            break;
        }
        case NET_MSG_STAGE_READY: {
            if (!netIsHost() || s_state != NET_STATE_INGAME) break;
            int slot = ((int)(intptr_t)peer->data - 1);
            if (slot < 0 || slot >= s_max_players || s_client_peers[slot] != peer ||
                !s_lobby_state.slots[slot].connected || size != 24) break;
            float x = netbufReadF32(&buf), y = netbufReadF32(&buf);
            float z = netbufReadF32(&buf), yaw = netbufReadF32(&buf);
            if (buf.error) break;
            bool was_ready = s_lobby_state.slots[slot].loaded != 0;
            /* back after a host change: still standing where it was, not respawned */
            bool returning = s_slot_grace_us[slot] != 0;
            s_slot_grace_us[slot] = 0;
            if (!was_ready && !returning && s_phase == NET_PHASE_IN_PROGRESS && s_round.config.mode != NET_MODE_COOP) {
                s_lobby_state.slots[slot].spectator = 1;
                netVoiceForgetSlot((uint8_t)slot);
            }
            if (!was_ready && g_playerPointers[slot] && g_playerPointers[slot]->prop) {
                struct player *pl = g_playerPointers[slot];
                pl->prop->pos.x = x;
                pl->prop->pos.y = y;
                pl->prop->pos.z = z;
                pl->pos = pl->prop->pos;
                pl->vv_theta = yaw;
            }
            if (s_phase == NET_PHASE_IN_PROGRESS) {
                netSendMatchSnapshot(peer);
                netSendWorldSnapshot(peer);
                netSendAmmoState(peer);
            }
            s_lobby_state.slots[slot].loaded = 1;
            netBroadcastLobbyState();
            netReadyProgress();
            /* co-op: its guards and where to start (a mission under way), or the new host's slots */
            if (!was_ready || returning) netCoopPlayerJoined(slot, returning);
            break;
        }
        case NET_MSG_ROUND_RESET: {
            if (netIsHost() || peer != s_server_peer || s_state != NET_STATE_INGAME) break;
            NetRoundSettings round;
            if (!netReadRoundSettings(&buf, &round) || netbufReadLeft(&buf)) break;
            s_round = round;netImportCombatIdentity(&round);
            s_phase = NET_PHASE_WARMUP;
            s_match_ended = false;
            s_round_reset_pending = true;
            s_countdown_end_us = 0;
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) s_remote_active[i] = false;
            s_lobby_state.slots[s_local_slot].loaded = 0;
            break;
        }
        case NET_MSG_VOTE: {
            /* a player's vote on a ballot: the host keeps the tally and tells everyone */
            if (!netIsHost() || size != 10 || s_lobby_state.config.next_round != NET_NEXT_VOTE) break;
            int slot = slot_id;
            uint8_t kind = netbufReadU8(&buf);
            uint8_t vote = netbufReadU8(&buf);
            if (buf.error || kind >= NET_BALLOT_COUNT || !netAcceptPlayerEvent(peer, slot)) break;
            if (kind == NET_BALLOT_WEAPONS && s_lobby_state.config.scenario == SCENARIO_MWTGG) break;
            s_vote[kind][slot] = vote < netBallotSize(kind) ? (int8_t)vote : -1;
            netBroadcastVotes(kind);
            break;
        }
        case NET_MSG_VOTES: {
            if (netIsHost() || peer != s_server_peer || size != 9 + GEVR_MAX_PLAYERS) break;
            uint8_t kind = netbufReadU8(&buf);
            if (kind >= NET_BALLOT_COUNT) break;
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                uint8_t vote = netbufReadU8(&buf);
                s_vote[kind][i] = vote < netBallotSize(kind) ? (int8_t)vote : -1;
            }
            break;
        }
        case NET_MSG_LOBBY_LOADOUT: {
            /* a player's four spawn guns (config.loadouts); the lobby state carries them */
            if (!netIsHost() || size != 12) break;
            int slot = ((int)(intptr_t)peer->data - 1);
            if (slot < 0 || slot >= s_max_players || s_client_peers[slot] != peer || slot_id != slot) break;
            uint8_t items[4];
            for (int k = 0; k < 4; k++) items[k] = netbufReadU8(&buf);
            bool valid = !buf.error;
            for (int k = 0; k < 4; k++) if (netItemIndexOf(items[k]) < 0) valid = false;
            if (!valid) break;
            memcpy(s_lobby_state.slots[slot].loadout, items, sizeof(items));
            netBroadcastLobbyState();
            break;
        }
        case NET_MSG_LOBBY_HANDOFF: {
            /* what a client keeps to carry the match on without the host */
            if (netIsHost() || peer != s_server_peer) break;
            char *code = netbufReadStr(&buf);
            const char *code_end = (const char *)buf.data + buf.rp;
            char *token = netbufReadStr(&buf);
            const char *token_end = (const char *)buf.data + buf.rp;
            char *name = netbufReadStr(&buf);
            const char *name_end = (const char *)buf.data + buf.rp;
            uint8_t max_players = netbufReadU8(&buf);
            if (buf.error || !code || !token || !name) break;
            snprintf(s_lobby_code, sizeof(s_lobby_code), "%.*s", (int)(code_end - code), code);
            snprintf(s_lobby_token, sizeof(s_lobby_token), "%.*s", (int)(token_end - token), token);
            netCleanName(s_game_name, name, name_end);
            if (max_players >= 2 && max_players <= GEVR_MAX_PLAYERS) s_lobby_max_players = max_players;
            NET_LOG("Lobby handoff kept: code %s, game '%s', %d players", s_lobby_code[0] ? "yes" : "none", s_game_name, max_players);
            break;
        }
        case NET_MSG_COOP_END: {
            if (netIsHost() || peer != s_server_peer || s_state != NET_STATE_INGAME || size < 14) break;
            uint8_t result = netbufReadU8(&buf);
            uint8_t next = netbufReadU8(&buf);
            uint32_t delay = netbufReadU32(&buf);
            bool in[GEVR_MAX_PLAYERS];
            uint16_t kills[GEVR_MAX_PLAYERS], hits[GEVR_MAX_PLAYERS][NET_COOP_TALLY_REGS];
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                in[i] = netbufReadU8(&buf) != 0;
                kills[i] = netbufReadU16(&buf);
                for (int r = 0; r < NET_COOP_TALLY_REGS; r++) hits[i][r] = netbufReadU16(&buf);
            }
            if (buf.error || netbufReadLeft(&buf) || result > NET_COOP_RESULT_ABORTED || !netCoopStageValid(next) ||
                !netCoopActive()) break;
            (void)delay;
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                s_coop_tally_in[i] = in[i];
                s_coop_tally_kills[i] = kills[i];
                memcpy(s_coop_tally_hits[i], hits[i], sizeof(hits[i]));
                if (in[i]) netCoopTallyName(i);
            }
            s_coop_tally_valid = true;
            netCoopApplyEnd(result);
            break;
        }
        case NET_MSG_COOP_MENU:
            /* the party's menus: the host's screen (net_coop_menu.c) */
            if (netIsHost() || peer != s_server_peer || s_state != NET_STATE_INGAME) break;
            netCoopReceiveMenu(&buf);
            break;
        case NET_MSG_CHR_STATE:
        case NET_MSG_CHR_SPAWN:
        case NET_MSG_CHR_REMOVE:
        case NET_MSG_COOP_DAMAGE:
        case NET_MSG_COOP_MISSION:
        case NET_MSG_COOP_TEXT:
        case NET_MSG_COOP_GRANT:
        case NET_MSG_CHR_AI:
        case NET_MSG_CHR_REMAP:
        case NET_MSG_COOP_JOIN:
        case NET_MSG_COOP_CINEMA:
            if (netIsHost() || peer != s_server_peer || s_state != NET_STATE_INGAME) break;
            netCoopReceive(msg_type, slot_id, 1, &buf);
            break;
        case NET_MSG_COOP_HIT:
        case NET_MSG_COOP_EVENT:
            if (!netIsHost() || s_state != NET_STATE_INGAME || slot_id >= GEVR_MAX_PLAYERS ||
                s_client_peers[slot_id] != peer || (int)(intptr_t)peer->data - 1 != slot_id) break;
            netCoopReceive(msg_type, slot_id, 0, &buf);
            break;
        case NET_MSG_COOP_TANK:
            /* a player's driven tank: the host poses its own and relays it (net_coop.c netCoopSendTank) */
            if (s_state != NET_STATE_INGAME || slot_id >= GEVR_MAX_PLAYERS || (int)slot_id == s_local_slot) break;
            if (netIsHost()) {
                if (s_client_peers[slot_id] != peer || (int)(intptr_t)peer->data - 1 != slot_id) break;
                if (size > 8 && (data[8] & NET_COOP_TANK_DRIVEN))
                    netBroadcastPacket(data, size, NET_CHAN_PLAYER_STATE, 0, peer);
                else
                    netBroadcastPacket(data, size, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, peer);
            } else if (peer != s_server_peer) {
                break;
            }
            netCoopReceive(msg_type, slot_id, !netIsHost(), &buf);
            break;
        case NET_MSG_COUNTDOWN: {
            if (netIsHost() || peer != s_server_peer || size != 12) break;
            uint32_t ms = netbufReadU32(&buf);
            s_countdown_end_us = ms ? sysGetMicroseconds() + (uint64_t)ms * 1000 : 0;
            break;
        }
        case NET_MSG_ROUND_NOTICE:
            netReceiveRoundNotice(peer, &buf, size);
            break;
        case NET_MSG_MATCH_END: {
            if (netIsHost() || peer != s_server_peer || size != 8 ||
                s_state != NET_STATE_INGAME || s_phase != NET_PHASE_IN_PROGRESS) break;
            s_match_ended = true;
            for (int i=0;i<GEVR_MAX_PLAYERS;i++) netVoiceForgetSlot((uint8_t)i);
            mpCalculateAwards(false);
            break;
        }
        case NET_MSG_ROUND_PHASE: {
            if (netIsHost() || peer != s_server_peer || size != 9) break;
            uint8_t phase = netbufReadU8(&buf);
            if (phase != NET_PHASE_WARMUP && phase != NET_PHASE_IN_PROGRESS) break;
            netTransitionRoundPhase((NetPhase)phase);
            for (int i=0;i<GEVR_MAX_PLAYERS;i++) netVoiceForgetSlot((uint8_t)i);
            break;
        }
        case NET_MSG_MATCH_SNAPSHOT: {
            if (netIsHost() || peer != s_server_peer || s_state != NET_STATE_INGAME ||
                s_local_slot < 0) break;
            bool game_over = netbufReadU8(&buf) != 0;
            u32 clock = netbufReadU32(&buf);
            NetRoundSettings round;
            NetMatchConfig pending;
            uint8_t teams[GEVR_MAX_PLAYERS], eliminated[GEVR_MAX_PLAYERS], order_out[GEVR_MAX_PLAYERS];
            uint16_t ping[GEVR_MAX_PLAYERS];
            if (!netReadRoundSettings(&buf, &round)) break;
            netbufReadMatchConfig(&buf, &pending);
            if (!netValidConfig(&pending)) break;
            bool valid_roster = true;
            for (int i=0;i<GEVR_MAX_PLAYERS;i++) {
                teams[i]=netbufReadU8(&buf); eliminated[i]=netbufReadU8(&buf);
                ping[i]=netbufReadU16(&buf); order_out[i]=netbufReadU8(&buf);
                if (teams[i]>NET_TEAM_NONE || eliminated[i]>1 || order_out[i]>GEVR_MAX_PLAYERS) valid_roster=false;
            }
            if (!valid_roster || buf.error) break;
            u32 score_bank[GEVR_MAX_PLAYERS];
            u32 scores[GEVR_MAX_PLAYERS][GEVR_MAX_PLAYERS];
            uint8_t occupied[GEVR_MAX_PLAYERS], special[GEVR_MAX_PLAYERS];
            float state[GEVR_MAX_PLAYERS][7];
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                score_bank[i] = netbufReadU32(&buf);
                for (int j = 0; j < GEVR_MAX_PLAYERS; j++) scores[i][j] = netbufReadU32(&buf);
            }
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                occupied[i] = netbufReadU8(&buf);
                for (int j = 0; j < 7; j++) state[i][j] = netbufReadF32(&buf);
                special[i] = netbufReadU8(&buf);
            }
            if (buf.error || netbufReadLeft(&buf) != 0) break;
            s_round = round;netImportCombatIdentity(&round);
            s_lobby_state.config = pending;
            s_lobby_received_us = sysGetMicroseconds();
            for (int i=0;i<GEVR_MAX_PLAYERS;i++) {
                s_lobby_state.slots[i].team=teams[i];
                s_lobby_state.slots[i].eliminated=eliminated[i];
                s_lobby_state.slots[i].ping_ms=ping[i];
                g_playerPlayerData[i].order_out_in_yolt=order_out[i];
                netVoiceForgetSlot((uint8_t)i);
            }
            D_80048394 = (s32)clock;
            D_800483A8 = (s32)clock;
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                g_playerPlayerData[i].gevr_score_bank = (s32)score_bank[i];
                for (int j = 0; j < GEVR_MAX_PLAYERS; j++)
                    g_playerPlayerData[i].kill_counts[j] = (s32)scores[i][j];
                if (i == s_local_slot || !occupied[i] || !g_playerPointers[i] ||
                    !g_playerPointers[i]->prop) continue;
                struct player *pl = g_playerPointers[i];
                pl->prop->pos.x = state[i][0];
                pl->prop->pos.y = state[i][1];
                pl->prop->pos.z = state[i][2];
                pl->pos = pl->prop->pos;
                pl->vv_theta = state[i][3];
                pl->vv_verta = state[i][4];
                pl->bondhealth = state[i][5];
                pl->bondarmour = state[i][6];
                pl->bonddead = pl->bondhealth <= 0;
                if (special[i] & 1) netTakeSpecialItem(i, ITEM_TOKEN);
                if (special[i] & 2) netTakeSpecialItem(i, ITEM_GOLDENGUN);
            }
            s_phase = NET_PHASE_IN_PROGRESS;
            s_waiting_for_match_snapshot = false;
            s_match_ended = game_over;
            if (game_over) mpCalculateAwards(false);
            break;
        }
        case NET_MSG_AMMO_STATE: {
            if (!netIsHost() && peer == s_server_peer && s_state == NET_STATE_INGAME &&
                netPlayersWereTicked()) netApplyAmmoPacket(&buf);
            break;
        }
        case NET_MSG_AMMO_IMPULSE: {
            if (!netIsHost() || s_state != NET_STATE_INGAME || !netPlayersWereTicked() ||
                !netAcceptPlayerEvent(peer,slot_id) || netSlotIsSpectator(slot_id)) break;
            u8 stage=netbufReadU8(&buf);uint32_t epoch=netbufReadU32(&buf);u16 index=netbufReadU16(&buf);coord3d dir;
            for(int i=0;i<3;i++) dir.f[i]=netbufReadF32(&buf);
            if (buf.error || netbufReadLeft(&buf) || stage != s_round.config.stage || epoch != s_round.world_epoch || index >= 0x8000) break;
            float len=dir.x*dir.x+dir.y*dir.y+dir.z*dir.z;
            if (!isfinite(len) || len < .5f || len > 1.5f) break;
            ObjectRecord *obj=netObjectByIndex(index,PROPDEF_AMMO);
            if (gevrAmmoNetworked(obj)) gevrAmmoImpulseWorld(obj,&dir);
            break;
        }
        case NET_MSG_WORLD_SNAPSHOT: {
            if (netIsHost() || peer != s_server_peer || s_state != NET_STATE_INGAME ||
                !g_CurrentSetup.propDefs) break;
            u8 count = netbufReadU8(&buf);
            if (count == 0 || count > 24 || size != 9u + (size_t)count * 18u) break;
            for (int i = 0; i < count; i++) {
                u16 index = netbufReadU16(&buf);
                u8 type = netbufReadU8(&buf);
                u8 has_prop = netbufReadU8(&buf);
                u8 enabled = netbufReadU8(&buf);
                u32 runtime = netbufReadU32(&buf);
                float position = netbufReadF32(&buf);
                u8 state = netbufReadU8(&buf);
                u32 regen = netbufReadU32(&buf);
                if (buf.error) break;
                if (index >= 0x8000) continue; /* pool slots are not a shared identity */
                ObjectRecord *obj = netObjectByIndex(index, type);
                if (!obj) continue;
                if (!has_prop) {
                    if (obj->prop) objFreePermanently(obj, true);
                    continue;
                }
                if (!obj->prop) continue;
                const u32 mask = RUNTIMEBITFLAG_REMOVE | RUNTIMEBITFLAG_DESTROYED |
                                 RUNTIMEBITFLAG_BEENOPENED;
                obj->runtime_bitflags = (obj->runtime_bitflags & ~mask) | (runtime & mask);
                if (enabled) chrpropEnable(obj->prop);
                else chrpropDisable(obj->prop);
                obj->prop->timetoregen = (s32)regen;
                if (type == PROPDEF_DOOR) {
                    DoorRecord *door = (DoorRecord *)obj;
                    door->openPosition = position;
                    door->openstate = (s8)state;
                    doorUpdateBbox(door);
                    extern void doorBuildClippedVertices(DoorRecord *door);
                    doorBuildClippedVertices(door);
                }
            }
            break;
        }
        case NET_MSG_PLAYER_STATE: {
            int slot = slot_id;
            if (s_state == NET_STATE_INGAME && slot >= 0 && slot < s_max_players &&
                slot != s_local_slot && s_lobby_state.slots[slot].connected &&
                s_lobby_state.slots[slot].loaded && !s_lobby_state.slots[slot].spectator &&
                ((!netIsHost() && peer == s_server_peer) ||
                 (netIsHost() && slot != s_host_slot && s_client_peers[slot] == peer &&
                  ((int)(intptr_t)peer->data - 1) == slot))) {
                struct netplayermove move;
                netbufReadPlayerMove(&buf, &move);
                if (buf.error || netbufReadLeft(&buf) != 0 || move.epoch!=s_combat_epoch || move.life_id!=s_hit_life[slot]) break;
                if(s_remote_active[slot] && (int32_t)(move.tick-s_remote_moves[slot].tick)<=0)break;
                s_remote_moves[slot] = move;
                if (netIsHost()) netObserveHitMove(slot,&move);
                
                /* Mirror to legacy NetMsgPlayerState for components that read it */
                s_remote_players[slot].header.slot_id = (uint8_t)slot;
                s_remote_players[slot].sequence = move.tick;
                s_remote_players[slot].pos_x = move.pos.x;
                s_remote_players[slot].pos_y = move.pos.y;
                s_remote_players[slot].pos_z = move.pos.z;
                s_remote_players[slot].head_yaw = move.angles[0];
                s_remote_players[slot].head_pitch = move.angles[1];
                s_remote_players[slot].stance = (uint8_t)move.crouchpos;
                s_remote_players[slot].weapon_id = (uint8_t)move.weaponnum;
                s_remote_players[slot].is_firing = (move.ucmd & UCMD_FIRE) ? 1 : 0;
                s_remote_players[slot].hand_x = move.handpos.x;
                s_remote_players[slot].hand_y = move.handpos.y;
                s_remote_players[slot].hand_z = move.handpos.z;
                s_remote_players[slot].hand_pitch = move.handrot.x;
                s_remote_players[slot].hand_yaw = move.handrot.y;
                s_remote_players[slot].hand_roll = move.handrot.z;
                
                s_remote_active[slot] = true;
                
                /* If host, relay to other clients */
                if (netIsHost()) {
                    netBroadcastPacket(data, size, NET_CHAN_PLAYER_STATE, ENET_PACKET_FLAG_UNSEQUENCED, peer);
                }
            }
            break;
        }
        case NET_MSG_HIT_REPORT: {
            if(netIsHost() && slot_id<GEVR_MAX_PLAYERS && s_client_peers[slot_id]==peer
                && (int)(intptr_t)peer->data-1==slot_id)netReceiveHitReport(slot_id,&buf);
            break;
        }
        case NET_MSG_DAMAGE_EVENT: netReceiveDamageEvent(peer,slot_id,&buf);break;
        case NET_MSG_RESPAWN: netReceiveRespawn(peer,slot_id,&buf,data,size);break;
        case NET_MSG_PROJECTILE: {
            if (netSlotIsSpectator(slot_id)) break;
            extern void gevrNetSpawnProjectile(s32 slot, s32 kind, s32 hand, s32 item, const coord3d *pos,
                                               const coord3d *vel, const f32 *rot9, const coord3d *extra, s32 cooktimer);
            int slot = slot_id;
            uint8_t kind = netbufReadU8(&buf);
            uint8_t hand = netbufReadU8(&buf);
            uint8_t item = netbufReadU8(&buf);
            int32_t cooktimer = (int32_t)netbufReadU32(&buf);
            coord3d pos, vel, extra;
            f32 rot[9];
            netbufReadCoord(&buf, &pos);
            netbufReadCoord(&buf, &vel);
            for (int i = 0; i < 9; i++) rot[i] = netbufReadF32(&buf);
            netbufReadCoord(&buf, &extra);
            if (buf.error || netbufReadLeft(&buf) != 0 || !netAcceptPlayerEvent(peer, slot)) break;
            if (kind < 1 || kind > 5 || hand > 1 || item >= ITEM_IDS_MAX ||
                !netCoordFinite(&pos) || !netCoordFinite(&vel) || !netCoordFinite(&extra)) break;
            bool rotok = true;
            for (int i = 0; i < 9; i++) rotok = rotok && isfinite(rot[i]) && fabsf(rot[i]) < 100.0f;
            if (!rotok) break;
            if (netIsHost()) {
                netBroadcastPacket(data, size, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, peer);
            }
            if (netPlayersWereTicked()) {
                NET_LOG("projectile rx: slot %d kind %d item %d", slot, kind, item);
                gevrNetSpawnProjectile(slot, kind, hand, item, &pos, &vel, rot, &extra, cooktimer);
            }
            break;
        }
        case NET_MSG_EXPLOSION: {
            if (netSlotIsSpectator(slot_id)) break;
            extern void gevrNetExplosionReceive(s32 slot, s32 type, coord3d *pos, u8 room, s32 ground, s32 flag8);
            int slot = slot_id;
            uint8_t type = netbufReadU8(&buf);
            uint8_t room = netbufReadU8(&buf);
            uint8_t flags = netbufReadU8(&buf);
            coord3d pos;
            netbufReadCoord(&buf, &pos);
            if (buf.error || netbufReadLeft(&buf) != 0 || !netAcceptPlayerEvent(peer, slot) ||
                !netCoordFinite(&pos)) break;
            if (netIsHost()) {
                netBroadcastPacket(data, size, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, peer);
            }
            if (netPlayersWereTicked()) {
                NET_LOG("explosion rx: slot %d type %d at %.0f,%.0f,%.0f", slot, type, pos.x, pos.y, pos.z);
                gevrNetExplosionReceive(slot, type, &pos, room, (flags & 1) != 0, (flags & 2) != 0);
            }
            break;
        }
        case NET_MSG_OBJECT_STATE: {
            if (netSlotIsSpectator(slot_id)) break;
            extern void gevrNetDoorApply(PropRecord *prop, PropRecord *byprop, s32 state);
            int slot = slot_id;
            uint16_t index = netbufReadU16(&buf);
            uint8_t type = netbufReadU8(&buf);
            uint8_t action = netbufReadU8(&buf);
            int8_t value = netbufReadS8(&buf);
            if (buf.error || netbufReadLeft(&buf) != 0 || !netAcceptPlayerEvent(peer, slot) ||
                !g_CurrentSetup.propDefs) break;
            if (netIsHost()) {
                netBroadcastPacket(data, size, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, peer);
            }
            if (!netPlayersWereTicked()) break;
            if (action == NET_OBJECT_SPECIAL_TAKEN) {
                netTakeSpecialItem(slot, (uint8_t)value);
                break;
            }
            if (index >= 0x8000) break; /* pool slots are not a shared identity (netSendObjectEvent) */
            ObjectRecord *obj = netObjectByIndex(index, type);
            if (!obj || !obj->prop) break;
            NET_LOG("object rx: slot %d index 0x%04x action %d value %d", slot, index, action, value);
            if (action == NET_OBJECT_PICKUP) {
                if (obj->prop->type != PROP_TYPE_OBJ && obj->prop->type != PROP_TYPE_WEAPON) break;
                if (obj->state & PROPSTATE_RESPAWN) {
                    /* Gone here too, and back on the sender's timer. Already
                     * regenerating here (collected a moment earlier on this
                     * headset, or a late event): the timer restarts, so it
                     * cannot come back before the sender's does. */
                    if ((obj->prop->flags & PROPFLAG_ENABLED) && obj->prop->timetoregen <= 0)
                        propExecuteTickOperation(obj->prop, TICKOP_FREE);
                    else
                        obj->prop->timetoregen = 0x4B0;
                } else if (value == TICKOP_GIVETOPLAYER) {
                    /* kept on the collector's player there; only hidden here */
                    if (obj->prop->flags & PROPFLAG_ENABLED) propExecuteTickOperation(obj->prop, TICKOP_DISABLE);
                } else {
                    /* freed for good there (objFree in propPickupByPlayer) */
                    objFreePermanently(obj, TRUE);
                }
            } else if (action == NET_OBJECT_DOOR_LOCK) {
                /* co-op: the host's script locked or unlocked it */
                if (!netIsHost() && slot == s_host_slot && obj->type == PROPDEF_DOOR && obj->prop->type == PROP_TYPE_DOOR)
                    ((DoorRecord *)obj)->keyflags = (((DoorRecord *)obj)->keyflags & ~0xFFu) | (u8)value;
            } else if (action == NET_OBJECT_DOOR) {
                if (value == DOORSTATE_WAITING) value = DOORSTATE_OPENING; /* opened, held for its sibling door */
                if (obj->type == PROPDEF_DOOR && obj->prop->type == PROP_TYPE_DOOR &&
                    (value == DOORSTATE_OPENING || value == DOORSTATE_CLOSING)) {
                    struct player *by = g_playerPointers[slot];
                    gevrNetDoorApply(obj->prop, by ? by->prop : NULL, value);
                }
            }
            break;
        }
        case NET_MSG_FIRE_EVENT: {
            uint8_t weapon = netbufReadU8(&buf);
            (void)weapon;
            if (netIsHost() && !buf.error && slot_id >= 0 && slot_id < s_max_players &&
                s_client_peers[slot_id] == peer && ((int)(intptr_t)peer->data - 1) == slot_id &&
                netSlotOccupied(slot_id)) {
                netBroadcastPacket(data, size, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, peer);
            }
            break;
        }
        case NET_MSG_VOIP_FRAME: {
            if (s_state != NET_STATE_INGAME && s_state != NET_STATE_HOSTING_LOBBY &&
                s_state != NET_STATE_CLIENT_LOBBY) break;
            if (slot_id >= GEVR_MAX_PLAYERS || slot_id == s_local_slot ||
                !s_lobby_state.slots[slot_id].connected) break;
            if (netIsHost()) {
                if (slot_id == s_host_slot || s_client_peers[slot_id] != peer ||
                    ((int)(intptr_t)peer->data - 1) != slot_id) break;
            } else if (peer != s_server_peer) break;
            const uint32_t sequence = netbufReadU32(&buf);
            const uint16_t payload_size = netbufReadU16(&buf);
            if (buf.error || payload_size == 0 || payload_size > GEVR_VOIP_MAX_BYTES ||
                netbufReadLeft(&buf) != payload_size || size != 14u + payload_size) break;
            netVoiceReceive(slot_id, sequence, data + buf.rp, payload_size);
            if (netIsHost()) {
                for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                    if (s_client_peers[i] && s_client_peers[i] != peer && netVoiceSameGroup(i, slot_id))
                        enet_peer_send(s_client_peers[i], NET_CHAN_VOIP, enet_packet_create(data, size, ENET_PACKET_FLAG_UNSEQUENCED));
                }
            }
            break;
        }
        case NET_MSG_APP_VERSION: {
            uint8_t slot = slot_id;
            if (slot >= GEVR_MAX_PLAYERS) break;
            char *ver = netbufReadStr(&buf);
            if (!ver || buf.error) break;
            strncpy(s_slot_app_version[slot], ver, sizeof(s_slot_app_version[slot]) - 1);
            s_slot_app_version[slot][sizeof(s_slot_app_version[slot]) - 1] = '\0';
            NET_LOG("Received app version from slot %d: %s", slot, s_slot_app_version[slot]);
            if (netIsHost()) {
                /* Relay peer's version to all other clients */
                netBroadcastPacket(data, size, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, peer);
            }
            break;
        }
        default:
            break;
    }
}

/* ---- Ballots (mpmenu.c NEXT MAP / NEXT WEAPONS rows) ---- */

static int netBallotSize(int kind) {
    return kind == NET_BALLOT_STAGE ? netStageCount() : netWeaponSetCount();
}

/* slot < 0: everyone's */
static void netClearVotes(int slot) {
    for (int k = 0; k < NET_BALLOT_COUNT; k++)
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
            if (slot < 0 || i == slot) s_vote[k][i] = -1;
}

int netGetVote(int kind, int slot) {
    return kind >= 0 && kind < NET_BALLOT_COUNT && slot >= 0 && slot < GEVR_MAX_PLAYERS ? s_vote[kind][slot] : -1;
}

static void netBroadcastVotes(int kind) {
    if (!netIsHost() || s_state != NET_STATE_INGAME || kind < 0 || kind >= NET_BALLOT_COUNT) return;
    u8 raw[9 + GEVR_MAX_PLAYERS];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_VOTES);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteU8(&buf, (uint8_t)kind);
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        netbufWriteU8(&buf, s_vote[kind][i] < 0 ? 0xFF : (uint8_t)s_vote[kind][i]);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

static void netBroadcastAllVotes(void) {
    for (int k = 0; k < NET_BALLOT_COUNT; k++) netBroadcastVotes(k);
}

void netSetLocalVote(int kind, int idx) {
    if (s_state != NET_STATE_INGAME || s_local_slot < 0 || s_local_slot >= GEVR_MAX_PLAYERS ||
        kind < 0 || kind >= NET_BALLOT_COUNT) return;
    if (s_lobby_state.config.next_round != NET_NEXT_VOTE || s_lobby_state.config.mode == NET_MODE_COOP ||
        (kind == NET_BALLOT_WEAPONS && s_lobby_state.config.scenario == SCENARIO_MWTGG)) return;
    if (idx < -1 || idx >= netBallotSize(kind)) idx = -1;
    s_vote[kind][s_local_slot] = (int8_t)idx;
    if (netIsHost()) {
        netBroadcastVotes(kind);
        return;
    }
    u8 raw[16];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_VOTE);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteU8(&buf, (uint8_t)kind);
    netbufWriteU8(&buf, idx < 0 ? 0xFF : (uint8_t)idx);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

/* The most voted choice on a ballot, the lowest slot's on a tie; -1 without a vote */
static int netTallyBallot(int kind) {
    int count[32] = { 0 };
    int best = -1;
    int n = netBallotSize(kind);
    if (n > 32) n = 32;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        int v = s_vote[kind][i];
        if (v >= 0 && v < n && s_lobby_state.slots[i].connected &&
            (kind != NET_BALLOT_STAGE || netStageEligible(v))) count[v]++;
    }
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        int v = s_vote[kind][i];
        if (v >= 0 && v < n && s_lobby_state.slots[i].connected && count[v] > 0 && (best < 0 || count[v] > count[best])) best = v;
    }
    return best;
}

/* The host, as the next round is decided: the ballots go into the config
 * (a map the party fits; no weapon vote in the Golden Gun scenario, whose
 * set is fixed), then the votes are cleared everywhere. */
static int netRotationPick(int kind, int current, unsigned favorites, int mode) {
    int choices[32], count = 0, n = netBallotSize(kind);
    for (int pass = 0; pass < 2 && count == 0; pass++) {
        for (int i = 0; i < n; i++) {
            if (kind == NET_BALLOT_STAGE && !netStageEligible(i)) continue;
            if (pass == 0 && favorites && !(favorites & (1u << i))) continue;
            choices[count++] = i;
        }
    }
    if (!count) return current;
    if (mode == NET_NEXT_PLAYLIST) {
        for (int i = 0; i < count; i++) if (choices[i] > current) return choices[i];
        return choices[0];
    }
    if (kind == NET_BALLOT_STAGE && count > 1) {
        for (int i = 0; i < count; i++) if (choices[i] == current) {
            memmove(&choices[i], &choices[i+1], (count-i-1)*sizeof(choices[0])); count--; break;
        }
    }
    uint32_t random = (uint32_t)sysGetMicroseconds() ^ s_rng_seed ^ (uint32_t)(kind * 0x9e3779b9u);
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    return choices[random % (uint32_t)count];
}
/*
 * The rotation (shuffle, playlist) turns only after a finished match: the
 * round that follows the launcher's launch, a warmup join or the host's
 * START MATCH plays the stage the host chose. Before this the warmup join's
 * countdown resolved the rotation too, so a shuffle host who picked Bunker II
 * warmed up there and started the match on a random other map (tester,
 * 2026-09-30). Ballots still count whenever somebody voted.
 */
static bool s_rotate_next = false;   /* declared with netResolveVotes above */

static void netResolveVotes(void) {
    int mode = s_lobby_state.config.next_round;
    bool rotate = s_rotate_next && mode != NET_NEXT_VOTE;
    int stage = mode == NET_NEXT_VOTE ? netTallyBallot(NET_BALLOT_STAGE) : rotate ?
        netRotationPick(NET_BALLOT_STAGE, netStageIndexOf(s_round.config.stage), VrMpFavStages, mode) : -1;
    int set = mode == NET_NEXT_VOTE ? netTallyBallot(NET_BALLOT_WEAPONS) : rotate ?
        netRotationPick(NET_BALLOT_WEAPONS, s_round.config.weapon_set, VrMpFavSets, mode) : -1;
    NET_LOG("round settings: mode %d (%s), lobby stage %d, last round stage %d, ballot stage %d set %d",
            mode, s_rotate_next ? "after a match" : "first round, no rotation",
            s_lobby_state.config.stage, s_round.config.stage, stage, set);
    s_rotate_next = false;
    if (stage >= 0 && netStageEligible(stage)) s_lobby_state.config.stage = netStage(stage)->level_id;
    if (!netStageEligible(netStageIndexOf(s_lobby_state.config.stage))) s_lobby_state.config.stage = s_round.config.stage;
    if (s_lobby_state.config.scenario == SCENARIO_MWTGG) s_lobby_state.config.weapon_set = 13;
    else if (set >= 0) s_lobby_state.config.weapon_set = (uint8_t)set;
    NET_LOG("Next map: %s", netStageName(netStageIndexOf(s_lobby_state.config.stage)));
    NET_LOG("Next weapons: %s", netWeaponSetName(s_lobby_state.config.weapon_set));
    netClearVotes(-1);
    netBroadcastAllVotes();
}

/* ---- Co-op (#94) ---- */

/* The host: one player only (a joiner's roster) */
void netCoopSendTo(int slot, const u8 *data, u32 size) {
    if (!netIsHost() || slot < 0 || slot >= GEVR_MAX_PLAYERS || !s_client_peers[slot] || !data || !size) return;
    enet_peer_send(s_client_peers[slot], NET_CHAN_RELIABLE, enet_packet_create(data, size, ENET_PACKET_FLAG_RELIABLE));
}

/* net_coop.c's messages: the host's go to every headset, a client's to the host */
void netCoopBroadcast(const u8 *data, u32 size, int reliable) {
    if (!data || !size) return;
    if (reliable) netBroadcastPacket(data, size, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    else netBroadcastPacket(data, size, NET_CHAN_PLAYER_STATE, 0, NULL);
}

/* The mission's end */

static uint64_t s_coop_next_at_us;       /* host: the party's menus load at this time */
static uint8_t s_coop_next_stage;        /* host: the stage it loads (the menus) */
#define NET_COOP_END_DELAY_MS 4000

static void netCoopApplyEnd(int result) {
    extern void gevrCoopMissionEndLocal(s32 result);
    if (s_coop_ended) return;
    netCoopTallyApplyLocal();
    s_coop_ended = true;
    NET_LOG("co-op: mission %s", result == NET_COOP_RESULT_COMPLETE ? "complete" :
            result == NET_COOP_RESULT_ALL_DOWN ? "failed, every player down" :
            result == NET_COOP_RESULT_ABORTED ? "aborted" : "failed");
    gevrCoopMissionEndLocal(result);
}

/*
 * boss.c bossReturnTitleStage in a co-op mission: the mission's end (the AI's
 * EndLevel, the exit's fade). The host's ends it for everyone: each headset
 * saves the completion to its own save and shows it, and the host loads the
 * next mission after a pause (the same one after a failure). Another
 * headset's own end is only logged: its guards are the host's to run.
 */
void netCoopMissionEnded(int result) {
    if (!netCoopActive() || s_coop_ended || result < 0 || result > NET_COOP_RESULT_ABORTED) return;
    if (!netIsHost()) {
        NET_LOG("co-op: this headset reached the mission's end (%d); the host's decides", result);
        return;
    }
    /* everyone back to the menus: the debrief, then the host's next choice (front.c) */
    s_coop_next_stage = NET_COOP_FRONT_STAGE;
    s_coop_next_at_us = sysGetMicroseconds() + NET_COOP_END_DELAY_MS * 1000ull;
    netCoopTallyCollect();
    u8 raw[16 + GEVR_MAX_PLAYERS * (3 + 2 * NET_COOP_TALLY_REGS)];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_COOP_END);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteU8(&buf, (uint8_t)result);
    netbufWriteU8(&buf, s_coop_next_stage);
    netbufWriteU32(&buf, NET_COOP_END_DELAY_MS);
    /* the party's tally: who played, their kills and their hits */
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        netbufWriteU8(&buf, s_coop_tally_in[i] ? 1 : 0);
        netbufWriteU16(&buf, s_coop_tally_kills[i]);
        for (int r = 0; r < NET_COOP_TALLY_REGS; r++) netbufWriteU16(&buf, s_coop_tally_hits[i][r]);
    }
    if (buf.error) return;
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    netCoopApplyEnd(result);
}

/*
 * The host's front end (front.c init_menu0B_runstage): the briefing's Start,
 * 007's Start or the statistics page's Next to Cuba. Every headset loads the
 * mission as a round reset (netTakeRoundReset, boss.c).
 */
void netCoopHostStartMission(int stage, int difficulty) {
    if (!netIsHost() || !netCoopSession() || s_round_reset_loading || !netCoopStageValid((uint8_t)stage) ||
        stage == NET_COOP_FRONT_STAGE) return;
    s_coop_next_at_us = 0;
    s_lobby_state.config.stage = (uint8_t)stage;
    s_lobby_state.config.difficulty = (uint8_t)(difficulty >= 0 && difficulty < NET_DIFFICULTY_COUNT ? difficulty : 0);
    NET_LOG("co-op: the host starts %s, %s", stage == NET_COOP_CUBA_STAGE ? "Cuba" :
            netCoopMissionName(netCoopMissionIndexOf((uint8_t)stage)), netDifficultyName(s_lobby_state.config.difficulty));
    netLatchRoundSettings();
    netBeginRoundReset(true);
}

/* The host, each frame: the next mission once the pause is over */
static void netCoopTick(void) {
    if (!netIsHost() || s_state != NET_STATE_INGAME || s_round.config.mode != NET_MODE_COOP) return;
    if (s_coop_next_at_us && sysGetMicroseconds() >= s_coop_next_at_us && !s_round_reset_loading) {
        s_coop_next_at_us = 0;
        s_lobby_state.config.stage = s_coop_next_stage;
        NET_LOG("co-op: back to the party's menus");
        netLatchRoundSettings();
        netBeginRoundReset(true);
    }
}

static uint64_t s_last_local_activity_us = 0;
static uint32_t s_last_idle_warning_sec = 0;

void netTouchLocalActivity(void) {
    s_last_local_activity_us = sysGetMicroseconds();
    s_last_idle_warning_sec = 0;
}

/* Isolated from the local inactivity watchdog: this is the host's round
 * state machine, shared by manual starts and automatic warmup expiry. */
static void netHostRoundTick(uint64_t now) {
    if (!netIsHost() || s_state != NET_STATE_INGAME) return;
    if (s_next_round_at_us && (!netRoundRosterReady() || !netAllLoaded())) {
        /* A newly connected player joins the ready check immediately. */
        netCancelRound();
        s_start_requested = true;
        netBroadcastRoundNotice();
    }
    if (s_match_ended && s_results_deadline_us && now >= s_results_deadline_us) netHostContinue();
    if (s_phase == NET_PHASE_WARMUP && !s_round_reset_loading) netReadyProgress();
    if (s_phase == NET_PHASE_WARMUP && s_warmup_end_us && now >= s_warmup_end_us &&
        !s_next_round_at_us && netGetConnectedPlayerCount() >= 2) {
        s_start_requested = true;
        s_vote_requested = s_lobby_state.config.next_round == NET_NEXT_VOTE;
    }
    if (!s_results_deadline_us) netTryHostStartRequest();
    if (s_next_round_at_us && now >= s_next_round_at_us &&
        netGetConnectedPlayerCount() >= 2 && netRoundRosterReady() && netAllLoaded()) {
        /* Reload once at the countdown boundary to clear practice scores,
         * deaths, pickups and clocks before the actual match. */
        netLatchRoundSettings();
        netBeginRoundReset(true);
    }
    if (s_round_reset_loading) netReadyProgress();
}

/* Packets are expected in a live round. Loading, the migration grace and the
 * co-op menus are not: refreshing the stamps there starts the 30 s clock only
 * once pose packets should be arriving again. */
static int netSilenceClockRuns(void) {
    if (s_state != NET_STATE_INGAME) return 0;
    if (s_phase != NET_PHASE_WARMUP && s_phase != NET_PHASE_IN_PROGRESS) return 0;
    if (s_round_reset_loading) return 0;
    if (netCoopSession() && bossGetStageNum() == LEVELID_TITLE) return 0;
    return 1;
}

static void netSilenceTick(uint64_t now) {
    if (!netSilenceClockRuns()) {
        s_host_heard_us = now;
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++) s_slot_heard_us[i] = now;
        return;
    }
    if (netIsHost()) {
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
            ENetPeer *peer;
            if (i == s_local_slot || !s_lobby_state.slots[i].connected || !s_lobby_state.slots[i].loaded)
                continue;
            /* No peer yet: the migration grace drops the slot on its own clock. */
            if (!s_client_peers[i] || s_slot_grace_us[i]) continue;
            if (!s_slot_heard_us[i]) { s_slot_heard_us[i] = now; continue; }
            if (now - s_slot_heard_us[i] < NET_SILENCE_TIMEOUT_US) continue;
            peer = s_client_peers[i];
            NET_LOG("Slot %d sent nothing for 30 s: dropping", i);
            /* reset, so the silent peer cannot keep the slot */
            netHostDropSlot(i, peer);
        }
    } else if (s_server_peer) {
        if (!s_host_heard_us) s_host_heard_us = now;
        else if (now - s_host_heard_us >= NET_SILENCE_TIMEOUT_US) {
            NET_LOG("Host sent nothing for 30 s");
            s_host_heard_us = 0;
            netHostLost(s_server_peer);
        }
    }
}

static void netRoundTick(void) {
    if (s_state == NET_STATE_INGAME) {
        uint64_t now = sysGetMicroseconds();
        /* co-op (#94): the party's menus are no place to idle out (a teammate follows the host's, input-less) */
        if (s_last_local_activity_us == 0 || (netCoopSession() && bossGetStageNum() == LEVELID_TITLE))
            s_last_local_activity_us = now;
        uint64_t idle_us = now - s_last_local_activity_us;
        if (idle_us >= 270ULL * 1000000ULL && idle_us < 300ULL * 1000000ULL) {
            uint32_t left = (uint32_t)((300ULL * 1000000ULL - idle_us) / 1000000ULL);
            if (left != s_last_idle_warning_sec) {
                s_last_idle_warning_sec = left;
                char msg[48];
                extern void hudmsgTopShow(char *mess);
                snprintf(msg, sizeof(msg), "IDLE WARNING: KICK IN %u S", left);
                hudmsgTopShow(msg);
            }
        } else if (idle_us >= 300ULL * 1000000ULL) {
            NET_LOG("Local player idle for 5 minutes: kicking to launcher");
            s_last_local_activity_us = 0;
            s_last_idle_warning_sec = 0;
            extern void gevrLobbySessionStopped(void);
            extern void gevrRestartToLauncher(void);
            gevrLobbySessionStopped();
            gevrRestartToLauncher();
            return;
        }

        netSilenceTick(now);
        if (s_state != NET_STATE_INGAME) return;

        netCoopTick();
        netHostRoundTick(now);
    }
}

/* ---- Host migration ---- */

void netSetGameName(const char *name) {
    snprintf(s_game_name, sizeof(s_game_name), "%s", name ? name : "");
}

const char *netGetGameName(void) {
    return s_game_name;
}

const char *netGetLobbyCode(void) {
    return s_lobby_code;
}

const char *netGetLobbyToken(void) {
    return s_lobby_token;
}

/* What a client keeps to carry the match on without the host: the internet
 * lobby and its owner token, the LAN beacon's name, the party's size. To each
 * client after WELCOME, to all when the lobby comes online. */
static void netSendLobbyHandoffTo(ENetPeer *peer) {
    if (!netIsHost()) return;
    u8 raw[192];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_LOBBY_HANDOFF);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteStr(&buf, s_lobby_code);
    netbufWriteStr(&buf, s_lobby_token);
    netbufWriteStr(&buf, s_game_name);
    netbufWriteU8(&buf, (uint8_t)s_max_players);
    if (buf.error) return;
    if (peer) {
        ENetPacket *packet = enet_packet_create(buf.data, buf.wp, ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, NET_CHAN_RELIABLE, packet);
    } else {
        netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    }
}

void netSetLobbyHandoff(const char *code, const char *token) {
    snprintf(s_lobby_code, sizeof(s_lobby_code), "%s", code ? code : "");
    snprintf(s_lobby_token, sizeof(s_lobby_token), "%s", token ? token : "");
    netSendLobbyHandoffTo(NULL);
}

int netGetLivePlayerCount(void) {
    int count = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        if (s_lobby_state.slots[i].connected && (i == s_local_slot || s_client_peers[i] || !netIsHost())) count++;
    return count;
}

/* The new host never came, or would not have this player: back to the launcher. */
static void netMigrationGiveUpNow(void) {
    NET_LOG("Host migration failed; leaving the match");
    s_rejoining = false;
    s_takeover_pending = false;
    if (s_host) {
        if (s_server_peer) enet_peer_disconnect_now(s_server_peer, 0);
        enet_host_destroy(s_host);
        s_host = NULL;
    }
    s_server_peer = NULL;
    s_state = NET_STATE_OFFLINE;
    netVoiceReset();
}

void netMigrationGiveUp(void) {
    if (s_state == NET_STATE_MIGRATING) netMigrationGiveUpNow();
}

/*
 * The host is gone mid-match. The lowest remaining slot takes over; the
 * others look for it (vr_launcher.cpp gevrLobbyGameTick rejoins through the
 * same internet lobby or the LAN beacon under the same name). Alone, the
 * survivor hosts the warmup.
 */
static void netHostLost(ENetPeer *peer) {
    netClearHostHits();
    int old = s_host_slot;
    int elected = -1;
    enet_address_get_ip(&peer->address, s_old_host_ip, sizeof(s_old_host_ip));
    s_server_peer = NULL;
    if (old >= 0 && old < GEVR_MAX_PLAYERS) {
        s_lobby_state.slots[old].connected = 0;
        s_lobby_state.slots[old].ready = 0;
        s_lobby_state.slots[old].loaded = 0;
        s_remote_active[old] = false;
        netVoiceForgetSlot((uint8_t)old);
        netForgetPlayerScore(old);
        s_slot_app_version[old][0] = '\0';
        netClearVotes(old);
    }
    for (int i = 0; i < s_max_players; i++) {
        if (i != old && (i == s_local_slot || (s_lobby_state.slots[i].connected && !s_lobby_state.slots[i].is_bot))) {
            elected = i;
            break;
        }
    }
    if (elected < 0) elected = s_local_slot;
    netCoopHostLost(old, elected == s_local_slot);
    s_host_slot = elected;
    s_state = NET_STATE_MIGRATING;
    s_last_latency_us = 0;
    for (int i=0;i<GEVR_MAX_PLAYERS;i++) s_lobby_state.slots[i].ping_ms = NET_PING_UNKNOWN;
    s_migrate_deadline_us = sysGetMicroseconds() + 30ull * 1000000;
    s_countdown_end_us = 0;
    s_next_round_at_us = 0;
    s_start_after_load = false;
    s_round_reset_loading = false;
    s_waiting_for_match_snapshot = false;
    if (elected == s_local_slot) {
        s_takeover_pending = true;
        NET_LOG("Host left: this headset (slot %d) takes the match over", s_local_slot);
    } else {
        s_rejoining = true;
        NET_LOG("Host left: slot %d takes over; rejoining as slot %d", elected, s_local_slot);
    }
}

bool netTakeHostTakeover(void) {
    bool pending = s_takeover_pending;
    s_takeover_pending = false;
    return pending;
}

/* The elected host serves the match from its own slot; the others' slots
 * wait for them (20 s), their copies out of the level meanwhile. */
bool netHostTakeOver(uint16_t port) {
    if (s_state != NET_STATE_MIGRATING || s_host_slot != s_local_slot) return false;
    if (s_host) {
        enet_host_destroy(s_host);
        s_host = NULL;
    }
    s_server_peer = NULL;
    ENetAddress address;
    enet_address_set_ip(&address, "0.0.0.0");
    address.port = port ? port : GEVR_DEFAULT_PORT;
    s_host = enet_host_create(&address, NET_HOST_PEERS, NET_CHAN_MAX, 0, 0, 0);
    if (!s_host) {
        NET_ERR("Failed to create the ENet host for the takeover on port %d", address.port);
        netMigrationGiveUpNow();
        return false;
    }
    enet_host_set_virtual_transport(s_host, s_virtual_send, s_virtual_receive, s_virtual_context);
    s_state = NET_STATE_INGAME;
    netResetCombatEpoch();
    s_takeover_pending = false;
    s_max_players = s_lobby_max_players;
    uint64_t now = sysGetMicroseconds();
    int waiting = 0, bots = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        s_client_peers[i] = NULL;
        s_remote_active[i] = false;
        if (i == s_local_slot) {
            s_lobby_state.slots[i].connected = 1;
            s_lobby_state.slots[i].loaded = 1;
        } else if (netSlotIsBot(i)) {
            /* the bots are this headset's now (gevr_bot.c), from where their copies stand */
            s_lobby_state.slots[i].loaded = 1;
            s_slot_grace_us[i] = 0;
            bots++;
        } else if (s_lobby_state.slots[i].connected) {
            s_lobby_state.slots[i].loaded = 0;
            s_slot_grace_us[i] = now + 20ull * 1000000;
            waiting++;
        }
    }
    /* alone with bots, the match goes on (user, 2026-10-07) */
    if (!waiting && !bots && s_round.config.mode != NET_MODE_COOP) netBeginRoundReset(false);
    else if (s_match_ended || g_gameOverFlag) {
        s_match_ended = true;
        s_results_deadline_us = now + 30000000ull;
    }
    NET_LOG("Hosting the match from slot %d on port %d; %d player(s) have 20 s to come back, %d bot(s) adopted", s_local_slot, address.port, waiting, bots);
    netCoopBecameHost();   /* co-op: the guards' AI resumes here */
    return true;
}

bool netMigrationWantsRejoin(const char **old_host_ip) {
    if (old_host_ip) *old_host_ip = s_old_host_ip;
    if (s_state != NET_STATE_MIGRATING || s_host_slot == s_local_slot) return false;
    if (sysGetMicroseconds() > s_migrate_deadline_us) {
        netMigrationGiveUpNow();
        return false;
    }
    return true;
}

bool netMigrationConnecting(void) {
    return s_state == NET_STATE_MIGRATING && s_host != NULL && s_server_peer != NULL;
}

/*
 * The host frees a client's slot, tells the others and settles the round:
 * after ENet's DISCONNECT, or when the same name arrives on a new connection
 * (stale: the earlier one, from a headset that crashed or quit without a
 * goodbye; reset, it raises no event of its own).
 */
static void netHostDropSlot(int slot, ENetPeer *stale) {
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS) return;
    if (stale) {
        char ip[64] = "";
        enet_address_get_ip(&stale->address, ip, sizeof(ip));
        netIceForgetPeer(ip);
        stale->data = NULL;
        enet_peer_reset(stale);
    }
    s_client_peers[slot] = NULL;
    s_client_can_be_kicked[slot] = false;
    s_client_caps[slot] = 0;
    s_client_caps_known[slot] = false;
    s_remote_active[slot] = false;
    netVoiceForgetSlot((uint8_t)slot);
    netForgetPlayerScore(slot);
    netCoopSlotLeft(slot);
    memset(&s_lobby_state.slots[slot], 0, sizeof(NetLobbySlot));
    s_lobby_state.slots[slot].team = NET_TEAM_NONE;
    s_lobby_state.slots[slot].ping_ms = NET_PING_UNKNOWN;
    s_slot_app_version[slot][0] = '\0';
    s_slot_grace_us[slot] = 0;
    s_slot_heard_us[slot] = 0;
    netClearVotes(slot);
    netUpdateBots(false);

    netBroadcastLobbyState();
    netBroadcastAllVotes();
    if (s_round.config.mode == NET_MODE_COOP) {
        /* co-op: the mission goes on for whoever is left, alone too (#94) */
    } else if (s_state == NET_STATE_INGAME && netGetConnectedPlayerCount() == 1 &&
        s_phase == NET_PHASE_IN_PROGRESS) {
        s_start_after_load = false;
        s_next_round_at_us = 0;
        netBeginRoundReset(false);
    } else if (s_state == NET_STATE_INGAME && netGetConnectedPlayerCount() < 2) {
        netCancelRound(); /* the countdown was for the player who left */
    }
}

int netHostCanKickPlayer(int slot) {
    if (!netIsHost() || (s_state != NET_STATE_INGAME && s_state != NET_STATE_HOSTING_LOBBY) ||
        slot == s_local_slot || slot == s_host_slot || !netLobbySlotConnected(slot)) return 0;
    if (netSlotIsBot(slot)) return 1;
    if (!s_client_can_be_kicked[slot]) return 0;
    ENetPeer *peer = s_client_peers[slot];
    return peer && (int)(intptr_t)peer->data - 1 == slot;
}

int netHostKickPlayer(int slot) {
    if (!netHostCanKickPlayer(slot)) return 0;
    if (netSlotIsBot(slot)) {
        /* the bot stays gone: Fill becomes Fixed with one fewer, Fixed counts one fewer */
        NET_LOG("bots: %s removed by the host", s_lobby_state.slots[slot].name);
        netReleaseBotSlot(slot);
        int left = 0;
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++) if (netSlotIsBot(i)) left++;
        s_lobby_state.config.bot_mode = left ? NET_BOT_FIXED : NET_BOT_OFF;
        if (left) s_lobby_state.config.bot_count = (uint8_t)left;
        VrMpBotMode = s_lobby_state.config.bot_mode;
        VrMpBotCount = s_lobby_state.config.bot_count;
        vrSettingsSave();
        netBroadcastLobbyState();
        return 1;
    }
    ENetPeer *peer = s_client_peers[slot];
    /* Graceful ENet disconnect delivers the reason. Free the roster now;
     * detach the old peer so its eventual event cannot drop a reused slot. */
    enet_peer_disconnect(peer, NET_DISCONNECT_KICKED);
    peer->data = NULL;
    netHostDropSlot(slot, NULL);
    return 1;
}

static bool netClientKickDisconnected(ENetPeer *peer, uint32_t reason) {
    if (!peer || netIsHost() || peer != s_server_peer || reason != NET_DISCONNECT_KICKED) return false;
    /* An intentional removal is not a host migration. Detach before the
     * launcher tears down ENet, so no event can retain a freed peer. */
    extern void gevrLobbySessionStopped(void);
    extern void gevrRestartToLauncher(void);
    NET_LOG("Host removed this player from the session");
    peer->data = NULL;
    s_server_peer = NULL;
    s_state = NET_STATE_OFFLINE;
    netVoiceReset();
    gevrLobbySessionStopped();
    gevrRestartToLauncher();
    return true;
}

void netPoll(void) {
    static uint64_t last_ammo_us;
    uint64_t ammo_now=sysGetMicroseconds();
    if (netIsHost() && s_state == NET_STATE_INGAME && !s_round_reset_loading && netPlayersWereTicked() &&
        ammo_now-last_ammo_us >= 50000) {
        last_ammo_us=ammo_now;netSendAmmoState(NULL);
    }
    if (netIsHost() && s_state == NET_STATE_INGAME && !s_round_reset_loading && netPlayersWereTicked())
        netCoopHostTick();
    else if (!netIsHost() && s_state == NET_STATE_INGAME && netPlayersWereTicked())
        netCoopClientTick();
    if (s_state == NET_STATE_INGAME && !s_round_reset_loading && netPlayersWereTicked())
        netCoopReviveTick();
    if (netIsHost() && s_state == NET_STATE_INGAME && s_phase == NET_PHASE_IN_PROGRESS && netPlayersWereTicked()) {
        bool changed = false;
        for (int i=0;i<GEVR_MAX_PLAYERS;i++) {
            int eliminated = s_round.config.scenario == SCENARIO_YOLT && netPlayerInRound(i) && (s_lobby_state.slots[i].eliminated || g_playerPlayerData[i].order_out_in_yolt != 0);
            if (s_lobby_state.slots[i].eliminated != eliminated) { s_lobby_state.slots[i].eliminated = eliminated; changed = true; }
        }
        if (changed) { for (int i=0;i<GEVR_MAX_PLAYERS;i++) netVoiceForgetSlot((uint8_t)i); netBroadcastLobbyState(); }
    }
    if (!s_host) {
        netVoiceTick();
        return;
    }
    
    ENetEvent event;
    while (enet_host_service(s_host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                char ip_buf[64] = "unknown";
                enet_address_get_ip(&event.peer->address, ip_buf, sizeof(ip_buf));
                NET_LOG("A peer connected from %s:%u", ip_buf, event.peer->address.port);
                /* A vanished peer (a headset that quit or lost its network) is
                 * dropped in about six seconds, not ENet's half minute: the
                 * two-headset test saw a quitter stay in the match. */
                enet_peer_timeout(event.peer, 32, 3000, 6000);
                if (!netIsHost()) {
                    /* Connected as client -> send hello */
                    u8 raw[64];
                    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
                    netbufStartWrite(&buf);
                    netbufWriteU32(&buf, GEVR_NET_MAGIC);
                    netbufWriteU16(&buf, GEVR_NET_VERSION);
                    netbufWriteU8(&buf, NET_MSG_HELLO);
                    netbufWriteU8(&buf, 0xFF);
                    netbufWriteStr(&buf, VrPlayerName);
                    if (s_state == NET_STATE_MIGRATING && s_local_slot >= 0 && s_local_slot < GEVR_MAX_PLAYERS) {
                        /* to the new host: the same character, and the slot it kept for this player */
                        netbufWriteU8(&buf, s_lobby_state.slots[s_local_slot].chr_id);
                        netbufWriteU8(&buf, (uint8_t)s_local_slot);
                    } else {
                        netbufWriteU8(&buf, s_preferred_chr_id);
                    }

                    ENetPacket *packet = enet_packet_create(buf.data, buf.wp, ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, NET_CHAN_RELIABLE, packet);
                    NET_LOG("Sent hello packet to host.");
                }
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                netHandlePacket(event.peer, event.packet->data, event.packet->dataLength);
                enet_packet_destroy(event.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                NET_LOG("Peer disconnected.");
                char departed_ip[64] = "";
                enet_address_get_ip(&event.peer->address, departed_ip, sizeof(departed_ip));
                netIceForgetPeer(departed_ip);
                if (netIsHost()) {
                    int slot = (int)(intptr_t)event.peer->data - 1;
                    if (slot >= 0 && slot < GEVR_MAX_PLAYERS && s_client_peers[slot] == event.peer)
                        netHostDropSlot(slot, NULL);
                } else if (netClientKickDisconnected(event.peer, event.data)) {
                    return;
                } else if (s_state == NET_STATE_INGAME && event.peer == s_server_peer) {
                    netHostLost(event.peer);
                } else if (s_state == NET_STATE_MIGRATING) {
                    /* an attempt at the new host failed; the glue tries again until the deadline */
                    NET_LOG("Rejoin attempt dropped");
                    s_server_peer = NULL;
                } else {
                    /* Server disconnected */
                    s_server_peer = NULL;
                    s_state = NET_STATE_OFFLINE;
                    netVoiceReset();
                }
                event.peer->data = NULL;
                break;
            }
            case ENET_EVENT_TYPE_NONE:
            default:
                break;
        }
    }
    if (netIsHost() && s_state == NET_STATE_INGAME) {
        /* after a host change: a player who has not come back in time is gone */
        uint64_t now = sysGetMicroseconds();
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
            if (s_slot_grace_us[i] && !s_client_peers[i] && now > s_slot_grace_us[i]) {
                NET_LOG("Slot %d did not come back after the host change", i);
                netHostDropSlot(i, NULL);
            }
        }
    }
    uint64_t lobby_now = sysGetMicroseconds();
    if (netIsHost() && lobby_now - s_last_latency_us >= 1000000) {
        s_last_latency_us = lobby_now;
        for (int i=0;i<GEVR_MAX_PLAYERS;i++) {
            ENetPeer *peer = s_client_peers[i];
            s_lobby_state.slots[i].ping_ms = i == s_host_slot ? 0 :
                (peer && peer->state == ENET_PEER_STATE_CONNECTED ?
                    netLatencyValue(peer->roundTripTime, peer->lastReceiveTime, enet_time_get()) : NET_PING_UNKNOWN);
        }
        netBroadcastLobbyState();
        netBroadcastRoundNotice();
    }
    netClockTick();
    netDrainHostHits();
    netHostRemoveOldForNoRadar();
    netRoundTick();
    if (s_waiting_for_match_snapshot && s_stage_ready_sent &&
        s_state == NET_STATE_INGAME && !netIsHost() &&
        sysGetMicroseconds() - s_last_stage_ready_us > 3000000) {
        NET_LOG("Still waiting for in-progress snapshot; repeating stage-ready request");
        netStageLoaded();
    }
    netVoiceTick();
}
