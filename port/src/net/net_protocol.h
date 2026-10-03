#ifndef _NET_PROTOCOL_H
#define _NET_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <ultra64.h>
#include "bondtypes.h"
#include "net/netbuf.h"
#include "net_rules.h"
#include "net_match.h"

#define GEVR_NET_MAGIC           0x47455652  /* "GEVR" */
#define GEVR_NET_VERSION         16  /* 16: co-op mode (mode, difficulty in the match config; guard, mission and revive messages); 15: clock synchronization, timestamped shots, epoch/life IDs; 14: next-round fun settings; 13: team voice routing; 12: friendly fire and authoritative ammo transforms; 11: voice modes, pending/active teams, elimination, ping; 10: the owner's health, armour and death in PLAYER_STATE; 9: the match config, spectators, loadouts, the left hand; 8: votes, host migration; 7: gun aim, projectile/explosion/object events */
#define GEVR_DEFAULT_PORT        27007
#define GEVR_DISCOVERY_PORT      27008
#define GEVR_MAX_PLAYERS         4
#define GEVR_MAX_NAME_LEN        24
#define GEVR_VOIP_MAX_BYTES      200 /* a 20 ms Opus frame at 24 kb/s averages ~60 bytes; VBR peaks higher */

/* ENet Channels */
enum {
    NET_CHAN_RELIABLE = 0,      /* Match state, lobby, damage, weapon events */
    NET_CHAN_PLAYER_STATE = 1,   /* Unreliable sequenced player commands / transforms */
    NET_CHAN_VOIP = 2,          /* Voice chat audio packets */
    NET_CHAN_MAX
};

/* User command bitflags (adapted from Perfect Dark port-net) */
#define UCMD_FIRE           (1 << 0)
#define UCMD_ACTIVATE       (1 << 1)
#define UCMD_RELOAD         (1 << 2)
#define UCMD_AIMMODE        (1 << 3)
#define UCMD_DUCK           (1 << 4)
#define UCMD_SELECT         (1 << 7)
#define UCMD_SELECT_DUAL    (1 << 8)
#define UCMD_AIMVALID       (1 << 9)    /* aimorigin/aimdir hold the right gun's barrel */
#define UCMD_FIRE_LEFT      (1 << 10)   /* the left gun's trigger (dual wielding) */
#define UCMD_AIMVALID_LEFT  (1 << 11)   /* aimorigin_l/aimdir_l hold the left gun's barrel */

