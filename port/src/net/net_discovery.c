#include "net_discovery.h"
#include "net_core.h"
#include <stdio.h>
#include <string.h>

#ifdef ntohl
#undef ntohl
#endif
#ifdef ntohs
#undef ntohs
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define CLOSE_SOCKET closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#define CLOSE_SOCKET close
#define SOCKET int
#define INVALID_SOCKET -1
#endif

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;      /* GEVR_NET_MAGIC */
    uint16_t version;    /* GEVR_NET_VERSION */
    uint16_t port;
    char     name[GEVR_MAX_NAME_LEN];
    uint8_t  player_count;
    uint8_t  max_players;
    uint8_t  stage_num;
} NetDiscoveryBeacon;
#pragma pack(pop)

static SOCKET s_disc_socket = INVALID_SOCKET;
static bool s_broadcasting = false;
static uint16_t s_broadcast_port = GEVR_DEFAULT_PORT;
static char s_broadcast_name[GEVR_MAX_NAME_LEN] = "GoldenEye VR Host";
static uint32_t s_last_broadcast_ms = 0;

static NetDiscoveredServer s_servers[GEVR_MAX_LAN_SERVERS];
static int s_server_count = 0;

bool netDiscoveryInit(void) {
    if (s_disc_socket != INVALID_SOCKET) return true;
    
    s_disc_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_disc_socket == INVALID_SOCKET) return false;
    
    /* Enable broadcast */
    int broadcast_enable = 1;
    setsockopt(s_disc_socket, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast_enable, sizeof(broadcast_enable));
    
    /* Allow address reuse */
    int reuse = 1;
#ifdef SO_REUSEADDR
    setsockopt(s_disc_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
#endif
#ifdef SO_REUSEPORT
    setsockopt(s_disc_socket, SOL_SOCKET, SO_REUSEPORT, (const char *)&reuse, sizeof(reuse));
#endif
    
    /* Non-blocking socket */
#ifdef _WIN32
    u_long non_blocking = 1;
    ioctlsocket(s_disc_socket, FIONBIO, &non_blocking);
#else
    int flags = fcntl(s_disc_socket, F_GETFL, 0);
    fcntl(s_disc_socket, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_in recv_addr;
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(GEVR_DISCOVERY_PORT);
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    
    bind(s_disc_socket, (struct sockaddr *)&recv_addr, sizeof(recv_addr));
    
    s_server_count = 0;
    return true;
}

void netDiscoveryShutdown(void) {
    if (s_disc_socket != INVALID_SOCKET) {
        CLOSE_SOCKET(s_disc_socket);
        s_disc_socket = INVALID_SOCKET;
    }
    s_broadcasting = false;
    s_server_count = 0;
}

void netDiscoveryStartBroadcasting(const char *server_name, uint16_t game_port) {
    s_broadcasting = true;
    s_broadcast_port = game_port ? game_port : GEVR_DEFAULT_PORT;
    if (server_name) {
        snprintf(s_broadcast_name, GEVR_MAX_NAME_LEN, "%s", server_name);
    }
}

void netDiscoveryStopBroadcasting(void) {
    s_broadcasting = false;
}

void netDiscoveryUpdate(uint32_t current_time_ms) {
    if (s_disc_socket == INVALID_SOCKET) return;
    
    /* 1. Broadcast beacon if hosting */
    if (s_broadcasting && (current_time_ms - s_last_broadcast_ms >= 1000)) {
        s_last_broadcast_ms = current_time_ms;
        
        NetDiscoveryBeacon beacon;
        beacon.magic = GEVR_NET_MAGIC;
        beacon.version = GEVR_NET_VERSION;
        beacon.port = s_broadcast_port;
        snprintf(beacon.name, GEVR_MAX_NAME_LEN, "%s", s_broadcast_name);
        beacon.player_count = (uint8_t)netGetConnectedPlayerCount();
        beacon.max_players = GEVR_MAX_PLAYERS;
        beacon.stage_num = 0x1B;
        
        struct sockaddr_in broadcast_addr;
        memset(&broadcast_addr, 0, sizeof(broadcast_addr));
        broadcast_addr.sin_family = AF_INET;
        broadcast_addr.sin_port = htons(GEVR_DISCOVERY_PORT);
        broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
        
        sendto(s_disc_socket, (const char *)&beacon, sizeof(beacon), 0,
               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
    }
    
    /* 2. Receive incoming beacons */
    char buffer[256];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);
    
    int bytes = recvfrom(s_disc_socket, buffer, sizeof(buffer), 0,
                         (struct sockaddr *)&sender_addr, &addr_len);
    while (bytes >= (int)sizeof(NetDiscoveryBeacon)) {
        const NetDiscoveryBeacon *b = (const NetDiscoveryBeacon *)buffer;
        if (b->magic == GEVR_NET_MAGIC && b->version == GEVR_NET_VERSION) {
            char sender_ip[32];
            inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, sizeof(sender_ip));
            
            /* Update existing or add new server */
            int found_idx = -1;
            for (int i = 0; i < s_server_count; i++) {
                if (strcmp(s_servers[i].host_ip, sender_ip) == 0 && s_servers[i].port == b->port) {
                    found_idx = i;
                    break;
                }
            }
            
            if (found_idx < 0 && s_server_count < GEVR_MAX_LAN_SERVERS) {
                found_idx = s_server_count++;
            }
            
            if (found_idx >= 0) {
                NetDiscoveredServer *srv = &s_servers[found_idx];
                snprintf(srv->host_ip, sizeof(srv->host_ip), "%s", sender_ip);
                srv->port = b->port;
                snprintf(srv->server_name, sizeof(srv->server_name), "%s", b->name);
                srv->player_count = b->player_count;
                srv->max_players = b->max_players;
                srv->stage_num = b->stage_num;
                srv->last_seen_ms = current_time_ms;
            }
        }
        
        bytes = recvfrom(s_disc_socket, buffer, sizeof(buffer), 0,
                         (struct sockaddr *)&sender_addr, &addr_len);
    }
    
    /* 3. Prune servers older than 5 seconds */
    for (int i = 0; i < s_server_count; i++) {
        if (current_time_ms - s_servers[i].last_seen_ms > 5000) {
            s_servers[i] = s_servers[s_server_count - 1];
            s_server_count--;
            i--;
        }
    }
}

int netDiscoveryGetServerCount(void) {
    return s_server_count;
}

const NetDiscoveredServer *netDiscoveryGetServer(int index) {
    if (index < 0 || index >= s_server_count) return NULL;
    return &s_servers[index];
}
