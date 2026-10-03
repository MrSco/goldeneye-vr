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

#ifdef __cplusplus
}
#endif

#endif /* _NET_MATCH_H */
