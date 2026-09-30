#ifdef ntohl
#undef ntohl
#endif
#ifdef ntohs
#undef ntohs
#endif

#include "net/netenet.h"
#include "net_core.h"
#include "net_ice.h"
#include "net/netbuf.h"
#include "net_voice.h"
#include "bondconstants.h"
#include "boss.h"
#include "game/player.h"
#include "game/front.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "game/chrai.h"
#include "game/loadobjectmodel.h"
#include "game/propobj.h"
#include "system.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#ifdef ANDROID
#include <android/log.h>
#define NET_LOG(...) __android_log_print(ANDROID_LOG_INFO, "GEVR-Net", __VA_ARGS__)
#define NET_ERR(...) __android_log_print(ANDROID_LOG_ERROR, "GEVR-Net", __VA_ARGS__)
#else
#define NET_LOG(...) printf("[GEVR-Net] " __VA_ARGS__); printf("\n")
#define NET_ERR(...) fprintf(stderr, "[GEVR-Net-Err] " __VA_ARGS__); fprintf(stderr, "\n")
#endif

static bool s_initialized = false;
static NetState s_state = NET_STATE_OFFLINE;
static NetPhase s_phase = NET_PHASE_WAITING;
static int s_max_players = GEVR_MAX_PLAYERS;
static bool s_round_reset_pending = false;
static bool s_round_reset_loading = false;
static bool s_pause_after_results = false;
static uint64_t s_next_round_at_us = 0;
static ENetHost *s_host = NULL;
static ENetPeer *s_server_peer = NULL; /* Used when we are a client */
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
static void netBroadcastStageVotes(void);
static void netSendLobbyHandoffTo(ENetPeer *peer);
static void netMigrationGiveUpNow(void);

/* Next map (mpmenu.c NEXT MAP row): each slot's vote, an index into s_mp_stages or -1 */
static int8_t s_stage_vote[GEVR_MAX_PLAYERS];
static uint8_t s_preferred_chr_id = 0;

void netSetPreferredCharacter(uint8_t chr_id) {
    if (chr_id < 12) s_preferred_chr_id = chr_id;
}

/* Remote player state cache */
static struct netplayermove s_remote_moves[GEVR_MAX_PLAYERS];
static NetMsgPlayerState s_remote_players[GEVR_MAX_PLAYERS];
static bool s_remote_active[GEVR_MAX_PLAYERS];
static bool s_waiting_for_match_snapshot = false;
static bool s_stage_ready_sent = false;
static uint64_t s_last_stage_ready_us = 0;

/* Current Lobby State */
static NetMsgLobbyState s_lobby_state;
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
static ENetPeer *s_client_peers[GEVR_MAX_PLAYERS];
extern s32 D_80048394;
extern s32 D_800483A8;
static void netBroadcastBuf(struct netbuf *buf, uint8_t channel, uint32_t flags, ENetPeer *except);
static void netHostDropSlot(int slot, ENetPeer *stale);

static void netForgetPlayerScore(int slot) {
    if (s_state != NET_STATE_INGAME || slot < 1 || slot >= GEVR_MAX_PLAYERS) return;
    for (int shooter = 0; shooter < GEVR_MAX_PLAYERS; shooter++) {
        if (shooter != slot) {
            g_playerPlayerData[shooter].kill_count += g_playerPlayerData[shooter].kill_counts[slot];
            g_playerPlayerData[shooter].kill_counts[slot] = 0;
        }
    }
    memset(g_playerPlayerData[slot].kill_counts, 0, sizeof(g_playerPlayerData[slot].kill_counts));
    g_playerPlayerData[slot].kill_count = 0;
}

static void netResetLobbyState(void) {
    memset(&s_lobby_state, 0, sizeof(s_lobby_state));
    s_lobby_state.header.magic = GEVR_NET_MAGIC;
    s_lobby_state.header.version = GEVR_NET_VERSION;
    s_lobby_state.header.msg_type = NET_MSG_LOBBY_STATE;
    s_lobby_state.stage_num = (uint8_t)LEVELID_FACILITY;
    s_lobby_state.scenario = 0;     /* Normal Deathmatch */
    s_lobby_state.weapon_set = 0;   /* the host's choice, set before hosting (vr_launcher.cpp); 0 is Slappers only */
    s_lobby_code[0] = '\0';
    s_lobby_token[0] = '\0';
    s_game_name[0] = '\0';
    s_lobby_max_players = GEVR_MAX_PLAYERS;
    
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        s_remote_active[i] = false;
        s_client_peers[i] = NULL;
        s_slot_app_version[i][0] = '\0';
        s_stage_vote[i] = -1;
        s_slot_grace_us[i] = 0;
        memset(&s_remote_moves[i], 0, sizeof(s_remote_moves[i]));
        memset(&s_remote_players[i], 0, sizeof(s_remote_players[i]));
    }
}

bool netInit(void) {
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
    
    s_host = enet_host_create(&address, GEVR_MAX_PLAYERS, NET_CHAN_MAX, 0, 0, 0);
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
    s_pause_after_results = false;
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
    s_pause_after_results = false;
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

uint8_t netGetLobbyStage(void) {
    return s_lobby_state.stage_num;
}

uint8_t netGetLobbyWeaponSet(void) {
    return s_lobby_state.weapon_set;
}

int netGetConnectedPlayerCount(void) {
    int count = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        if (s_lobby_state.slots[i].connected) count++;
    }
    return count;
}

int netGetMaxPlayers(void) {
    return s_max_players;
}

void netSetMaxPlayers(int max_players) {
    if (max_players >= 2 && max_players <= GEVR_MAX_PLAYERS)
        s_max_players = max_players;
}

NetPhase netGetPhase(void) {
    return s_phase;
}

bool netSlotOccupied(int slot) {
    return slot >= 0 && slot < s_max_players &&
           (slot == s_local_slot || (s_lobby_state.slots[slot].connected &&
            (s_state != NET_STATE_INGAME || s_lobby_state.slots[slot].ready)));
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
    s_phase = phase;
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

static void netBroadcastLobbyState(void) {
    u8 raw[256];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_LOBBY_STATE);
    netbufWriteU8(&buf, 0);
    netbufWriteU8(&buf, s_lobby_state.stage_num);
    netbufWriteU8(&buf, s_lobby_state.scenario);
    netbufWriteU8(&buf, s_lobby_state.weapon_set);
    netbufWriteU8(&buf, s_lobby_state.countdown_secs);
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        netbufWriteU8(&buf, s_lobby_state.slots[i].connected);
        netbufWriteU8(&buf, s_lobby_state.slots[i].ready);
        netbufWriteU8(&buf, s_lobby_state.slots[i].chr_id);
        netbufWriteStr(&buf, s_lobby_state.slots[i].name);
    }
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

static void netSendMatchSnapshot(ENetPeer *peer) {
    u8 raw[512];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_MATCH_SNAPSHOT);
    netbufWriteU8(&buf, 0);
    netbufWriteU32(&buf, (u32)D_80048394);
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        netbufWriteU32(&buf, (u32)g_playerPlayerData[i].kill_count);
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
    }
    if (buf.error) return;
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

