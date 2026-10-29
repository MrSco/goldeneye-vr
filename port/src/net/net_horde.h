/* Integer-only game/port interface (the game's bool is s32). */
#ifndef GEVR_NET_HORDE_H
#define GEVR_NET_HORDE_H
#include "horde_rules.h"
#ifdef __cplusplus
extern "C" {
#endif
struct ChrRecord;
struct DoorRecord;
struct netbuf;
int netHordeActive(void);
int netHordeRunning(void); /* loaded, all players ready, not migrating */
void netHordeRetry(void);
void netHordeAdmitPlayers(void);
void netHordeTick(void);
void netHordeReceive(struct netbuf *b);
void netHordeReceiveClient(int slot, struct netbuf *b);
void netHordeRequestAction(unsigned action, unsigned item);
void netHordeSend(int slot); /* -1 broadcasts, otherwise late-join snapshot */
void gevrHordePrepareSetup(void);
int gevrHordeSkipProp(int type);
void gevrHordeStageLoaded(void);
void gevrHordeTick(int ticks);
void gevrHordeDefeat(void);
void gevrHordeRevived(void);
int gevrHordeDefeated(void);
void gevrHordeBecameHost(void);
void gevrHordeNotifyGuardHit(struct ChrRecord *chr, int attacker, int hitpart);
int gevrHordeOnUse(void);
int gevrHordeOnGrip(int ctrl, const float hand[3]);
void gevrHordeHoldInput(int channel, int pressed, int held, const float *hand, unsigned ticks);
int gevrHordeBossNoStun(const struct ChrRecord *chr);
int gevrHordeInstaKill(const struct ChrRecord *chr);
int gevrHordeDoorHeldOpen(const struct DoorRecord *door);
int gevrHordeXeniaWeapon(struct ChrRecord *chr);
int gevrHordeHandleAction(int slot, unsigned action, unsigned item);
uint64_t gevrHordeGuardToken(const struct ChrRecord *chr);
int gevrHordeTokenCurrent(uint64_t token);
unsigned gevrHordeGuardId(const struct ChrRecord *chr);
void gevrHordeSetGuardId(struct ChrRecord *chr, unsigned id);
/* The kind's look and run speed from the body the host chose (every headset). */
void gevrHordeDecorateGuard(struct ChrRecord *chr, int body);
/* The lobby's Start at wave, 1 normally (net_core.c). */
unsigned netHordeStartWave(void);
const HordeState *gevrHordeState(void);
void gevrHordeApplyState(const HordeState *state);
void gevrHordeText(char *text, unsigned size);
#ifdef __cplusplus
}
#endif
#endif
