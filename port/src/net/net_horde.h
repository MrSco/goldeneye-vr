/* Integer-only game/port interface (the game's bool is s32). */
#ifndef GEVR_NET_HORDE_H
#define GEVR_NET_HORDE_H
#include "horde_rules.h"
#ifdef __cplusplus
extern "C" {
#endif
struct ChrRecord;
struct netbuf;
int netHordeActive(void);
int netHordeRunning(void); /* loaded, all players ready, not migrating */
void netHordeRetry(void);
void netHordeAdmitPlayers(void);
void netHordeTick(void);
void netHordeReceive(struct netbuf *b);
void netHordeSend(int slot); /* -1 broadcasts, otherwise late-join snapshot */
void gevrHordePrepareSetup(void);
int gevrHordeSkipProp(int type);
void gevrHordeStageLoaded(void);
void gevrHordeTick(int ticks);
void gevrHordeDefeat(void);
void gevrHordeRevived(void);
int gevrHordeDefeated(void);
void gevrHordeBecameHost(void);
uint64_t gevrHordeGuardToken(const struct ChrRecord *chr);
int gevrHordeTokenCurrent(uint64_t token);
unsigned gevrHordeGuardId(const struct ChrRecord *chr);
void gevrHordeSetGuardId(struct ChrRecord *chr, unsigned id);
const HordeState *gevrHordeState(void);
void gevrHordeApplyState(const HordeState *state);
void gevrHordeText(char *text, unsigned size);
#ifdef __cplusplus
}
#endif
#endif
