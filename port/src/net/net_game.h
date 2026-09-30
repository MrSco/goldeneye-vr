/* Integer-only bridge for the original game C headers (which define bool). */
#ifndef GEVR_NET_GAME_H
#define GEVR_NET_GAME_H
#include "net_match.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { CFG_STAGE, CFG_SCENARIO, CFG_WEAPON_SET, CFG_GAME_LENGTH, CFG_HEALTH,
       CFG_DUAL_WIELD, CFG_LOADOUTS, CFG_NEXT_ROUND, CFG_CUSTOM0, CFG_CUSTOM1,
       CFG_CUSTOM2, CFG_CUSTOM3 };
int gevrNetConfigGet(int field);
void gevrNetConfigSet(int field, int value);
int gevrNetSlotChr(int slot);
int gevrNetSlotLoadout(int slot, int k);
unsigned char gevrNetItemAt(int idx);
int netStageEligible(int idx);
int netActiveDualWield(void);
int netRemoteWeapon(int slot, int hand);
int netRemoteTrigger(int slot, int hand);
int netActiveLoadoutItem(int slot, int k);
int netCountdownSecondsLeft(void);
void netHostContinue(void);
void netHostReturnToLobby(void);
void netHostStartRoundNow(void);
int gevrSpectating(void);
int netPlayerIsSpectator(int slot);
int netPlayerInRound(int slot);
void gevrSpectatorAim(float yaw);
int netSpectatorTarget(void);
void netSpectatorFrame(void);
void netSpectatorReset(void);
void gevrGiveOnlineLoadout(void);
void gevrEquipOnlineLoadout(void);
void gevrPreloadOnlineLoadouts(void);
#ifdef __cplusplus
}
#endif
#endif
