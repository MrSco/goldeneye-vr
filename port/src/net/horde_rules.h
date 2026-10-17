/* Horde's clock and quotas: independent of the engine and transport. */
#ifndef GEVR_HORDE_RULES_H
#define GEVR_HORDE_RULES_H
#include <stdint.h>
#include <stdio.h>
enum { HORDE_PREPARE, HORDE_WAVE, HORDE_INTERMISSION, HORDE_DEFEAT };
enum { HORDE_BOX_IDLE, HORDE_BOX_ROLLING, HORDE_BOX_READY, HORDE_BOX_MOVING };
enum { HORDE_GRANT_NONE, HORDE_GRANT_BOX_WEAPON, HORDE_GRANT_WALL_WEAPON, HORDE_GRANT_WALL_AMMO, HORDE_GRANT_ARMOR };
enum { HORDE_ACT_BOX, HORDE_ACT_WALL_BUY, HORDE_ACT_WALL_AMMO, HORDE_ACT_AMMO_PICKUP };
#define HORDE_AMMO_DROPS 24
#define HORDE_AMMO_LIFETIME (60 * 60)
typedef struct {
    uint32_t id, ticks;
    float pos[3];
} HordeAmmoDrop;
#define HORDE_GUARD_SLOTS 64
#define HORDE_MAX_QUOTA 240
#define HORDE_BREAK_TICKS (15 * 60)
#define HORDE_START_CREDITS 500u
#define HORDE_HIT_CREDITS 10u
#define HORDE_KILL_CREDITS 60u
#define HORDE_HEADSHOT_CREDITS 100u
#define HORDE_MAX_CREDITS 999990u
#define HORDE_BOX_COST 950u
#define HORDE_BOX_SPOTS 4u
#define HORDE_BOX_POOL_SIZE 12u
#define HORDE_BOX_ROLL_TICKS (3 * 60)
#define HORDE_BOX_CLAIM_TICKS (8 * 60)
#define HORDE_BOX_MOVE_TICKS (3 * 60)
#define HORDE_WALL_COUNT 6u
typedef struct {
    uint32_t revision, wave, completed, remaining, alive, kills, revives;
    uint32_t timer, spawn_timer, next_id, rng, elapsed;
    uint32_t credits[4], grant_seq[4], box_timer;
    uint32_t ammo_grants[4], wall_owned[4];
    HordeAmmoDrop ammo[HORDE_AMMO_DROPS];
    uint8_t phase, players, box_state, box_spot, box_buyer, box_weapon, box_uses;
    uint8_t grant_kind[4], grant_item[4];
    uint8_t reserved[1]; /* identical layout even under the game's packed AI headers */
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
static inline unsigned hordeEnemyHealthPercent(uint32_t wave) {
    unsigned bonus = wave > 21 ? 20 : wave ? wave - 1 : 0;
    return 100 + 15 * bonus; /* 100% on Wave 1 up to 400% on Wave 21+ */
}
static inline unsigned hordeEnemyWeaponTier(uint32_t wave, uint32_t serial) {
    if (wave <= 2) return 0; /* Pistols (PP7 / DD44) */
    if (wave <= 4) return (serial & 1) ? 1 : 0; /* Klobb + Pistols */
    if (wave <= 6) return 1 + (serial & 1); /* Klobb / D5K */
    if (wave <= 9) return 2 + (serial & 1); /* D5K / KF7 Soviet */
    if (wave <= 12) return 3 + (serial & 1); /* KF7 Soviet / AR33 */
    return 4 + (serial & 1); /* AR33 / RC-P90 */
}
static inline uint32_t hordeWallCost(unsigned wall_idx, int ammo_only) {
    static const uint32_t costs[HORDE_WALL_COUNT] = { 500, 750, 1200, 1400, 1800, 1500 };
    if (wall_idx >= HORDE_WALL_COUNT) return 0;
    return (ammo_only && wall_idx < 5) ? costs[wall_idx] / 2 : costs[wall_idx];
}
static inline void hordeAddCredits(HordeState *s, unsigned slot, uint32_t amount) {
    if (slot >= 4 || !amount) return;
    s->credits[slot] = HORDE_MAX_CREDITS - s->credits[slot] < amount
        ? HORDE_MAX_CREDITS : s->credits[slot] + amount;
    s->revision++;
}
static inline void hordeRewardHit(HordeState *s, unsigned slot) {
    if (s->phase != HORDE_DEFEAT) hordeAddCredits(s, slot, HORDE_HIT_CREDITS);
}
static inline void hordeInit(HordeState *s, uint32_t seed) {
    *s = (HordeState){0};
    s->timer = HORDE_BREAK_TICKS; s->next_id = 1; s->rng = seed ? seed : 1;
    for (unsigned i = 0; i < 4; i++) s->credits[i] = HORDE_START_CREDITS;
}
static inline void hordeTickBox(HordeState *s, unsigned ticks) {
    if (s->box_state == HORDE_BOX_IDLE) return;
    if (s->box_state == HORDE_BOX_ROLLING) {
        if (s->box_timer > ticks) {
            unsigned prev_step = s->box_timer / 15, next_step = (s->box_timer - ticks) / 15;
            s->box_timer -= ticks;
            if (prev_step != next_step) {
                s->box_weapon = (uint8_t)((s->box_weapon + 1 + (hordeRandom(s) % (HORDE_BOX_POOL_SIZE - 1))) % HORDE_BOX_POOL_SIZE);
                s->revision++;
            }
            return;
        }
        /* Teddy Bear relocation roll after 4+ uses (20% chance, guaranteed by 10th use). */
        if (s->box_uses >= 4 && (s->box_uses >= 10 || (hordeRandom(s) % 5) == 0)) {
            hordeAddCredits(s, s->box_buyer, HORDE_BOX_COST);
            s->box_state = HORDE_BOX_MOVING;
            s->box_timer = HORDE_BOX_MOVE_TICKS;
        } else {
            s->box_weapon = (uint8_t)(hordeRandom(s) % HORDE_BOX_POOL_SIZE);
            s->box_state = HORDE_BOX_READY;
            s->box_timer = HORDE_BOX_CLAIM_TICKS;
        }
        s->revision++;
        return;
    }
    if (s->box_timer > ticks) { s->box_timer -= ticks; return; }
    s->box_timer = 0;
    if (s->box_state == HORDE_BOX_MOVING) {
        s->box_spot = (uint8_t)((s->box_spot + 1 + (hordeRandom(s) % (HORDE_BOX_SPOTS - 1))) % HORDE_BOX_SPOTS);
        s->box_uses = 0;
    }
    s->box_state = HORDE_BOX_IDLE;
    s->revision++;
}
static inline int hordeUseBox(HordeState *s, unsigned slot) {
    if (s->phase == HORDE_DEFEAT || slot >= 4) return 0;
    if (s->box_state == HORDE_BOX_IDLE) {
        if (s->credits[slot] < HORDE_BOX_COST) return 0;
        s->credits[slot] -= HORDE_BOX_COST;
        s->box_state = HORDE_BOX_ROLLING;
        s->box_buyer = (uint8_t)slot;
        s->box_timer = HORDE_BOX_ROLL_TICKS;
        s->box_weapon = (uint8_t)(hordeRandom(s) % HORDE_BOX_POOL_SIZE);
        if (s->box_uses < 255) s->box_uses++;
        s->revision++;
        return 1;
    }
    if (s->box_state == HORDE_BOX_READY && s->box_buyer == slot) {
        s->grant_seq[slot]++;
        s->grant_kind[slot] = HORDE_GRANT_BOX_WEAPON;
        s->grant_item[slot] = s->box_weapon;
        s->box_state = HORDE_BOX_IDLE;
        s->box_timer = 0;
        s->revision++;
        return 2;
    }
    return 0;
}
static inline int hordeBuyWall(HordeState *s, unsigned slot, unsigned wall_idx, int ammo_only) {
    if (s->phase == HORDE_DEFEAT || slot >= 4 || wall_idx >= HORDE_WALL_COUNT || ammo_only) return 0;
    if (wall_idx<5 && (s->wall_owned[slot] & (1u<<wall_idx))) return 0;
    uint32_t cost = hordeWallCost(wall_idx, ammo_only);
    if (!cost || s->credits[slot] < cost) return 0;
    s->credits[slot] -= cost;
    if (wall_idx<5) s->wall_owned[slot] |= 1u<<wall_idx;
    s->grant_seq[slot]++;
    s->grant_item[slot] = (uint8_t)wall_idx;
    s->grant_kind[slot] = wall_idx == 5 ? HORDE_GRANT_ARMOR
        : (ammo_only ? HORDE_GRANT_WALL_AMMO : HORDE_GRANT_WALL_WEAPON);
    s->revision++;
    return 1;
}
/* Returns a phase change. Only successful allocation consumes remaining. */
static inline int hordeAdvance(HordeState *s, unsigned ticks, unsigned players) {
    if (s->phase == HORDE_DEFEAT || !ticks) return 0;
    if (ticks > 15) ticks = 15; /* no burst after suspend/loading */
    if (UINT32_MAX - s->elapsed >= ticks) s->elapsed += ticks;
    hordeTickBox(s, ticks);
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
static inline void hordeEnemyKilled(HordeState *s, int killed, unsigned killer_slot, int headshot) {
    if (s->alive) s->alive--;
    if (killed) {
        if (s->kills < UINT32_MAX) s->kills++;
        if (killer_slot < 4)
            hordeAddCredits(s, killer_slot, headshot ? HORDE_HEADSHOT_CREDITS : HORDE_KILL_CREDITS);
    } else if (s->remaining < HORDE_MAX_QUOTA) s->remaining++;
    s->revision++;
}
/* a death no player is known to have caused: counted, paid to nobody */
static inline void hordeEnemyGone(HordeState *s, int killed) {
    hordeEnemyKilled(s, killed, 4, 0);
}
static inline int hordeStateValid(const HordeState *s) {
    for (unsigned i = 0; i < 4; i++)
        if (s->credits[i] > HORDE_MAX_CREDITS || s->grant_kind[i] > HORDE_GRANT_ARMOR) return 0;
    return s->phase <= HORDE_DEFEAT && s->players <= 4
        && s->remaining <= HORDE_MAX_QUOTA && s->alive <= 16
        && s->timer <= HORDE_BREAK_TICKS && s->spawn_timer <= 90
        && s->completed <= s->wave && s->next_id != 0
        && s->box_state <= HORDE_BOX_MOVING && s->box_spot < HORDE_BOX_SPOTS
        && s->box_buyer < 4 && s->box_weapon < HORDE_BOX_POOL_SIZE
        && s->box_timer <= HORDE_BOX_CLAIM_TICKS;
}
static inline void hordePlayerStatus(const HordeState *s, unsigned slot, char *text, unsigned size) {
    uint32_t pts = slot < 4 ? s->credits[slot] : 0;
    if (s->phase == HORDE_DEFEAT)
        snprintf(text, size, "HORDE OVER - %u WAVES - %u KILLS - %u PTS", s->completed, s->kills, pts);
    else if (s->phase == HORDE_WAVE)
        snprintf(text, size, "WAVE %u - %u ENEMIES LEFT - %u PTS", s->wave, s->remaining + s->alive, pts);
    else snprintf(text, size, "WAVE %u IN %u - %u PTS", s->wave == UINT32_MAX ? s->wave : s->wave + 1, (s->timer + 59) / 60, pts);
}
static inline void hordeStatus(const HordeState *s, char *text, unsigned size) {
    hordePlayerStatus(s, 0, text, size);
}
#endif
