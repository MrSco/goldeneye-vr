#ifndef _NET_CORE_H
#define _NET_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "net_protocol.h"
#include "net_match.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NET_STATE_OFFLINE = 0,
    NET_STATE_HOSTING_LOBBY,
    NET_STATE_CONNECTING,
    NET_STATE_CLIENT_LOBBY,
    NET_STATE_INGAME,
    NET_STATE_MIGRATING     /* the host left mid-match: taking over, or rejoining the new host */
} NetState;

typedef enum {
    NET_PHASE_WAITING = 0,
    NET_PHASE_WARMUP = 1,
    NET_PHASE_IN_PROGRESS = 2
} NetPhase;

/* Lifecycle */
bool netInit(void);
void netShutdown(void);
void netPoll(void);
/* Local host policy, persisted on this headset; no additional wire fields. */
void netSetHostEqualization(int enabled, unsigned cap_ms);
int netGetHostEqualization(unsigned *cap_ms);
unsigned netGetSlotHostDelayMs(int slot);
void netHostEqualizationText(char *text, unsigned size);
void netBeginLocalShot(void);
void netEndLocalShot(void);

/* Session Management */
bool netHostStart(uint16_t port);
bool netConnect(const char *host_addr, uint16_t port);
void netDisconnect(void);

/* Status Queries */
NetState netGetState(void);
bool netIsActive(void);
bool netIsHost(void);
int netGetLocalSlot(void);
int netGetHostSlot(void);
int netGetConnectedPlayerCount(void);
int netGetMaxPlayers(void);
int netLobbyMinPlayers(void);   /* the fewest players the host can pick with this lobby */
NetPhase netGetPhase(void);
bool netSlotOccupied(int slot);
bool netTakeRoundReset(void);
void netStageLoaded(void);
uint64_t netGetCountdownEndUs(void);   /* the next round's start, sysGetMicroseconds clock; 0 none */
bool netTakeStageFadeIn(void);         /* once after each online stage load */
void netHostRoundEnded(void);
void netHostContinue(void);
void netHostReturnToLobby(void);
void netHostStartRoundNow(void);
int netCountdownSecondsLeft(void);
void netHostRequestVotes(void);
int netHostStartRequested(void);
int netWarmupSecondsLeft(void);
void netRoundNoticeText(char *text, unsigned size);
int netHostKickPlayer(int slot);
int netHostCanKickPlayer(int slot);
int netLobbySlotConnected(int slot);
void netTouchLocalActivity(void);
const NetMsgLobbyState *netGetLobbyState(void);
const char *netGetSlotName(int slot);   /* NULL for an empty slot */
void netSetLocalAppVersion(const char *version);
const char *netGetSlotAppVersion(int slot); /* NULL or empty if not announced */
uint8_t netGetLobbyStage(void);
uint8_t netGetLobbyWeaponSet(void);
uint32_t netGetRandomSeed(void);

/* Lobby Operations */
void netLobbySetReady(bool ready);
void netLobbySetCharacter(uint8_t chr_id);
void netSetPreferredCharacter(uint8_t chr_id);
void netLobbySetLoadout(const uint8_t items[4]);   /* my four spawn guns */
bool netLobbyHostLaunchMatch(void);
int netLobbyCanLaunch(void);

/* The match config (net_protocol.h NetMatchConfig) */
const NetMatchConfig *netGetMatchConfig(void);
const NetMatchConfig *netGetActiveMatchConfig(void);
void netLobbySetConfig(const NetMatchConfig *config);   /* host: kept and told to everyone */
void netApplyMatchConfig(void);             /* every headset, before each stage load */
int netGetPlayingCount(void);               /* connected and not spectating */
int netCoopActive(void);                    /* online in a co-op mission (NetMatchConfig.mode) */
int netCoopSession(void);                   /* a co-op party, in its menus or a mission */
void netCoopMissionEnded(int result);       /* boss.c bossReturnTitleStage: the mission ended here (NET_COOP_RESULT_*) */
int netMpPlayerCount(int fallback);         /* the game's player_count online: the humans in the round */
bool netSlotIsSpectator(int slot);
bool netLocalIsSpectator(void);
int netVoiceSameGroup(int a, int b);
int netVoiceModeForPair(int a, int b);
void netSendSpecialTaken(s32 item);         /* the local player took the flag or the Golden Gun */

/* Gameplay State Sending */
void netSendLocalPlayerMove(const struct netplayermove *move);
void netSendLocalPlayerState(const NetMsgPlayerState *state);
void netSendHitReport(uint8_t target_slot, uint8_t weapon_id, uint8_t hit_part, float hit_x, float hit_y, float hit_z, float dmg);
void netSendWorldHitReport(uint8_t target_slot, uint8_t weapon_id, float hit_x, float hit_y, float hit_z, float dmg);
void netSendRespawnEvent(uint8_t pad_index, float theta);
void netSendFireEvent(uint8_t weapon_id);

/* Remote State Retrieval */
const struct netplayermove *netGetRemotePlayerMove(int slot_id);
const NetMsgPlayerState *netGetRemotePlayerState(int slot_id);
bool netIsRemotePlayerActive(int slot_id);
int netLastAttacker(int slot);   /* the last slot whose damage this headset applied to slot's player, or slot itself */
int netGetRemoteAim(int slot_id, int hand, coord3d *origin, coord3d *dir);   /* 1 with the owner's world-space barrel */

/* VoIP */
void netSendVoipChunk(uint32_t sequence, const uint8_t *opus_data, uint16_t size);

/* Ballots (mpmenu.c NEXT MAP / NEXT WEAPONS rows); kind is NET_BALLOT_* */
void netSetLocalVote(int kind, int idx);    /* -1: no vote */
int netGetVote(int kind, int slot);         /* -1 none */

/* Host migration (vr_launcher.cpp gevrLobbyGameTick drives the transports) */
void netSetGameName(const char *name);              /* the LAN beacon's name, shared with the clients */
void netSetLobbyHandoff(const char *code, const char *token);   /* the internet lobby, shared with the clients */
const char *netGetGameName(void);
const char *netGetLobbyCode(void);
const char *netGetLobbyToken(void);
int netGetLivePlayerCount(void);            /* connected players with a live connection (the lobby service's count) */
bool netTakeHostTakeover(void);             /* once: this headset was elected host; set the transport, then netHostTakeOver */
bool netHostTakeOver(uint16_t port);
bool netMigrationWantsRejoin(const char **old_host_ip);   /* a client still looking for the new host */
bool netMigrationConnecting(void);          /* its ENet connection is under way */
void netMigrationGiveUp(void);

#ifdef __cplusplus
}
#endif

#endif /* _NET_CORE_H */