/* Packet Opcodes */
typedef enum {
    NET_MSG_NONE = 0,
    
    /* Handshake & Lobby */
    NET_MSG_HELLO = 1,          /* Client -> Server */
    NET_MSG_WELCOME = 2,        /* Server -> Client */
    NET_MSG_LOBBY_STATE = 3,    /* Server -> All */
    NET_MSG_LOBBY_READY = 4,    /* Client -> Server */
    NET_MSG_START_MATCH = 5,    /* Server -> All */
    NET_MSG_LOBBY_CHARACTER = 6, /* Client -> Server (change character without re-handshaking) */
    
    /* In-Match State */
    NET_MSG_PLAYER_STATE = 10,  /* Player -> Server / Peers */
    NET_MSG_FIRE_EVENT = 11,    /* Shooter -> Server / Peers */
    NET_MSG_HIT_REPORT = 12,    /* Shooter -> Server */
    NET_MSG_DAMAGE_EVENT = 13,  /* Server -> All */
    NET_MSG_RESPAWN = 14,       /* Respawning player -> host -> peers */
    NET_MSG_MATCH_END = 15,     /* Server -> All */
    NET_MSG_STAGE_READY = 16,   /* Loaded client -> host */
    NET_MSG_ROUND_RESET = 17,   /* Host -> all, reload stage */
    NET_MSG_ROUND_PHASE = 18,   /* Host -> all, warmup/active clock */
    NET_MSG_MATCH_SNAPSHOT = 19,/* Host -> loaded late joiner */
    
    /* Voice Chat */
    NET_MSG_VOIP_FRAME = 20,    /* Player -> Server / Peers */
    NET_MSG_WORLD_SNAPSHOT = 21,/* Host -> loaded late joiner: pickups and doors */
    NET_MSG_APP_VERSION = 22,   /* Player -> Host / Peers: app version string */

    /* What a player does to the world (owner -> host -> peers) */
    NET_MSG_PROJECTILE = 23,    /* a thrown or launched projectile: spawner, point, velocity */
    NET_MSG_EXPLOSION = 24,     /* a damaging explosion the player caused */
    NET_MSG_OBJECT_STATE = 25,  /* a pickup collected or a door used */
    NET_MSG_COUNTDOWN = 26,     /* Host -> all: the next round starts in N ms (0 cancels) */
    NET_MSG_VOTE = 27,          /* Player -> host: my vote (ballot kind, value; 0xFF none) */
    NET_MSG_VOTES = 28,         /* Host -> all: a ballot's votes, every slot */
    NET_MSG_LOBBY_HANDOFF = 29, /* Host -> client: lobby code, owner token, game name, max players (host migration) */
    NET_MSG_LOBBY_TEAM = 31,    /* Client -> host: pending team choice */
    NET_MSG_AMMO_IMPULSE = 32,  /* Shooter -> host: setup crate ID and world direction */
    NET_MSG_AMMO_STATE = 33,    /* Host -> peers: crate transforms and respawn state */
    NET_MSG_CLOCK = 34,        /* Host probe -> client reply, four-timestamp clock synchronization */
    NET_MSG_LOBBY_LOADOUT = 30, /* Client -> host: my four spawn guns */

    /* Co-op (protocol 16, #94) */
    NET_MSG_COOP_END = 35,      /* Host -> all: the mission ended (success, the next mission, the delay) */
    NET_MSG_CHR_STATE = 36,     /* Host -> all, unreliable: guards as the host runs them (NetChrState) */
    NET_MSG_CHR_SPAWN = 37,     /* Host -> all: a guard the host's AI spawned */
    NET_MSG_CHR_REMOVE = 38,    /* Host -> all: a guard the host removed */
    NET_MSG_COOP_DAMAGE = 39,   /* Host -> all: a guard hurt a player (the target slot, the damage, its direction) */
    NET_MSG_COOP_HIT = 40,      /* Client -> host: my player hit a guard (its host slot, the part, the gun, the direction) */
} NetMsgType;

/*
 * A guard as the host runs it (co-op, #94): what a puppet on another headset
 * needs to stand, move, animate, aim and fire where the host's does. slot is
 * the guard's index in g_ChrSlots on the host. The animation is its offset
 * in the animation segment (ptr_animation_table), the same on every headset.
 */
