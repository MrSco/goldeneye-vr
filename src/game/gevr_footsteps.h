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

#endif