static void netSendObjectEvent(ObjectRecord *obj, uint8_t action, int8_t value) {
    if (s_state != NET_STATE_INGAME || s_local_slot < 0 || !obj) return;
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
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
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
    if (!netIsActive() || get_cur_playernum() != s_local_slot) return;
    netSendObjectEvent(obj, NET_OBJECT_PICKUP, (int8_t)tickop);
}

/* propobj.c propdoorInteract: the local player opened or closed this door
 * (SVC_PROP_DOOR sends the door's new mode). */
void netSendDoorState(ObjectRecord *door, s32 state) {
    if (!netIsActive() || get_cur_playernum() != s_local_slot) return;
    netSendObjectEvent(door, NET_OBJECT_DOOR, (int8_t)state);
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

static bool netAllLoaded(void) {
    for (int i = 0; i < s_max_players; i++)
        if (s_lobby_state.slots[i].connected && !s_lobby_state.slots[i].ready) return false;
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

uint64_t netGetCountdownEndUs(void) {
    return s_countdown_end_us;
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

/* One fade from black per online stage load (net_player_sync.c). */
bool netTakeStageFadeIn(void) {
    bool pending = s_stage_fade_in;
    s_stage_fade_in = false;
    return pending;
}

static void netResolveStageVote(void);

static void netBeginRoundReset(void) {
    u8 raw[16];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    s_countdown_end_us = 0;
    /* the next map first, so the lobby state and the reset both name it */
    netResolveStageVote();
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        if (s_lobby_state.slots[i].connected) s_lobby_state.slots[i].ready = 0;
    netBroadcastLobbyState();
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_ROUND_RESET);
    netbufWriteU8(&buf, 0);
    netbufWriteU8(&buf, s_lobby_state.stage_num);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    netBroadcastRoundPhase(NET_PHASE_WARMUP);
    s_round_reset_pending = true;
    s_round_reset_loading = true;
}

void netHostRoundEnded(void) {
    if (!netIsHost() || s_state != NET_STATE_INGAME || s_phase != NET_PHASE_IN_PROGRESS) return;
    u8 raw[8];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_MATCH_END);
    netbufWriteU8(&buf, 0);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

void netHostReturnToWarmup(void) {
    if (!netIsHost() || s_state != NET_STATE_INGAME || s_phase != NET_PHASE_IN_PROGRESS) return;
    s_pause_after_results = true;
    netBeginRoundReset();
}

void netStageLoaded(void) {
    netPlayersTickedReset(); /* events queued through the load are for the old stage */
    if (s_state != NET_STATE_INGAME || s_local_slot < 0) return;
    s_stage_fade_in = true;
    s_lobby_state.slots[s_local_slot].ready = 1;
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
        netBroadcastLobbyState();
        if (netGetConnectedPlayerCount() == 1) s_round_reset_loading = false;
        if (netGetConnectedPlayerCount() >= 2 && netAllLoaded()) {
            if (s_round_reset_loading) {
                s_round_reset_loading = false;
                if (s_pause_after_results) {
                    s_pause_after_results = false;
                    netScheduleRound(15);
                } else netBroadcastRoundPhase(NET_PHASE_IN_PROGRESS);
            } else if (s_phase == NET_PHASE_WARMUP && !s_next_round_at_us) netScheduleRound(10); /* a joiner: everyone sees the countdown */
        }
    }
}

void netLobbySetReady(bool ready) {
    if (s_local_slot < 0 || s_local_slot >= GEVR_MAX_PLAYERS) return;
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
        /* Broadcast updated lobby state */
        u8 lraw[256];
        struct netbuf lbuf = { .data = lraw, .size = sizeof(lraw) };
        netbufStartWrite(&lbuf);
        netbufWriteU32(&lbuf, GEVR_NET_MAGIC);
        netbufWriteU16(&lbuf, GEVR_NET_VERSION);
        netbufWriteU8(&lbuf, NET_MSG_LOBBY_STATE);
        netbufWriteU8(&lbuf, 0);
        netbufWriteU8(&lbuf, s_lobby_state.stage_num);
        netbufWriteU8(&lbuf, s_lobby_state.scenario);
        netbufWriteU8(&lbuf, s_lobby_state.weapon_set);
        netbufWriteU8(&lbuf, s_lobby_state.countdown_secs);
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
            netbufWriteU8(&lbuf, s_lobby_state.slots[i].connected);
            netbufWriteU8(&lbuf, s_lobby_state.slots[i].ready);
            netbufWriteU8(&lbuf, s_lobby_state.slots[i].chr_id);
            netbufWriteStr(&lbuf, s_lobby_state.slots[i].name);
        }
        netBroadcastBuf(&lbuf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    } else if (s_server_peer) {
        netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    }
}