enum {
    NET_CHR_HIDDEN = 1,         /* CHRFLAG_HIDDEN */
    NET_CHR_FIRE_RIGHT = 2,     /* its right gun's fire shows (weaponIsGunfireVisible) */
    NET_CHR_FIRE_LEFT = 4,
    NET_CHR_FLIP = 8,           /* the animation is mirrored (model gunhand) */
    NET_CHR_NO_TRANSLATE = 16,  /* CHRFLAG_IGNORE_ANIM_TRANSLATION */
    NET_CHR_INVINCIBLE = 32,    /* CHRFLAG_INVINCIBLE */
};
#define NET_CHR_NO_ANIM 0xFFFF
#define NET_CHR_STATE_BYTES 44
#define NET_CHR_STATES_PER_PACKET 24
typedef struct {
    u16 slot;
    u8 flags;                   /* NET_CHR_* */
    u8 actiontype;              /* ACT_TYPE */
    coord3d pos;                /* prop->pos */
    f32 ground;
    u16 yaw;                    /* the model's subroty, 0..65535 for 0..2 pi */
    u16 anim;                   /* the animation's offset, NET_CHR_NO_ANIM none */
    f32 frame;                  /* animframe1 */
    f32 speed;                  /* the model's speed (negative backwards) */
    s16 aim[4];                 /* aimendlshoulder, aimendrshoulder, aimendback, aimendsideback, x 10000 */
    u8 weapon[2];               /* the held guns' ITEM_IDS, 0 none */
    u8 fade;                    /* fadealpha */
    u8 damage;                  /* damage / maxdamage, x 100 (0 unhurt .. 100 dead) */
} NetChrState;
static inline void netbufWriteChrState(struct netbuf *b, const NetChrState *c) {
    netbufWriteU16(b,c->slot);netbufWriteU8(b,c->flags);netbufWriteU8(b,c->actiontype);
    netbufWriteCoord(b,&c->pos);netbufWriteF32(b,c->ground);
    netbufWriteU16(b,c->yaw);netbufWriteU16(b,c->anim);
    netbufWriteF32(b,c->frame);netbufWriteF32(b,c->speed);
    for(int i=0;i<4;i++)netbufWriteS16(b,c->aim[i]);
    netbufWriteU8(b,c->weapon[0]);netbufWriteU8(b,c->weapon[1]);netbufWriteU8(b,c->fade);netbufWriteU8(b,c->damage);
}
static inline int netbufReadChrState(struct netbuf *b, NetChrState *c) {
    c->slot=netbufReadU16(b);c->flags=netbufReadU8(b);c->actiontype=netbufReadU8(b);
    netbufReadCoord(b,&c->pos);c->ground=netbufReadF32(b);
    c->yaw=netbufReadU16(b);c->anim=netbufReadU16(b);
    c->frame=netbufReadF32(b);c->speed=netbufReadF32(b);
    for(int i=0;i<4;i++)c->aim[i]=netbufReadS16(b);
    c->weapon[0]=netbufReadU8(b);c->weapon[1]=netbufReadU8(b);c->fade=netbufReadU8(b);c->damage=netbufReadU8(b);
    return !b->error && isfinite(c->pos.x) && isfinite(c->pos.y) && isfinite(c->pos.z) && fabsf(c->pos.x) < 1.0e6f &&
        fabsf(c->pos.y) < 1.0e6f && fabsf(c->pos.z) < 1.0e6f && isfinite(c->ground) && fabsf(c->ground) < 1.0e6f &&
        isfinite(c->frame) && fabsf(c->frame) < 1.0e5f && isfinite(c->speed) && fabsf(c->speed) < 100.0f &&
        c->weapon[0] < ITEM_IDS_MAX && c->weapon[1] < ITEM_IDS_MAX && c->damage <= 100;
}

/* Protocol 15 combat identity. Shot IDs identify one firing action; hit IDs
 * identify individual pellets/penetrations, so replay protection preserves them. */
