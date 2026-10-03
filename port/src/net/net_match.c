#include "net_match.h"
#include "net_rules.h"
#include <ultra64.h>
#include <bondtypes.h>
#include "bondconstants.h"

/* The launcher's order; the level ids are the ROM's LEVELID values. Online
 * every stage takes the host's player count, two to eight (issue #88; the
 * game's front.c multi_stage_setups capped the smaller maps at 3 or 2 for
 * split screen). A map with fewer start pads than players stands the extra
 * ones beside a pad (bondview_r.c gevrSpreadStartPad). */
static const NetMatchStage s_stages[] = {
    { "Facility",  34 }, { "Complex",   31 }, { "Temple",    38 }, { "Stack",    46 },
    { "Caverns",   39 }, { "Library",   48 }, { "Basement",  45 }, { "Caves",    50 },
    { "Egypt",     32 }, { "Bunker II", 27 }, { "Archives",  24 },
};

/* The twenty missions (front.c mission_folder_setup_entries), the ROM's LEVELID values */
static const NetCoopMission s_coop_missions[] = {
    { "Dam", 33 }, { "Facility", 34 }, { "Runway", 35 }, { "Surface I", 36 }, { "Bunker I", 9 },
    { "Silo", 20 }, { "Frigate", 26 }, { "Surface II", 43 }, { "Bunker II", 27 }, { "Statue", 22 },
    { "Archives", 24 }, { "Streets", 29 }, { "Depot", 30 }, { "Train", 25 }, { "Jungle", 37 },
    { "Control", 23 }, { "Caverns", 39 }, { "Cradle", 41 }, { "Aztec", 28 }, { "Egyptian", 32 },
};

static const char *const s_difficulties[] = { "Agent", "Secret Agent", "00 Agent", "007" };

/* mp_weapon.c mp_weapon_set_text_table's order, as the ROM's LmpweaponsE names them */
static const char *const s_weapon_sets[] = {
    "Slappers only", "Pistols", "Throwing Knives", "Automatics", "Power Weapons",
    "Sniper Rifles", "Grenades", "Remote Mines", "Grenade Launchers", "Timed Mines",
    "Proximity Mines", "Rockets", "Lasers", "Golden Gun", "Custom",
};

static const char *const s_scenarios[] = {
    "Normal", "You Only Live Twice", "The Living Daylights", "The Man With The Golden Gun", "Licence To Kill", "Team 2v2", "Team 3v1", "Team 2v1",
    "Team 3v3", "Team 4v4",   /* net_rules.h: online only, by the game's 2v2 rules */
};

/* front.c multi_game_lengths */
static const char *const s_lengths[] = {
    "No limit", "5 minutes", "10 minutes", "20 minutes", "First to 5", "First to 10", "First to 20", "Last one standing",
};

/* front.c MP_handicap_table: the damage taken, as the game's menu words it */
static const char *const s_health[] = {
    "-10 (Hero)", "-4 (Veteran)", "-3 (Veteran)", "-2 (Veteran)", "-1 (Veteran)", "Normal",
    "+1 (Novice)", "+2 (Novice)", "+3 (Novice)", "+4 (Novice)", "+10 (Rookie)",
};

/* front.c mp_chr_setup, all 64: the game's own, then Rare's staff heads on Bond's tuxedo */
static const char *const s_characters[] = {
    "James Bond", "Natalya", "Trevelyan", "Xenia", "Ourumov", "Boris", "Valentin", "Mishkin",
    "Mayday", "Jaws", "Oddjob", "Baron Samedi",
    "Russian Soldier", "Russian Infantry", "Scientist", "Female Scientist", "Russian Commandant",
    "Janus Marine", "Naval Officer", "Helicopter Pilot", "St. Petersburg Guard", "Female Civilian",
    "Civilian", "Civilian 2", "Civilian 3", "Siberian Guard", "Arctic Commando", "Siberian Guard 2",
    "Siberian Special Forces", "Jungle Commando", "Janus Special Forces", "Moonraker Elite",
    "Female Moonraker Elite", "Rosika",
    "Karl", "Martin", "Mark", "Dave", "Duncan", "B", "Steve E", "Grant", "Graeme", "Ken", "Alan", "Pete",
    "Shaun", "Dwayne", "Des", "Chris", "Lee", "Neil", "Jim", "Robin", "Steve H", "Terrorist", "Biker",
    "Joel", "Scott", "Joe", "Sally", "Marion", "Mandy", "Vivien",
};

/* Every gun of the fourteen sets, plus the four that have a third-person
 * model of their own (player.c getPropForHeldItem): the knife, the shotgun,
 * the Phantom and the silenced D5K. */
