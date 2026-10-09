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
       NET_FUN_HIT_EVERY = 64, NET_FUN_MASK = 119 };
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
static inline float netVoiceDistanceGain(int mode, float horizontal_distance) {
    if (mode == NET_VOICE_COUCH || horizontal_distance <= 3000.0f) return 1.0f;
    if (horizontal_distance >= 6000.0f) return 0.10f;
    float remaining = 1.0f - (horizontal_distance - 3000.0f) / 3000.0f;
    return 0.10f + 0.90f * remaining * remaining * remaining;
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