typedef struct {
    uint64_t epoch, shot_us;
    uint32_t shooter_life, target_life, shot_id, hit_id;
    uint8_t target, weapon, part;
    float hx, hy, hz, damage;
} NetHitReport;
#define NET_HIT_REPORT_BYTES 51
static inline void netbufWriteHitReport(struct netbuf *b,const NetHitReport *h) {
    netbufWriteU64(b,h->epoch);netbufWriteU64(b,h->shot_us);
    netbufWriteU32(b,h->shooter_life);netbufWriteU32(b,h->target_life);
    netbufWriteU32(b,h->shot_id);netbufWriteU32(b,h->hit_id);
    netbufWriteU8(b,h->target);netbufWriteU8(b,h->weapon);netbufWriteU8(b,h->part);
    netbufWriteF32(b,h->hx);netbufWriteF32(b,h->hy);netbufWriteF32(b,h->hz);netbufWriteF32(b,h->damage);
}
static inline int netbufReadHitReport(struct netbuf *b,NetHitReport *h) {
    h->epoch=netbufReadU64(b);h->shot_us=netbufReadU64(b);
    h->shooter_life=netbufReadU32(b);h->target_life=netbufReadU32(b);
    h->shot_id=netbufReadU32(b);h->hit_id=netbufReadU32(b);
    h->target=netbufReadU8(b);h->weapon=netbufReadU8(b);h->part=netbufReadU8(b);
    h->hx=netbufReadF32(b);h->hy=netbufReadF32(b);h->hz=netbufReadF32(b);h->damage=netbufReadF32(b);
    return !b->error && h->epoch && h->shot_us && h->shooter_life && h->target_life && h->shot_id && h->hit_id
        && h->target<GEVR_MAX_PLAYERS && h->weapon<ITEM_IDS_MAX && isfinite(h->damage) && h->damage>0
        && isfinite(h->hx) && isfinite(h->hy) && isfinite(h->hz);
}
typedef struct { uint64_t epoch,t0,t1,t2;uint32_t nonce;uint8_t reply; } NetClockExchange;
#define NET_CLOCK_EXCHANGE_BYTES 37
static inline void netbufWriteClock(struct netbuf *b,const NetClockExchange *c) {
    netbufWriteU8(b,c->reply);netbufWriteU64(b,c->epoch);netbufWriteU32(b,c->nonce);
    netbufWriteU64(b,c->t0);netbufWriteU64(b,c->t1);netbufWriteU64(b,c->t2);
}
static inline int netbufReadClock(struct netbuf *b,NetClockExchange *c) {
    c->reply=netbufReadU8(b);c->epoch=netbufReadU64(b);c->nonce=netbufReadU32(b);
    c->t0=netbufReadU64(b);c->t1=netbufReadU64(b);c->t2=netbufReadU64(b);
    return !b->error && c->reply<=1 && c->epoch && c->nonce && c->t0
        && (c->reply ? c->t1 && c->t2>=c->t1 : !c->t1 && !c->t2);
}

/* NET_MSG_OBJECT_STATE actions */
enum {
    NET_OBJECT_PICKUP = 1,      /* value: the collector's tick operation */
    NET_OBJECT_DOOR = 2,        /* value: the door's new DOORSTATE */
    NET_OBJECT_SPECIAL_TAKEN = 3, /* value: the item (the Golden Gun, the flag) the player now holds; index unused */
};

/* The ballots of NET_MSG_VOTE / NET_MSG_VOTES */
enum {
    NET_BALLOT_STAGE = 0,       /* a stage index (net_match.c) */
    NET_BALLOT_WEAPONS = 1,     /* a weapon set */
    NET_BALLOT_COUNT
};

/*
 * The match as the host has set it: what every headset applies before each
 * stage load (net_core.c netApplyMatchConfig). In LOBBY_STATE, WELCOME and
 * START_MATCH.
 */
typedef struct {
    uint8_t stage;          /* LEVELID */
    uint8_t scenario;       /* MPSCENARIOS, 0..7 */
    uint8_t weapon_set;     /* 0..13 GoldenEye's, NET_WEAPON_SET_CUSTOM the host's own four */
    uint8_t game_length;    /* front.c multi_game_lengths index, 0..7 */
    uint8_t health;         /* front.c MP_handicap_table index, 0..10, for everyone */
    uint8_t dual_wield;     /* NET_DUAL_OFF / DOUBLES / ANY */
    uint8_t loadouts;       /* 1: every player spawns with its own four guns */
    uint8_t next_round;     /* NET_NEXT_VOTE / SHUFFLE / PLAYLIST (the host's rows show it) */
    uint8_t friendly_fire;  /* host controlled, applies live; 1 preserves native damage */
    uint8_t voice_mode;     /* NET_VOICE_PROXIMITY / COUCH, applies live */
    uint8_t fun_flags;      /* NET_FUN_*: next round only */
    uint8_t gun_size;       /* NET_GUN_NORMAL / TINY / BIG: visuals only */
    uint8_t custom_set[4];  /* the custom set's guns, ITEM_IDS */
    uint8_t mode;           /* NET_MODE_DEATHMATCH / NET_MODE_COOP (protocol 16) */
    uint8_t difficulty;     /* co-op: DIFFICULTY_AGENT .. DIFFICULTY_007 */
} NetMatchConfig;

