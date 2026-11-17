#ifndef GEVR_FOOTSTEPS_H
#define GEVR_FOOTSTEPS_H

#include <ultra64.h>

/*
 * Carnivorous' Footsteps patch in a hack campaign that carries it (Mario
 * Characters V3.17; gevr_romlayout.h footsteps). gevr_footsteps.c.
 */

/* AI 0x2B: cmd points at it, the PRINT after it spells the poke. The label to go to, or -1. */
s32 gevrFootstepsCommand(const u8 *cmd);
/* the end of init_watch_at_start_of_stage */
void gevrFootstepsStageStart(s32 stage);
/* AI 0x36: objective i shown or hidden */
void gevrFootstepsToggleObjective(s32 i);
/* the difficulty from which briefing objective i counts */
s32 gevrFootstepsObjectiveDifficulty(s32 i, s32 difficulty);

/* Mario Characters' cheats: their rows from the cartridge (after binding), what each does when it
 * comes on (-1: nothing), whose unlock it takes, and Cheat Options' rows a column and top */
void gevrFootstepsBindCheats(void);
s32 gevrFootstepsCheatEffect(s32 cheat);
s32 gevrFootstepsCheatUnlockAs(s32 cheat);
s32 gevrFootstepsCheatRows(void);
s32 gevrFootstepsCheatTop(void);

/* AI 0xFB's fog fade without gas; a sound heard from a place */
struct coord3d;
void gevrFootstepsFogFade(void);
void gevrFootstepsSoundAt(s32 sound, struct coord3d *pos);
/* AI 0xBF: Mario Characters gives a spawning guard a rocket launcher with Enemy Rockets on in Cheat Options */
s32 gevrFootstepsEnemyRockets(void);

/* AI 0xE9: the stage's two environments swapped after the switch */
void gevrFootstepsSwapSkies(void);
/* after binding: its cheat rows and its edited global AI lists */
void gevrFootstepsBind(void);

/*
 * Online co-op (net_coop.c): the host shares what its scripts wrote to the
 * hack's memory as bytes, and the objectives' show/hide state. Ranges are
 * vram addresses and lengths; Take empties what it returns, Touched is
 * everything since the bind (a drop-in's snapshot).
 */
/* -1: some writes were not kept (nothing took them for a while): send everything */
s32 gevrFootstepsTakeWrites(u32 *start, u32 *len, s32 cap);
s32 gevrFootstepsTouchedWrites(u32 *start, u32 *len, s32 cap);
void gevrFootstepsApplyWrite(u32 vram, const u8 *bytes, u32 len);
void gevrFootstepsObjectiveState(s16 *flags, s8 *difficulty);
void gevrFootstepsSetObjectiveState(const s16 *flags, const s8 *difficulty);
/*
 * Each player's own cheat lists (Play as, Tiny and Mushroom Mode): 1 for a
 * list that only touches the player. CommandIn runs a poke from list (its
 * writes stay out of the co-op log when it is one); PlayerFlags is that
 * list's own stage flags, NULL for any other list; RunningLocal marks a
 * teammate's headset running them itself (net_coop.c), where a list that
 * switches its own list ends.
 */
s32 gevrFootstepsPlayerList(const u8 *list);
s32 gevrFootstepsCommandIn(const u8 *list, const u8 *cmd);
s32 *gevrFootstepsPlayerFlags(const u8 *list);
void gevrFootstepsRunningLocal(s32 on);
s32 gevrFootstepsIsRunningLocal(void);
/* A hash of the written bytes in [vram0, vram1): the co-op log compares host and teammate */
u32 gevrFootstepsWrittenHash(u32 vram0, u32 vram1);

#endif
