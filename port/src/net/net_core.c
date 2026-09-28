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
#include "game/player.h"
#include "game/front.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "game/chrai.h"
#include "game/loadobjectmodel.h"
#include "game/propobj.h"
#include <stdio.h>
#include <string.h>

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

/* Remote player state cache */
static struct netplayermove s_remote_moves[GEVR_MAX_PLAYERS];
static NetMsgPlayerState s_remote_players[GEVR_MAX_PLAYERS];
static bool s_remote_active[GEVR_MAX_PLAYERS];

/* Current Lobby State */
static NetMsgLobbyState s_lobby_state;

static uint32_t s_rng_seed = 0;

uint32_t netGetRandomSeed(void) {
    return s_rng_seed;
}

/* ENet Peer to slot mapping on host */
static ENetPeer *s_client_peers[GEVR_MAX_PLAYERS];
extern s32 D_80048394;
extern s32 D_800483A8;
static void netBroadcastBuf(struct netbuf *buf, uint8_t channel, uint32_t flags, ENetPeer *except);

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
    
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        s_remote_active[i] = false;
        s_client_peers[i] = NULL;
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
    snprintf(s_lobby_state.slots[0].name, GEVR_MAX_NAME_LEN, "Host");
    
    player_char[0] = 0;
    
    NET_LOG("Multiplayer server hosted on port %d", address.port);
    return true;
}

bool netConnect(const char *host_addr, uint16_t port) {
    if (!s_initialized && !netInit()) return false;
    netDisconnect();
    
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
    
    s_state = NET_STATE_CONNECTING;
    s_local_slot = -1;
    netResetLobbyState();
    
    NET_LOG("Connecting to %s:%d...", host_addr, address.port);
    return true;
}

