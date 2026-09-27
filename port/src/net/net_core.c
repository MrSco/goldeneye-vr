#ifdef ntohl
#undef ntohl
#endif
#ifdef ntohs
#undef ntohs
#endif

#include "net/netenet.h"
#include "net_core.h"
#include "net/netbuf.h"
#include "bondconstants.h"
#include "game/player.h"
#include "game/front.h"
#include "game/bondview.h"
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
static ENetHost *s_host = NULL;
static ENetPeer *s_server_peer = NULL; /* Used when we are a client */
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
    
    s_state = NET_STATE_HOSTING_LOBBY;
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

bool netLobbyHostLaunchMatch(void) {
    if (!netIsHost()) return false;

    int connected = 0;
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        if (s_lobby_state.slots[i].connected) {
            if (i != connected || !s_lobby_state.slots[i].ready) return false;
            connected++;
        }
    }
    if (connected < 2) return false;
    
    s_rng_seed = (uint32_t)(sysGetMicroseconds());
    if (s_rng_seed == 0) s_rng_seed = 0x12345678;
    
    extern void randomSetSeed(u32);
    randomSetSeed(s_rng_seed);
    
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
    netbufWriteU32(&buf, s_rng_seed); /* Synchronized RNG Seed */
    netbufWriteU8(&buf, (uint8_t)netGetConnectedPlayerCount());
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        netbufWriteU8(&buf, s_lobby_state.slots[i].chr_id);
    }
    
    netBroadcastBuf(&buf, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    s_state = NET_STATE_INGAME;
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

void netSendVoipChunk(const uint8_t *opus_data, uint16_t size) {
    if (s_state != NET_STATE_INGAME || size == 0 || size > GEVR_VOIP_MAX_BYTES) return;
    
    u8 raw[256];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_VOIP_FRAME);
    netbufWriteU8(&buf, (uint8_t)s_local_slot);
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
        return;
    }
    
    switch (msg_type) {
        case NET_MSG_HELLO: {
            if (!netIsHost()) break;
            char *name = netbufReadStr(&buf);
            uint8_t requested_chr = netbufReadU8(&buf);
            
            /* Find an available slot */
            int assigned = -1;
            for (int i = 1; i < GEVR_MAX_PLAYERS; i++) {
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
            NET_LOG("Assigned player '%s' to slot %d", name ? name : "Player", assigned);
            break;
        }
        case NET_MSG_WELCOME: {
            if (netIsHost()) break;
            s_local_slot = netbufReadU8(&buf);
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
                s_lobby_state.slots[i].connected = netbufReadU8(&buf);
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
            if (!netIsHost()) break;
            int slot = (int)(intptr_t)peer->data;
            uint8_t ready = netbufReadU8(&buf);
            if (slot >= 1 && slot < GEVR_MAX_PLAYERS) {
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
            if (!netIsHost()) break;
            int slot = (int)(intptr_t)peer->data;
            uint8_t chr_id = netbufReadU8(&buf);
            if (!buf.error && slot >= 1 && slot < GEVR_MAX_PLAYERS &&
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
            s_lobby_state.stage_num = netbufReadU8(&buf);
            s_lobby_state.scenario = netbufReadU8(&buf);
            s_lobby_state.weapon_set = netbufReadU8(&buf);
            uint32_t seed = netbufReadU32(&buf);
            uint8_t num_players = netbufReadU8(&buf);
            for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
                uint8_t ch = netbufReadU8(&buf);
                player_char[i] = ch;
            }
            s_rng_seed = seed;
            extern void randomSetSeed(u32);
            randomSetSeed(s_rng_seed);
            (void)num_players;
            s_state = NET_STATE_INGAME;
            NET_LOG("Starting match on stage %d! Seed: 0x%08X", s_lobby_state.stage_num, s_rng_seed);
            break;
        }
        case NET_MSG_PLAYER_STATE: {
            int slot = slot_id;
            if (slot >= 0 && slot < GEVR_MAX_PLAYERS && slot != s_local_slot) {
                struct netplayermove move;
                netbufReadPlayerMove(&buf, &move);
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
            
            if (netIsHost()) {
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
            if (target < GEVR_MAX_PLAYERS && g_playerPointers[target] != NULL) {
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
            if (netIsHost()) {
                netBroadcastPacket(data, size, NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, peer);
            }
            break;
        }
        case NET_MSG_VOIP_FRAME: {
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
    if (!s_host) return;
    
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
                if (netIsHost()) {
                    int slot = (int)(intptr_t)event.peer->data;
                    if (slot >= 1 && slot < GEVR_MAX_PLAYERS) {
                        s_client_peers[slot] = NULL;
                        s_remote_active[slot] = false;
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
                    }
                } else {
                    /* Server disconnected */
                    s_server_peer = NULL;
                    s_state = NET_STATE_OFFLINE;
                }
                event.peer->data = NULL;
                break;
            }
            case ENET_EVENT_TYPE_NONE:
            default:
                break;
        }
    }
}