void netLobbySetCharacter(uint8_t chr_id) {
    if (s_local_slot < 0 || s_local_slot >= GEVR_MAX_PLAYERS) return;
    s_lobby_state.slots[s_local_slot].chr_id = chr_id;
    player_char[s_local_slot] = chr_id;
    
    if (netIsHost()) {
        u8 lraw[256];
        struct netbuf lbuf = { .data = lraw, .size = sizeof(lraw) };
        netbufStartWrite(&lbuf);
        netbufWriteU32(&lbuf, GEVR_NET_MAGIC);
        netbufWriteU16(&lbuf, GEVR_NET_VERSION);
        netbufWriteU8(&lbuf, NET_MSG_LOBBY_STATE);
        netbufWriteU8(&lbuf, 0);
        netbufWriteU8(&lbuf, s_lobby_state.stage_num);
        netbufWriteU8(&lbuf, s_lobby_state.scenario);
        netbufWriteU8(&lbuf, s_lobby_state.weapon_set);
        netbufWriteU8(&lbuf, s_lobby_state.countdown_secs);
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
            netbufWriteU8(&lbuf, s_lobby_state.slots[i].connected);
            netbufWriteU8(&lbuf, s_lobby_state.slots[i].ready);
            netbufWriteU8(&lbuf, s_lobby_state.slots[i].chr_id);
            netbufWriteStr(&lbuf, s_lobby_state.slots[i].name);
        }
        netBroadcastBuf(&lbuf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
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

void netLobbySetMatchConfig(uint8_t stage_num, uint8_t scenario, uint8_t weapon_set) {
    if (!netIsHost()) return;
    s_lobby_state.stage_num = stage_num;
    s_lobby_state.scenario = scenario;
    s_lobby_state.weapon_set = weapon_set;
    
    u8 lraw[256];
    struct netbuf lbuf = { .data = lraw, .size = sizeof(lraw) };
    netbufStartWrite(&lbuf);
    netbufWriteU32(&lbuf, GEVR_NET_MAGIC);
    netbufWriteU16(&lbuf, GEVR_NET_VERSION);
    netbufWriteU8(&lbuf, NET_MSG_LOBBY_STATE);
    netbufWriteU8(&lbuf, 0);
    netbufWriteU8(&lbuf, s_lobby_state.stage_num);
    netbufWriteU8(&lbuf, s_lobby_state.scenario);
    netbufWriteU8(&lbuf, s_lobby_state.weapon_set);
    netbufWriteU8(&lbuf, s_lobby_state.countdown_secs);
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        netbufWriteU8(&lbuf, s_lobby_state.slots[i].connected);
        netbufWriteU8(&lbuf, s_lobby_state.slots[i].ready);
        netbufWriteU8(&lbuf, s_lobby_state.slots[i].chr_id);
        netbufWriteStr(&lbuf, s_lobby_state.slots[i].name);
    }
    netBroadcastBuf(&lbuf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

static void netSendMatchStartTo(ENetPeer *peer) {
    u8 raw[256];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_START_MATCH);
    netbufWriteU8(&buf, 0);
    netbufWriteU8(&buf, s_lobby_state.stage_num);
    netbufWriteU8(&buf, s_lobby_state.scenario);
    netbufWriteU8(&buf, s_lobby_state.weapon_set);
    netbufWriteU32(&buf, s_rng_seed);
    netbufWriteU8(&buf, (uint8_t)netGetConnectedPlayerCount());
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        netbufWriteU8(&buf, s_lobby_state.slots[i].chr_id);
    netbufWriteU8(&buf, (uint8_t)s_phase);
    if (peer) {
        ENetPacket *packet = enet_packet_create(buf.data, buf.wp, ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, NET_CHAN_RELIABLE, packet);
    } else {
        netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    }
}

bool netLobbyHostLaunchMatch(void) {
    if (!netIsHost()) return false;

    int connected = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        if (s_lobby_state.slots[i].connected) {
            if (i >= s_max_players || !s_lobby_state.slots[i].ready) return false;
            connected++;
        }
    }
    if (connected < 1) return false;
    
    s_rng_seed = (uint32_t)(sysGetMicroseconds());
    if (s_rng_seed == 0) s_rng_seed = 0x12345678;
    
    extern void randomSetSeed(u32);
    randomSetSeed(s_rng_seed);
    
    s_phase = NET_PHASE_WARMUP;
    netSendMatchStartTo(NULL);
    s_state = NET_STATE_INGAME;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        if (s_lobby_state.slots[i].connected) s_lobby_state.slots[i].ready = 0;
    NET_LOG("Match launched! Stage: %d, Players: %d, Seed: 0x%08X", s_lobby_state.stage_num, netGetConnectedPlayerCount(), s_rng_seed);
    return true;
}

void netSendLocalPlayerMove(const struct netplayermove *move) {
    if (s_state != NET_STATE_INGAME || s_local_slot < 0) return;
    
    u8 raw[256];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_PLAYER_STATE);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWritePlayerMove(&buf, move);
    
    netBroadcastBuf(&buf, NET_CHAN_PLAYER_STATE, ENET_PACKET_FLAG_UNSEQUENCED, NULL);
}

void netSendLocalPlayerState(const NetMsgPlayerState *state) {
    if (s_state != NET_STATE_INGAME || s_local_slot < 0) return;
    
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
    if (slot_id < 0 || slot_id >= GEVR_MAX_PLAYERS || slot_id == s_local_slot) return false;
    return s_remote_active[slot_id];
}

/* The remote player's right gun barrel in world space, when its owner aims
 * from a tracked controller (bondview2.c gevrStereoShot for a copy). */
int netGetRemoteAim(int slot_id, coord3d *origin, coord3d *dir) {
    if (!netIsRemotePlayerActive(slot_id) || !origin || !dir) return 0;
    const struct netplayermove *m = &s_remote_moves[slot_id];
    if (!(m->ucmd & UCMD_AIMVALID)) return 0;
    float len = sqrtf(m->aimdir.x * m->aimdir.x + m->aimdir.y * m->aimdir.y + m->aimdir.z * m->aimdir.z);
    if (!(len > 0.0001f) || !isfinite(len) ||
        !isfinite(m->aimorigin.x) || !isfinite(m->aimorigin.y) || !isfinite(m->aimorigin.z)) return 0;
    *origin = m->aimorigin;
    dir->x = m->aimdir.x / len;
    dir->y = m->aimdir.y / len;
    dir->z = m->aimdir.z / len;
    return 1;
}

static void netProcessHitReport(uint8_t shooter_slot, uint8_t target, uint8_t weapon, float hx, float hy, float hz, float dmg) {
    if (!netIsHost()) return;
    (void)hy;
    
    NET_LOG("Hit reported: shooter %d -> target %d (dmg: %.1f)", shooter_slot, target, dmg);
    
    float vx = hx;
    float vz = hz;
    
    /* Host resolves damage and broadcasts damage event to all peers */
    u8 draw[128];
    struct netbuf dbuf = { .data = draw, .size = sizeof(draw) };
    netbufStartWrite(&dbuf);
    netbufWriteU32(&dbuf, GEVR_NET_MAGIC);
    netbufWriteU16(&dbuf, GEVR_NET_VERSION);
    netbufWriteU8(&dbuf, NET_MSG_DAMAGE_EVENT);
    netbufWriteU8(&dbuf, 0);
    netbufWriteU8(&dbuf, target);
    netbufWriteU8(&dbuf, shooter_slot);
    netbufWriteU8(&dbuf, weapon);
    netbufWriteF32(&dbuf, dmg);
    netbufWriteF32(&dbuf, vx);
    netbufWriteF32(&dbuf, vz);
    
    netBroadcastBuf(&dbuf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    
    /* Also execute damage on host locally */
    if (target < GEVR_MAX_PLAYERS && g_playerPointers[target] != NULL) {
        s32 prev = get_cur_playernum();
        set_cur_player(target);
        extern s32 s_gevrExplosionDamage;
        s_gevrExplosionDamage = (weapon == ITEM_GRENADE || weapon == ITEM_GRENADELAUNCH || weapon == ITEM_ROCKETLAUNCH || weapon == ITEM_PROXIMITYMINE || weapon == ITEM_TIMEDMINE || weapon == ITEM_REMOTEMINE || weapon == ITEM_TANKSHELLS);
        record_damage_kills(dmg, vx, vz, shooter_slot, 1);
        s_gevrExplosionDamage = 0;
        set_cur_player(prev);
    }
}

/* Host only: damage from an explosion no player owns (player -1). It is
 * resolved with the target as its own attacker, so a death counts as a
 * suicide rather than a kill for the host's slot. */
void netSendWorldHitReport(uint8_t target_slot, uint8_t weapon_id, float hit_x, float hit_y, float hit_z, float dmg) {
    if (s_state != NET_STATE_INGAME || !netIsHost()) return;
    netProcessHitReport(target_slot, target_slot, weapon_id, hit_x, hit_y, hit_z, dmg);
}

void netSendHitReport(uint8_t target_slot, uint8_t weapon_id, uint8_t hit_part, float hit_x, float hit_y, float hit_z, float dmg) {
    if (s_state != NET_STATE_INGAME) return;
    
    if (netIsHost()) {
        netProcessHitReport((uint8_t)s_local_slot, target_slot, weapon_id, hit_x, hit_y, hit_z, dmg);
        return;
    }
    
    u8 raw[128];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_HIT_REPORT);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteU8(&buf, target_slot);
    netbufWriteU8(&buf, weapon_id);
    netbufWriteU8(&buf, hit_part);
    netbufWriteF32(&buf, hit_x);
    netbufWriteF32(&buf, hit_y);
    netbufWriteF32(&buf, hit_z);
    netbufWriteF32(&buf, dmg);
    
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

void netSendRespawnEvent(uint8_t pad_index, float theta) {
    if (s_state != NET_STATE_INGAME || s_local_slot < 0 || pad_index >= startpadcount) return;

    u8 raw[32];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_RESPAWN);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteU8(&buf, pad_index);
    netbufWriteF32(&buf, theta);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
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
    if (s_state != NET_STATE_INGAME || s_local_slot < 0 || !pos || !vel || !rot9) return;

    u8 raw[128];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    coord3d zero = { 0 };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_PROJECTILE);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
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

