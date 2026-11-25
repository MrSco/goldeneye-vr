#ifndef _MP_WATCH_H_
#define _MP_WATCH_H_
#include <ultra64.h>

typedef enum TEXTCOLORS {
    GREEN_NORMAL=0,
    GREEN_HIGHLIGHT,
    RED_NORMAL,
    RED_HIGHLIGHT,
    BLUE_NORMAL,
    BLUE_HIGHLIGHT
} TEXTCOLORS;

struct AwardMetrics {
	s32 num_shots;                    // 0x00
	s32 num_headshots;                // 0x04
	s32 num_kills;                    // 0x08
	s32 num_deaths;                   // 0x0c
	s32 num_suicides;                 // 0x10
	f32 ks_ratio;                     // 0x14
	f32 kd_ratio;                     // 0x18
	s32 damage_to_backside;           // 0x1c
	s32 time_other_players_on_screen; // 0x20
	f32 avg_km_per_hour;              // 0x24
	f32 body_armor_pickups;           // 0x28
	u32 awards;                       // 0x2c
	s32 longest_inning;               // 0x30
	s32 shortest_inning;              // 0x34
};

extern s32 g_stopPlayFlag;
extern s32 g_gameOverFlag;

void mpwatchUnpauseGame(void);
Gfx *display_text_for_playerdata_on_MP_menu(Gfx *gdl, s32 x, s32 y, s32 points, TEXTCOLORS text_color);

s32 checkGamePaused(void);

void mpCalculateAwards(bool gameoverdelay);
s32 get_points_for_mp_player(s32 arg0);
void mpwatchSetStopPlayFlag(void);
Gfx * mp_watch_menu_display(Gfx *arg0);
s32 disablePlayerActionsWhenPausedOrInMpMenu(void);
s32 mpwatchShouldDisplayGauges(void);
#ifdef GEVR
/* online: the Select Character panel in a player's view (dropin: just joined) */
void gevrCharSelOpen(s32 slot, s32 dropin);
s32 gevrCharSelIsOpen(s32 slot);
/* online: the Select Stage photo grid in a player's view (vote: NEXT MAP's ballot, else the host's MAP) */
void gevrStageSelOpen(s32 slot, s32 vote);
s32 gevrStageSelIsOpen(s32 slot);
/* online: any of the game's own screens above is up for slot; leave it as B would (whether one was) */
s32 gevrMenuPanelIsOpen(s32 slot);
s32 gevrMenuPanelCancel(s32 slot);
/* where the current player's panel is drawn: their view, or in stereo the head-locked HUD's canvas */
void gevrMenuPanelRect(s32 *x, s32 *y, s32 *w, s32 *h);
void gevrMenuPanelFit(s32 sw, s32 sh, s32 fbw, s32 fbh, s32 *x, s32 *y, s32 *w, s32 *h);
/* online: the Handicap (each player's health) and Teams screens in a player's view */
void gevrHandicapOpen(s32 slot);
s32 gevrHandicapIsOpen(s32 slot);
void gevrTeamSelOpen(s32 slot);
s32 gevrTeamSelIsOpen(s32 slot);
/* online deathmatch: the Multiplayer Options folder in a player's view (from: the
 * watch page PREVIOUS goes back to, -1 closes the menu); START in the warmup opens it */
void gevrMpOptionsOpen(s32 slot, s32 from);
s32 gevrMpOptionsIsOpen(s32 slot);
s32 gevrMpOptionsAvailable(s32 slot);
s32 gevrMpOptionsStartPressed(s32 slot);
void gevrMpOptionsFromPause(s32 slot);
#endif

#endif