static const NetMatchItem s_items[] = {
    { "PP7",                ITEM_WPPK },
    { "PP7 (Silenced)",     ITEM_WPPKSIL },
    { "DD44 Dostovei",      ITEM_TT33 },
    { "Cougar Magnum",      ITEM_RUGER },
    { "Golden Gun",         ITEM_GOLDENGUN },
    { "Klobb",              ITEM_SKORPION },
    { "ZMG (9mm)",          ITEM_UZI },
    { "D5K Deutsche",       ITEM_MP5K },
    { "D5K (Silenced)",     ITEM_MP5KSIL },
    { "Phantom",            ITEM_SPECTRE },
    { "KF7 Soviet",         ITEM_AK47 },
    { "AR33 Assault Rifle", ITEM_M16 },
    { "RC-P90",             ITEM_FNP90 },
    { "Shotgun",            ITEM_SHOTGUN },
    { "Automatic Shotgun",  ITEM_AUTOSHOT },
    { "Sniper Rifle",       ITEM_SNIPERRIFLE },
    { "Moonraker Laser",    ITEM_LASER },
    { "Grenade Launcher",   ITEM_GRENADELAUNCH },
    { "Rocket Launcher",    ITEM_ROCKETLAUNCH },
    { "Hand Grenade",       ITEM_GRENADE },
    { "Throwing Knife",     ITEM_THROWKNIFE },
    { "Hunting Knife",      ITEM_KNIFE },
    { "Remote Mine",        ITEM_REMOTEMINE },
    { "Timed Mine",         ITEM_TIMEDMINE },
    { "Proximity Mine",     ITEM_PROXIMITYMINE },
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

int netStageCount(void) { return COUNT(s_stages); }
const NetMatchStage *netStage(int idx) { return idx >= 0 && idx < COUNT(s_stages) ? &s_stages[idx] : &s_stages[0]; }
const char *netStageName(int idx) { return idx >= 0 && idx < COUNT(s_stages) ? s_stages[idx].name : ""; }
int netStageIndexOf(uint8_t level_id) {
    for (int i = 0; i < COUNT(s_stages); i++)
        if (s_stages[i].level_id == level_id) return i;
    return -1;
}

int netCoopMissionCount(void) { return COUNT(s_coop_missions); }
const NetCoopMission *netCoopMission(int idx) { return idx >= 0 && idx < COUNT(s_coop_missions) ? &s_coop_missions[idx] : &s_coop_missions[0]; }
const char *netCoopMissionName(int idx) { return idx >= 0 && idx < COUNT(s_coop_missions) ? s_coop_missions[idx].name : ""; }
int netCoopMissionIndexOf(uint8_t level_id) {
    for (int i = 0; i < COUNT(s_coop_missions); i++)
        if (s_coop_missions[i].level_id == level_id) return i;
    return -1;
}
int netCoopStageValid(uint8_t level_id) {
    return netCoopMissionIndexOf(level_id) >= 0 || level_id == NET_COOP_FRONT_STAGE || level_id == NET_COOP_CUBA_STAGE;
}
const char *netCoopStageName(uint8_t level_id) {
    if (level_id == NET_COOP_FRONT_STAGE) return "in the menus";
    if (level_id == NET_COOP_CUBA_STAGE) return "Cuba";
    return netCoopMissionName(netCoopMissionIndexOf(level_id));
}
const char *netDifficultyName(int difficulty) {
    return difficulty >= 0 && difficulty < COUNT(s_difficulties) ? s_difficulties[difficulty] : "";
}

int netWeaponSetCount(void) { return COUNT(s_weapon_sets); }
const char *netWeaponSetName(int idx) { return idx >= 0 && idx < COUNT(s_weapon_sets) ? s_weapon_sets[idx] : ""; }

int netScenarioCount(void) { return COUNT(s_scenarios); }
const char *netScenarioName(int idx) { return idx >= 0 && idx < COUNT(s_scenarios) ? s_scenarios[idx] : ""; }

int netGameLengthCount(void) { return COUNT(s_lengths); }
const char *netGameLengthName(int idx) { return idx >= 0 && idx < COUNT(s_lengths) ? s_lengths[idx] : ""; }

int netHealthCount(void) { return COUNT(s_health); }
const char *netHealthName(int idx) { return idx >= 0 && idx < COUNT(s_health) ? s_health[idx] : ""; }

int netCharacterCount(void) { return COUNT(s_characters); }
const char *netCharacterName(int idx) { return idx >= 0 && idx < COUNT(s_characters) ? s_characters[idx] : ""; }

int netItemCount(void) { return COUNT(s_items); }
const NetMatchItem *netItem(int idx) { return idx >= 0 && idx < COUNT(s_items) ? &s_items[idx] : &s_items[0]; }
int netItemIndexOf(int item) {
    for (int i = 0; i < COUNT(s_items); i++)
        if (s_items[i].item == item) return i;
    return -1;
}
const char *netItemName(int item) {
    int idx = netItemIndexOf(item);
    return idx >= 0 ? s_items[idx].name : "";
}

const char *netDualWieldName(int mode) {
    return mode == NET_DUAL_DOUBLES ? "Doubles" : mode == NET_DUAL_ANY ? "Any two guns" : "Off";
}

const char *netNextRoundName(int mode) {
    return mode == NET_NEXT_SHUFFLE ? "Shuffle" : mode == NET_NEXT_PLAYLIST ? "Playlist" : "Vote";
}

const char *netVoiceModeName(int mode) { return mode == NET_VOICE_COUCH ? "Couch" : "Proximity"; }
const char *netTeamName(int team) { return team == NET_TEAM_RED ? "Red" : team == NET_TEAM_BLUE ? "Blue" : "Unassigned"; }
