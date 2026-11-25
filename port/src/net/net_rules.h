/* Pure multiplayer rules shared with native regression checks. */
#ifndef GEVR_NET_RULES_H
#define GEVR_NET_RULES_H

/* Hit immunity, a hit ignored for a while after the last. Neither bit (the
 * default, the user's pick, 2026-10-08): a quarter second
 * (NET_HIT_SHORT_TICKS). NET_FUN_HIT_IMMUNITY: GoldenEye's, the red flash's
 * half second to a second. NET_FUN_HIT_EVERY: none, every hit counts, as in
 * Perfect Dark. The default needs no bit, so a saved 0 (every setup that
 * never touched the rule) gets it. Builds 0.4.11-0.4.13 (protocol 19) know
 * neither bit, so v0.4.14 is protocol 20. */
enum { NET_FUN_DK = 1, NET_FUN_PAINTBALL = 2, NET_FUN_LINE = 4, NET_FUN_NO_RADAR = 16, NET_FUN_HIT_IMMUNITY = 32,
       NET_FUN_HIT_EVERY = 64, NET_FUN_NO_TELEPORT = 128, NET_FUN_MASK = 247 };
#define NET_HIT_SHORT_TICKS 15
/* 0 every hit counts, 1 short, 2 GoldenEye's */
static inline int netHitImmunity(int flags) {
    return (flags & NET_FUN_HIT_IMMUNITY) ? 2 : (flags & NET_FUN_HIT_EVERY) ? 0 : 1;
}
static inline int netWithHitImmunity(int flags, int immunity) {
    flags &= ~(NET_FUN_HIT_IMMUNITY | NET_FUN_HIT_EVERY);
    return flags | (immunity == 2 ? NET_FUN_HIT_IMMUNITY : immunity == 0 ? NET_FUN_HIT_EVERY : 0);
}
/* Co-op-only rule in the existing config byte; no packet layout changes. */
#define NET_COOP_FAST_REINFORCEMENTS 8
#define NET_COOP_FUN_MASK (NET_FUN_MASK | NET_COOP_FAST_REINFORCEMENTS)
/* Zero is normal so existing saved preferences and zeroed configs stay normal. */
enum { NET_MOVE_NORMAL, NET_MOVE_50, NET_MOVE_75, NET_MOVE_125,
       NET_MOVE_150, NET_MOVE_175, NET_MOVE_200, NET_MOVE_COUNT };
static inline int netMovementSpeedPercent(int mode) {
    static const int percent[NET_MOVE_COUNT] = {100, 50, 75, 125, 150, 175, 200};
    return mode >= 0 && mode < NET_MOVE_COUNT ? percent[mode] : 100;
}
static inline int netMovementSpeedMode(int percent) {
    percent = percent < 50 ? 50 : percent > 200 ? 200 : percent;
    percent = ((percent + 12) / 25) * 25;
    for (int i = 0; i < NET_MOVE_COUNT; i++)
        if (netMovementSpeedPercent(i) == percent) return i;
    return NET_MOVE_NORMAL;
}
static inline float netMovementSpeedFactor(int mode) {
    return netMovementSpeedPercent(mode) * 0.01f;
}

enum { NET_GUN_NORMAL, NET_GUN_TINY, NET_GUN_BIG };
static inline float netGunSizeFactor(int mode) { return mode == NET_GUN_TINY ? 0.2f : mode == NET_GUN_BIG ? 2.0f : 1.0f; }
#include <stdint.h>
enum { NET_VOICE_PROXIMITY, NET_VOICE_COUCH };
enum { NET_TEAM_RED, NET_TEAM_BLUE, NET_TEAM_NONE };
#define NET_PING_UNKNOWN UINT16_MAX
#define NET_RADAR_BRIGHT_RANGE 4000.0f
static inline uint16_t netLatencyValue(uint32_t rtt, uint32_t last_receive, uint32_t now) {
    if (!last_receive || (uint32_t)(now-last_receive) > 5000) return NET_PING_UNKNOWN;
    return rtt >= NET_PING_UNKNOWN ? NET_PING_UNKNOWN-1 : (uint16_t)rtt;
}
/* The scenarios: the game's own eight (MPSCENARIOS, 0..7: 5..7 its 2v2, 3v1
 * and 2v1), then two online team sizes past four players (protocol 16). */
enum { NET_SCENARIO_3V3 = 8, NET_SCENARIO_4V4 = 9 };
static inline int netScenarioHasTeams(int scenario) { return scenario >= 5 && scenario <= NET_SCENARIO_4V4; }
/* The game's scenario for a net one: 3v3 and 4v4 play by its 2v2 rules (teams
 * set per player, set_players_team_or_scenario_item_flag); the sizes are ours. */