/* explosion.c explosionCreate: a damaging explosion the local player caused */
void netSendExplosion(s32 type, const coord3d *pos, const u8 *rooms, s32 ground, s32 flag8) {
    if (s_state != NET_STATE_INGAME || s_local_slot < 0 || !pos) return;

    u8 raw[48];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_EXPLOSION);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteU8(&buf, (uint8_t)type);
    netbufWriteU8(&buf, rooms ? rooms[0] : 0xff);
    netbufWriteU8(&buf, (uint8_t)((ground ? 1 : 0) | (flag8 ? 2 : 0)));
    netbufWriteCoord(&buf, pos);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    NET_LOG("explosion tx: type %d at %.0f,%.0f,%.0f", (int)type, pos->x, pos->y, pos->z);
}

/* A world event from player slot: sent by that player's own headset, relayed
 * by the host, applied only while the level's players are live. */
static bool netAcceptPlayerEvent(ENetPeer *peer, int slot) {
    if (s_state != NET_STATE_INGAME || slot < 0 || slot >= s_max_players || slot == s_local_slot ||
        !s_lobby_state.slots[slot].connected) return false;
    if (netIsHost()) {
        if (slot == s_host_slot || s_client_peers[slot] != peer || (int)(intptr_t)peer->data != slot) return false;
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
    
    netBroadcastBuf(&buf, NET_CHAN_VOIP, ENET_PACKET_FLAG_UNSEQUENCED, NULL);
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
    
    switch (msg_type) {
        case NET_MSG_HELLO: {
            if (!netIsHost()) break;
            if (peer->data) break;
            char *name = netbufReadStr(&buf);
            const char *name_end = (const char *)buf.data + buf.rp;
            uint8_t requested_chr = netbufReadU8(&buf);
            if (buf.error) break;
            if (requested_chr >= 12) requested_chr = 0;

            /* The same name back on a new connection: the earlier one is dead
             * (a crash, a quit without a goodbye) and its slot is theirs again. */
            char clean[GEVR_MAX_NAME_LEN];
            netCleanName(clean, name, name_end);
            for (int i = 1; i < s_max_players; i++) {
                if (s_lobby_state.slots[i].connected && s_client_peers[i] && s_client_peers[i] != peer &&
                    strcasecmp(s_lobby_state.slots[i].name, clean) == 0) {
                    NET_LOG("%s is back on a new connection; slot %d's earlier one dropped", clean, i);
                    netHostDropSlot(i, s_client_peers[i]);
                }
            }

            /* A player back after a host migration names its slot, kept for
             * it (s_slot_grace_us); otherwise the first free one. Slot 0 is
             * the first host's: after a migration it stays empty. */
            int assigned = -1;
            int previous = netbufReadLeft(&buf) >= 1 ? netbufReadU8(&buf) : 0xFF;
            if (previous >= 1 && previous < s_max_players && previous != s_host_slot &&
                s_lobby_state.slots[previous].connected && s_client_peers[previous] == NULL &&
                s_slot_grace_us[previous] && strcasecmp(s_lobby_state.slots[previous].name, clean) == 0) {
                assigned = previous;
                NET_LOG("%s is back in slot %d after the host change", clean, previous);
            }
            for (int i = 1; assigned < 0 && i < s_max_players; i++) {
                if (i != s_host_slot && !s_lobby_state.slots[i].connected) {
                    assigned = i;
                }
            }
            
            if (assigned < 0) {
                NET_ERR("Rejecting connection: lobby full");
                enet_peer_disconnect(peer, 0);
                break;
            }
            
            s_client_peers[assigned] = peer;
            peer->data = (void *)(intptr_t)assigned;
            
            s_lobby_state.slots[assigned].connected = 1;
            s_lobby_state.slots[assigned].ready = 0;
            s_lobby_state.slots[assigned].chr_id = requested_chr;
            netSetJoinerName(assigned, name, name_end);
            player_char[assigned] = requested_chr;
            
            /* Send welcome to client */
            u8 wraw[64];
            struct netbuf wbuf = { .data = wraw, .size = sizeof(wraw) };
            netbufStartWrite(&wbuf);
            netbufWriteU32(&wbuf, GEVR_NET_MAGIC);
            netbufWriteU16(&wbuf, GEVR_NET_VERSION);
            netbufWriteU8(&wbuf, NET_MSG_WELCOME);
            netbufWriteU8(&wbuf, 0);
            netbufWriteU8(&wbuf, (uint8_t)assigned);
            netbufWriteU8(&wbuf, s_lobby_state.stage_num);
            netbufWriteU8(&wbuf, s_lobby_state.scenario);
            netbufWriteU8(&wbuf, s_lobby_state.weapon_set);
            netbufWriteU8(&wbuf, (uint8_t)s_host_slot);

            ENetPacket *wp = enet_packet_create(wbuf.data, wbuf.wp, ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(peer, NET_CHAN_RELIABLE, wp);
            
            /* Send host's app version and known peer versions to the new client */
            netSendLocalAppVersion(peer);
            for (int k = 1; k < s_max_players; k++) {
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
            
            /* Broadcast updated lobby state */
            u8 lraw[256];
            struct netbuf lbuf = { .data = lraw, .size = sizeof(lraw) };
            netbufStartWrite(&lbuf);
            netbufWriteU32(&lbuf, GEVR_NET_MAGIC);
            netbufWriteU16(&lbuf, GEVR_NET_VERSION);
            netbufWriteU8(&lbuf, NET_MSG_LOBBY_STATE);
            netbufWriteU8(&lbuf, 0);
            netbufWriteU8(&lbuf, s_lobby_state.stage_num);
            netbufWriteU8(&lbuf, s_lobby_state.scenario);
            netbufWriteU8(&lbuf, s_lobby_state.weapon_set);
            netbufWriteU8(&lbuf, s_lobby_state.countdown_secs);
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                netbufWriteU8(&lbuf, s_lobby_state.slots[i].connected);
                netbufWriteU8(&lbuf, s_lobby_state.slots[i].ready);
                netbufWriteU8(&lbuf, s_lobby_state.slots[i].chr_id);
                netbufWriteStr(&lbuf, s_lobby_state.slots[i].name);
            }
            netBroadcastBuf(&lbuf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
            if (s_state == NET_STATE_INGAME) netSendMatchStartTo(peer);
            netSendLobbyHandoffTo(peer);
            netBroadcastStageVotes();
            NET_LOG("Assigned player '%s' to slot %d", s_lobby_state.slots[assigned].name, assigned);
            break;
        }
        case NET_MSG_WELCOME: {
            if (netIsHost() || peer != s_server_peer ||
                (s_state != NET_STATE_CONNECTING && s_state != NET_STATE_MIGRATING)) break;
            const bool rejoin = s_state == NET_STATE_MIGRATING;
            int slot = netbufReadU8(&buf);
            if (slot < 1 || slot >= GEVR_MAX_PLAYERS || (rejoin && slot != s_local_slot)) {
                /* a rejoin under another slot would move the player's own struct: not mid-level */
                NET_ERR("Welcomed as slot %d%s; leaving", slot, rejoin ? " after a host change" : "");
                enet_peer_disconnect(peer, 0);
                if (rejoin) netMigrationGiveUpNow();
                break;
            }
            s_local_slot = slot;
            s_lobby_state.stage_num = netbufReadU8(&buf);
            s_lobby_state.scenario = netbufReadU8(&buf);
            s_lobby_state.weapon_set = netbufReadU8(&buf);
            if (netbufReadLeft(&buf) >= 1) {
                int host = netbufReadU8(&buf);
                if (host >= 0 && host < GEVR_MAX_PLAYERS && host != s_local_slot) s_host_slot = host;
            }
            s_state = NET_STATE_CLIENT_LOBBY;
            strncpy(s_slot_app_version[s_local_slot], s_local_app_version, sizeof(s_slot_app_version[0]) - 1);
            netSendLocalAppVersion(s_server_peer);
            NET_LOG("Connected! Assigned local slot: %d, stage: %d", s_local_slot, s_lobby_state.stage_num);
            break;
        }
        case NET_MSG_LOBBY_STATE: {
            if (netIsHost()) break;
            s_lobby_state.stage_num = netbufReadU8(&buf);
            s_lobby_state.scenario = netbufReadU8(&buf);
            s_lobby_state.weapon_set = netbufReadU8(&buf);
            s_lobby_state.countdown_secs = netbufReadU8(&buf);
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                uint8_t was_connected = s_lobby_state.slots[i].connected;
                s_lobby_state.slots[i].connected = netbufReadU8(&buf);
                if (was_connected && !s_lobby_state.slots[i].connected) {
                    netVoiceForgetSlot((uint8_t)i);
                    netForgetPlayerScore(i);
                    s_slot_app_version[i][0] = '\0';
                }
                s_lobby_state.slots[i].ready = netbufReadU8(&buf);
                s_lobby_state.slots[i].chr_id = netbufReadU8(&buf);
                char *name = netbufReadStr(&buf);
                if (name) {
                    netCleanName(s_lobby_state.slots[i].name, name, (const char *)buf.data + buf.rp);
                }
                player_char[i] = s_lobby_state.slots[i].chr_id;
            }
            break;
        }
        case NET_MSG_LOBBY_READY: {
            if (s_state != NET_STATE_HOSTING_LOBBY) break;
            int slot = (int)(intptr_t)peer->data;
            /* netLobbySetReady sends the slot in the header, then one byte. */
            uint8_t ready = netbufReadU8(&buf);
            if (slot >= 1 && slot < s_max_players && s_client_peers[slot] == peer &&
                s_lobby_state.slots[slot].connected && !buf.error && size == 9 &&
                slot_id == slot && ready <= 1) {
                s_lobby_state.slots[slot].ready = ready;
                
                u8 lraw[256];
                struct netbuf lbuf = { .data = lraw, .size = sizeof(lraw) };
                netbufStartWrite(&lbuf);
                netbufWriteU32(&lbuf, GEVR_NET_MAGIC);
                netbufWriteU16(&lbuf, GEVR_NET_VERSION);
                netbufWriteU8(&lbuf, NET_MSG_LOBBY_STATE);
                netbufWriteU8(&lbuf, 0);
                netbufWriteU8(&lbuf, s_lobby_state.stage_num);
                netbufWriteU8(&lbuf, s_lobby_state.scenario);
                netbufWriteU8(&lbuf, s_lobby_state.weapon_set);
                netbufWriteU8(&lbuf, s_lobby_state.countdown_secs);
                for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                    netbufWriteU8(&lbuf, s_lobby_state.slots[i].connected);
                    netbufWriteU8(&lbuf, s_lobby_state.slots[i].ready);
                    netbufWriteU8(&lbuf, s_lobby_state.slots[i].chr_id);
                    netbufWriteStr(&lbuf, s_lobby_state.slots[i].name);
                }
                netBroadcastBuf(&lbuf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
            }
            break;
        }
        case NET_MSG_LOBBY_CHARACTER: {
            if (s_state != NET_STATE_HOSTING_LOBBY) break;
            int slot = (int)(intptr_t)peer->data;
            /* netLobbySetCharacter sends the slot in the header, then one byte. */
            uint8_t chr_id = netbufReadU8(&buf);
            if (!buf.error && size == 9 && slot >= 1 && slot < s_max_players &&
                slot_id == slot && chr_id < 12 &&
                s_client_peers[slot] == peer) {
                s_lobby_state.slots[slot].chr_id = chr_id;
                player_char[slot] = chr_id;
                
                u8 lraw[256];
                struct netbuf lbuf = { .data = lraw, .size = sizeof(lraw) };
                netbufStartWrite(&lbuf);
                netbufWriteU32(&lbuf, GEVR_NET_MAGIC);
                netbufWriteU16(&lbuf, GEVR_NET_VERSION);
                netbufWriteU8(&lbuf, NET_MSG_LOBBY_STATE);
                netbufWriteU8(&lbuf, 0);
                netbufWriteU8(&lbuf, s_lobby_state.stage_num);
                netbufWriteU8(&lbuf, s_lobby_state.scenario);
                netbufWriteU8(&lbuf, s_lobby_state.weapon_set);
                netbufWriteU8(&lbuf, s_lobby_state.countdown_secs);
                for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                    netbufWriteU8(&lbuf, s_lobby_state.slots[i].connected);
                    netbufWriteU8(&lbuf, s_lobby_state.slots[i].ready);
                    netbufWriteU8(&lbuf, s_lobby_state.slots[i].chr_id);
                    netbufWriteStr(&lbuf, s_lobby_state.slots[i].name);
                }
                netBroadcastBuf(&lbuf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
            }
            break;
        }
        case NET_MSG_START_MATCH: {
            if (netIsHost() || peer != s_server_peer || s_state != NET_STATE_CLIENT_LOBBY) break;
            s_lobby_state.stage_num = netbufReadU8(&buf);
            s_lobby_state.scenario = netbufReadU8(&buf);
            s_lobby_state.weapon_set = netbufReadU8(&buf);
            uint32_t seed = netbufReadU32(&buf);
            uint8_t num_players = netbufReadU8(&buf);
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                uint8_t ch = netbufReadU8(&buf);
                player_char[i] = ch;
            }
            uint8_t phase = netbufReadU8(&buf);
            if (buf.error || phase > NET_PHASE_IN_PROGRESS) break;
            s_rng_seed = seed;
            extern void randomSetSeed(u32);
            randomSetSeed(s_rng_seed);
            (void)num_players;
            s_state = NET_STATE_INGAME;
            /* A late joiner stays in warmup until the reliable match snapshot arrives. */
            s_phase = phase == NET_PHASE_IN_PROGRESS ? NET_PHASE_WARMUP : (NetPhase)phase;
            s_waiting_for_match_snapshot = phase == NET_PHASE_IN_PROGRESS;
            s_stage_ready_sent = false;
            s_lobby_state.slots[s_local_slot].ready = 0;
            NET_LOG("Starting match on stage %d! Seed: 0x%08X", s_lobby_state.stage_num, s_rng_seed);
            if (s_rejoining) {
                /* Back with the new host: the stage is loaded already, so this
                 * is the load's end (STAGE_READY, then the snapshots) - unless
                 * the map changed meanwhile, which is a reload. */
                s_rejoining = false;
                if ((int)s_lobby_state.stage_num == (int)bossGetStageNum()) {
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
            int slot = (int)(intptr_t)peer->data;
            if (slot < 1 || slot >= s_max_players || s_client_peers[slot] != peer ||
                !s_lobby_state.slots[slot].connected || size != 24) break;
            float x = netbufReadF32(&buf), y = netbufReadF32(&buf);
            float z = netbufReadF32(&buf), yaw = netbufReadF32(&buf);
            if (buf.error) break;
            bool was_ready = s_lobby_state.slots[slot].ready != 0;
            /* back after a host change: still standing where it was, not respawned */
            bool returning = s_slot_grace_us[slot] != 0;
            s_slot_grace_us[slot] = 0;
            if (!was_ready && !returning && s_phase == NET_PHASE_IN_PROGRESS && startpadcount > 0 && g_playerPointers[slot]) {
                s32 previous = get_cur_playernum();
                set_cur_player(slot);
                mp_respawn_handler_net(0, yaw);
                set_cur_player(previous);
                u8 respawn_raw[16];
                struct netbuf respawn = { .data = respawn_raw, .size = sizeof(respawn_raw) };
                netbufStartWrite(&respawn);
                netbufWriteU32(&respawn, GEVR_NET_MAGIC);
                netbufWriteU16(&respawn, GEVR_NET_VERSION);
                netbufWriteU8(&respawn, NET_MSG_RESPAWN);
                netbufWriteU8(&respawn, (u8)slot);
                netbufWriteU8(&respawn, 0);
                netbufWriteF32(&respawn, yaw);
                netBroadcastBuf(&respawn, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, peer);
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
            }
            s_lobby_state.slots[slot].ready = 1;
            netBroadcastLobbyState();
            if (s_phase == NET_PHASE_WARMUP && netGetConnectedPlayerCount() >= 2 && netAllLoaded()) {
                if (s_round_reset_loading) {
                    s_round_reset_loading = false;
                    if (s_pause_after_results) {
                        s_pause_after_results = false;
                        netScheduleRound(15);
                    } else netBroadcastRoundPhase(NET_PHASE_IN_PROGRESS);
                } else netBeginRoundReset();
            }
            break;
        }
        case NET_MSG_ROUND_RESET: {
            if (netIsHost() || peer != s_server_peer || (size != 8 && size != 9)) break;
            if (size == 9) {
                /* the next map (the vote's, or the same): boss.c reloads it */
                uint8_t stage = netbufReadU8(&buf);
                if (!buf.error) s_lobby_state.stage_num = stage;
            }
            s_phase = NET_PHASE_WARMUP;
            s_round_reset_pending = true;
            s_countdown_end_us = 0;
            s_lobby_state.slots[s_local_slot].ready = 0;
            break;
        }
        case NET_MSG_STAGE_VOTE: {
            /* a player's next-map vote: the host keeps the tally and tells everyone */
            if (!netIsHost() || size != 9) break;
            int slot = slot_id;
            uint8_t vote = netbufReadU8(&buf);
            if (buf.error || !netAcceptPlayerEvent(peer, slot)) break;
            s_stage_vote[slot] = vote < netStageCount() ? (int8_t)vote : -1;
            netBroadcastStageVotes();
            break;
        }
        case NET_MSG_STAGE_VOTES: {
            if (netIsHost() || peer != s_server_peer || size != 8 + GEVR_MAX_PLAYERS) break;
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                uint8_t vote = netbufReadU8(&buf);
                s_stage_vote[i] = vote < netStageCount() ? (int8_t)vote : -1;
            }
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
        case NET_MSG_COUNTDOWN: {
            if (netIsHost() || peer != s_server_peer || size != 12) break;
            uint32_t ms = netbufReadU32(&buf);
            s_countdown_end_us = ms ? sysGetMicroseconds() + (uint64_t)ms * 1000 : 0;
            break;
        }
        case NET_MSG_MATCH_END: {
            if (netIsHost() || peer != s_server_peer || size != 8 ||
                s_state != NET_STATE_INGAME || s_phase != NET_PHASE_IN_PROGRESS) break;
            extern void mpCalculateAwards(bool);
            mpCalculateAwards(false);
            break;
        }
        case NET_MSG_ROUND_PHASE: {
            if (netIsHost() || peer != s_server_peer || size != 9) break;
            uint8_t phase = netbufReadU8(&buf);
            if (phase != NET_PHASE_WARMUP && phase != NET_PHASE_IN_PROGRESS) break;
            s_phase = (NetPhase)phase;
            break;
        }
        case NET_MSG_MATCH_SNAPSHOT: {
            if (netIsHost() || peer != s_server_peer || s_state != NET_STATE_INGAME ||
                s_local_slot < 0) break;
            u32 clock = netbufReadU32(&buf);
            u32 score_bank[GEVR_MAX_PLAYERS];
            u32 scores[GEVR_MAX_PLAYERS][GEVR_MAX_PLAYERS];
            uint8_t occupied[GEVR_MAX_PLAYERS];
            float state[GEVR_MAX_PLAYERS][7];
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                score_bank[i] = netbufReadU32(&buf);
                for (int j = 0; j < GEVR_MAX_PLAYERS; j++) scores[i][j] = netbufReadU32(&buf);
            }
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                occupied[i] = netbufReadU8(&buf);
                for (int j = 0; j < 7; j++) state[i][j] = netbufReadF32(&buf);
            }
            if (buf.error || netbufReadLeft(&buf) != 0) break;
            D_80048394 = (s32)clock;
            D_800483A8 = (s32)clock;
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                g_playerPlayerData[i].kill_count = (s32)score_bank[i];
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
            }
            s_phase = NET_PHASE_IN_PROGRESS;
            s_waiting_for_match_snapshot = false;
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
                }
            }
            break;
        }
        case NET_MSG_PLAYER_STATE: {
            int slot = slot_id;
            if (s_state == NET_STATE_INGAME && slot >= 0 && slot < s_max_players &&
                slot != s_local_slot && s_lobby_state.slots[slot].connected &&
                s_lobby_state.slots[slot].ready &&
                ((!netIsHost() && peer == s_server_peer) ||
                 (netIsHost() && slot != s_host_slot && s_client_peers[slot] == peer &&
                  (int)(intptr_t)peer->data == slot))) {
                struct netplayermove move;
                netbufReadPlayerMove(&buf, &move);
                if (buf.error || netbufReadLeft(&buf) != 0) break;
                s_remote_moves[slot] = move;
                
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
            uint8_t target = netbufReadU8(&buf);
            uint8_t weapon = netbufReadU8(&buf);
            uint8_t hit_loc = netbufReadU8(&buf);
            float hx = netbufReadF32(&buf);
            float hy = netbufReadF32(&buf);
            float hz = netbufReadF32(&buf);
            float dmg = netbufReadF32(&buf);
            (void)hit_loc;
            
            if (netIsHost() && s_phase == NET_PHASE_IN_PROGRESS && !buf.error &&
                slot_id > 0 && slot_id < s_max_players && s_client_peers[slot_id] == peer &&
                (int)(intptr_t)peer->data == slot_id && netSlotOccupied(slot_id) &&
                netSlotOccupied(target)) {
                netProcessHitReport((uint8_t)slot_id, target, weapon, hx, hy, hz, dmg);
            }
            break;
        }
        case NET_MSG_DAMAGE_EVENT: {
            uint8_t target = netbufReadU8(&buf);
            uint8_t attacker = netbufReadU8(&buf);
            uint8_t weapon = netbufReadU8(&buf);
            float dmg = netbufReadF32(&buf);
            float vx = netbufReadF32(&buf);
            float vz = netbufReadF32(&buf);
            (void)weapon;
            
            /* Apply authoritative damage to the target player across all connected headsets */
            if (!netIsHost() && peer == s_server_peer && !buf.error &&
                s_phase == NET_PHASE_IN_PROGRESS && netSlotOccupied(target) &&
                attacker < GEVR_MAX_PLAYERS && g_playerPointers[target] != NULL) {
                NET_LOG("Player %d took %.1f damage from attacker %d", target, dmg, attacker);
                s32 prev = get_cur_playernum();
                set_cur_player(target);
                extern s32 s_gevrExplosionDamage;
                s_gevrExplosionDamage = (weapon == ITEM_GRENADE || weapon == ITEM_GRENADELAUNCH || weapon == ITEM_ROCKETLAUNCH || weapon == ITEM_PROXIMITYMINE || weapon == ITEM_TIMEDMINE || weapon == ITEM_REMOTEMINE || weapon == ITEM_TANKSHELLS);
                record_damage_kills(dmg, vx, vz, attacker, 1);
                s_gevrExplosionDamage = 0;
                set_cur_player(prev);
            }
            break;
        }
        case NET_MSG_RESPAWN: {
            uint8_t pad_index = netbufReadU8(&buf);
            float theta = netbufReadF32(&buf);
            int slot = slot_id;
            /* Applied whether or not this headset saw the player die: the
             * respawn resets health, armour and position, so it also brings
             * a copy that drifted back in line with its owner. */
            if (s_state != NET_STATE_INGAME || buf.error ||
                slot < 0 || slot >= GEVR_MAX_PLAYERS || slot == s_local_slot ||
                pad_index >= startpadcount || g_playerPointers[slot] == NULL ||
                g_playerPointers[slot]->prop == NULL) break;
            if (netIsHost()) {
                if (slot == s_host_slot || s_client_peers[slot] != peer ||
                    (int)(intptr_t)peer->data != slot) break;
            } else if (peer != s_server_peer) {
                break;
            }

            s32 prev = get_cur_playernum();
            set_cur_player(slot);
            mp_respawn_handler_net(pad_index, theta);
            s_remote_moves[slot].pos = g_playerPointers[slot]->prop->pos;
            s_remote_moves[slot].angles[0] = theta;
            s_remote_active[slot] = true;
            set_cur_player(prev);

            if (netIsHost()) {
                netBroadcastPacket(data, size, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, peer);
            }
            break;
        }
        case NET_MSG_PROJECTILE: {
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
            if (netIsHost() && !buf.error && slot_id > 0 && slot_id < s_max_players &&
                s_client_peers[slot_id] == peer && (int)(intptr_t)peer->data == slot_id &&
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
                    (int)(intptr_t)peer->data != slot_id) break;
            } else if (peer != s_server_peer) break;
            const uint32_t sequence = netbufReadU32(&buf);
            const uint16_t payload_size = netbufReadU16(&buf);
            if (buf.error || payload_size == 0 || payload_size > GEVR_VOIP_MAX_BYTES ||
                netbufReadLeft(&buf) != payload_size || size != 14u + payload_size) break;
            netVoiceReceive(slot_id, sequence, data + buf.rp, payload_size);
            if (netIsHost()) {
                netBroadcastPacket(data, size, NET_CHAN_VOIP, ENET_PACKET_FLAG_UNSEQUENCED, peer);
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

/* ---- Next map (mpmenu.c NEXT MAP row) ---- */

/* The multiplayer stages, as the launcher lists them (vr_launcher.cpp). */
static const struct { uint8_t id; uint8_t maxplayers; const char *name; } s_mp_stages[] = {
    { 34, 4, "FACILITY" }, { 31, 4, "COMPLEX" },   { 38, 4, "TEMPLE" },   { 46, 4, "STACK" },
    { 39, 3, "CAVERNS" },  { 48, 4, "LIBRARY" },   { 45, 4, "BASEMENT" }, { 50, 4, "CAVES" },
    { 32, 2, "EGYPT" },    { 27, 3, "BUNKER II" }, { 24, 3, "ARCHIVES" },
};

int netStageCount(void) {
    return (int)(sizeof(s_mp_stages) / sizeof(s_mp_stages[0]));
}

const char *netStageName(int idx) {
    return idx >= 0 && idx < netStageCount() ? s_mp_stages[idx].name : "";
}

int netStageMaxPlayers(int idx) {
    return idx >= 0 && idx < netStageCount() ? s_mp_stages[idx].maxplayers : 0;
}

int netStageIndexOf(uint8_t level_id) {
    for (int i = 0; i < netStageCount(); i++)
        if (s_mp_stages[i].id == level_id) return i;
    return -1;
}

int netGetStageVote(int slot) {
    return slot >= 0 && slot < GEVR_MAX_PLAYERS ? s_stage_vote[slot] : -1;
}

static void netBroadcastStageVotes(void) {
    if (!netIsHost() || s_state != NET_STATE_INGAME) return;
    u8 raw[16];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_STAGE_VOTES);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        netbufWriteU8(&buf, s_stage_vote[i] < 0 ? 0xFF : (uint8_t)s_stage_vote[i]);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

void netSetLocalStageVote(int idx) {
    if (s_state != NET_STATE_INGAME || s_local_slot < 0 || s_local_slot >= GEVR_MAX_PLAYERS) return;
    if (idx < -1 || idx >= netStageCount()) idx = -1;
    s_stage_vote[s_local_slot] = (int8_t)idx;
    if (netIsHost()) {
        netBroadcastStageVotes();
        return;
    }
    u8 raw[16];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_STAGE_VOTE);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
    netbufWriteU8(&buf, idx < 0 ? 0xFF : (uint8_t)idx);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

/* The host, as a round reset begins: the most voted map is the next one,
 * the lowest slot's on a tie; without a vote the map stays. */
static void netResolveStageVote(void) {
    int count[32] = { 0 };
    int best = -1;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        if (s_stage_vote[i] >= 0 && s_stage_vote[i] < netStageCount() &&
            (i == s_local_slot || s_lobby_state.slots[i].connected)) count[s_stage_vote[i]]++;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        int v = s_stage_vote[i];
        if (v >= 0 && v < netStageCount() && count[v] > 0 && (best < 0 || count[v] > count[best])) best = v;
    }
    if (best >= 0 && s_mp_stages[best].maxplayers >= netGetConnectedPlayerCount() &&
        s_mp_stages[best].id != s_lobby_state.stage_num) {
        NET_LOG("Next map: %s (%d vote%s)", s_mp_stages[best].name, count[best], count[best] == 1 ? "" : "s");
        s_lobby_state.stage_num = s_mp_stages[best].id;
    }
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) s_stage_vote[i] = -1;
    netBroadcastStageVotes();
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
    int old = s_host_slot;
    int elected = -1;
    enet_address_get_ip(&peer->address, s_old_host_ip, sizeof(s_old_host_ip));
    s_server_peer = NULL;
    if (old >= 0 && old < GEVR_MAX_PLAYERS) {
        s_lobby_state.slots[old].connected = 0;
        s_lobby_state.slots[old].ready = 0;
        s_remote_active[old] = false;
        netVoiceForgetSlot((uint8_t)old);
        netForgetPlayerScore(old);
        s_slot_app_version[old][0] = '\0';
        s_stage_vote[old] = -1;
    }
    for (int i = 0; i < s_max_players; i++) {
        if (i != old && (i == s_local_slot || s_lobby_state.slots[i].connected)) {
            elected = i;
            break;
        }
    }
    if (elected < 0) elected = s_local_slot;
    s_host_slot = elected;
    s_state = NET_STATE_MIGRATING;
    s_migrate_deadline_us = sysGetMicroseconds() + 30ull * 1000000;
    s_countdown_end_us = 0;
    s_next_round_at_us = 0;
    s_pause_after_results = false;
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
    s_host = enet_host_create(&address, GEVR_MAX_PLAYERS, NET_CHAN_MAX, 0, 0, 0);
    if (!s_host) {
        NET_ERR("Failed to create the ENet host for the takeover on port %d", address.port);
        netMigrationGiveUpNow();
        return false;
    }
    enet_host_set_virtual_transport(s_host, s_virtual_send, s_virtual_receive, s_virtual_context);
    s_state = NET_STATE_INGAME;
    s_takeover_pending = false;
    s_max_players = s_lobby_max_players;
    uint64_t now = sysGetMicroseconds();
    int waiting = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        s_client_peers[i] = NULL;
        s_remote_active[i] = false;
        if (i == s_local_slot) {
            s_lobby_state.slots[i].connected = 1;
            s_lobby_state.slots[i].ready = 1;
        } else if (s_lobby_state.slots[i].connected) {
            s_lobby_state.slots[i].ready = 0;
            s_slot_grace_us[i] = now + 20ull * 1000000;
            waiting++;
        }
    }
    NET_LOG("Hosting the match from slot %d on port %d; %d player(s) have 20 s to come back", s_local_slot, address.port, waiting);
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
    if (slot < 1 || slot >= GEVR_MAX_PLAYERS) return;
    if (stale) {
        char ip[64] = "";
        enet_address_get_ip(&stale->address, ip, sizeof(ip));
        netIceForgetPeer(ip);
        stale->data = NULL;
        enet_peer_reset(stale);
    }
    s_client_peers[slot] = NULL;
    s_remote_active[slot] = false;
    netVoiceForgetSlot((uint8_t)slot);
    netForgetPlayerScore(slot);
    memset(&s_lobby_state.slots[slot], 0, sizeof(NetLobbySlot));
    s_slot_app_version[slot][0] = '\0';
    s_slot_grace_us[slot] = 0;
    s_stage_vote[slot] = -1;

    /* Broadcast updated lobby state */
    u8 lraw[256];
    struct netbuf lbuf = { .data = lraw, .size = sizeof(lraw) };
    netbufStartWrite(&lbuf);
    netbufWriteU32(&lbuf, GEVR_NET_MAGIC);
    netbufWriteU16(&lbuf, GEVR_NET_VERSION);
    netbufWriteU8(&lbuf, NET_MSG_LOBBY_STATE);
    netbufWriteU8(&lbuf, 0);
    netbufWriteU8(&lbuf, s_lobby_state.stage_num);
    netbufWriteU8(&lbuf, s_lobby_state.scenario);
    netbufWriteU8(&lbuf, s_lobby_state.weapon_set);
    netbufWriteU8(&lbuf, s_lobby_state.countdown_secs);
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        netbufWriteU8(&lbuf, s_lobby_state.slots[i].connected);
        netbufWriteU8(&lbuf, s_lobby_state.slots[i].ready);
        netbufWriteU8(&lbuf, s_lobby_state.slots[i].chr_id);
        netbufWriteStr(&lbuf, s_lobby_state.slots[i].name);
    }
    netBroadcastBuf(&lbuf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    netBroadcastStageVotes();
    if (s_state == NET_STATE_INGAME && netGetConnectedPlayerCount() == 1 &&
        s_phase == NET_PHASE_IN_PROGRESS) {
        s_pause_after_results = false;
        s_next_round_at_us = 0;
        netBeginRoundReset();
    } else if (s_state == NET_STATE_INGAME && netGetConnectedPlayerCount() < 2) {
        netCancelRound(); /* the countdown was for the player who left */
    }
}

void netPoll(void) {
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
                    netHostDropSlot((int)(intptr_t)event.peer->data, NULL);
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
    if (netIsHost() && s_state == NET_STATE_INGAME && s_phase == NET_PHASE_WARMUP &&
        s_next_round_at_us && sysGetMicroseconds() >= s_next_round_at_us &&
        netGetConnectedPlayerCount() >= 2 && netAllLoaded()) {
        s_next_round_at_us = 0;
        netBeginRoundReset();
    }
    if (s_waiting_for_match_snapshot && s_stage_ready_sent &&
        s_state == NET_STATE_INGAME && !netIsHost() &&
        sysGetMicroseconds() - s_last_stage_ready_us > 3000000) {
        NET_LOG("Still waiting for in-progress snapshot; repeating stage-ready request");
        netStageLoaded();
    }
    netVoiceTick();
}
