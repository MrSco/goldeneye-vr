/*
 * The multiplayer match's tables, shared by the launcher (C++), the net
 * layer and the game's lobby page: one list each of the stages, the weapon
 * sets, the scenarios, the game lengths, the health steps, the characters
 * and the guns a custom set or a loadout may hold. The game's own text for
 * these lives in the ROM, which the launcher runs before; these are the
 * English names, used by every screen so they read the same everywhere.
 */
#ifndef _NET_MATCH_H
#define _NET_MATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stages, in the launcher's order: name, LEVELID, the players the map takes */
typedef struct { const char *name; uint8_t level_id; } NetMatchStage;
int netStageCount(void);
const NetMatchStage *netStage(int idx);
const char *netStageName(int idx);
int netStageIndexOf(uint8_t level_id);      /* -1 unknown */

/*
 * The match's mode (NetMatchConfig.mode). Co-op plays a solo mission, the
 * config's stage then being the mission's LEVELID and its difficulty the
 * solo difficulty (DIFFICULTY_AGENT .. DIFFICULTY_007).
 */
enum { NET_MODE_DEATHMATCH = 0, NET_MODE_COOP = 1 };
#define NET_COOP_MAX_PLAYERS 4
#define NET_LOBBY_COOP_STAGE 0x80   /* a game list's stage byte: 0x80 | the co-op mission's LEVELID */
#define NET_DIFFICULTY_COUNT 4

/* Co-op missions, in the game's mission folder order (front.c mission_folder_setup_entries) */
typedef struct { const char *name; uint8_t level_id; } NetCoopMission;
int netCoopMissionCount(void);
const NetCoopMission *netCoopMission(int idx);
const char *netCoopMissionName(int idx);
int netCoopMissionIndexOf(uint8_t level_id);   /* -1 not a co-op mission */
/*
 * A co-op party plays the solo campaign through the game's own menus: the
 * config's stage is then the title stage (LEVELID_TITLE), and Cradle's
 * success leads to Cuba (LEVELID_CUBA, the credits), as front.c's
 * statistics page has it.
 */
#define NET_COOP_FRONT_STAGE 90
#define NET_COOP_CUBA_STAGE 54
int netCoopStageValid(uint8_t level_id);       /* a mission, the menus or Cuba */
const char *netCoopStageName(uint8_t level_id); /* where the party is: "in the menus", a mission */
/* How a co-op mission ended (NET_MSG_COOP_END), for the debrief (front.c) */
enum { NET_COOP_RESULT_FAILED = 0, NET_COOP_RESULT_COMPLETE = 1, NET_COOP_RESULT_ALL_DOWN = 2, NET_COOP_RESULT_ABORTED = 3 };
const char *netDifficultyName(int difficulty);

/* Weapon sets: GoldenEye's fourteen (mp_weapon.c) and the host's custom one */
#define NET_WEAPON_SET_CUSTOM 14
int netWeaponSetCount(void);
const char *netWeaponSetName(int idx);

/* Scenarios: the five for any party size, SCENARIO_NORMAL..SCENARIO_LTK, then
 * the team ones: the game's 2v2, 3v1 and 2v1 and online 3v3 and 4v4 (net_rules.h) */
int netScenarioCount(void);
const char *netScenarioName(int idx);

/* Game lengths: front.c multi_game_lengths, 0 unlimited .. 7 last one standing */
int netGameLengthCount(void);
const char *netGameLengthName(int idx);

/* Health: front.c MP_handicap_table, 0 (Hero, x10 damage) .. 10 (Rookie); 5 is normal */
int netHealthCount(void);
const char *netHealthName(int idx);

/* Characters: front.c mp_chr_setup, all 64 */
int netCharacterCount(void);
const char *netCharacterName(int idx);

/* Guns a custom set or a loadout may hold: a name and its ITEM_IDS value */
typedef struct { const char *name; uint8_t item; } NetMatchItem;
int netItemCount(void);
const NetMatchItem *netItem(int idx);
int netItemIndexOf(int item);              /* -1 not offered */
const char *netItemName(int item);         /* by item id; "" when not offered */

/* The modes of the lobby's DUAL WIELD and NEXT ROUND rows */
enum { NET_DUAL_OFF = 0, NET_DUAL_DOUBLES = 1, NET_DUAL_ANY = 2 };
enum { NET_NEXT_VOTE = 0, NET_NEXT_SHUFFLE = 1, NET_NEXT_PLAYLIST = 2 };
const char *netDualWieldName(int mode);
const char *netNextRoundName(int mode);
const char *netVoiceModeName(int mode);
const char *netTeamName(int team);
enum { NET_BOT_MODE_OFF = 0, NET_BOT_MODE_FILL = 1, NET_BOT_MODE_FIXED = 2, NET_BOT_MODE_COUNT = 3 };
enum { NET_BOT_DIFF_EASY = 0, NET_BOT_DIFF_MEDIUM = 1, NET_BOT_DIFF_HARD = 2, NET_BOT_DIFF_COUNT = 3 };
const char *netBotModeName(int mode);
const char *netBotDifficultyName(int diff);

#ifdef __cplusplus
}
#endif

#endif /* _NET_MATCH_H */
