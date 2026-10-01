#include "net_ice.h"
#include "net/netenet.h"
#include "net_core.h"
#include "system.h"
#include "juice/juice.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" void netSetVirtualTransport(ENetVirtualSendCallback, ENetVirtualReceiveCallback, void *);

namespace {
constexpr int kMaxInternetPeers = 3;
constexpr size_t kMaxQueuedPackets = 256;
constexpr uint16_t kGamePort = 27007;

struct Incoming {
    ENetAddress address{};
    std::vector<char> data;
};
struct Peer {
    std::string id;
    std::string turnUser;
    std::string turnPassword;
    ENetAddress virtualAddress{};
    juice_agent_t *agent = nullptr;
    bool gathered = false;
    bool descriptionTaken = false;
    bool connected = false;
    bool failed = false;
    bool enetStarted = false;
    bool relayAvailable = false;
    bool reflexiveAvailable = false;
    std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
};

std::mutex g_mutex;
std::array<std::unique_ptr<Peer>, kMaxInternetPeers> g_peers;
std::deque<Incoming> g_incoming;
bool g_hosting = false;

Peer *findPeer(const char *id) {
    for (auto &peer : g_peers) if (peer && peer->id == id) return peer.get();
    return nullptr;
}

void onState(juice_agent_t *, juice_state_t state, void *user) {
    auto *peer = static_cast<Peer *>(user);
    std::lock_guard<std::mutex> lock(g_mutex);
    peer->connected = state == JUICE_STATE_CONNECTED || state == JUICE_STATE_COMPLETED;
    peer->failed = state == JUICE_STATE_FAILED || state == JUICE_STATE_DISCONNECTED;
}

void onGathered(juice_agent_t *, void *user) {
    auto *peer = static_cast<Peer *>(user);
    std::lock_guard<std::mutex> lock(g_mutex);
    peer->gathered = true;
}

void onReceive(juice_agent_t *, const char *data, size_t size, void *user) {
    if (!data || !size || size > ENET_PROTOCOL_MAXIMUM_MTU) return;
    auto *peer = static_cast<Peer *>(user);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_incoming.size() >= kMaxQueuedPackets) g_incoming.pop_front();
    g_incoming.push_back({peer->virtualAddress, std::vector<char>(data, data + size)});
}

int virtualSend(const ENetAddress *address, const ENetBuffer *buffers, size_t count, void *) {
    Peer *target = nullptr;
    for (auto &peer : g_peers) {
        if (peer && peer->agent && peer->virtualAddress.port == address->port &&
            std::memcmp(&peer->virtualAddress.ipv6, &address->ipv6, sizeof(address->ipv6)) == 0) {
            target = peer.get();
            break;
        }
    }
    if (!target) return -2;
    std::vector<char> packet;
    for (size_t i = 0; i < count; ++i) {
        auto *data = static_cast<const char *>(buffers[i].data);
        packet.insert(packet.end(), data, data + buffers[i].dataLength);
    }
    if (packet.size() > ENET_PROTOCOL_MAXIMUM_MTU) return -1;
    return juice_send(target->agent, packet.data(), packet.size()) == JUICE_ERR_SUCCESS
        ? static_cast<int>(packet.size()) : -1;
}

int virtualReceive(ENetAddress *address, ENetBuffer *buffer, void *) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_incoming.empty()) return 0;
    Incoming packet = std::move(g_incoming.front());
    g_incoming.pop_front();
    if (packet.data.size() > buffer->dataLength) return -2;
    *address = packet.address;
    std::memcpy(buffer->data, packet.data.data(), packet.data.size());
    return static_cast<int>(packet.data.size());
}

bool createPeer(Peer *peer, const char *offer) {
    // TURN is the fallback for peers that STUN hole punching cannot reach (symmetric
    // and carrier-grade NAT). Cloudflare answers TURN over UDP on 3478 and 443; the
    // second entry covers networks that block 3478. libjuice speaks UDP only, so
    // there is no TCP or TLS fallback here. Without credentials (the lobby service
    // refused or ran out of relay budget) the session gathers STUN candidates only.
    juice_turn_server_t turn[2]{};
    for (auto &server : turn) {
        server.host = "turn.cloudflare.com";
        server.username = peer->turnUser.c_str();
        server.password = peer->turnPassword.c_str();
    }
    turn[0].port = 3478;
    turn[1].port = 443;
    const bool haveTurn = !peer->turnUser.empty() && !peer->turnPassword.empty();
    juice_config_t config{};
    config.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;
    config.stun_server_host = "stun.cloudflare.com";
    config.stun_server_port = 3478;
    config.turn_servers = haveTurn ? turn : nullptr;
    config.turn_servers_count = haveTurn ? 2 : 0;
    config.cb_state_changed = onState;
    config.cb_gathering_done = onGathered;
    config.cb_recv = onReceive;
    config.user_ptr = peer;
    peer->agent = juice_create(&config);
    if (!peer->agent) return false;
    if (offer && juice_set_remote_description(peer->agent, offer) != JUICE_ERR_SUCCESS) return false;
    return juice_gather_candidates(peer->agent) == JUICE_ERR_SUCCESS;
}

void destroyPeers() {
    netSetVirtualTransport(nullptr, nullptr, nullptr);
    for (auto &peer : g_peers) {
        if (peer && peer->agent) juice_destroy(peer->agent);
        peer.reset();
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_incoming.clear();
}
}

