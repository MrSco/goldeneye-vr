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
       CFG_MAX_PLAYERS, CFG_FAST_REINFORCEMENTS, CFG_BOT_MODE, CFG_BOT_COUNT, CFG_BOT_DIFFICULTY, CFG_MOVEMENT_SPEED,
       CFG_HANDICAPS, CFG_HORDE_STAGE, CFG_HORDE_START_WAVE };
int gevrNetConfigGet(int field);
/* Horde's setup break and START WAVE (net_core.c; horde_rules.h hordeHoldSetup) */
int netHordeActive(void);
int netHordeSetupBreak(void);
int netHordeRestartPending(void);
int netHordeHostCanStart(void);
void netHordeHostStart(void);
/* Online in a co-op mission: where retail reads two or more players as a
 * deathmatch, the game asks this and plays the solo mission's rules. */
int gevrCoopActive(void);
int gevrCoopFastReinforcements(void); /* active host rule; independent of this headset's solo preference */
/* The lobby's or match's mode, for the settings table (port/src/gevr_settings.c):
 * co-op, or co-op and Horde. Unlike gevrCoopActive, true in the launcher's lobby. */
int gevrNetConfigCoop(void);
int gevrNetConfigPve(void);
void gevrNetConfigSet(int field, int value);
int gevrNetSlotChr(int slot);
int gevrNetSlotIsBot(int slot);       /* the host runs this slot with AI input (gevr_bot.c) */
int gevrNetOwnsSlot(int slot);        /* this headset decides the slot's life: its own, the host's bots */
int gevrNetSlotIsLocalHuman(int slot); /* controlled by a local human at this machine */
int gevrLocalViewCount(void);         /* viewports drawn on this screen (online: local humans; offline: getPlayerCount) */
int gevrLocalViewIndex(int slot);     /* 0..3 viewport index on this screen, or -1 if remote/bot */
int gevrNetBotFoes(int a, int b);     /* b is a's foe: not itself, not its side in a team round */
int gevrNetBotDifficulty(void);       /* the round's NET_BOT_MEAT .. NET_BOT_DARK */
int gevrNetBotRowsEditable(void);     /* the host's bot rows: deathmatch, outside a round in progress */
int netGetSlotTeam(int slot);
int netGetActiveSlotTeam(int slot);   /* the round's (netGetSlotTeam: the next round's) */
/* Per-player health (protocol 29, net_rules.h NET_HANDICAP_*): the slot's value, the
 * table index it will play with, the round's, whether this machine may change it */
int netGetSlotHandicap(int slot);
int netSlotHandicapIndex(int slot);
int netRoundHandicapIndex(int slot);
int netSlotHandicapEditable(int slot);
int gevrNetScenarioAccepted(int scenario);   /* the host's setter would take it (the folder's Scenario list) */
void netLobbySetHandicapSlot(int slot, int value);   /* a local seat's own (the host's seat: the host's) */
void netHostSetSlotHandicap(int slot, int value);    /* the host: anyone's */
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
/* a local player choosing their character after dropping in: no damage (net_core.c) */
void netSetSlotChoosing(int slot, int choosing);
int netSlotChoosing(int slot);
int netSlotChoosingSeen(int slot);   /* any machine's player, from their moves */
void gevrVoiceListenerBasis(float forward[3], float up[3]);
int gevrNetSlotLoadout(int slot, int k);
unsigned char gevrNetItemAt(int idx);
int netStageEligible(int idx);
int netActiveDualWield(void);
int netActiveFunFlags(void);
int netActiveLineMode(void);
int netActiveGunSize(void);
int netActiveMovementSpeed(void);
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
void netRoundNoticeTextFit(char *text, unsigned size, int narrow);   /* narrow: quarter views */
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
