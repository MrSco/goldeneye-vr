#ifdef ntohl
#undef ntohl
#endif
#ifdef ntohs
#undef ntohs
#endif

#include "net_core.h"
#include <enet/enet.h>
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
static NetMsgPlayerState s_remote_players[GEVR_MAX_PLAYERS];
static bool s_remote_active[GEVR_MAX_PLAYERS];

/* Current Lobby State */
static NetMsgLobbyState s_lobby_state;

/* ENet Peer to slot mapping on host */
static ENetPeer *s_client_peers[GEVR_MAX_PLAYERS];

static void netResetLobbyState(void) {
    memset(&s_lobby_state, 0, sizeof(s_lobby_state));
    s_lobby_state.header.magic = GEVR_NET_MAGIC;
    s_lobby_state.header.version = GEVR_NET_VERSION;
    s_lobby_state.header.msg_type = NET_MSG_LOBBY_STATE;
    s_lobby_state.stage_num = 0x1B; /* Facility default (LEVELID_SEV_FACILITY) */
    s_lobby_state.scenario = 0;     /* Normal Deathmatch */
    s_lobby_state.weapon_set = 0;   /* Standard weapons */
    
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        s_remote_active[i] = false;
        s_client_peers[i] = NULL;
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
    address.host = ENET_HOST_ANY;
    address.port = port ? port : GEVR_DEFAULT_PORT;
    
    s_host = enet_host_create(&address, GEVR_MAX_PLAYERS, NET_CHAN_MAX, 0, 0);
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
    
    NET_LOG("Multiplayer server hosted on port %d", address.port);
    return true;
}

bool netConnect(const char *host_addr, uint16_t port) {
    if (!s_initialized && !netInit()) return false;
    netDisconnect();
    
    s_host = enet_host_create(NULL, 1, NET_CHAN_MAX, 0, 0);
    if (!s_host) {
        NET_ERR("Failed to create client ENet host!");
        return false;
    }
    
    ENetAddress address;
    enet_address_set_host(&address, host_addr ? host_addr : "127.0.0.1");
    address.port = port ? port : GEVR_DEFAULT_PORT;
    
    s_server_peer = enet_host_connect(s_host, &address, NET_CHAN_MAX, 0);
    if (!s_server_peer) {
        NET_ERR("Failed to initiate connection to %s:%d", host_addr, address.port);
        enet_host_destroy(s_host);
        s_host = NULL;
        return false;
    }
    
    s_state = NET_STATE_CONNECTING;
    s_local_slot = -1;
    netResetLobbyState();
    NET_LOG("Connecting to host at %s:%d...", host_addr, address.port);
    return true;
}