/* No native pointers or structure padding enter the wire format. */
#define NET_AMMO_STATE_BYTES 116
typedef struct {
    u16 index;
    coord3d pos, runtime_pos, speed;
    Mtxf mtx, rotation;
    u32 regen;
    u8 enabled, moving;
} NetAmmoState;
static inline int netAmmoStateValid(const NetAmmoState *s) {
    if (s->index >= 0x8000 || s->regen > 1200 || s->enabled > 1 || s->moving > 1) return 0;
    for (int i=0;i<3;i++) {
        if (!isfinite(s->pos.f[i]) || fabsf(s->pos.f[i]) > 1000000 ||
            !isfinite(s->runtime_pos.f[i]) || fabsf(s->runtime_pos.f[i]) > 1000000 ||
            !isfinite(s->speed.f[i]) || fabsf(s->speed.f[i]) > 1000) return 0;
        for(int j=0;j<3;j++) if (!isfinite(s->mtx.m[i][j]) || fabsf(s->mtx.m[i][j]) > 1000 ||
            !isfinite(s->rotation.m[i][j]) || fabsf(s->rotation.m[i][j]) > 2) return 0;
    }
    return 1;
}
static inline void netbufWriteAmmoState(struct netbuf *b, const NetAmmoState *s) {
    netbufWriteU16(b,s->index); netbufWriteU8(b,s->enabled); netbufWriteU8(b,s->moving);
    netbufWriteU32(b,s->regen);
    for(int i=0;i<3;i++) netbufWriteF32(b,s->pos.f[i]);
    for(int i=0;i<3;i++) netbufWriteF32(b,s->runtime_pos.f[i]);
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) netbufWriteF32(b,s->mtx.m[i][j]);
    for(int i=0;i<3;i++) netbufWriteF32(b,s->speed.f[i]);
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) netbufWriteF32(b,s->rotation.m[i][j]);
}
static inline int netbufReadAmmoState(struct netbuf *b, NetAmmoState *s) {
    *s = (NetAmmoState){0}; s->mtx.m[3][3] = s->rotation.m[3][3] = 1;
    s->index=netbufReadU16(b);s->enabled=netbufReadU8(b);s->moving=netbufReadU8(b);
    s->regen=netbufReadU32(b);
    for(int i=0;i<3;i++) s->pos.f[i]=netbufReadF32(b);
    for(int i=0;i<3;i++) s->runtime_pos.f[i]=netbufReadF32(b);
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) s->mtx.m[i][j]=netbufReadF32(b);
    for(int i=0;i<3;i++) s->speed.f[i]=netbufReadF32(b);
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) s->rotation.m[i][j]=netbufReadF32(b);
    return !b->error && netAmmoStateValid(s);
}

/* The players a config's stage takes: a co-op mission takes the party's four */
static inline int netConfigMaxPlayers(const NetMatchConfig *c) {
    if (!c) return 0;
    if (c->mode == NET_MODE_COOP) return netCoopMissionIndexOf(c->stage) >= 0 ? NET_COOP_MAX_PLAYERS : 0;
    return netStageMaxPlayers(netStageIndexOf(c->stage));
}

