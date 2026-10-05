/* Integer-only bridge for the original game C headers (which define bool). */
#ifndef GEVR_NET_GAME_H
#define GEVR_NET_GAME_H
#include "net_match.h"
#include "net_rules.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { CFG_STAGE, CFG_SCENARIO, CFG_WEAPON_SET, CFG_GAME_LENGTH, CFG_HEALTH,
       CFG_DUAL_WIELD, CFG_LOADOUTS, CFG_NEXT_ROUND, CFG_CUSTOM0, CFG_CUSTOM1,
       CFG_CUSTOM2, CFG_CUSTOM3, CFG_VOICE_MODE, CFG_FRIENDLY_FIRE, CFG_FUN_FLAGS, CFG_GUN_SIZE,
       CFG_MAX_PLAYERS, CFG_FAST_REINFORCEMENTS };
int gevrNetConfigGet(int field);
/* Online in a co-op mission: where retail reads two or more players as a
 * deathmatch, the game asks this and plays the solo mission's rules. */
int gevrCoopActive(void);
int gevrCoopFastReinforcements(void); /* active host rule; independent of this headset's solo preference */
void gevrNetConfigSet(int field, int value);
int gevrNetSlotChr(int slot);
int netGetSlotTeam(int slot);
int netGetSlotPing(int slot);
/* Local host policy. Cap is clamped to 0..80 ms. */
void netSetHostEqualization(int enabled, unsigned cap_ms);
int netGetHostEqualization(unsigned *cap_ms);
unsigned netGetSlotHostDelayMs(int slot);
void netHostEqualizationText(char *text, unsigned size);
void netBeginLocalShot(void);
void netEndLocalShot(void);
int netVoiceSlotSpectating(int slot);
int netTeamRosterReady(void);
void netLobbySetTeam(unsigned char team);
int netTeamScore(int team);
int netDamageAllowed(int attacker, int target);
void gevrVoiceListenerBasis(float forward[3], float up[3]);
int gevrNetSlotLoadout(int slot, int k);
unsigned char gevrNetItemAt(int idx);
int netStageEligible(int idx);
int netActiveDualWield(void);
int netActiveFunFlags(void);
int netActiveLineMode(void);
int netActiveGunSize(void);
int netLobbyCanLaunch(void);
int netRoundRosterReady(void);
int netLocalReady(void);
void gevrNetSetReady(int ready);
int netRemoteWeapon(int slot, int hand);
int netRemoteTrigger(int slot, int hand);
int netActiveLoadoutItem(int slot, int k);
int netCountdownSecondsLeft(void);
void netHostContinue(void);
void netHostReturnToLobby(void);
int netHostCanStartRound(void);
void netHostStartRoundNow(void);
void netHostRequestVotes(void);
int netHostStartRequested(void);
int netWarmupSecondsLeft(void);
void netRoundNoticeText(char *text, unsigned size);
int netHostKickPlayer(int slot);
int netHostCanKickPlayer(int slot);
int netLobbySlotConnected(int slot);
int gevrSpectating(void);
int netPlayerIsSpectator(int slot);
/*
 * A connected slot's name, or NULL. Declared here because bondview2.c called
 * it with no prototype: C's implicit int return truncated the 64-bit pointer
 * and sign-extended it (fault address 0xffffffffbda918ee, report fa289829,
 * a spectator label on a late join), the port's 32-bit-pointer defect class.
 */
const char *netGetSlotName(int slot);
int netPlayerInRound(int slot);
void gevrSpectatorAim(float yaw);
int netSpectatorTarget(void);
void netSpectatorFrame(void);
void netSpectatorReset(void);
void gevrGiveOnlineLoadout(void);
void gevrEquipOnlineLoadout(void);
void gevrPreloadOnlineLoadouts(void);
void netTouchLocalActivity(void);
#ifdef __cplusplus
}
#endif
#endif
