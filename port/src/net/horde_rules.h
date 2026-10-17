/* Horde's clock and quotas: independent of the engine and transport. */
#ifndef GEVR_HORDE_RULES_H
#define GEVR_HORDE_RULES_H
#include <stdint.h>
#include <stdio.h>
enum { HORDE_PREPARE, HORDE_WAVE, HORDE_INTERMISSION, HORDE_DEFEAT };
#define HORDE_GUARD_SLOTS 64
#define HORDE_MAX_QUOTA 240
#define HORDE_BREAK_TICKS (15 * 60)
typedef struct {
    uint32_t revision, wave, completed, remaining, alive, kills, revives;
    uint32_t timer, spawn_timer, next_id, rng, elapsed;
    uint8_t phase, players;
    uint8_t reserved[2]; /* identical layout even under the game's packed AI headers */
} HordeState;
static inline uint32_t hordeRandom(HordeState *s) {
    uint32_t x = s->rng ? s->rng : 1;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return s->rng = x;
}
static inline unsigned hordeCapacity(unsigned players) {
    return players <= 1 ? 8 : players == 2 ? 12 : 16;
}
static inline unsigned hordeQuota(uint32_t wave, unsigned players) {
    /* Cap duration as well as concurrency. Arithmetic stays bounded forever. */
    unsigned base = 6 + 2 * (wave > 40 ? 39 : wave ? wave - 1 : 0);
    unsigned count = (base * (players + 1) + 1) / 2;
    return count > HORDE_MAX_QUOTA ? HORDE_MAX_QUOTA : count;
}
static inline void hordeInit(HordeState *s, uint32_t seed) {
    *s = (HordeState){0};
    s->timer = HORDE_BREAK_TICKS; s->next_id = 1; s->rng = seed ? seed : 1;
}
/* Returns a phase change. Only successful allocation consumes remaining. */
static inline int hordeAdvance(HordeState *s, unsigned ticks, unsigned players) {
    if (s->phase == HORDE_DEFEAT || !ticks) return 0;
    if (ticks > 15) ticks = 15; /* no burst after suspend/loading */
    if (UINT32_MAX - s->elapsed >= ticks) s->elapsed += ticks;
    if (s->phase == HORDE_WAVE) {
        s->spawn_timer = s->spawn_timer > ticks ? s->spawn_timer - ticks : 0;
        if (s->remaining || s->alive) return 0;
        s->completed = s->wave; s->phase = HORDE_INTERMISSION;
        s->timer = HORDE_BREAK_TICKS; s->revision++; return 1;
    }
    s->timer = s->timer > ticks ? s->timer - ticks : 0;
    if (s->timer) return 0;
    if (s->wave < UINT32_MAX) s->wave++;
    s->players = players < 1 ? 1 : players > 4 ? 4 : players;
    s->remaining = hordeQuota(s->wave, s->players);
    s->phase = HORDE_WAVE; s->spawn_timer = 0; s->revision++; return 1;
}
static inline int hordeCanSpawn(const HordeState *s) {
    return s->phase == HORDE_WAVE && s->remaining && !s->spawn_timer
        && s->alive < hordeCapacity(s->players);
}
static inline void hordeSpawned(HordeState *s) {
    s->remaining--; s->alive++; s->revision++;
    s->spawn_timer = s->wave < 15 ? 90 - s->wave * 4 : 30;
}
static inline void hordeEnemyGone(HordeState *s, int killed) {
    if (s->alive) s->alive--;
    if (killed) { if (s->kills<UINT32_MAX) s->kills++; }
    else if (s->remaining<HORDE_MAX_QUOTA) s->remaining++;
    s->revision++;
}
static inline int hordeStateValid(const HordeState *s) {
    return s->phase <= HORDE_DEFEAT && s->players <= 4
        && s->remaining <= HORDE_MAX_QUOTA && s->alive <= 16
        && s->timer <= HORDE_BREAK_TICKS && s->spawn_timer <= 90
        && s->completed <= s->wave && s->next_id != 0;
}
static inline void hordeStatus(const HordeState *s, char *text, unsigned size) {
    if (s->phase == HORDE_DEFEAT)
        snprintf(text, size, "HORDE OVER - %u WAVES - %u KILLS", s->completed, s->kills);
    else if (s->phase == HORDE_WAVE)
        snprintf(text, size, "WAVE %u - %u ENEMIES LEFT", s->wave, s->remaining + s->alive);
    else snprintf(text, size, "WAVE %u IN %u - RESUPPLY", s->wave == UINT32_MAX ? s->wave : s->wave + 1, (s->timer + 59) / 60);
}
#endif