static inline int netMatchConfigValid(const NetMatchConfig *c) {
    if (!c || c->mode > NET_MODE_COOP) return 0;
    if (c->mode == NET_MODE_COOP) {
        /* the deathmatch fields ride along unused; the mission and difficulty decide */
        if (netCoopMissionIndexOf(c->stage) < 0 || c->difficulty >= NET_DIFFICULTY_COUNT ||
            c->voice_mode > NET_VOICE_COUCH || c->friendly_fire > 1 || c->gun_size > NET_GUN_BIG) return 0;
        return 1;
    }
    if (netStageIndexOf(c->stage) < 0 || c->scenario >= netScenarioCount() ||
        c->weapon_set >= netWeaponSetCount() || c->game_length >= netGameLengthCount() ||
        c->health >= netHealthCount() || c->dual_wield > NET_DUAL_ANY || c->loadouts > 1 ||
        (c->fun_flags & ~NET_FUN_MASK) != 0 || c->gun_size > NET_GUN_BIG || c->friendly_fire > 1 || c->next_round > NET_NEXT_PLAYLIST || c->voice_mode > NET_VOICE_COUCH ||
        (netScenarioHasTeams(c->scenario) && netStageMaxPlayers(netStageIndexOf(c->stage)) < netTeamRequiredPlayers(c->scenario))) return 0;
    for(int k=0;k<4;k++) if(netItemIndexOf(c->custom_set[k]) < 0) return 0;
    return 1;
}

static inline u32 netbufWriteMatchConfig(struct netbuf *buf, const NetMatchConfig *c) {
    netbufWriteU8(buf, c->stage);
    netbufWriteU8(buf, c->scenario);
    netbufWriteU8(buf, c->weapon_set);
    netbufWriteU8(buf, c->game_length);
    netbufWriteU8(buf, c->health);
    netbufWriteU8(buf, c->dual_wield);
    netbufWriteU8(buf, c->loadouts);
    netbufWriteU8(buf, c->next_round);
    netbufWriteU8(buf, c->friendly_fire);
    netbufWriteU8(buf, c->voice_mode);
    netbufWriteU8(buf, c->fun_flags);
    netbufWriteU8(buf, c->gun_size);
    for (int i = 0; i < 4; i++) netbufWriteU8(buf, c->custom_set[i]);
    netbufWriteU8(buf, c->mode);
    netbufWriteU8(buf, c->difficulty);
    return buf->error;
}

static inline u32 netbufReadMatchConfig(struct netbuf *buf, NetMatchConfig *c) {
    c->stage = netbufReadU8(buf);
    c->scenario = netbufReadU8(buf);
    c->weapon_set = netbufReadU8(buf);
    c->game_length = netbufReadU8(buf);
    c->health = netbufReadU8(buf);
    c->dual_wield = netbufReadU8(buf);
    c->loadouts = netbufReadU8(buf);
    c->next_round = netbufReadU8(buf);
    c->friendly_fire = netbufReadU8(buf);
    c->voice_mode = netbufReadU8(buf);
    c->fun_flags = netbufReadU8(buf);
    c->gun_size = netbufReadU8(buf);
    for (int i = 0; i < 4; i++) c->custom_set[i] = netbufReadU8(buf);
    c->mode = netbufReadU8(buf);
    c->difficulty = netbufReadU8(buf);
    return buf->error;
}

/* Player movement & input command struct (serialized via netbuf) */
struct netplayermove {
    u32 tick;
    u64 epoch, clock_us, death_us;
    u32 life_id;
    u32 ucmd;
    f32 movespeed[2];   /* [0]: forward (-1..1), [1]: strafe (-1..1) */
    f32 angles[2];      /* [0]: theta (yaw), [1]: verta (pitch) */
    f32 crosspos[2];    /* crosshair pos */
    s8  weaponnum;      /* equipped weapon ITEM_* */
    s8  crouchpos;      /* 0: stand, 1: crouch */
    coord3d pos;        /* world position */
    coord3d handpos;    /* 6DoF hand aim position in world/view */
    coord3d handrot;    /* 6DoF hand aim rotation (pitch, yaw, roll) */
    coord3d aimorigin;  /* right gun's muzzle, world space (UCMD_AIMVALID) */
    coord3d aimdir;     /* right gun's barrel direction, world space, unit length */
    s8  weaponnum_left; /* the left hand's ITEM_*, ITEM_UNARMED when empty (dual wielding) */
    coord3d aimorigin_l;/* left gun's muzzle (UCMD_AIMVALID_LEFT) */
    coord3d aimdir_l;   /* left gun's barrel direction */
    /*
     * Protocol 10: the owner's own health, armour and death, which its
     * copies mirror (net_player_sync.c). Every headset applied the host's
     * damage events to its own accounting, and those drift (armour picked up
     * on one headset, the damage-flash gate): a client died on the host's
     * headset and stayed alive on its own, its corpse ignoring its moves
     * (playtest 2026-09-30). Perfect Dark's port-net sends SVC_PLAYER_STATS.
     */
    f32 health;         /* bondhealth, 0..1 */
    f32 armour;         /* bondarmour, 0..1 */
    u8  dead;           /* bonddead */
};

