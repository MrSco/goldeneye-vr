/*
 * Mods' scripts (docs/mod-system.md, Scripting): Lua 5.4 in a sandbox, for
 * the mod campaign this run plays (gevr_modpack.c gevrModActive). A script
 * reacts to events (mod.on) and calls the API; a script error, a runaway
 * call or memory past its cap turns the mod off, never the game.
 */
#ifndef GEVR_MODSCRIPT_H
#define GEVR_MODSCRIPT_H

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_MODSCRIPT_BUDGET_US 250          /* a frame, all mods together, averaged over a second */
#define GEVR_MODSCRIPT_OVER_SECONDS 3         /* over it this long: the costliest mod pauses for the mission */
#define GEVR_MODSCRIPT_MEMORY_CAP (32u << 20) /* each mod's Lua heap */
#define GEVR_MODSCRIPT_RUNAWAY_US 100000      /* one call in play; an endless loop stops here */
#define GEVR_MODSCRIPT_LOAD_RUNAWAY_US 2000000 /* one call while loading (not counted in the budget) */
#define GEVR_MODSCRIPT_DATA_CAP (1u << 20)    /* what mod.save may keep, each mod */
#define GEVR_MODSCRIPT_SCRIPT_CAP (1u << 20)  /* one .lua file */
#define GEVR_MODSCRIPT_TICK_CAP 0.25          /* tick(dt): dt is at most this, in seconds */

/* An event's argument (gevrModScriptEvent) */
enum { GEVR_MODARG_NUMBER = 1, GEVR_MODARG_STRING = 2, GEVR_MODARG_INTEGER = 3, GEVR_MODARG_BOOL = 4 };
typedef struct GevrModArg {
    int kind;        /* GEVR_MODARG_* */
    double n;        /* a number, an integer or a boolean (0 or 1) */
    const char *s;   /* a string */
} GevrModArg;

/* main.c, after romdataInit: the mod campaign's scripts/main.lua, then campaign_enter */
void gevrModScriptStart(void);
/* lv.c lvlStageLoad: done 0 before proplvreset2 (the last mission's
 * mission_end, then mission_prepare), done 1 at its end (mission_start).
 * mission: a stage a campaign plays (not the title's). */
void gevrModScriptStageLoad(int stage, int mission, int done);
/* file.c end_of_mission_briefing: the mission was won, so mission_end says "complete" */
void gevrModScriptMissionComplete(void);
/* boss.c, each frame after the players' ticks: tick(dt), the budget, the perf log */
void gevrModScriptFrame(int ticks);
/* An event in play from the game's code (the API areas, src/game/gevr_modapi.c),
 * counted in the budget; nothing outside a mission's play. cancellable: a
 * handler returning false asks the game not to do it, and the result is 1. */
int gevrModScriptEvent(const char *event, const GevrModArg *args, int nargs, int cancellable);
/* A note for the local player's screen (a mod stopped or paused), taken once */
const char *gevrModScriptTakeNote(void);
/* A mod's scripts are running now */
int gevrModScriptRunning(void);
/* All of it down (a test's teardown; the game never needs it) */
void gevrModScriptShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
