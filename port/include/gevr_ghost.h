#ifndef GEVR_GHOST_H
#define GEVR_GHOST_H
/*
 * The ghost race's files (src/game/gevr_ghost.c), for the launcher's Ghosts tab
 * (port/vr/vr_ghosts.cpp): the run on file for a mission, which run a mission
 * races, and a run made ready to upload to the ghost boards.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* header flags: what a run was set under (the file's, the Worker's ghosts.ts) */
#define GEVR_GHF_STEREO    0x01
#define GEVR_GHF_BEAN      0x02
#define GEVR_GHF_GEX       0x04
#define GEVR_GHF_FORCEPULL 0x08
#define GEVR_GHF_ALLY      0x10
#define GEVR_GHF_THIRD     0x20
#define GEVR_GHF_SPEED     0x40   /* a movement speed other than the game's */
/* what the boards do not rank (Dab's Ghost Trials: a run outside the trial's rules) */
#define GEVR_GHF_UNRANKED  (GEVR_GHF_FORCEPULL | GEVR_GHF_ALLY | GEVR_GHF_SPEED)

typedef struct {
    int time60;          /* the mission time, sixtieths */
    int numsamples;
    int stage, difficulty, campaign;
    int flags;           /* GEVR_GHF_* */
    unsigned timestamp;  /* when it was set, seconds since 1970 */
    char owner[17];      /* the friend code of who set it, "" if not yet stamped */
} GevrGhostInfo;

/* "$S/ghosts/<campaign>-sNN-dN-<what>": what is "best", "race.txt", or a downloaded
 * run's tag (a friend code) */
void gevrGhostFilePath(char *out, int n, int campaign, int stage, int difficulty, const char *what);
/* a ghost file's header, checked as the race checks it (sizes and hash); 0 if none */
int gevrGhostFileInfo(const char *rel, GevrGhostInfo *out);
/* write the setter's friend code into a run that has none: 1 when the file is this
 * player's (stamped now or before), 0 when another's or unreadable */
int gevrGhostStampOwner(const char *rel, const char *friendCode);
/* the run a mission races: a file name in the ghosts folder, or "" for the best run
 * on file; setting "" goes back to the best run */
void gevrGhostGetRace(int campaign, int stage, int difficulty, char *out, int n);
void gevrGhostSetRace(int campaign, int stage, int difficulty, const char *fileName);
/* the folder, made if missing (a download is written there) */
void gevrGhostEnsureDir(void);

#ifdef __cplusplus
}
#endif
#endif