void netDisconnect(void) {
    netVoiceReset();
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
    return s_state == NET_STATE_HOSTING_LOBBY || (s_state == NET_STATE_INGAME && s_local_slot == 0);
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

static void netBroadcastPacket(const void *data, size_t size, uint8_t channel, uint32_t flags, ENetPeer *except) {
    if (!s_host) return;
    
    if (netIsHost()) {
        for (int i = 1; i < GEVR_MAX_PLAYERS; i++) {
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
    for (int source = 0; source < 3; source++) {
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

static void netBeginRoundReset(void) {
    u8 raw[8];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_ROUND_RESET);
    netbufWriteU8(&buf, 0);
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    netBroadcastRoundPhase(NET_PHASE_WARMUP);
    s_round_reset_pending = true;
    s_round_reset_loading = true;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++)
        if (s_lobby_state.slots[i].connected) s_lobby_state.slots[i].ready = 0;
    netBroadcastLobbyState();
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
    if (s_state != NET_STATE_INGAME || s_local_slot < 0) return;
    s_lobby_state.slots[s_local_slot].ready = 1;
    if (!netIsHost()) {
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
                    s_next_round_at_us = sysGetMicroseconds() + 15000000;
                } else netBroadcastRoundPhase(NET_PHASE_IN_PROGRESS);
            } else if (s_phase == NET_PHASE_WARMUP) netBeginRoundReset();
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
        record_damage_kills(dmg, vx, vz, shooter_slot, 1);
        set_cur_player(prev);
    }
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
            uint8_t requested_chr = netbufReadU8(&buf);
            if (buf.error) break;
            if (requested_chr >= 12) requested_chr = 0;
            
            /* Find an available slot */
            int assigned = -1;
            for (int i = 1; i < s_max_players; i++) {
                if (!s_lobby_state.slots[i].connected) {
                    assigned = i;
                    break;
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
            snprintf(s_lobby_state.slots[assigned].name, GEVR_MAX_NAME_LEN, "%s", name ? name : "Player");
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
            
            ENetPacket *wp = enet_packet_create(wbuf.data, wbuf.wp, ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(peer, NET_CHAN_RELIABLE, wp);
            
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
            NET_LOG("Assigned player '%s' to slot %d", name ? name : "Player", assigned);
            break;
        }
        case NET_MSG_WELCOME: {
            if (netIsHost() || peer != s_server_peer || s_state != NET_STATE_CONNECTING) break;
            s_local_slot = netbufReadU8(&buf);
            if (s_local_slot < 1 || s_local_slot >= GEVR_MAX_PLAYERS) {
                enet_peer_disconnect(peer, 0);
                break;
            }
            s_lobby_state.stage_num = netbufReadU8(&buf);
            s_lobby_state.scenario = netbufReadU8(&buf);
            s_lobby_state.weapon_set = netbufReadU8(&buf);
            s_state = NET_STATE_CLIENT_LOBBY;
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
                }
                s_lobby_state.slots[i].ready = netbufReadU8(&buf);
                s_lobby_state.slots[i].chr_id = netbufReadU8(&buf);
                char *name = netbufReadStr(&buf);
                if (name) {
                    snprintf(s_lobby_state.slots[i].name, GEVR_MAX_NAME_LEN, "%s", name);
                }
                player_char[i] = s_lobby_state.slots[i].chr_id;
            }
            break;
        }
        case NET_MSG_LOBBY_READY: {
            if (s_state != NET_STATE_HOSTING_LOBBY) break;
            int slot = (int)(intptr_t)peer->data;
            uint8_t claimed_slot = netbufReadU8(&buf);
            uint8_t ready = netbufReadU8(&buf);
            if (slot >= 1 && slot < s_max_players && s_client_peers[slot] == peer &&
                s_lobby_state.slots[slot].connected && !buf.error && size == 10 &&
                claimed_slot == slot && slot_id == slot && ready <= 1) {
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
            uint8_t claimed_slot = netbufReadU8(&buf);
            uint8_t chr_id = netbufReadU8(&buf);
            if (!buf.error && size == 10 && slot >= 1 && slot < s_max_players &&
                claimed_slot == slot && slot_id == slot && chr_id < 12 &&
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
            s_lobby_state.slots[s_local_slot].ready = 0;
            NET_LOG("Starting match on stage %d! Seed: 0x%08X", s_lobby_state.stage_num, s_rng_seed);
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
            if (s_phase == NET_PHASE_IN_PROGRESS && startpadcount > 0 && g_playerPointers[slot]) {
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
            if (g_playerPointers[slot] && g_playerPointers[slot]->prop) {
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
                        s_next_round_at_us = sysGetMicroseconds() + 15000000;
                    } else netBroadcastRoundPhase(NET_PHASE_IN_PROGRESS);
                } else netBeginRoundReset();
            }
            break;
        }
        case NET_MSG_ROUND_RESET: {
            if (netIsHost() || peer != s_server_peer || size != 8) break;
            s_phase = NET_PHASE_WARMUP;
            s_round_reset_pending = true;
            s_lobby_state.slots[s_local_slot].ready = 0;
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
                ObjectRecord *obj = NULL;
                if (index >= 0x9000 && index < 0x9000 + MAX_AMMO_CRATES)
                    obj = (ObjectRecord *)&g_AmmoCrates[index - 0x9000];
                else if (index >= 0x8000 && index < 0x8000 + MAX_WEAPON_SLOTS)
                    obj = (ObjectRecord *)&g_WeaponSlots[index - 0x8000];
                else if (index < 0x8000 && netSnapshotObjectType(type))
                    obj = setupGetPtrToCommandByIndex(index);
                if (!obj || obj->type != type) continue;
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
                 (netIsHost() && slot > 0 && s_client_peers[slot] == peer &&
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
                record_damage_kills(dmg, vx, vz, attacker, 1);
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
                if (slot == 0 || s_client_peers[slot] != peer ||
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
                if (slot_id == 0 || s_client_peers[slot_id] != peer ||
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
        default:
            break;
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
                if (!netIsHost()) {
                    /* Connected as client -> send hello */
                    u8 raw[64];
                    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
                    netbufStartWrite(&buf);
                    netbufWriteU32(&buf, GEVR_NET_MAGIC);
                    netbufWriteU16(&buf, GEVR_NET_VERSION);
                    netbufWriteU8(&buf, NET_MSG_HELLO);
                    netbufWriteU8(&buf, 0xFF);
                    netbufWriteStr(&buf, "QuestPlayer");
                    netbufWriteU8(&buf, 0); /* Default Bond */
                    
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
                    int slot = (int)(intptr_t)event.peer->data;
                    if (slot >= 1 && slot < GEVR_MAX_PLAYERS) {
                        s_client_peers[slot] = NULL;
                        s_remote_active[slot] = false;
                        netVoiceForgetSlot((uint8_t)slot);
                        netForgetPlayerScore(slot);
                        memset(&s_lobby_state.slots[slot], 0, sizeof(NetLobbySlot));
                        
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
                        if (s_state == NET_STATE_INGAME && netGetConnectedPlayerCount() == 1 &&
                            s_phase == NET_PHASE_IN_PROGRESS) {
                            s_pause_after_results = false;
                            s_next_round_at_us = 0;
                            netBeginRoundReset();
                        }
                    }
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
    if (netIsHost() && s_state == NET_STATE_INGAME && s_phase == NET_PHASE_WARMUP &&
        s_next_round_at_us && sysGetMicroseconds() >= s_next_round_at_us &&
        netGetConnectedPlayerCount() >= 2 && netAllLoaded()) {
        s_next_round_at_us = 0;
        netBeginRoundReset();
    }
    netVoiceTick();
}
