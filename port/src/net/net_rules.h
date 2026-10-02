/* Pure multiplayer rules shared with native regression checks. */
#ifndef GEVR_NET_RULES_H
#define GEVR_NET_RULES_H

enum { NET_FUN_DK = 1, NET_FUN_PAINTBALL = 2, NET_FUN_LINE = 4, NET_FUN_MASK = 7 };
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
static inline int netScenarioHasTeams(int scenario) { return scenario >= 5 && scenario <= 7; }
static inline int netTeamCapacity(int scenario, int team) {
    if (!netScenarioHasTeams(scenario) || team < 0 || team > 1) return 0;
    return team == NET_TEAM_RED ? (scenario == 6 ? 3 : 2) : (scenario == 5 ? 2 : 1);
}
static inline int netTeamRequiredPlayers(int scenario) {
    return netTeamCapacity(scenario, 0) + netTeamCapacity(scenario, 1);
}
static inline int netTeamRosterComplete(int scenario, const uint8_t connected[4], const uint8_t team[4]) {
    int counts[2] = {0,0};
    if (!netScenarioHasTeams(scenario)) return 1;
    for (int i=0;i<4;i++) if (connected[i]) {
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