void netDisconnect(void) {
    if (s_server_peer) {
        enet_peer_disconnect(s_server_peer, 0);
        enet_host_flush(s_host);
        s_server_peer = NULL;
    }
    
    if (s_host) {
        enet_host_destroy(s_host);
        s_host = NULL;
    }
    
    s_state = NET_STATE_OFFLINE;
    s_local_slot = -1;
    netResetLobbyState();
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

static void netBroadcastPacket(const void *data, size_t size, uint8_t channel, uint32_t flags, ENetPeer *exclude_peer) {
    if (!s_host) return;
    
    ENetPacket *packet = enet_packet_create(data, size, flags);
    if (!packet) return;
    
    if (netIsHost()) {
        for (int i = 1; i < GEVR_MAX_PLAYERS; i++) {
            if (s_client_peers[i] && s_client_peers[i] != exclude_peer) {
                enet_peer_send(s_client_peers[i], channel, packet);
            }
        }
    } else if (s_server_peer) {
        enet_peer_send(s_server_peer, channel, packet);
    }
}

void netLobbySetReady(bool ready) {
    if (s_local_slot < 0 || s_local_slot >= GEVR_MAX_PLAYERS) return;
    s_lobby_state.slots[s_local_slot].ready = ready ? 1 : 0;
    
    if (netIsHost()) {
        netBroadcastPacket(&s_lobby_state, sizeof(s_lobby_state), NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    } else if (s_server_peer) {
        NetHeader hdr = { GEVR_NET_MAGIC, GEVR_NET_VERSION, NET_MSG_LOBBY_READY, (uint8_t)s_local_slot };
        enet_peer_send(s_server_peer, NET_CHAN_RELIABLE, enet_packet_create(&hdr, sizeof(hdr), ENET_PACKET_FLAG_RELIABLE));
    }
}

void netLobbySetCharacter(uint8_t chr_id) {
    if (s_local_slot < 0 || s_local_slot >= GEVR_MAX_PLAYERS) return;
    s_lobby_state.slots[s_local_slot].chr_id = chr_id;
    if (netIsHost()) {
        netBroadcastPacket(&s_lobby_state, sizeof(s_lobby_state), NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    }
}

void netLobbySetMatchConfig(uint8_t stage_num, uint8_t scenario, uint8_t weapon_set) {
    if (!netIsHost()) return;
    s_lobby_state.stage_num = stage_num;
    s_lobby_state.scenario = scenario;
    s_lobby_state.weapon_set = weapon_set;
    netBroadcastPacket(&s_lobby_state, sizeof(s_lobby_state), NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

bool netLobbyHostLaunchMatch(void) {
    if (!netIsHost()) return false;
    
    NetMsgStartMatch launch;
    launch.header.magic = GEVR_NET_MAGIC;
    launch.header.version = GEVR_NET_VERSION;
    launch.header.msg_type = NET_MSG_START_MATCH;
    launch.header.slot_id = 0;
    launch.stage_num = s_lobby_state.stage_num;
    launch.scenario = s_lobby_state.scenario;
    launch.weapon_set = s_lobby_state.weapon_set;
    launch.random_seed = 0x12345678;
    
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        launch.start_pad[i] = (uint8_t)i;
    }
    
    netBroadcastPacket(&launch, sizeof(launch), NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
    s_state = NET_STATE_INGAME;
    NET_LOG("Match launched! Stage: %d, Players: %d", launch.stage_num, netGetConnectedPlayerCount());
    return true;
}

void netSendLocalPlayerState(const NetMsgPlayerState *state) {
    if (s_state != NET_STATE_INGAME || s_local_slot < 0) return;
    
    NetMsgPlayerState msg = *state;
    msg.header.magic = GEVR_NET_MAGIC;
    msg.header.version = GEVR_NET_VERSION;
    msg.header.msg_type = NET_MSG_PLAYER_STATE;
    msg.header.slot_id = (uint8_t)s_local_slot;
    
    netBroadcastPacket(&msg, sizeof(msg), NET_CHAN_PLAYER_STATE, ENET_PACKET_FLAG_UNSEQUENCED, NULL);
}

const NetMsgPlayerState *netGetRemotePlayerState(int slot_id) {
    if (slot_id < 0 || slot_id >= GEVR_MAX_PLAYERS) return NULL;
    return &s_remote_players[slot_id];
}

bool netIsRemotePlayerActive(int slot_id) {
    if (slot_id < 0 || slot_id >= GEVR_MAX_PLAYERS || slot_id == s_local_slot) return false;
    return s_remote_active[slot_id];
}

void netSendHitReport(uint8_t target_slot, uint8_t weapon_id, uint8_t hit_part, float hit_x, float hit_y, float hit_z, float dmg) {
    if (s_state != NET_STATE_INGAME) return;
    
    NetMsgHitReport hit;
    hit.header.magic = GEVR_NET_MAGIC;
    hit.header.version = GEVR_NET_VERSION;
    hit.header.msg_type = NET_MSG_HIT_REPORT;
    hit.header.slot_id = (uint8_t)s_local_slot;
    hit.target_slot = target_slot;
    hit.weapon_id = weapon_id;
    hit.hit_location = hit_part;
    hit.hit_x = hit_x; hit.hit_y = hit_y; hit.hit_z = hit_z;
    hit.damage = dmg;
    
    netBroadcastPacket(&hit, sizeof(hit), NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

void netSendFireEvent(uint8_t weapon_id) {
    if (s_state != NET_STATE_INGAME) return;
    
    NetHeader hdr;
    hdr.magic = GEVR_NET_MAGIC;
    hdr.version = GEVR_NET_VERSION;
    hdr.msg_type = NET_MSG_FIRE_EVENT;
    hdr.slot_id = (uint8_t)s_local_slot;
    
    netBroadcastPacket(&hdr, sizeof(hdr), NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
}

void netSendVoipChunk(const uint8_t *opus_data, uint16_t size) {
    if (s_state != NET_STATE_INGAME || size == 0 || size > GEVR_VOIP_MAX_BYTES) return;
    
    NetMsgVoipFrame voip;
    voip.header.magic = GEVR_NET_MAGIC;
    voip.header.version = GEVR_NET_VERSION;
    voip.header.msg_type = NET_MSG_VOIP_FRAME;
    voip.header.slot_id = (uint8_t)s_local_slot;
    voip.payload_size = size;
    memcpy(voip.payload, opus_data, size);
    
    netBroadcastPacket(&voip, sizeof(voip), NET_CHAN_VOIP, ENET_PACKET_FLAG_UNSEQUENCED, NULL);
}

static void netHandlePacket(ENetPeer *peer, const uint8_t *data, size_t size) {
    if (size < sizeof(NetHeader)) return;
    
    const NetHeader *hdr = (const NetHeader *)data;
    if (hdr->magic != GEVR_NET_MAGIC || hdr->version != GEVR_NET_VERSION) {
        NET_ERR("Ignored packet with invalid magic/version");
        return;
    }
    
    switch (hdr->msg_type) {
        case NET_MSG_HELLO: {
            if (!netIsHost()) break;
            const NetMsgHello *hello = (const NetMsgHello *)data;
            
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
            s_lobby_state.slots[assigned].chr_id = hello->requested_chr_id;
            snprintf(s_lobby_state.slots[assigned].name, GEVR_MAX_NAME_LEN, "%s", hello->player_name);
            
            /* Send welcome to client */
            NetMsgWelcome welcome;
            welcome.header.magic = GEVR_NET_MAGIC;
            welcome.header.version = GEVR_NET_VERSION;
            welcome.header.msg_type = NET_MSG_WELCOME;
            welcome.header.slot_id = 0;
            welcome.assigned_slot = (uint8_t)assigned;
            welcome.stage_num = s_lobby_state.stage_num;
            welcome.scenario = s_lobby_state.scenario;
            welcome.weapon_set = s_lobby_state.weapon_set;
            enet_peer_send(peer, NET_CHAN_RELIABLE, enet_packet_create(&welcome, sizeof(welcome), ENET_PACKET_FLAG_RELIABLE));
            
            /* Broadcast updated lobby state */
            netBroadcastPacket(&s_lobby_state, sizeof(s_lobby_state), NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
            NET_LOG("Assigned player '%s' to slot %d", hello->player_name, assigned);
            break;
        }
        case NET_MSG_WELCOME: {
            if (netIsHost()) break;
            const NetMsgWelcome *welcome = (const NetMsgWelcome *)data;
            s_local_slot = welcome->assigned_slot;
            s_state = NET_STATE_CLIENT_LOBBY;
            NET_LOG("Connected! Assigned local slot: %d", s_local_slot);
            break;
        }
        case NET_MSG_LOBBY_STATE: {
            if (netIsHost()) break;
            if (size >= sizeof(NetMsgLobbyState)) {
                memcpy(&s_lobby_state, data, sizeof(NetMsgLobbyState));
            }
            break;
        }
        case NET_MSG_LOBBY_READY: {
            if (!netIsHost()) break;
            int slot = (int)(intptr_t)peer->data;
            if (slot >= 1 && slot < GEVR_MAX_PLAYERS) {
                s_lobby_state.slots[slot].ready = !s_lobby_state.slots[slot].ready;
                netBroadcastPacket(&s_lobby_state, sizeof(s_lobby_state), NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
            }
            break;
        }
        case NET_MSG_START_MATCH: {
            if (size >= sizeof(NetMsgStartMatch)) {
                const NetMsgStartMatch *launch = (const NetMsgStartMatch *)data;
                s_state = NET_STATE_INGAME;
                NET_LOG("Starting match on stage %d!", launch->stage_num);
            }
            break;
        }
        case NET_MSG_PLAYER_STATE: {
            if (size >= sizeof(NetMsgPlayerState)) {
                const NetMsgPlayerState *ps = (const NetMsgPlayerState *)data;
                int slot = ps->header.slot_id;
                if (slot >= 0 && slot < GEVR_MAX_PLAYERS && slot != s_local_slot) {
                    s_remote_players[slot] = *ps;
                    s_remote_active[slot] = true;
                    
                    /* If host, relay to other peers */
                    if (netIsHost()) {
                        netBroadcastPacket(data, size, NET_CHAN_PLAYER_STATE, ENET_PACKET_FLAG_UNSEQUENCED, peer);
                    }
                }
            }
            break;
        }
        case NET_MSG_HIT_REPORT: {
            if (netIsHost()) {
                const NetMsgHitReport *hit = (const NetMsgHitReport *)data;
                NET_LOG("Hit reported: shooter %d -> target %d (dmg: %.1f)", hit->header.slot_id, hit->target_slot, hit->damage);
                
                /* Host resolves damage and broadcasts damage event */
                NetMsgDamageEvent dmg;
                dmg.header.magic = GEVR_NET_MAGIC;
                dmg.header.version = GEVR_NET_VERSION;
                dmg.header.msg_type = NET_MSG_DAMAGE_EVENT;
                dmg.header.slot_id = 0;
                dmg.target_slot = hit->target_slot;
                dmg.attacker_slot = hit->header.slot_id;
                dmg.weapon_id = hit->weapon_id;
                dmg.damage = hit->damage;
                dmg.new_health = 0.0f; /* Will be hooked to engine health */
                dmg.is_dead = 0;
                netBroadcastPacket(&dmg, sizeof(dmg), NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
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
                NET_LOG("A peer connected from %x:%u", event.peer->address.host, event.peer->address.port);
                if (!netIsHost()) {
                    /* We connected to host as client: send HELLO */
                    NetMsgHello hello;
                    hello.header.magic = GEVR_NET_MAGIC;
                    hello.header.version = GEVR_NET_VERSION;
                    hello.header.msg_type = NET_MSG_HELLO;
                    hello.header.slot_id = 0xFF;
                    snprintf(hello.player_name, GEVR_MAX_NAME_LEN, "Agent 007");
                    hello.requested_chr_id = 0;
                    enet_peer_send(event.peer, NET_CHAN_RELIABLE, enet_packet_create(&hello, sizeof(hello), ENET_PACKET_FLAG_RELIABLE));
                }
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                netHandlePacket(event.peer, event.packet->data, event.packet->dataLength);
                enet_packet_destroy(event.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                NET_LOG("A peer disconnected.");
                if (netIsHost()) {
                    int slot = (int)(intptr_t)event.peer->data;
                    if (slot >= 1 && slot < GEVR_MAX_PLAYERS) {
                        s_client_peers[slot] = NULL;
                        s_lobby_state.slots[slot].connected = 0;
                        s_lobby_state.slots[slot].ready = 0;
                        s_remote_active[slot] = false;
                        netBroadcastPacket(&s_lobby_state, sizeof(s_lobby_state), NET_CHAN_RELIABLE, ENET_PACKET_FLAG_RELIABLE, NULL);
                    }
                } else if (event.peer == s_server_peer) {
                    NET_LOG("Disconnected from server.");
                    s_server_peer = NULL;
                    s_state = NET_STATE_OFFLINE;
                }
                event.peer->data = NULL;
                break;
            }
            default:
                break;
        }
    }
}