static inline int netGameScenario(int scenario) {
    return scenario == NET_SCENARIO_3V3 || scenario == NET_SCENARIO_4V4 ? 5 : scenario;
}
static inline int netTeamCapacity(int scenario, int team) {
    static const unsigned char caps[][2] = { {2,2}, {3,1}, {2,1}, {3,3}, {4,4} };   /* red, blue */
    if (!netScenarioHasTeams(scenario) || team < 0 || team > 1) return 0;
    return caps[scenario - 5][team];
}
static inline int netTeamRequiredPlayers(int scenario) {
    return netTeamCapacity(scenario, 0) + netTeamCapacity(scenario, 1);
}
static inline int netTeamRosterComplete(int scenario, int slots, const uint8_t *connected, const uint8_t *team) {
    int counts[2] = {0,0};
    if (!netScenarioHasTeams(scenario)) return 1;
    for (int i=0;i<slots;i++) if (connected[i]) {
        if (team[i] > 1) return 0;
        counts[team[i]]++;
    }
    return counts[0] == netTeamCapacity(scenario,0) && counts[1] == netTeamCapacity(scenario,1);
}
/* Proximity: full volume to 8 m, a cubic fade to silence at 25 m (world units are cm).
 * The old 30 m / 60 m curve with a 10% floor carried voices across whole MP maps. */
#define NET_VOICE_FULL_CM 800.0f
#define NET_VOICE_SILENT_CM 2500.0f
static inline float netVoiceDistanceGain(int mode, float horizontal_distance) {
    if (mode == NET_VOICE_COUCH || horizontal_distance <= NET_VOICE_FULL_CM) return 1.0f;
    if (horizontal_distance >= NET_VOICE_SILENT_CM) return 0.0f;
    float remaining = 1.0f - (horizontal_distance - NET_VOICE_FULL_CM) / (NET_VOICE_SILENT_CM - NET_VOICE_FULL_CM);
    return remaining * remaining * remaining;
}
static inline int netVoiceGroupsMatch(int running, int scenario, int a_spectator, int b_spectator, int a_team, int b_team) {
    if (!running) return 1;
    if (a_spectator || b_spectator) return a_spectator && b_spectator;
    return 1; /* Active opponents remain audible with proximity attenuation. */
}
static inline int netVoicePairMode(int scenario, int selected, int a_team, int b_team) {
    if (!netScenarioHasTeams(scenario)) return selected;
    return a_team < 2 && a_team == b_team ? NET_VOICE_COUCH : NET_VOICE_PROXIMITY;
}
/*
 * A player's own health (protocol 29): the lobby slot's handicap, 0
 * (NET_HANDICAP_MATCH) the host's match health, otherwise one more than a
 * front.c MP_handicap_table index (1 Hero, x10 damage .. 11 Rookie), so a
 * cleared slot plays at the match's. NET_HANDICAP_HOST marks the host's pick
 * for that player, which always counts; a player's own pick counts only while
 * the host's Handicaps rule is on (NetMatchConfig.handicaps), so a stranger
 * cannot make themselves tougher in a match that did not ask for it.
 * Deathmatch only: co-op's difficulty and Horde's waves set the damage.
 */
#define NET_HANDICAP_MATCH 0
#define NET_HANDICAP_HOST  0x80
static inline int netHandicapValid(int value, int count) {
    return value == NET_HANDICAP_MATCH || (value > 0 && value <= 0xFF && (value & 0x7F) >= 1 && (value & 0x7F) <= count);
}
static inline int netHandicapIsHosts(int value) { return value != NET_HANDICAP_MATCH && (value & NET_HANDICAP_HOST) != 0; }
/* A table index as a slot's value (without the host's mark) */
static inline int netHandicapOf(int index) { return index + 1; }
/* The health a player plays with: an index into the table */
static inline int netHandicapResolve(int value, int rule, int match, int count) {
    if (match < 0 || match >= count) match = count > 5 ? 5 : 0;
    if (!netHandicapValid(value, count) || value == NET_HANDICAP_MATCH) return match;
    return netHandicapIsHosts(value) || rule ? (value & 0x7F) - 1 : match;
}
/* May the player change their own: the rule on, and the host has not set it */
static inline int netHandicapOwnEditable(int value, int rule) { return rule && !netHandicapIsHosts(value); }
/* The value for table index shown: the match's health is the match's again (it follows the host's) */
static inline int netHandicapPick(int index, int match, int count) {
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    return index == match ? NET_HANDICAP_MATCH : netHandicapOf(index);
}
/* One step along the table from the health shown */
static inline int netHandicapStep(int value, int rule, int match, int count, int dir) {
    return netHandicapPick(netHandicapResolve(value, rule, match, count) + (dir < 0 ? -1 : dir > 0 ? 1 : 0), match, count);
}
static inline int netTeamKillPoints(int shooter_team, int victim_team, int kills) {
    return shooter_team == victim_team ? -kills : kills;
}
static inline int netTeamDamageAllowed(int scenario, int friendly_fire, int self,
                                      int attacker_team, int target_team) {
    return self || friendly_fire || !netScenarioHasTeams(scenario) ||
        attacker_team == NET_TEAM_NONE || target_team == NET_TEAM_NONE ||
        attacker_team != target_team;
}
#endif
