#ifndef _NET_DISCOVERY_H
#define _NET_DISCOVERY_H

#include <stdbool.h>
#include <stdint.h>
#include "net_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_DISCOVERY_PORT 27008
#define GEVR_MAX_LAN_SERVERS 8

typedef struct {
    char     host_ip[32];
    uint16_t port;
    char     server_name[GEVR_MAX_NAME_LEN];
    uint8_t  player_count;
    uint8_t  max_players;
    uint8_t  stage_num;
    uint8_t  weapon_set;
    uint8_t  phase;
    uint8_t  joinable;
    uint32_t last_seen_ms;
} NetDiscoveredServer;

bool netDiscoveryInit(void);
void netDiscoveryShutdown(void);
void netDiscoveryUpdate(uint32_t current_time_ms);

/* Host beacon control */
void netDiscoveryStartBroadcasting(const char *server_name, uint16_t game_port);
void netDiscoveryStopBroadcasting(void);

/* Client LAN list query */
int netDiscoveryGetServerCount(void);
const NetDiscoveredServer *netDiscoveryGetServer(int index);

#ifdef __cplusplus
}
#endif

#endif /* _NET_DISCOVERY_H */