/* Serialization for netplayermove */
static inline u32 netbufWritePlayerMove(struct netbuf *buf, const struct netplayermove *m) {
    netbufWriteU32(buf, m->tick);
    netbufWriteU64(buf,m->epoch);netbufWriteU64(buf,m->clock_us);netbufWriteU64(buf,m->death_us);netbufWriteU32(buf,m->life_id);
    netbufWriteU32(buf, m->ucmd);
    netbufWriteF32(buf, m->movespeed[0]);
    netbufWriteF32(buf, m->movespeed[1]);
    netbufWriteF32(buf, m->angles[0]);
    netbufWriteF32(buf, m->angles[1]);
    netbufWriteF32(buf, m->crosspos[0]);
    netbufWriteF32(buf, m->crosspos[1]);
    netbufWriteS8(buf, m->weaponnum);
    netbufWriteS8(buf, m->crouchpos);
    netbufWriteCoord(buf, &m->pos);
    netbufWriteCoord(buf, &m->handpos);
    netbufWriteCoord(buf, &m->handrot);
    netbufWriteCoord(buf, &m->aimorigin);
    netbufWriteCoord(buf, &m->aimdir);
    netbufWriteS8(buf, m->weaponnum_left);
    netbufWriteCoord(buf, &m->aimorigin_l);
    netbufWriteCoord(buf, &m->aimdir_l);
    netbufWriteF32(buf, m->health);
    netbufWriteF32(buf, m->armour);
    netbufWriteU8(buf, m->dead);
    return buf->error;
}

static inline u32 netbufReadPlayerMove(struct netbuf *buf, struct netplayermove *m) {
    m->tick = netbufReadU32(buf);
    m->epoch=netbufReadU64(buf);m->clock_us=netbufReadU64(buf);m->death_us=netbufReadU64(buf);m->life_id=netbufReadU32(buf);
    m->ucmd = netbufReadU32(buf);
    m->movespeed[0] = netbufReadF32(buf);
    m->movespeed[1] = netbufReadF32(buf);
    m->angles[0] = netbufReadF32(buf);
    m->angles[1] = netbufReadF32(buf);
    m->crosspos[0] = netbufReadF32(buf);
    m->crosspos[1] = netbufReadF32(buf);
    m->weaponnum = netbufReadS8(buf);
    m->crouchpos = netbufReadS8(buf);
    netbufReadCoord(buf, &m->pos);
    netbufReadCoord(buf, &m->handpos);
    netbufReadCoord(buf, &m->handrot);
    netbufReadCoord(buf, &m->aimorigin);
    netbufReadCoord(buf, &m->aimdir);
    m->weaponnum_left = netbufReadS8(buf);
    netbufReadCoord(buf, &m->aimorigin_l);
    netbufReadCoord(buf, &m->aimdir_l);
    m->health = netbufReadF32(buf);
    m->armour = netbufReadF32(buf);
    m->dead = netbufReadU8(buf);
    return buf->error;
}

#pragma pack(push, 1)

