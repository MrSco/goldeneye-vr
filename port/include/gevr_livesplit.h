#ifndef GEVR_LIVESPLIT_H
#define GEVR_LIVESPLIT_H
/*
 * LiveSplit (port/src/gevr_livesplit.c, docs/speedrun.md): the mission clock to
 * the runner's LiveSplit through its TCP server (port 16834). The game posts
 * what happened; a thread of the client's own does the talking.
 */
#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_LIVESPLIT_PORT 16834

/* every frame of a mission, for this headset's player: whether the mission's
 * clock is running (Bond has control) and what it reads, in sixtieths */
void gevrLiveSplitFrame(int running, int time60);
/* a completed mission (file.c end_of_mission_briefing), the debrief's time */
void gevrLiveSplitMissionComplete(int time60);
/* a stage loads (prop.c): a mission that started and did not complete ended */
void gevrLiveSplitStageReset(void);
/* for the launcher's line: "Connected", "No LiveSplit server at ...", "" when off */
const char *gevrLiveSplitStatus(void);
/* 1 while connected */
int gevrLiveSplitConnected(void);

#ifdef __cplusplus
}
#endif
#endif
