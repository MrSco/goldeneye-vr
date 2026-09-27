#ifndef _NET_CORE_H
#define _NET_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "net_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NET_STATE_OFFLINE = 0,
    NET_STATE_HOSTING_LOBBY,
    NET_STATE_CONNECTING,
    NET_STATE_CLIENT_LOBBY,
    NET_STATE_INGAME
} NetState;

/* Lifecycle */
bool netInit(void);
void netShutdown(void);
void netPoll(void);

/* Session Management */
bool netHostStart(uint16_t port);
bool netConnect(const char *host_addr, uint16_t port);
void netDisconnect(void);

/* Status Queries */
NetState netGetState(void);
bool netIsActive(void);
bool netIsHost(void);
int netGetLocalSlot(void);
int netGetConnectedPlayerCount(void);
const NetMsgLobbyState *netGetLobbyState(void);

/* Lobby Operations */
void netLobbySetReady(bool ready);
void netLobbySetCharacter(uint8_t chr_id);
void netLobbySetMatchConfig(uint8_t stage_num, uint8_t scenario, uint8_t weapon_set);
bool netLobbyHostLaunchMatch(void);

/* Gameplay State Sending */
void netSendLocalPlayerState(const NetMsgPlayerState *state);
void netSendHitReport(uint8_t target_slot, uint8_t weapon_id, uint8_t hit_part, float hit_x, float hit_y, float hit_z, float dmg);
void netSendFireEvent(uint8_t weapon_id);

/* Remote State Retrieval */
const NetMsgPlayerState *netGetRemotePlayerState(int slot_id);
bool netIsRemotePlayerActive(int slot_id);

/* VoIP */
void netSendVoipChunk(const uint8_t *opus_data, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif /* _NET_CORE_H */