/* Common packet header */
typedef struct {
    uint32_t magic;     /* GEVR_NET_MAGIC */
    uint16_t version;   /* GEVR_NET_VERSION */
    uint8_t  msg_type;  /* NetMsgType */
    uint8_t  slot_id;   /* Sender player slot (0..3) */
} NetHeader;

/* Client hello */
typedef struct {
    NetHeader header;
    char      player_name[GEVR_MAX_NAME_LEN];
    uint8_t   requested_chr_id;
} NetMsgHello;

/* Server welcome (on the wire: assigned slot, the match config, the host's slot) */
typedef struct {
    NetHeader header;
    uint8_t   assigned_slot;
    NetMatchConfig config;
    uint8_t   host_slot;
} NetMsgWelcome;

/* Lobby Player Slot Info */
typedef struct {
    uint8_t   connected;
    uint8_t   ready;        /* consent to pending lobby choices */
    uint8_t   loaded;       /* active-stage acknowledgement, independent of consent */
    uint8_t   chr_id;       /* mp_chr_setup index, 0..63 */
    uint8_t   spectator;    /* joined a live round: watching until the next one */
    uint8_t   loadout[4];   /* the player's four spawn guns (config.loadouts) */
    uint8_t   team;         /* pending team, NET_TEAM_NONE until chosen */
    uint8_t   eliminated;   /* cannot respawn until the next match */
    uint16_t  ping_ms;
    char      name[GEVR_MAX_NAME_LEN];
} NetLobbySlot;

/* Lobby State Broadcast (net_core.c netBroadcastLobbyState writes it, the
 * NET_MSG_LOBBY_STATE handler reads it: the one place each) */
typedef struct {
    NetHeader     header;
    NetMatchConfig config;
    uint8_t       countdown_secs;
    NetLobbySlot  slots[GEVR_MAX_PLAYERS];
} NetMsgLobbyState;

/* Match Launch (on the wire: config, seed, player count, chr_id[4], phase) */
typedef struct {
    NetHeader header;
    NetMatchConfig config;
    uint32_t  random_seed;
} NetMsgStartMatch;

/* High-frequency player state */
typedef struct {
    NetHeader header;
    uint32_t  sequence;
    
    /* Position and movement */
    float     pos_x, pos_y, pos_z;
    float     vel_x, vel_y, vel_z;
    
    /* Head (HMD) orientation in degrees */
    float     head_yaw;
    float     head_pitch;
    
    /* Aim / Gun Hand pose */
    float     hand_yaw;
    float     hand_pitch;
    float     hand_roll;
    float     hand_x, hand_y, hand_z;
    
    /* Action / Stance */
    uint8_t   stance;        /* 0: stand, 1: crouch */
    uint8_t   weapon_id;     /* ITEM_* id */
    uint8_t   is_firing;     /* 1 if trigger pulled */
    uint8_t   pad;
} NetMsgPlayerState;

/* Hit report from local client to host */
typedef struct {
    NetHeader header;
    uint8_t   target_slot;
    uint8_t   weapon_id;
    uint8_t   hit_location;  /* Head, torso, limb */
    uint8_t   pad;
    float     hit_x, hit_y, hit_z;
    float     damage;
} NetMsgHitReport;

/* Damage & Death broadcast from host to all */
typedef struct {
    NetHeader header;
    uint8_t   target_slot;
    uint8_t   attacker_slot;
    uint8_t   weapon_id;
    uint8_t   is_dead;
    float     damage;
    float     vector_x, vector_z;
    float     new_health;
    float     new_armor;
} NetMsgDamageEvent;

/* VoIP voice packet */
typedef struct {
    NetHeader header;
    uint32_t  sequence;
    uint16_t  payload_size;
    uint8_t   payload[GEVR_VOIP_MAX_BYTES];
} NetMsgVoipFrame;

#pragma pack(pop)

#endif /* _NET_PROTOCOL_H */
