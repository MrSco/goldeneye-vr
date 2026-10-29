/* Horde's clock and quotas: independent of the engine and transport. */
#ifndef GEVR_HORDE_RULES_H
#define GEVR_HORDE_RULES_H
#include <stdint.h>
#include <stdio.h>
enum { HORDE_PREPARE, HORDE_WAVE, HORDE_INTERMISSION, HORDE_DEFEAT };
enum { HORDE_BOX_IDLE, HORDE_BOX_ROLLING, HORDE_BOX_READY, HORDE_BOX_MOVING };
enum { HORDE_GRANT_NONE, HORDE_GRANT_BOX_WEAPON, HORDE_GRANT_WALL_WEAPON, HORDE_GRANT_WALL_AMMO, HORDE_GRANT_ARMOR, HORDE_GRANT_EXPLOSIVE };
enum { HORDE_ACT_BOX, HORDE_ACT_WALL_BUY, HORDE_ACT_WALL_AMMO, HORDE_ACT_AMMO_PICKUP, HORDE_ACT_POWER_PICKUP };
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
#define HORDE_WALL_COUNT 8u
#define HORDE_POWER_DROPS 4
#define HORDE_INSTAKILL_TICKS (30 * 60)
enum { HORDE_POWER_NUKE, HORDE_POWER_MAX_AMMO, HORDE_POWER_INSTAKILL, HORDE_POWER_COUNT };
typedef struct {
    uint32_t id, ticks;
    float pos[3];
    uint32_t kind;
} HordePowerDrop;
#define HORDE_HOLD_TICKS 90u
/* A fresh press starts one hold. Losing the target cancels until release. */
typedef struct { unsigned target, ticks, down; } HordeHold;
static inline int hordeHoldTick(HordeHold *h, unsigned target, int pressed, int held, unsigned ticks) {
    if (!held) { *h=(HordeHold){0}; return 0; }
    if (pressed && !h->down) { h->down=1; h->target=target; h->ticks=0; return 0; }
    if (!h->down) { h->down=1; return 0; }
    if (!target || target!=h->target) { h->target=0; return 0; }
    if (ticks>15) ticks=15;
    h->ticks+=ticks;
    if (h->ticks<HORDE_HOLD_TICKS) return 0;
    h->target=0; return 1;
}
#define HORDE_GUN_SLOTS 2u
#define HORDE_ITEM_LIMIT 128u   /* item IDs a gun slot may hold */
typedef struct {
    uint32_t revision, wave, completed, remaining, alive, kills, revives;
    uint32_t timer, spawn_timer, next_id, rng, elapsed;
    uint32_t credits[4], grant_seq[4], box_timer;
    uint32_t ammo_grants[4];
    HordeAmmoDrop ammo[HORDE_AMMO_DROPS];
    HordePowerDrop powers[HORDE_POWER_DROPS];
    uint32_t instakill_ticks, max_ammo_seq, power_seq, power_kind;
    uint32_t explosives[4][2]; /* cumulative buys survive dropped/coalesced snapshots */
    uint8_t phase, players, box_state, box_spot, box_buyer, box_weapon, box_uses;
    uint8_t grant_kind[4], grant_item[4];
    /* Two guns a player (the starting PP7 one of them), oldest first, as item
     * IDs; a grant's replaced gun for the player's headset to drop (0: none). */
    uint8_t guns[4][HORDE_GUN_SLOTS], grant_replace[4];
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
#define HORDE_MAX_ALIVE 24u   /* hordeCapacity's most, a swarm round's half again */
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
/* Special rounds (user, 2026-10-17): an Oddjob swarm on waves 5, 15, 25...
 * (Zombies' hell hounds: many small, fast, weak), bosses on 10, 20, 30... */
enum { HORDE_WAVE_NORMAL, HORDE_WAVE_SWARM, HORDE_WAVE_BOSS };
static inline unsigned hordeWaveKind(uint32_t wave) {
    if (wave >= 5 && wave % 10 == 5) return HORDE_WAVE_SWARM;
    if (wave >= 10 && wave % 10 == 0) return HORDE_WAVE_BOSS;
    return HORDE_WAVE_NORMAL;
}
/* What one spawn is: decided from the wave, the spawn's serial and how many
 * of the wave have spawned, so host and replay agree. */
enum { HORDE_ENEMY_GUARD, HORDE_ENEMY_ARMORED, HORDE_ENEMY_JANUS, HORDE_ENEMY_BOSS, HORDE_ENEMY_SWARM,
       HORDE_ENEMY_KINDS };
static inline unsigned hordeBossCount(unsigned players) {
    return players >= 3 ? 3 : players == 2 ? 2 : 1;
}
static inline unsigned hordeEnemyKind(uint32_t wave, uint32_t serial, unsigned spawned, unsigned players) {
    unsigned kind = hordeWaveKind(wave);
    if (kind == HORDE_WAVE_SWARM) return HORDE_ENEMY_SWARM;
    if (kind == HORDE_WAVE_BOSS && spawned < hordeBossCount(players)) return HORDE_ENEMY_BOSS;
    uint32_t h = serial * 2654435761u;   /* spread consecutive serials */
    if (wave >= 8 && (h >> 24) % 5 == 0) return HORDE_ENEMY_JANUS;
    if (wave >= 4 && (h >> 16) % 4 == 0) return HORDE_ENEMY_ARMORED;
    return HORDE_ENEMY_GUARD;
}
/* Health in percent of the body's own, on top of the wave's scaling. */
/* Whether an enemy's death drops an ammo crate (user, 2026-10-18: every
 * enemy dropping one was too many): one in three, by its id, so every
 * headset agrees; bosses always. A crate holds twice the old amounts. */
#define HORDE_AMMO_DROP_EVERY 3u
static inline int hordeDropsAmmo(uint32_t id, int boss) {
    if (boss) return 1;
    return ((id * 2654435761u) >> 16) % HORDE_AMMO_DROP_EVERY == 0;
}

static inline unsigned hordeEnemyKindHealthPercent(unsigned kind, unsigned players) {
    switch (kind) {
    case HORDE_ENEMY_ARMORED: return 175;
    case HORDE_ENEMY_JANUS: return 150;
    case HORDE_ENEMY_BOSS: return 1000 + 400 * (players > 1 ? players - 1 : 0);
    case HORDE_ENEMY_SWARM: return 40;
    default: return 100;
    }
}
/* Movement speed in percent: faster as waves climb, the swarm fastest. */
static inline unsigned hordeEnemySpeedPercent(uint32_t wave, unsigned kind) {
    unsigned climb = wave > 21 ? 20 : wave ? wave - 1 : 0;
    unsigned pct = 100 + 2 * climb;   /* 100% on wave 1, 140% from wave 21 */
    if (kind == HORDE_ENEMY_SWARM) pct += 180; /* 288% at wave 5, up to 320% */
    if (kind == HORDE_ENEMY_BOSS) pct = pct > 110 ? 110 : pct;
    return pct;
}
/* Guards' accuracy in percent of 00 Agent's: Agent's 60% left a whole wave
 * rarely landing a shot; climbs with the waves. Damage stays Agent's. */
static inline unsigned hordeAiAccuracyPercent(uint32_t wave) {
    unsigned climb = wave > 21 ? 20 : wave ? wave - 1 : 0;
    return 100 + 2 * climb;
}
/* A swarm round brings half as many again, in more at once. */
static inline unsigned hordeWaveQuota(uint32_t wave, unsigned players) {
    unsigned q = hordeQuota(wave, players);
    if (hordeWaveKind(wave) == HORDE_WAVE_SWARM) q += q / 2;
    return q > HORDE_MAX_QUOTA ? HORDE_MAX_QUOTA : q;
}
/* Starting later (lobby option): the waves skipped pay as if played. */
#define HORDE_MAX_START_WAVE 50u
static inline uint32_t hordeStartCredits(uint32_t start_wave) {
    uint32_t skipped = start_wave > 1 ? (start_wave > HORDE_MAX_START_WAVE ? HORDE_MAX_START_WAVE : start_wave) - 1 : 0;
    return HORDE_START_CREDITS + 400u * skipped;
}
static inline uint32_t hordeWallCost(unsigned wall_idx, int ammo_only) {
    static const uint32_t costs[HORDE_WALL_COUNT] = { 500, 750, 1200, 1400, 1800, 1500, 500, 750 };
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
/* start_wave: the first wave played (1 normally; the lobby's Start at wave). */
static inline void hordeInitAt(HordeState *s, uint32_t seed, uint32_t start_wave) {
    *s = (HordeState){0};
    s->timer = HORDE_BREAK_TICKS; s->next_id = 1; s->rng = seed ? seed : 1;
    if (start_wave > HORDE_MAX_START_WAVE) start_wave = HORDE_MAX_START_WAVE;
    s->wave = s->completed = start_wave > 1 ? start_wave - 1 : 0;
    for (unsigned i = 0; i < 4; i++) s->credits[i] = hordeStartCredits(start_wave);
}
static inline void hordeInit(HordeState *s, uint32_t seed) { hordeInitAt(s, seed, 1); }
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
static inline int hordeGunOwned(const HordeState *s, unsigned slot, unsigned item) {
    if (slot >= 4 || !item) return 0;
    for (unsigned i = 0; i < HORDE_GUN_SLOTS; i++) if (s->guns[slot][i] == item) return 1;
    return 0;
}
/* A new gun into the player's two (user, 2026-10-17: "2 guns only"): a free
 * slot, else the gun asked to go (the one in hand) or the oldest. Returns the
 * gun that went, 0 for none. */
static inline unsigned hordeTakeGun(HordeState *s, unsigned slot, unsigned item, unsigned replace) {
    if (slot >= 4 || !item || item >= HORDE_ITEM_LIMIT || hordeGunOwned(s, slot, item)) return 0;
    for (unsigned i = 0; i < HORDE_GUN_SLOTS; i++)
        if (!s->guns[slot][i]) { s->guns[slot][i] = (uint8_t)item; return 0; }
    unsigned k = 0;
    for (unsigned i = 0; i < HORDE_GUN_SLOTS; i++) if (replace && s->guns[slot][i] == replace) k = i;
    unsigned old = s->guns[slot][k];
    for (unsigned i = k; i + 1 < HORDE_GUN_SLOTS; i++) s->guns[slot][i] = s->guns[slot][i + 1];
    s->guns[slot][HORDE_GUN_SLOTS - 1] = (uint8_t)item;
    return old;
}
/* item: the ID of box_weapon's gun, for the claim; replace as hordeBuyWall. */
static inline int hordeUseBox(HordeState *s, unsigned slot, unsigned item, unsigned replace) {
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
        s->grant_replace[slot] = (uint8_t)hordeTakeGun(s, slot, item, replace);
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
/* item: the wall gun's ID (0 for armour); replace: the gun to give up. */
static inline int hordeBuyWall(HordeState *s, unsigned slot, unsigned wall_idx, int ammo_only,
                               unsigned item, unsigned replace) {
    if (s->phase == HORDE_DEFEAT || slot >= 4 || wall_idx >= HORDE_WALL_COUNT || ammo_only) return 0;
    if (wall_idx < 5 && (!item || hordeGunOwned(s, slot, item))) return 0;
    uint32_t cost = hordeWallCost(wall_idx, ammo_only);
    if (!cost || s->credits[slot] < cost) return 0;
    s->credits[slot] -= cost;
    s->grant_replace[slot] = wall_idx < 5 ? (uint8_t)hordeTakeGun(s, slot, item, replace) : 0;
    s->grant_seq[slot]++;
    s->grant_item[slot] = (uint8_t)wall_idx;
    s->grant_kind[slot] = wall_idx == 5 ? HORDE_GRANT_ARMOR
        : wall_idx >= 6 ? HORDE_GRANT_EXPLOSIVE : HORDE_GRANT_WALL_WEAPON;
    if (wall_idx >= 6) s->explosives[slot][wall_idx-6]++;
    s->revision++;
    return 1;
}
/* Returns a phase change. Only successful allocation consumes remaining. */
static inline int hordeAdvance(HordeState *s, unsigned ticks, unsigned players) {
    if (s->phase == HORDE_DEFEAT || !ticks) return 0;
    if (ticks > 15) ticks = 15; /* no burst after suspend/loading */
    if (UINT32_MAX - s->elapsed >= ticks) s->elapsed += ticks;
    hordeTickBox(s, ticks);
    if (s->instakill_ticks) {
        s->instakill_ticks=s->instakill_ticks>ticks?s->instakill_ticks-ticks:0;
        if (!s->instakill_ticks) s->revision++;
    }
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
    s->remaining = hordeWaveQuota(s->wave, s->players);
    s->phase = HORDE_WAVE; s->spawn_timer = 0; s->revision++; return 1;
}
static inline unsigned hordeWaveCapacity(const HordeState *s) {
    unsigned cap = hordeCapacity(s->players);
    return hordeWaveKind(s->wave) == HORDE_WAVE_SWARM ? cap + cap / 2 : cap;
}
static inline int hordeCanSpawn(const HordeState *s) {
    return s->phase == HORDE_WAVE && s->remaining && !s->spawn_timer
        && s->alive < hordeWaveCapacity(s);
}
/* How many of this wave have spawned (despawn replacements count again). */
static inline unsigned hordeWaveSpawned(const HordeState *s) {
    unsigned quota = hordeWaveQuota(s->wave, s->players);
    return quota > s->remaining ? quota - s->remaining : 0;
}
static inline void hordeSpawned(HordeState *s) {
    s->remaining--; s->alive++; s->revision++;
    s->spawn_timer = hordeWaveKind(s->wave) == HORDE_WAVE_SWARM ? 20 : s->wave < 15 ? 90 - s->wave * 4 : 30;
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
    for (unsigned i = 0; i < 4; i++) {
        if (s->credits[i] > HORDE_MAX_CREDITS || s->grant_kind[i] > HORDE_GRANT_EXPLOSIVE
            || s->grant_replace[i] >= HORDE_ITEM_LIMIT) return 0;
        unsigned kind=s->grant_kind[i],item=s->grant_item[i];
        if ((kind==HORDE_GRANT_BOX_WEAPON && item>=HORDE_BOX_POOL_SIZE)
            || ((kind==HORDE_GRANT_WALL_WEAPON || kind==HORDE_GRANT_WALL_AMMO) && item>=5)
            || (kind==HORDE_GRANT_ARMOR && item!=5)
            || (kind==HORDE_GRANT_EXPLOSIVE && (item<6 || item>=HORDE_WALL_COUNT))) return 0;
        for (unsigned g = 0; g < HORDE_GUN_SLOTS; g++) if (s->guns[i][g] >= HORDE_ITEM_LIMIT) return 0;
    }
    if (s->instakill_ticks>HORDE_INSTAKILL_TICKS || s->power_kind>=HORDE_POWER_COUNT) return 0;
    for (unsigned i=0;i<HORDE_POWER_DROPS;i++) {
        if (s->powers[i].kind>=HORDE_POWER_COUNT || s->powers[i].ticks>HORDE_AMMO_LIFETIME
            || (s->powers[i].id && !s->powers[i].ticks)) return 0;
        for (unsigned j=0;j<i;j++) if (s->powers[i].id && s->powers[i].id==s->powers[j].id) return 0;
    }
    return s->phase <= HORDE_DEFEAT && s->players <= 4
        && s->remaining <= HORDE_MAX_QUOTA && s->alive <= HORDE_MAX_ALIVE
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
    else if (s->phase == HORDE_WAVE) {
        static const char *kinds[] = { "WAVE", "ODDJOB SWARM - WAVE", "BOSS WAVE" };
        snprintf(text, size, "%s %u - %u ENEMIES LEFT - %u PTS", kinds[hordeWaveKind(s->wave)], s->wave,
                 s->remaining + s->alive, pts);
    } else snprintf(text, size, "WAVE %u IN %u - %u PTS", s->wave == UINT32_MAX ? s->wave : s->wave + 1, (s->timer + 59) / 60, pts);
}
static inline void hordeStatus(const HordeState *s, char *text, unsigned size) {
    hordePlayerStatus(s, 0, text, size);
}
#endif