extern "C" void netIceStartHost(void) {
    destroyPeers();
    g_hosting = true;
    netSetVirtualTransport(virtualSend, virtualReceive, nullptr);
}

extern "C" bool netIceStartClient(const char *id, const char *turnUser, const char *turnPassword) {
    if (!id || !turnUser || !turnPassword) return false;
    destroyPeers();
    g_hosting = false;
    auto peer = std::make_unique<Peer>();
    peer->id = id;
    peer->turnUser = turnUser;
    peer->turnPassword = turnPassword;
    enet_address_set_ip(&peer->virtualAddress, "127.100.0.1");
    peer->virtualAddress.port = kGamePort;
    if (!createPeer(peer.get(), nullptr)) {
        if (peer->agent) juice_destroy(peer->agent);
        return false;
    }
    g_peers[0] = std::move(peer);
    netSetVirtualTransport(virtualSend, virtualReceive, nullptr);
    return true;
}

extern "C" bool netIceAddHostPeer(const char *id, const char *offer, const char *turnUser, const char *turnPassword) {
    if (!g_hosting || !id || !offer || !turnUser || !turnPassword || findPeer(id)) return false;
    for (int i = 0; i < kMaxInternetPeers; ++i) {
        if (g_peers[i]) continue;
        auto peer = std::make_unique<Peer>();
        peer->id = id;
        peer->turnUser = turnUser;
        peer->turnPassword = turnPassword;
        char address[32];
        std::snprintf(address, sizeof(address), "127.100.0.%d", i + 2);
        enet_address_set_ip(&peer->virtualAddress, address);
        peer->virtualAddress.port = kGamePort;
        if (!createPeer(peer.get(), offer)) {
            if (peer->agent) juice_destroy(peer->agent);
            return false;
        }
        g_peers[i] = std::move(peer);
        return true;
    }
    return false;
}

extern "C" bool netIceApplyAnswer(const char *id, const char *answer) {
    Peer *peer = id ? findPeer(id) : nullptr;
    return peer && answer && juice_set_remote_description(peer->agent, answer) == JUICE_ERR_SUCCESS;
}

extern "C" bool netIceTakeDescription(const char *id, char *out, size_t capacity) {
    Peer *peer = id ? findPeer(id) : nullptr;
    if (!peer || !out || capacity < JUICE_MAX_SDP_STRING_LEN) return false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!peer->gathered || peer->descriptionTaken) return false;
        peer->descriptionTaken = true;
    }
    if (juice_get_local_description(peer->agent, out, capacity) == JUICE_ERR_SUCCESS) {
        // Publish once the peer has some path to the internet: a server-reflexive
        // candidate (STUN hole punching) or a relay. TURN is a fallback, not a gate,
        // so a blocked relay port no longer fails a join that could have gone direct.
        // Host-only candidates mean STUN itself failed; that session would depend on
        // router forwarding, so it is not advertised.
        const bool relay = std::strstr(out, " typ relay") != nullptr;
        const bool reflexive = std::strstr(out, " typ srflx") != nullptr;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            peer->relayAvailable = relay;
            peer->reflexiveAvailable = reflexive;
        }
        if (!relay)
            sysLogPrintf(LOG_NOTE, "net: ice %s: no relay candidate, %s", peer->id.c_str(),
                         reflexive ? "direct path only" : "no internet path");
        return relay || reflexive;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    peer->descriptionTaken = false;
    return false;
}

extern "C" const char *netIceStatus(const char *id) {
    Peer *peer = id ? findPeer(id) : nullptr;
    if (!peer) return "Internet connection ended";
    std::lock_guard<std::mutex> lock(g_mutex);
    if (peer->failed) return "Internet connection failed";
    if (peer->gathered && peer->descriptionTaken && !peer->relayAvailable && !peer->reflexiveAvailable)
        return "No internet path found; check your connection";
    if (!g_hosting && !peer->connected && std::chrono::steady_clock::now() - peer->created > std::chrono::seconds(45))
        return "Internet connection timed out";
    return nullptr;
}

extern "C" void netIcePoll(void) {
    if (g_hosting) {
        for (auto &peer : g_peers) {
            if (!peer) continue;
            bool failed, connected;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                failed = peer->failed;
                connected = peer->connected;
            }
            const bool timedOut = !connected && std::chrono::steady_clock::now() - peer->created > std::chrono::seconds(90);
            if (failed || timedOut) {
                if (peer->agent) juice_destroy(peer->agent);
                peer.reset();
            }
        }
        return;
    }
    Peer *peer = g_peers[0].get();
    if (!peer) return;
    bool connected;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        connected = peer->connected;
    }
    if (connected && !peer->enetStarted) {
        peer->enetStarted = true;
        netConnect("127.100.0.1", kGamePort);
    }
}

extern "C" void netIceStop(void) {
    destroyPeers();
    g_hosting = false;
}

extern "C" int netIcePeerCount(void) {
    int count = 0;
    for (const auto &peer : g_peers) if (peer) ++count;
    return count;
}

extern "C" void netIceForgetPeer(const char *virtualIp) {
    if (!virtualIp) return;
    for (auto &peer : g_peers) {
        if (!peer) continue;
        char address[64];
        enet_address_get_ip(&peer->virtualAddress, address, sizeof(address));
        if (std::strcmp(address, virtualIp) != 0) continue;
        if (peer->agent) juice_destroy(peer->agent);
        peer.reset();
        break;
    }
}
