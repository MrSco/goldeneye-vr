/*
 * Online co-op (#94): the host runs the guards; every other headset shows them
 * as puppets of the host's.
 *
 * Each headset loads the same mission from the same seed, so the setup's
 * guards stand in the same g_ChrSlots on every headset at load, and a guard
 * is named by its slot on the host. Ten times a second the host sends each
 * guard that changed (NetChrState: where it stands and faces, its animation
 * and frame, its aim, its guns, its fire, its fade and its hurt), and every
 * guard once a second. A client runs no guard AI and no background lists
 * (chr.c chrTick, chrprop.c chrpropTick): each guard plays the host's
 * animation through the game's own animation and ground code, and is
 * corrected toward the host's position. A spawn and a removal travel
 * reliably; a spawned guard takes whatever slot is free here, so a client
 * keeps a map from the host's slots to its own.
 *
 * The host's guards act on every player, not only its own: each guard's AI
 * and fire run as a target player (set_cur_player), the nearest living one,
 * or for ten seconds whoever shot it. A guard's damage to another player's
 * copy goes to that player's headset (NET_MSG_COOP_DAMAGE). A player's shot,
 * blow or throw that hits a guard on a client goes to the host as a report
 * (NET_MSG_COOP_HIT), which applies it as the game does its own; a client
 * never hurts a guard itself, and the host does not hurt one with another
 * player's copy, whose owner reports.
 */
#include "net_core.h"   /* first, as in net_core.c: <stdbool.h> before the game's headers */
#include "net_coop.h"
#include "net_protocol.h"
#include "net_voice.h"
#include "net/netbuf.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "system.h"
#include "game/chr.h"
#include "game/initanitable.h"
#include "game/model.h"
#include "game/objecthandler.h"
#include "game/player.h"
#include "game/stan.h"

#define COOP_LOG(...) sysLogPrintf(LOG_NOTE, "coop: " __VA_ARGS__)

#define COOP_MAX_SLOTS 512
#define COOP_SEND_INTERVAL_US 100000ull     /* ten state packets a second */
#define COOP_REFRESH_US 1000000ull          /* every guard at least once a second */

extern s32 g_GlobalTimer;
extern enum PROP getPropForHeldItem(ITEM_IDS item);
extern void chrUpdateAnim(ChrRecord *chr, s32 tickamount);
extern void chrStopFiring(ChrRecord *self);
extern void chrSetFiring(ChrRecord *self, s32 hand, s32 firing);
extern void sub_GAME_7F02BFE4(ChrRecord *self, s32 hand, s32 firing);   /* a guard's gun sound */
extern void chrpropActivateThisFrame(PropRecord *prop);
extern void chrpropEnable(PropRecord *prop);
extern AIRecord *ailistFindById(s32 ID);
extern void record_damage_kills(f32 damage_amount, f32 vectorx, f32 vectorz, s32 playerid, s32 affects_armor);
/* The game's bool is s32 (bondtypes.h); this file, after <stdbool.h>, would
 * see a one-byte one, so these are declared here as the game defines them. */
extern s32 chraiGetAIListID(AIRecord *AIList, s32 *isGlobalAIList);
extern s32 handles_shot_actors(ChrRecord *self, s32 hitpart, coord3d *vector, s32 weaponid, s32 isPlayer);
extern void objFreePermanently(ObjectRecord *obj, s32 freeprop);
extern void propobjSetDropped(PropRecord *prop, DROPTYPE droptype);
extern Model *retrieve_header_for_body_and_head(s32 body, s32 head, u32 bitflags);
extern void chrlvMergeKneelToStand(ChrRecord *self, f32 mergetime);
extern bool netSlotOccupied(int slot);
extern s32 chrGetNumFree(void);
extern bool netSlotIsSpectator(int slot);
extern s32 getPlayerCount(void);
extern int bondinvAddInvItem(int item);
extern ObjectRecord *objFindByTagId(s32 TagID);
extern u32 *ptr_last_tag_entry_type16;

static void coopHostMission(u64 now);
static void coopMissionReset(void);
static bool s_downed[4];                  /* revive: each player down, not dead (every headset's view) */
static int s_host_sees_me_down = -1;      /* a teammate's headset: the host's last word on its player, -1 none */
static u64 s_down_sent_us;
static void coopReviveReset(void);
static void coopAnnounceDowned(int slot, bool down);
static void coopDropInReset(void);
static void coopRecordSetup(void);
static void coopHostDropIn(u64 now);
struct netbuf;
static void coopHeader(struct netbuf *b, u8 type);
static void coopSendEvent(u8 kind, s32 a, s32 b2, int nargs);
static void coopWriteSpawn(struct netbuf *buf, ChrRecord *chr, s32 slot, AIRecord *ailist, s32 spawnflags);
static bool coopTeammateUp(int i);

/* Drop-in and a host change (below, "Drop-in, drop-out and a host change") */
#define COOP_BACKGROUND_MAX 64
typedef struct {
    s16 ailist;            /* its AI list's id, -1 none */
    u16 aioffset;
    s16 aireturnlist;
    u8 morale, alertness, flags2, random;
    s32 timer60;
    s16 padpreset1, chrpreset1, chrseeshot, chrseedie;
    u32 chrflags;
    u16 hidden;
    s8 sleep;
    bool valid;
} CoopAi;

static bool s_setup[COOP_MAX_SLOTS];
static bool s_setup_recorded;
static bool s_spawned[COOP_MAX_SLOTS];
static s32 s_spawn_flags[COOP_MAX_SLOTS];
static CoopAi s_ai[COOP_MAX_SLOTS];          /* a client: the host's guards' AI, by the host's slot */
static CoopAi s_ai_bg[COOP_BACKGROUND_MAX];  /* and its background lists (g_ActiveChrs) */
static u64 s_ai_sent_us;
static u64 s_stage_loaded_us;
static u64 s_roster_at_us[4];
static u8 s_roster_left[4];
static bool s_roster_remap[4];
static bool s_roster_join[4];               /* a fresh joiner's first roster: start it beside a teammate */
static bool s_remap_valid;                  /* the host took over: its remap is the guards' names for anyone */
static u64 s_await_since_us;
static bool s_await_remap;                   /* a client between hosts: the new host's slots are not yet known */
static u16 s_remap_old[COOP_MAX_SLOTS], s_remap_new[COOP_MAX_SLOTS];   /* the new host: old host's slot -> its own */
static int s_remap_count;

/* ---- The animation's identity ---- */

static u16 coopAnimId(const ModelAnimation *anim)
{
    uintptr_t base = (uintptr_t)ptr_animation_table->data;
    if (!anim || (uintptr_t)anim < base || (uintptr_t)anim - base >= NET_CHR_NO_ANIM) return NET_CHR_NO_ANIM;
    return (u16)((uintptr_t)anim - base);
}

/* Only one of the game's own chr animations (animation_table_ptrs1, relocated, ends at 0) */
static ModelAnimation *coopAnimById(u16 id)
{
    uintptr_t want;
    if (id == NET_CHR_NO_ANIM) return NULL;
    want = (uintptr_t)ptr_animation_table->data + id;
    for (s32 i = 0; animation_table_ptrs1[i] != 0; i++)
        if (animation_table_ptrs1[i] == want) return (ModelAnimation *)want;
    return NULL;
}

static s32 coopSlotOf(const ChrRecord *chr)
{
    if (!g_ChrSlots || chr < g_ChrSlots || chr >= g_ChrSlots + g_NumChrSlots) return -1;
    return (s32)(chr - g_ChrSlots);
}

static int coopChrLive(const ChrRecord *chr)
{
    return chr && chr->model && chr->chrnum >= 0 && chr->prop && chr->prop->type == PROP_TYPE_CHR;
}

static u8 coopWeaponOf(ChrRecord *chr, s32 hand)
{
    PropRecord *prop = chrGetEquippedWeaponProp(chr, hand);
    return prop && prop->weapon && prop->weapon->weaponnum > 0 && prop->weapon->weaponnum < ITEM_IDS_MAX ?
        (u8)prop->weapon->weaponnum : 0;
}

/* ---- Host: the guards' state ---- */

typedef struct {
    NetChrState st;
    u64 sent_us;
    u8 firecount[2];
    bool sent;
} CoopSent;
static CoopSent s_sent[COOP_MAX_SLOTS];
static u64 s_last_send_us;
static u32 s_state_seq;

static void coopCollect(ChrRecord *chr, s32 slot, NetChrState *st, CoopSent *prev)
{
    Model *model = chr->model;
    memset(st, 0, sizeof(*st));
    st->slot = (u16)slot;
    st->actiontype = (u8)chr->actiontype;
    st->pos = chr->prop->pos;
    st->ground = chr->ground;
    {
        f32 yaw = getsubroty(model);
        while (yaw < 0.0f) yaw += M_TAU_F;
        while (yaw >= M_TAU_F) yaw -= M_TAU_F;
        st->yaw = (u16)(yaw * (65536.0f / M_TAU_F));
    }
    st->anim = coopAnimId(model->anim);
    st->frame = model->animframe1;
    st->speed = model->speed;
    st->aim[0] = (s16)(chr->aimendlshoulder * 10000.0f);
    st->aim[1] = (s16)(chr->aimendrshoulder * 10000.0f);
    st->aim[2] = (s16)(chr->aimendback * 10000.0f);
    st->aim[3] = (s16)(chr->aimendsideback * 10000.0f);
    st->weapon[0] = coopWeaponOf(chr, GUNRIGHT);
    st->weapon[1] = coopWeaponOf(chr, GUNLEFT);
    st->fade = chr->fadealpha;
    {
        f32 hurt = chr->maxdamage > 0.0f ? chr->damage / chr->maxdamage : 0.0f;
        st->damage = hurt <= 0.0f ? 0 : hurt >= 1.0f ? 100 : (u8)(hurt * 100.0f);
    }
    if (chr->chrflags & CHRFLAG_HIDDEN) st->flags |= NET_CHR_HIDDEN;
    if (chr->chrflags & CHRFLAG_IGNORE_ANIM_TRANSLATION) st->flags |= NET_CHR_NO_TRANSLATE;
    if (chr->chrflags & CHRFLAG_INVINCIBLE) st->flags |= NET_CHR_INVINCIBLE;
    if (model->gunhand) st->flags |= NET_CHR_FLIP;
    /* fired since the last packet: the puppet shows it fire until the next one */
    if (chr->firecount[GUNRIGHT] != prev->firecount[GUNRIGHT]) st->flags |= NET_CHR_FIRE_RIGHT;
    if (chr->firecount[GUNLEFT] != prev->firecount[GUNLEFT]) st->flags |= NET_CHR_FIRE_LEFT;
}

/* A visible change since the last packet, or one due a refresh */
static int coopChanged(const NetChrState *a, const NetChrState *b)
{
    f32 dx = a->pos.x - b->pos.x, dy = a->pos.y - b->pos.y, dz = a->pos.z - b->pos.z;
    s32 dyaw = (s32)a->yaw - (s32)b->yaw;
    if (dyaw < 0) dyaw = -dyaw;
    if (dyaw > 32768) dyaw = 65536 - dyaw;
    return a->flags != b->flags || a->actiontype != b->actiontype || a->anim != b->anim ||
        dx * dx + dy * dy + dz * dz > 1.0f || dyaw > 100 || fabsf(a->frame - b->frame) > 0.5f ||
        a->speed != b->speed || memcmp(a->aim, b->aim, sizeof(a->aim)) || a->weapon[0] != b->weapon[0] ||
        a->weapon[1] != b->weapon[1] || a->fade != b->fade || a->damage != b->damage;
}

/* ---- The host's scripted cutscenes, on every headset ---- */

/*
 * A level's scripts (chrai.c) run on the host only: a cutscene's camera
 * (CameraSwitch, CameraOrbitPad, CameraLookAtBondFromPad), its fades and its
 * "Bond has no control" would play on the host alone. While one runs, the
 * host sends twenty times a second the camera it sees (eye and target), the
 * pad that places it among the rooms, and its screen's fade; a change of
 * state goes reliably. The other headsets watch it (bondview2.c
 * gevrCoopCinemaTick): their player stands, their screen fades with the
 * host's, and their camera is the host's. Bond is the host's player, whom
 * they see there; the other players' copies stay out of the shot.
 */
#define COOP_CINEMA_SEND_US 50000ull
#define COOP_CINEMA_STALE_US 2000000ull

typedef struct {
    u8 flags;                   /* NET_COOP_CINEMA_*: 0 none */
    f32 pos[3], pos2[3];        /* the camera's eye and target */
    s16 pad;                    /* the pad that places the camera, -1 none */
    u8 rgb[3];                  /* the host's screen fade */
    f32 frac;
} CoopCinema;

static CoopCinema s_cinema_sent, s_cinema_seen;
static u64 s_cinema_sent_us, s_cinema_seen_us;

static void coopCinemaHostTick(u64 now)
{
    CoopCinema c;
    s32 pad = -1;
    u8 raw[16 + 40];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    bool changed;

    memset(&c, 0, sizeof(c));
    c.flags = (u8)gevrCoopCinemaCollect(c.pos, c.pos2, &pad, c.rgb, &c.frac);
    c.pad = (s16)(pad >= -1 && pad < 0x7FFF ? pad : -1);
    if (!c.flags && !s_cinema_sent.flags) return;
    changed = c.flags != s_cinema_sent.flags || c.pad != s_cinema_sent.pad;
    if (!changed && now - s_cinema_sent_us < COOP_CINEMA_SEND_US) return;
    coopHeader(&buf, NET_MSG_COOP_CINEMA);
    netbufWriteU8(&buf, c.flags);
    for (int i = 0; i < 3; i++) netbufWriteF32(&buf, c.pos[i]);
    for (int i = 0; i < 3; i++) netbufWriteF32(&buf, c.pos2[i]);
    netbufWriteU16(&buf, (u16)c.pad);
    for (int i = 0; i < 3; i++) netbufWriteU8(&buf, c.rgb[i]);
    netbufWriteF32(&buf, c.frac);
    if (buf.error) return;
    netCoopBroadcast(raw, buf.wp, changed);
    if (changed) COOP_LOG("cutscene %s (camera %s, pad %d)", c.flags ? "on" : "off",
                          (c.flags & NET_COOP_CINEMA_CAMERA) ? "the host's" : "own", c.pad);
    s_cinema_sent = c;
    s_cinema_sent_us = now;
}

static void coopReceiveCinema(struct netbuf *b)
{
    CoopCinema c;
    memset(&c, 0, sizeof(c));
    c.flags = netbufReadU8(b);
    for (int i = 0; i < 3; i++) c.pos[i] = netbufReadF32(b);
    for (int i = 0; i < 3; i++) c.pos2[i] = netbufReadF32(b);
    c.pad = (s16)netbufReadU16(b);
    for (int i = 0; i < 3; i++) c.rgb[i] = netbufReadU8(b);
    c.frac = netbufReadF32(b);
    if (b->error || netbufReadLeft(b) || (c.flags & ~NET_COOP_CINEMA_MASK) || c.pad < -1) return;
    for (int i = 0; i < 3; i++)
        if (!isfinite(c.pos[i]) || !isfinite(c.pos2[i])) return;
    if (!isfinite(c.frac) || c.frac < 0.0f || c.frac > 1.0f) return;
    if (c.flags != s_cinema_seen.flags)
        COOP_LOG("the host's cutscene %s (camera %s, pad %d)", c.flags ? "on" : "off",
                 (c.flags & NET_COOP_CINEMA_CAMERA) ? "the host's" : "own", c.pad);
    s_cinema_seen = c;
    s_cinema_seen_us = sysGetMicroseconds();
}

/* bondview2.c, a teammate's headset: the host's cutscene now, 0 none (or the host gone quiet) */
int netCoopCinemaState(float *pos, float *pos2, int *pad, unsigned char *rgb, float *frac)
{
    if (!gevrCoopPuppets() || !s_cinema_seen.flags || sysGetMicroseconds() - s_cinema_seen_us > COOP_CINEMA_STALE_US)
        return 0;
    for (int i = 0; i < 3; i++) {
        pos[i] = s_cinema_seen.pos[i];
        pos2[i] = s_cinema_seen.pos2[i];
        rgb[i] = s_cinema_seen.rgb[i];
    }
    *pad = s_cinema_seen.pad;
    *frac = s_cinema_seen.frac;
    return s_cinema_seen.flags;
}

static void coopCinemaReset(void)
{
    gevrCoopCinemaReset();
    memset(&s_cinema_sent, 0, sizeof(s_cinema_sent));
    memset(&s_cinema_seen, 0, sizeof(s_cinema_seen));
    s_cinema_sent_us = s_cinema_seen_us = 0;
}

void netCoopHostTick(void)
{
    u8 raw[16 + NET_CHR_STATES_PER_PACKET * NET_CHR_STATE_BYTES];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    u64 now = sysGetMicroseconds();
    int count = 0;

    if (!netCoopActive() || !netIsHost()) return;
    coopHostMission(now);
    coopHostDropIn(now);
    coopCinemaHostTick(now);
    if (!g_ChrSlots || now - s_last_send_us < COOP_SEND_INTERVAL_US) return;
    s_last_send_us = now;
    s_state_seq++;
    for (s32 slot = 0; slot < g_NumChrSlots && slot < COOP_MAX_SLOTS; slot++) {
        ChrRecord *chr = &g_ChrSlots[slot];
        CoopSent *prev = &s_sent[slot];
        NetChrState st;
        if (!coopChrLive(chr)) continue;
        coopCollect(chr, slot, &st, prev);
        if (prev->sent && !coopChanged(&st, &prev->st) && now - prev->sent_us < COOP_REFRESH_US) continue;
        if (!count) {
            netbufStartWrite(&buf);
            netbufWriteU32(&buf, GEVR_NET_MAGIC);
            netbufWriteU16(&buf, GEVR_NET_VERSION);
            netbufWriteU8(&buf, NET_MSG_CHR_STATE);
            netbufWriteU8(&buf, (uint8_t)netGetLocalSlot());
            netbufWriteU32(&buf, s_state_seq);
            netbufWriteU8(&buf, 0);   /* the count, filled in below */
        }
        netbufWriteChrState(&buf, &st);
        prev->st = st;
        prev->sent = true;
        prev->sent_us = now;
        prev->firecount[GUNRIGHT] = chr->firecount[GUNRIGHT];
        prev->firecount[GUNLEFT] = chr->firecount[GUNLEFT];
        if (++count == NET_CHR_STATES_PER_PACKET) {
            raw[12] = (u8)count;
            netCoopBroadcast(buf.data, buf.wp, false);
            count = 0;
        }
    }
    if (count) {
        raw[12] = (u8)count;
        netCoopBroadcast(buf.data, buf.wp, false);
    }
}

/* chraction.c chrSpawnAtCoord on the host: a guard its AI spawned */
void gevrCoopChrSpawned(ChrRecord *chr, AIRecord *ailist, s32 spawnflags)
{
    u8 raw[48];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    s32 slot = coopSlotOf(chr);

    if (!netCoopActive() || slot < 0 || slot >= COOP_MAX_SLOTS) return;
    s_spawned[slot] = true;   /* not the setup's: a joiner's roster has it */
    s_spawn_flags[slot] = spawnflags;
    if (!netIsHost() || !chr->prop || !chr->model) return;
    coopWriteSpawn(&buf, chr, slot, ailist, spawnflags);
    s_sent[slot].sent = false;
    netCoopBroadcast(buf.data, buf.wp, true);
    COOP_LOG("spawn tx: slot %d chrnum %d body %d head %d", slot, chr->chrnum, chr->bodynum, chr->headnum);
}

/* A clone's final script ID, or the previous clone's reassigned ID. Reuse
 * the reliable spawn record: an existing puppet updates its ID without
 * allocating another guard. Late-join rosters already use these final IDs. */
void gevrCoopChrIdentityChanged(ChrRecord *chr)
{
    s32 slot = coopSlotOf(chr);
    if (!netCoopActive() || !netIsHost() || slot < 0 || slot >= COOP_MAX_SLOTS || !s_spawned[slot]) return;
    gevrCoopChrSpawned(chr, chr->ailist, s_spawn_flags[slot]);
}

/* chr.c chrTick on the host: a guard about to be freed (CHRHIDDEN_REMOVE) */
void gevrCoopChrRemoved(ChrRecord *chr)
{
    u8 raw[16];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    s32 slot = coopSlotOf(chr);

    if (slot >= 0 && slot < COOP_MAX_SLOTS) s_spawned[slot] = false;
    if (!netCoopActive() || !netIsHost() || slot < 0) return;
    if (slot < COOP_MAX_SLOTS) s_sent[slot].sent = false;
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_CHR_REMOVE);
    netbufWriteU8(&buf, (uint8_t)netGetLocalSlot());
    netbufWriteU16(&buf, (u16)slot);
    netbufWriteU8(&buf, 0);   /* not a roster's */
    netCoopBroadcast(buf.data, buf.wp, true);
}

/* ---- Host: whom each guard acts on ---- */

typedef struct { s8 player; s32 until60; } CoopTarget;
static CoopTarget s_target[COOP_MAX_SLOTS];
s32 g_gevrCoopGuardTick = FALSE;   /* the host is running a guard's AI or fire as its target */

static int coopPlayerTargetable(int i)
{
    struct player *pl = i >= 0 && i < 4 ? g_playerPointers[i] : NULL;
    return pl && pl->prop && !pl->bonddead && netSlotOccupied(i) && !netSlotIsSpectator(i) && !gevrCoopDowned(i);
}

s32 gevrCoopGuardTarget(ChrRecord *chr)
{
    s32 slot = coopSlotOf(chr);
    s32 best = netGetLocalSlot();
    f32 bestd = 3.4e38f;
    CoopTarget *t;

    if (slot < 0 || slot >= COOP_MAX_SLOTS || !chr->prop) return best;
    t = &s_target[slot];
    if (coopPlayerTargetable(t->player) && g_GlobalTimer < t->until60) return t->player;
    for (int i = 0; i < 4; i++) {
        if (!coopPlayerTargetable(i)) continue;
        coord3d *p = &g_playerPointers[i]->prop->pos;
        f32 dx = p->x - chr->prop->pos.x, dy = p->y - chr->prop->pos.y, dz = p->z - chr->prop->pos.z;
        f32 d = dx * dx + dy * dy * 4.0f + dz * dz;   /* another floor counts as farther */
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    t->player = (s8)best;
    t->until60 = g_GlobalTimer + 60;   /* reconsidered each second */
    return best;
}

/* The host: player shot (or struck) this guard: it turns on them for ten seconds */
void gevrCoopGuardProvoked(ChrRecord *chr, s32 player)
{
    s32 slot = coopSlotOf(chr);
    if (!netCoopActive() || !netIsHost() || slot < 0 || slot >= COOP_MAX_SLOTS || player < 0 || player >= 4) return;
    s_target[slot].player = (s8)player;
    s_target[slot].until60 = g_GlobalTimer + 600;
}

/*
 * The host's guards' grenades and rockets (chraction.c, as one is thrown or
 * launched). The game gives such an object player 0's owner bits; online
 * that would make the host's guard's blast player 0's, dropped on the host
 * when player 0 is someone else and kept from hurting teammates when it is
 * the host. explosion.c explosionCreate asks gevrCoopGuardExplosive.
 */
#define COOP_GUARD_EXPLOSIVES 32
static ObjectRecord *s_guard_explosive[COOP_GUARD_EXPLOSIVES];
static u32 s_guard_explosive_next;

void gevrCoopGuardLaunched(ObjectRecord *obj)
{
    if (!obj || !netCoopActive() || !netIsHost()) return;
    for (int i = 0; i < COOP_GUARD_EXPLOSIVES; i++)
        if (s_guard_explosive[i] == obj) return;
    s_guard_explosive[s_guard_explosive_next++ % COOP_GUARD_EXPLOSIVES] = obj;
}

/*
 * Anything blown up while the host runs a guard (chr.c, set_cur_player to
 * its target): its fire hitting a drum, its script destroying an object.
 * The target's slot would make it that player's blast, dropped on the host
 * (explosion.c) when the target is another headset's.
 */
s32 gevrCoopGuardBlastNow(void)
{
    return g_gevrCoopGuardTick && netCoopActive() && netIsHost();
}

/* One of them is going off: TRUE once (it is forgotten) */
s32 gevrCoopGuardExplosive(ObjectRecord *obj)
{
    if (!obj || !netCoopActive() || !netIsHost()) return FALSE;
    for (int i = 0; i < COOP_GUARD_EXPLOSIVES; i++) {
        if (s_guard_explosive[i] == obj) {
            s_guard_explosive[i] = NULL;
            return TRUE;
        }
    }
    return FALSE;
}

/* ---- Damage between guards and players ---- */

static void coopSendGuardDamage(s32 target, f32 damage, f32 vx, f32 vz)
{
    u8 raw[32];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_COOP_DAMAGE);
    netbufWriteU8(&buf, (uint8_t)netGetLocalSlot());
    netbufWriteU8(&buf, (u8)target);
    netbufWriteF32(&buf, damage);
    netbufWriteF32(&buf, vx);
    netbufWriteF32(&buf, vz);
    netCoopBroadcast(buf.data, buf.wp, true);
}

/*
 * bondview2.c record_damage_kills, at its top: the host's guard hurting
 * another player's copy. The copy's life is its owner's (net_player_sync.c),
 * so the damage goes to the owner, who takes it as the game would.
 */
s32 gevrCoopForwardGuardDamage(f32 damage, f32 vx, f32 vz)
{
    s32 cur = get_cur_playernum();
    if (!g_gevrCoopGuardTick || !netCoopActive() || !netIsHost() || cur == netGetLocalSlot()) return FALSE;
    if (cur >= 0 && cur < 4 && netSlotOccupied(cur) && isfinite(damage) && damage > 0.0f)
        coopSendGuardDamage(cur, damage, vx, vz);
    return TRUE;
}

/*
 * chraction.c handles_shot_actors, for a guard hit by a player: nonzero when
 * the hit is not this headset's to apply. On a client the local player's hit
 * goes to the host as a report (2: the shooter's own headset); any other
 * player's is their own headset's (1). On the host another player's copy
 * hurts no guard: its owner reports.
 */
s32 gevrCoopGuardHitElsewhere(ChrRecord *chr, s32 hitpart, coord3d *vector, s32 weaponid)
{
    extern s32 g_gevrCoopApplyingHit;
    s32 shooter = get_cur_playernum();
    s32 slot = coopSlotOf(chr);
    if (!netCoopActive() || slot < 0) return FALSE;
    if (netIsHost()) return shooter != netGetLocalSlot() && !g_gevrCoopApplyingHit;
    if (s_await_remap) return 1;   /* this headset's slots are not the new host's yet */
    if (shooter == netGetLocalSlot() && vector) {
        u8 raw[40];
        struct netbuf buf = { .data = raw, .size = sizeof(raw) };
        netbufStartWrite(&buf);
        netbufWriteU32(&buf, GEVR_NET_MAGIC);
        netbufWriteU16(&buf, GEVR_NET_VERSION);
        netbufWriteU8(&buf, NET_MSG_COOP_HIT);
        netbufWriteU8(&buf, (uint8_t)shooter);
        netbufWriteU16(&buf, (u16)netCoopHostSlotOf(slot));
        netbufWriteU8(&buf, (u8)hitpart);
        netbufWriteU8(&buf, (u8)weaponid);
        netbufWriteCoord(&buf, vector);
        netCoopBroadcast(buf.data, buf.wp, true);
        return 2;
    }
    return 1;
}

/* chraction.c chrlvExplosionDamage: a client's explosions hurt no guard (the host's copy of each does) */
s32 gevrCoopPuppets(void)
{
    return netCoopActive() && !netIsHost();
}

/* chr.c chrTick: the host runs the guards */
s32 gevrCoopHostGuards(void)
{
    return netCoopActive() && netIsHost();
}

s32 g_gevrCoopApplyingHit = FALSE;

/* The host: a client's report that its player hit a guard */
static void coopApplyHit(int shooter, struct netbuf *b)
{
    u16 slot = netbufReadU16(b);
    u8 hitpart = netbufReadU8(b);
    u8 weapon = netbufReadU8(b);
    coord3d vector;
    netbufReadCoord(b, &vector);
    if (b->error || netbufReadLeft(b) || shooter < 0 || shooter >= 4 || !g_playerPointers[shooter] ||
        slot >= g_NumChrSlots || weapon >= ITEM_IDS_MAX || !isfinite(vector.x) || !isfinite(vector.y) || !isfinite(vector.z)) return;
    ChrRecord *chr = &g_ChrSlots[slot];
    if (!coopChrLive(chr)) return;
    s32 prev = get_cur_playernum();
    set_cur_player(shooter);
    g_gevrCoopApplyingHit = TRUE;
    handles_shot_actors(chr, hitpart, &vector, weapon, TRUE);
    g_gevrCoopApplyingHit = FALSE;
    set_cur_player(prev);
    gevrCoopGuardProvoked(chr, shooter);
}

/* A client: a guard's damage to this headset's player */
static void coopApplyDamage(struct netbuf *b)
{
    u8 target = netbufReadU8(b);
    f32 damage = netbufReadF32(b);
    f32 vx = netbufReadF32(b), vz = netbufReadF32(b);
    if (b->error || netbufReadLeft(b) || target != netGetLocalSlot() || !g_playerPointers[target] ||
        !isfinite(damage) || damage <= 0.0f || damage > 100.0f || !isfinite(vx) || !isfinite(vz)) return;
    s32 prev = get_cur_playernum();
    set_cur_player(target);
    record_damage_kills(damage, vx, vz, -1, 1);
    set_cur_player(prev);
}

/* ---- Clients: the puppets ---- */

typedef struct {
    NetChrState st;
    u32 seq;
    bool valid;
    bool fresh;          /* not applied yet */
    bool firing[2];
} CoopPuppet;
static CoopPuppet s_puppet[COOP_MAX_SLOTS];   /* by the host's slot */
static s16 s_local_of[COOP_MAX_SLOTS];        /* host slot -> this headset's slot, -1 none */
static s16 s_host_of[COOP_MAX_SLOTS];         /* and back */

int netCoopHostSlotOf(int localslot)
{
    return localslot >= 0 && localslot < COOP_MAX_SLOTS ? s_host_of[localslot] : -1;
}

void netCoopStageLoaded(void)
{
    memset(s_puppet, 0, sizeof(s_puppet));
    memset(s_sent, 0, sizeof(s_sent));
    memset(s_target, 0, sizeof(s_target));
    for (int i = 0; i < COOP_MAX_SLOTS; i++) {
        /* the setup's guards: the same slots on every headset */
        s_local_of[i] = (s16)i;
        s_host_of[i] = (s16)i;
        s_target[i].player = -1;
    }
    memset(s_guard_explosive, 0, sizeof(s_guard_explosive));
    coopMissionReset();
    coopReviveReset();
    coopDropInReset();
    coopCinemaReset();
    s_last_send_us = 0;
    g_gevrCoopGuardTick = FALSE;
    g_gevrCoopApplyingHit = FALSE;
}

static ChrRecord *coopLocalChr(int hostslot)
{
    if (hostslot < 0 || hostslot >= COOP_MAX_SLOTS || !g_ChrSlots) return NULL;
    int local = s_local_of[hostslot];
    return local >= 0 && local < g_NumChrSlots ? &g_ChrSlots[local] : NULL;
}

static void coopReceiveStates(struct netbuf *b)
{
    u32 seq = netbufReadU32(b);
    u8 count = netbufReadU8(b);
    if (s_await_remap) return;   /* the new host's slots, not yet known */
    if (b->error || count == 0 || count > NET_CHR_STATES_PER_PACKET || netbufReadLeft(b) != count * NET_CHR_STATE_BYTES) return;
    for (int i = 0; i < count; i++) {
        NetChrState st;
        if (!netbufReadChrState(b, &st)) return;
        if (st.slot >= COOP_MAX_SLOTS) continue;
        CoopPuppet *p = &s_puppet[st.slot];
        if (p->valid && (int32_t)(seq - p->seq) < 0) continue;   /* an older packet */
        p->st = st;
        p->seq = seq;
        p->valid = true;
        p->fresh = true;
    }
}

/* A guard the host has, to make here (NET_MSG_CHR_SPAWN) */
typedef struct {
    u16 hostslot;
    s16 chrnum;
    s8 body, head;
    u32 flags;
    s32 listid;
    coord3d pos;
    f32 angle;
    u64 until_us;          /* waiting for a free slot until then */
} CoopSpawn;
/* One pending record per host slot: a reinforcement burst must not lose
 * guards merely because this client's old bodies haven't freed slots yet. */
#define COOP_PENDING_SPAWNS COOP_MAX_SLOTS
static CoopSpawn s_pending_spawn[COOP_PENDING_SPAWNS];
static int s_pending_spawns;

/* 1 made (or here already), 0 no free slot yet, -1 cannot be made here */
static int coopMakeSpawn(const CoopSpawn *sp)
{
    u16 hostslot = sp->hostslot;
    ChrRecord *existing = coopLocalChr(hostslot);
    if (existing && coopChrLive(existing) && !(existing->hidden & CHRHIDDEN_REMOVE) &&
        existing->bodynum == sp->body && existing->headnum == sp->head) {
        existing->chrnum = sp->chrnum;   /* already here: the setup's, or made before */
        return 1;
    }
    /* chrAllocate takes the first free slot unchecked (chr.c): none, no call.
     * A roster's removals free theirs at the guards' next chrTick. */
    if (chrGetNumFree() < 1) return 0;
    coord3d at = sp->pos, pos = sp->pos;
    f32 y;
    StandTile *stan = stanFindTileBelowPos(&at, NULL, &y);
    if (!stan) return -1;
    Model *header = retrieve_header_for_body_and_head(sp->body, sp->head, sp->flags);
    AIRecord *ailist = sp->listid >= 0 ? ailistFindById(sp->listid) : NULL;
    PropRecord *prop = header ? chrAllocate(header, &pos, sp->angle, stan, ailist) : NULL;
    if (!prop || !prop->chr) return -1;
    chrpropActivateThisFrame(prop);
    chrpropEnable(prop);
    prop->chr->headnum = sp->head;
    prop->chr->bodynum = sp->body;
    prop->chr->chrnum = sp->chrnum;
    s32 local = coopSlotOf(prop->chr);
    if (local >= 0 && local < COOP_MAX_SLOTS) {
        s_spawned[local] = true;
        s_spawn_flags[local] = (s32)sp->flags;
    }
    if (s_local_of[hostslot] >= 0 && s_local_of[hostslot] < COOP_MAX_SLOTS && s_host_of[s_local_of[hostslot]] == hostslot)
        s_host_of[s_local_of[hostslot]] = -1;
    s_local_of[hostslot] = (s16)local;
    if (local >= 0 && local < COOP_MAX_SLOTS) {
        if (s_host_of[local] >= 0 && s_host_of[local] < COOP_MAX_SLOTS && s_local_of[s_host_of[local]] == local &&
            s_host_of[local] != hostslot)
            s_local_of[s_host_of[local]] = -1;
        s_host_of[local] = (s16)hostslot;
    }
    /* a new guard: nothing of the slot's last one (a late dying state, its AI) */
    memset(&s_puppet[hostslot], 0, sizeof(s_puppet[hostslot]));
    s_ai[hostslot].valid = false;
    COOP_LOG("spawn rx: host slot %d is slot %d here (chrnum %d)", hostslot, local, sp->chrnum);
    return 1;
}

static void coopForgetPendingSpawn(u16 hostslot)
{
    for (int i = 0; i < s_pending_spawns; i++) {
        if (s_pending_spawn[i].hostslot != hostslot) continue;
        s_pending_spawn[i--] = s_pending_spawn[--s_pending_spawns];
    }
}

/* A client, each frame: spawns waiting for a slot */
static void coopRetrySpawns(void)
{
    u64 now = sysGetMicroseconds();
    for (int i = 0; i < s_pending_spawns; i++) {
        int made = coopMakeSpawn(&s_pending_spawn[i]);
        if (made == 0 && now < s_pending_spawn[i].until_us) continue;
        if (made <= 0)
            COOP_LOG("spawn rx: host slot %d body %d head %d: could not be made here",
                     s_pending_spawn[i].hostslot, s_pending_spawn[i].body, s_pending_spawn[i].head);
        s_pending_spawn[i--] = s_pending_spawn[--s_pending_spawns];
    }
}

static void coopReceiveSpawn(struct netbuf *b)
{
    CoopSpawn sp;
    sp.hostslot = netbufReadU16(b);
    sp.chrnum = netbufReadS16(b);
    sp.body = netbufReadS8(b);
    sp.head = netbufReadS8(b);
    sp.flags = netbufReadU32(b);
    sp.listid = netbufReadS32(b);
    netbufReadCoord(b, &sp.pos);
    sp.angle = netbufReadF32(b);
    if (s_await_remap) return;   /* its roster, after the remap, has it */
    if (b->error || netbufReadLeft(b) || sp.hostslot >= COOP_MAX_SLOTS || !isfinite(sp.pos.x) || !isfinite(sp.pos.y) ||
        !isfinite(sp.pos.z) || !isfinite(sp.angle) || !g_ChrSlots) return;
    coopForgetPendingSpawn(sp.hostslot);
    int made = coopMakeSpawn(&sp);
    if (made == 0 && s_pending_spawns < COOP_PENDING_SPAWNS) {
        sp.until_us = sysGetMicroseconds() + 10000000ull;
        s_pending_spawn[s_pending_spawns++] = sp;
    } else if (made <= 0) {
        COOP_LOG("spawn rx: host slot %d body %d head %d: could not be made here", sp.hostslot, sp.body, sp.head);
    }
}

/*
 * A guard the host freed. A roster's (roster 1) names a setup guard gone
 * on the host: only while this headset's slot still holds that setup guard,
 * so a second roster, or one after a remap, takes no guard it has made since.
 */
static void coopReceiveRemove(struct netbuf *b)
{
    u16 hostslot = netbufReadU16(b);
    u8 roster = netbufReadU8(b);
    if (s_await_remap || b->error || netbufReadLeft(b) || hostslot >= COOP_MAX_SLOTS || roster > 1) return;
    s32 local = s_local_of[hostslot];
    if (roster && (local != hostslot || s_spawned[local])) return;
    ChrRecord *chr = coopLocalChr(hostslot);
    coopForgetPendingSpawn(hostslot);
    if (chr && coopChrLive(chr)) chr->hidden |= CHRHIDDEN_REMOVE;   /* freed at its next chrTick */
    s_puppet[hostslot].valid = false;
    s_ai[hostslot].valid = false;
    /* the host may fill its slot again before then: a spawn there is a new guard */
    s_local_of[hostslot] = -1;
    if (local >= 0 && local < COOP_MAX_SLOTS && s_host_of[local] == hostslot) s_host_of[local] = -1;
}

static void coopSyncHand(ChrRecord *chr, s32 hand, u8 want, bool dying)
{
    u8 have = coopWeaponOf(chr, hand);
    if (have == want) return;
    if (!want) {
        /* gone on the host: dropped as the guard fell, or put away */
        if (dying) {
            /* chrTick's objDrop needs a projectile before it can detach the gun (#114). */
            propobjSetDropped(chr->weapons_held[hand], DROPTYPE_DEFAULT);
            chr->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
        }
        else if (chr->weapons_held[hand] && chr->weapons_held[hand]->obj) objFreePermanently(chr->weapons_held[hand]->obj, 1);
        return;
    }
    if (chr->weapons_held[hand] && chr->weapons_held[hand]->obj) objFreePermanently(chr->weapons_held[hand]->obj, 1);
    s32 model = (s32)getPropForHeldItem((ITEM_IDS)want);
    if (model >= 0) chrGiveWeapon(chr, model, (ITEM_IDS)want, hand == GUNLEFT ? PROPFLAG_WEAPON_LEFTHANDED : 0);
}

/*
 * chr.c chrTick, on a client, for a guard: in place of its AI, animation
 * and fire. The host's latest state for it: its animation, frame and speed,
 * its facing and aim, its guns and its fire, then the game's own animation
 * step, which walks it on the floor, corrected toward where the host has it.
 */
void netCoopPuppetTick(ChrRecord *chr, s32 tickamount)
{
    s32 local = coopSlotOf(chr);
    s32 hostslot = netCoopHostSlotOf(local);
    CoopPuppet *p = hostslot >= 0 && hostslot < COOP_MAX_SLOTS ? &s_puppet[hostslot] : NULL;
    Model *model = chr->model;

    if (!model) return;
    if (chr->actiontype == ACT_INIT) {
        /* the AI's first tick for a new guard, without the AI (chraction.c chrlvActionTick) */
        chr->chrflags |= CHRFLAG_INIT;
        chrlvMergeKneelToStand(chr, 0.0f);
        chr->sleep = 0;
    }
    if (!p || !p->valid) return;   /* nothing from the host yet: it stands */

    NetChrState *st = &p->st;
    bool dying = st->actiontype == ACT_DIE || st->actiontype == ACT_DEAD;
    if (p->fresh) {
        ModelAnimation *anim = coopAnimById(st->anim);
        s32 flip = (st->flags & NET_CHR_FLIP) != 0;
        p->fresh = false;
        if (st->flags & NET_CHR_HIDDEN) chr->chrflags |= CHRFLAG_HIDDEN;
        else chr->chrflags &= ~CHRFLAG_HIDDEN;
        if (st->flags & NET_CHR_INVINCIBLE) chr->chrflags |= CHRFLAG_INVINCIBLE;
        else chr->chrflags &= ~CHRFLAG_INVINCIBLE;
        if (st->flags & NET_CHR_NO_TRANSLATE) chr->chrflags |= CHRFLAG_IGNORE_ANIM_TRANSLATION;
        else chr->chrflags &= ~CHRFLAG_IGNORE_ANIM_TRANSLATION;
        chr->fadealpha = st->fade;
        chr->damage = chr->maxdamage * (st->damage / 100.0f);
        if (dying && chr->actiontype != st->actiontype) {
            /* down on the host: the puppet's own action holds nothing that would move it */
            memset(&chr->act_die, 0, sizeof(chr->act_die));
            chr->actiontype = st->actiontype;
            chrStopFiring(chr);
        } else if (!dying && (chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD)) {
            chr->actiontype = ACT_STAND;   /* up on the host (a late dying state for a slot used again) */
        }
        if (anim && (model->anim != anim || model->gunhand != flip)) {
            modelSetAnimation(model, anim, flip, st->frame, st->speed, dying ? 0.0f : 8.0f);
        } else if (anim) {
            f32 diff = model->animframe1 - st->frame;
            if (anim->unk07 & 1) {
                /* a looping animation: the shorter way round */
                f32 n = anim->unk04 > 0 ? (f32)anim->unk04 : 1.0f;
                while (diff > n / 2) diff -= n;
                while (diff < -n / 2) diff += n;
            }
            if (fabsf(diff) > 6.0f) modelSetAnimation(model, anim, flip, st->frame, st->speed, 0.0f);
            else if (model->speed != st->speed) modelSetAnimSpeed(model, st->speed, 4.0f);
        }
        {
            f32 yaw = st->yaw * (M_TAU_F / 65536.0f);
            setsubroty(model, yaw);
        }
        chr->aimendlshoulder = st->aim[0] / 10000.0f;
        chr->aimendrshoulder = st->aim[1] / 10000.0f;
        chr->aimendback = st->aim[2] / 10000.0f;
        chr->aimendsideback = st->aim[3] / 10000.0f;
        chr->aimendcount = 6;   /* chrUpdateAimProperties blends to it until the next packet */
        coopSyncHand(chr, GUNRIGHT, st->weapon[0], dying);
        coopSyncHand(chr, GUNLEFT, st->weapon[1], dying);
    }

    /* the host's fire: the flash and the gun's sound while it lasts */
    for (s32 hand = 0; hand < 2; hand++) {
        bool firing = !dying && (st->flags & (hand == GUNRIGHT ? NET_CHR_FIRE_RIGHT : NET_CHR_FIRE_LEFT)) != 0;
        if (!chrGetEquippedWeaponProp(chr, hand)) {
            p->firing[hand] = false;
            continue;
        }
        if (firing != p->firing[hand]) {
            sub_GAME_7F02BFE4(chr, hand, firing);   /* it reads the hand's gun: none, no call */
            p->firing[hand] = firing;
        }
        chrSetFiring(chr, hand, firing && (g_GlobalTimer & 2) != 0);
    }

    if (!model->anim) return;
    {
        coord3d cur;
        if (chr->chrflags & CHRFLAG_IGNORE_ANIM_TRANSLATION) modelTickAnim(model, tickamount, 0);
        else chrUpdateAnim(chr, tickamount);
        getsuboffset(model, &cur);
        f32 dx = st->pos.x - cur.x, dy = st->pos.y - cur.y, dz = st->pos.z - cur.z;
        f32 d2 = dx * dx + dz * dz;
        if (d2 > 0.25f || dy * dy > 400.0f) {
            coord3d next = cur;
            StandTile *tile = chr->prop->stan;
            bool snap = d2 > 200.0f * 200.0f || dy * dy > 150.0f * 150.0f;
            f32 k = snap ? 1.0f : 0.25f;
            next.x += dx * k;
            next.z += dz * k;
            if (snap) next.y = st->pos.y;
            if (!tile || !walkTilesBetweenPoints_NoCallback(&tile, cur.x, cur.z, next.x, next.z) || !tile) {
                f32 y;
                coord3d probe = next;
                probe.y += 100.0f;
                tile = stanFindTileBelowPos(&probe, NULL, &y);
            }
            if (tile) chr->prop->stan = tile;
            if (snap) chr->ground = st->ground;
            setsuboffset(model, &next);
            subcalcpos(model);
            getsuboffset(model, &chr->prop->pos);
            chrDetectRooms(chr);
        }
    }
}

/* ---- The mission: the host's, shown everywhere ---- */

extern s32 objectiveregisters1;   /* chr.c: the stage flags */
extern void hudmsgBottomShow(char *mess);
extern void hudmsgTopShow(char *mess);
extern u8 *langGet(s32 slotID);
extern void objectivestatusCheckRoomEntered(s32 roomid);
extern void objectivestatusCheckDeposit(s32 weaponnum, s32 roomid);
extern s32 alarmIsActive(void);   /* propobj.c; bool there is s32 */
extern void alarmActivate(void);
extern void alarmDeactivate(void);
#define COOP_MISSION_ALARM 1

#define COOP_OBJECTIVES 10
#define COOP_MISSION_CHECK_US 250000ull      /* the host looks for a change four times a second */
#define COOP_MISSION_REFRESH_US 2000000ull   /* and sends it all every two seconds (a joiner, a lost packet) */

static u8 s_host_status[COOP_OBJECTIVES];    /* a teammate's headset: the host's objectives */
static s32 s_held[4][NET_COOP_HELD_MAX];     /* the host: the objective items each teammate holds */
static u8 s_held_count[4];
static u8 s_sent_status[COOP_OBJECTIVES];
static u8 s_sent_status_count;
static s32 s_sent_flags;
static u8 s_sent_mission_bits;   /* COOP_MISSION_* */
static u8 s_sent_downed;         /* the downed players, a bit each */
static u64 s_mission_check_us, s_mission_sent_us;
static s32 s_sent_held[NET_COOP_HELD_MAX];   /* a teammate's headset: what it last reported */
static u8 s_sent_held_count;
static u64 s_held_check_us, s_held_sent_us;

int gevrCoopHostObjectiveStatus(int objective)
{
    return objective >= 0 && objective < COOP_OBJECTIVES ? s_host_status[objective] : 0;
}

int gevrCoopTeammateHolds(int tag)
{
    if (!netCoopActive() || !netIsHost()) return FALSE;
    for (int i = 0; i < 4; i++) {
        if (i == netGetLocalSlot() || !netSlotOccupied(i)) continue;
        for (int k = 0; k < s_held_count[i]; k++)
            if (s_held[i][k] == tag) return TRUE;
    }
    return FALSE;
}

static void coopMissionReset(void)
{
    memset(s_host_status, 0, sizeof(s_host_status));
    memset(s_held, 0, sizeof(s_held));
    memset(s_held_count, 0, sizeof(s_held_count));
    s_sent_status_count = 0;
    s_sent_flags = 0;
    s_sent_mission_bits = 0;
    s_sent_downed = 0;
    s_mission_check_us = s_mission_sent_us = 0;
    s_sent_held_count = 0;
    s_held_check_us = s_held_sent_us = 0;
}

static void coopHeader(struct netbuf *b, u8 type)
{
    netbufStartWrite(b);
    netbufWriteU32(b, GEVR_NET_MAGIC);
    netbufWriteU16(b, GEVR_NET_VERSION);
    netbufWriteU8(b, type);
    netbufWriteU8(b, (uint8_t)netGetLocalSlot());
}

/* The host, from netCoopHostTick: the stage flags and the objectives, when they change */
static void coopHostMission(u64 now)
{
    u8 statuses[COOP_OBJECTIVES];
    s32 count;
    u8 bits, downed = 0;
    u8 raw[32];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };

    if (now - s_mission_check_us < COOP_MISSION_CHECK_US) return;
    s_mission_check_us = now;
    count = gevrCoopObjectiveSnapshot(statuses, COOP_OBJECTIVES, netGetLocalSlot());
    if (count < 0) count = 0;
    bits = alarmIsActive() ? COOP_MISSION_ALARM : 0;
    for (int i = 0; i < 4; i++) if (s_downed[i]) downed |= (u8)(1 << i);
    if (count == s_sent_status_count && !memcmp(statuses, s_sent_status, (size_t)count) &&
        objectiveregisters1 == s_sent_flags && bits == s_sent_mission_bits && downed == s_sent_downed &&
        now - s_mission_sent_us < COOP_MISSION_REFRESH_US) return;
    coopHeader(&buf, NET_MSG_COOP_MISSION);
    netbufWriteS32(&buf, objectiveregisters1);
    netbufWriteU8(&buf, bits);
    netbufWriteU8(&buf, downed);
    netbufWriteU8(&buf, (u8)count);
    for (s32 i = 0; i < count; i++) netbufWriteU8(&buf, statuses[i]);
    netCoopBroadcast(buf.data, buf.wp, true);
    memcpy(s_sent_status, statuses, (size_t)count);
    s_sent_status_count = (u8)count;
    s_sent_flags = objectiveregisters1;
    s_sent_mission_bits = bits;
    s_sent_downed = downed;
    s_mission_sent_us = now;
}

static void coopReceiveMission(struct netbuf *b)
{
    s32 flags = netbufReadS32(b);
    u8 bits = netbufReadU8(b);
    u8 downed = netbufReadU8(b);
    u8 count = netbufReadU8(b);
    u8 statuses[COOP_OBJECTIVES];
    if (b->error || count > COOP_OBJECTIVES || netbufReadLeft(b) != count) return;
    for (int i = 0; i < count; i++) {
        statuses[i] = netbufReadU8(b);
        if (statuses[i] > 2) return;   /* OBJECTIVESTATUS_INCOMPLETE .. FAILED */
    }
    objectiveregisters1 = flags;
    /* the host's alarm: on or off here as there */
    if ((bits & COOP_MISSION_ALARM) && !alarmIsActive()) alarmActivate();
    else if (!(bits & COOP_MISSION_ALARM) && alarmIsActive()) alarmDeactivate();
    /* the others' down or up (this headset's own player is its own to say) */
    for (int i = 0; i < 4; i++) {
        bool down = (downed & (1 << i)) != 0;
        if (i == netGetLocalSlot()) s_host_sees_me_down = down;
        if (i == netGetLocalSlot() || down == s_downed[i]) continue;
        s_downed[i] = down;
        coopAnnounceDowned(i, down);
    }
    memset(s_host_status, 0, sizeof(s_host_status));
    memcpy(s_host_status, statuses, count);
}

/*
 * Mission gadgets the whole party can carry (issues #128/#129). Guns stay
 * per headset. Keys and documents stay with whoever picked them up: a keyed
 * door still checks that headset's own inventory.
 */
int gevrCoopSharedGadget(s32 item)
{
    switch (item) {
        case ITEM_BOMBCASE:
        case ITEM_PLASTIQUE:
        case ITEM_FLAREPISTOL:
        case ITEM_PITONGUN:
        case ITEM_BUNGEE:
        case ITEM_DOORDECODER:
        case ITEM_BOMBDEFUSER:
        case ITEM_CAMERA:
        case ITEM_LOCKEXPLODER:
        case ITEM_DOOREXPLODER:
        case ITEM_BRIEFCASE:
        case ITEM_WEAPONCASE:
        case ITEM_SAFECRACKERCASE:
        case ITEM_KEYANALYSERCASE:
        case ITEM_BUG:
        case ITEM_MICROCAMERA:
        case ITEM_BUGDETECTOR:
        case ITEM_EXPLOSIVEFLOPPY:
        case ITEM_POLARIZEDGLASSES:
        case ITEM_DARKGLASSES:
        case ITEM_CREDITCARD:
        case ITEM_GASKEYRING:
        case ITEM_DATATHIEF:
        case ITEM_WATCHIDENTIFIER:
        case ITEM_WATCHCOMMUNICATOR:
        case ITEM_WATCHGEIGERCOUNTER:
        case ITEM_WATCHMAGNETREPEL:
        case ITEM_WATCHMAGNETATTRACT:
        case ITEM_DATTAPE:
            return TRUE;
        default:
            return FALSE;
    }
}

/* The nearest living teammate, for a background list that has no guard slot. */
s32 gevrCoopNearestPlayer(const coord3d *pos)
{
    s32 best = netGetLocalSlot();
    f32 bestd = 3.4e38f;

    if (best < 0) best = 0;
    if (!pos) return best;
    for (int i = 0; i < 4; i++) {
        coord3d *p;
        f32 dx, dy, dz, d;
        if (!coopPlayerTargetable(i) || !g_playerPointers[i]->prop) continue;
        p = &g_playerPointers[i]->prop->pos;
        dx = p->x - pos->x;
        dy = p->y - pos->y;
        dz = p->z - pos->z;
        d = dx * dx + dy * dy * 4.0f + dz * dz;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return best;
}

static void coopGrantLocal(s32 item)
{
    s32 prev;
    s32 i;

    /* Slots are not always packed at the front: a leaver can leave a hole,
     * and getPlayerCount() would then stop before a later occupied slot. */
    if (item <= ITEM_UNARMED || item >= ITEM_IDS_MAX) return;
    prev = get_cur_playernum();
    for (i = 0; i < 4; i++) {
        if (!g_playerPointers[i] || !netSlotOccupied(i) || netSlotIsSpectator(i)) continue;
        set_cur_player(i);
        bondinvAddInvItem(item);
    }
    set_cur_player(prev);
}

static void coopBroadcastGrant(s32 item)
{
    u8 raw[16];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };

    coopHeader(&buf, NET_MSG_COOP_GRANT);
    netbufWriteS32(&buf, item);
    netCoopBroadcast(buf.data, buf.wp, true);
}

void gevrCoopGrantItem(s32 item)
{
    if (!netCoopActive() || !gevrCoopSharedGadget(item)) return;
    coopGrantLocal(item);
    if (netIsHost()) coopBroadcastGrant(item);
    else coopSendEvent(NET_COOP_EVENT_GRANT, item, 0, 1);
}

static s32 coopTagOf(ObjectRecord *obj)
{
    TagObjectRecord *tag = (TagObjectRecord *)ptr_last_tag_entry_type16;

    while (tag) {
        if (tag->TaggedObject == obj) return tag->ID;
        tag = tag->NextTag;
    }
    return -1;
}

void gevrCoopReportGadgetUse(ObjectRecord *obj)
{
    s32 tag;

    if (!obj || !gevrCoopPuppets() || get_cur_playernum() != netGetLocalSlot()) return;
    tag = coopTagOf(obj);
    if (tag < 0) return;
    coopSendEvent(NET_COOP_EVENT_GADGET, tag, 0, 1);
}

static void coopApplyGadgetUse(s32 tag)
{
    ObjectRecord *obj = objFindByTagId(tag);

    if (obj) obj->state |= PROPSTATE_ACTIVATED;
}

static void coopReceiveGrant(struct netbuf *b)
{
    s32 item = netbufReadS32(b);

    if (b->error || netbufReadLeft(b) || !gevrCoopSharedGadget(item)) return;
    coopGrantLocal(item);
}

static void coopSendEvent(u8 kind, s32 a, s32 b2, int nargs)
{
    u8 raw[24];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    if (!gevrCoopPuppets()) return;
    coopHeader(&buf, NET_MSG_COOP_EVENT);
    netbufWriteU8(&buf, kind);
    if (nargs > 0) netbufWriteS32(&buf, a);
    if (nargs > 1) netbufWriteS32(&buf, b2);
    netCoopBroadcast(buf.data, buf.wp, true);
}

/* objective_status.c and gunfire.c, on a teammate's headset: its own player's */
void gevrCoopReportRoom(int room) { if (get_cur_playernum() == netGetLocalSlot()) coopSendEvent(NET_COOP_EVENT_ROOM, room, 0, 1); }
void gevrCoopReportDeposit(int item, int room) { coopSendEvent(NET_COOP_EVENT_DEPOSIT, item, room, 2); }
void gevrCoopReportPhoto(int tag) { if (get_cur_playernum() == netGetLocalSlot()) coopSendEvent(NET_COOP_EVENT_PHOTO, tag, 0, 1); }
void gevrCoopReportAlarm(int on) { if (get_cur_playernum() == netGetLocalSlot()) coopSendEvent(NET_COOP_EVENT_ALARM, on ? 1 : 0, 0, 1); }
void gevrCoopReportKeyCopy(void) { if (get_cur_playernum() == netGetLocalSlot()) coopSendEvent(NET_COOP_EVENT_KEYCOPY, 0, 0, 0); }

/* netPoll, a teammate's headset: the objective items its player holds, when that changes */
void netCoopClientTick(void)
{
    s32 tags[NET_COOP_HELD_MAX];
    s32 n;
    u64 now = sysGetMicroseconds();
    u8 raw[16 + NET_COOP_HELD_MAX * 4];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };

    if (!gevrCoopPuppets() || now - s_held_check_us < COOP_MISSION_CHECK_US * 2) return;
    s_held_check_us = now;
    /* the host has this player down or up wrongly (a lost event, a rejoin): say again */
    {
        s32 me = netGetLocalSlot();
        if (me >= 0 && me < 4 && s_host_sees_me_down >= 0 && s_host_sees_me_down != (int)s_downed[me] &&
            now - s_down_sent_us > 1000000ull) {
            s_down_sent_us = now;
            coopSendEvent(NET_COOP_EVENT_DOWNED, s_downed[me] ? 1 : 0, 0, 1);
        }
    }
    n = gevrCoopHeldObjectiveTags(tags, NET_COOP_HELD_MAX, netGetLocalSlot());
    if (n < 0) n = 0;
    if (n == s_sent_held_count && !memcmp(tags, s_sent_held, (size_t)n * sizeof(s32)) &&
        now - s_held_sent_us < COOP_MISSION_REFRESH_US * 2) return;
    coopHeader(&buf, NET_MSG_COOP_EVENT);
    netbufWriteU8(&buf, NET_COOP_EVENT_HELD);
    netbufWriteU8(&buf, (u8)n);
    for (s32 i = 0; i < n; i++) netbufWriteS32(&buf, tags[i]);
    netCoopBroadcast(buf.data, buf.wp, true);
    memcpy(s_sent_held, tags, (size_t)n * sizeof(s32));
    s_sent_held_count = (u8)n;
    s_held_sent_us = now;
}

/* The host: a teammate's event */
static void coopReceiveEvent(int slot, struct netbuf *b)
{
    u8 kind = netbufReadU8(b);
    struct player *pl = slot >= 0 && slot < 4 ? g_playerPointers[slot] : NULL;
    if (b->error || !pl || slot == netGetLocalSlot()) return;
    switch (kind) {
        case NET_COOP_EVENT_ROOM: {
            s32 room = netbufReadS32(b);
            if (!b->error && !netbufReadLeft(b)) objectivestatusCheckRoomEntered(room);
            break;
        }
        case NET_COOP_EVENT_DEPOSIT: {
            s32 item = netbufReadS32(b), room = netbufReadS32(b);
            if (!b->error && !netbufReadLeft(b) && item > 0 && item < ITEM_IDS_MAX) objectivestatusCheckDeposit(item, room);
            break;
        }
        case NET_COOP_EVENT_PHOTO: {
            s32 tag = netbufReadS32(b);
            if (!b->error && !netbufReadLeft(b)) gevrCoopApplyPhoto(tag);
            break;
        }
        case NET_COOP_EVENT_ALARM: {
            /* a teammate's alarm switch: the host's alarm, sent to all with the mission */
            s32 on = netbufReadS32(b);
            if (b->error || netbufReadLeft(b)) break;
            if (on && !alarmIsActive()) alarmActivate();
            else if (!on && alarmIsActive()) alarmDeactivate();
            s_mission_check_us = 0;
            break;
        }
        case NET_COOP_EVENT_KEYCOPY:
            if (!netbufReadLeft(b)) pl->copiedgoldeneye = TRUE;
            break;
        case NET_COOP_EVENT_DOWNED: {
            s32 down = netbufReadS32(b);
            if (b->error || netbufReadLeft(b) || (down != 0 && down != 1) || s_downed[slot] == (down != 0)) break;
            s_downed[slot] = down != 0;
            s_mission_check_us = 0;   /* to the others at once */
            coopAnnounceDowned(slot, down != 0);
            break;
        }
        case NET_COOP_EVENT_HELD: {
            u8 n = netbufReadU8(b);
            s32 tags[NET_COOP_HELD_MAX];
            if (b->error || n > NET_COOP_HELD_MAX || netbufReadLeft(b) != n * 4u) break;
            for (int i = 0; i < n; i++) tags[i] = netbufReadS32(b);
            if (b->error) break;
            memcpy(s_held[slot], tags, n * sizeof(s32));
            s_held_count[slot] = n;
            break;
        }
        case NET_COOP_EVENT_GRANT: {
            s32 item = netbufReadS32(b);
            if (!b->error && !netbufReadLeft(b)) gevrCoopGrantItem(item);
            break;
        }
        case NET_COOP_EVENT_GADGET: {
            s32 tag = netbufReadS32(b);
            if (!b->error && !netbufReadLeft(b)) coopApplyGadgetUse(tag);
            break;
        }
        default:
            break;
    }
    COOP_LOG("event rx: slot %d kind %d", slot, kind);
}

/* ---- Revive (#94): down, not dead, until a teammate stands beside you ---- */

#define COOP_REVIVE_RANGE 160.0f            /* a teammate this close (and on the same floor) */
#define COOP_REVIVE_US 3000000ull           /* for three seconds */
#define COOP_DOWNED_HEALTH 0.001f           /* alive: nothing that reads health or bonddead takes it for dead */
#define COOP_REVIVED_HEALTH 0.5f

static u64 s_revive_since_us;               /* this headset's player down: a teammate beside it since */
static bool s_reviving_shown[4];            /* this headset's player beside a downed teammate: said so */
static bool s_all_down_ended;               /* the host: the mission failed for it */

static void coopReviveReset(void)
{
    memset(s_downed, 0, sizeof(s_downed));
    memset(s_reviving_shown, 0, sizeof(s_reviving_shown));
    s_host_sees_me_down = -1;
    s_down_sent_us = 0;
    s_revive_since_us = 0;
    s_all_down_ended = false;
}

s32 gevrCoopDowned(s32 player)
{
    return player >= 0 && player < 4 && s_downed[player] && netCoopActive();
}

int gevrCoopLocalDowned(void)
{
    return get_cur_playernum() == netGetLocalSlot() && gevrCoopDowned(netGetLocalSlot());
}

static void coopShowLocal(int top, const char *text)
{
    s32 prev = get_cur_playernum();
    s32 me = netGetLocalSlot();
    if (me < 0 || me >= 4 || !g_playerPointers[me]) return;
    set_cur_player(me);
    if (top) hudmsgTopShow((char *)text);
    else hudmsgBottomShow((char *)text);
    set_cur_player(prev);
}

static void coopAnnounceDowned(int slot, bool down)
{
    char msg[64];
    const char *name = netGetSlotName(slot);
    snprintf(msg, sizeof(msg), down ? "%s IS DOWN" : "%s IS BACK UP", name && name[0] ? name : "A TEAMMATE");
    coopShowLocal(0, msg);
    COOP_LOG("slot %d %s", slot, down ? "down" : "up");
}

/* This headset's player is down or up: the host's mission packet tells the others */
static void coopSetLocalDowned(bool down)
{
    s32 me = netGetLocalSlot();
    if (me < 0 || me >= 4 || s_downed[me] == down) return;
    s_downed[me] = down;
    s_revive_since_us = 0;
    if (netIsHost()) s_mission_check_us = 0;
    else {
        coopSendEvent(NET_COOP_EVENT_DOWNED, down ? 1 : 0, 0, 1);
        s_down_sent_us = sysGetMicroseconds();
    }
    COOP_LOG("this player is %s", down ? "down" : "up");
}

void netCoopSlotLeft(int slot)
{
    if (slot < 0 || slot >= 4) return;
    s_downed[slot] = false;
    s_held_count[slot] = 0;
    s_mission_check_us = 0;
}

/* bondview2.c record_damage_kills: this headset's player's health ran out */
int gevrCoopGoDown(void)
{
    struct player *pl = g_CurrentPlayer;
    if (!netCoopActive() || get_cur_playernum() != netGetLocalSlot() || !pl) return FALSE;
    pl->bondhealth = COOP_DOWNED_HEALTH;
    if (!gevrCoopDowned(netGetLocalSlot())) {
        coopSetLocalDowned(true);
        coopShowLocal(1, "YOU ARE DOWN");
        coopShowLocal(0, "A TEAMMATE BESIDE YOU REVIVES YOU");
    }
    return TRUE;
}

static bool coopBeside(struct player *a, struct player *b)
{
    f32 dx, dy, dz;
    if (!a || !b || !a->prop || !b->prop) return false;
    dx = a->prop->pos.x - b->prop->pos.x;
    dy = a->prop->pos.y - b->prop->pos.y;
    dz = a->prop->pos.z - b->prop->pos.z;
    return dx * dx + dz * dz < COOP_REVIVE_RANGE * COOP_REVIVE_RANGE && dy * dy < 150.0f * 150.0f;
}

static bool coopTeammateUp(int i)
{
    struct player *pl = i >= 0 && i < 4 ? g_playerPointers[i] : NULL;
    return pl && pl->prop && !pl->bonddead && netSlotOccupied(i) && !netSlotIsSpectator(i) && !s_downed[i];
}

/*
 * Each frame (netPoll). Down: a teammate who is up and beside this player
 * for three seconds brings it back on half health. Up: standing beside a
 * downed teammate says so (their headset keeps the time). The host: when
 * every player in the mission is down, the mission has failed.
 */
void netCoopReviveTick(void)
{
    s32 me = netGetLocalSlot();
    struct player *pl = me >= 0 && me < 4 ? g_playerPointers[me] : NULL;
    u64 now = sysGetMicroseconds();

    if (!netCoopActive()) return;
    coopRecordSetup();
    if (gevrCoopPuppets()) {
        coopRetrySpawns();
        if (s_await_remap && now - s_await_since_us > 15000000ull) {
            /* no remap came (the new host never took over this mission?): its slots as ours */
            s_await_remap = false;
            COOP_LOG("no remap from the new host in 15 s: taking its guard slots as this headset's");
        }
    }
    if (!pl || !pl->prop) return;
    if (s_downed[me]) {
        int by = -1;
        pl->bondhealth = COOP_DOWNED_HEALTH;
        for (int i = 0; i < 4 && by < 0; i++)
            if (i != me && coopTeammateUp(i) && coopBeside(pl, g_playerPointers[i])) by = i;
        if (by < 0) {
            s_revive_since_us = 0;
        } else if (!s_revive_since_us) {
            char msg[64];
            const char *name = netGetSlotName(by);
            s_revive_since_us = now;
            snprintf(msg, sizeof(msg), "%s IS REVIVING YOU", name && name[0] ? name : "A TEAMMATE");
            coopShowLocal(0, msg);
        } else if (now - s_revive_since_us >= COOP_REVIVE_US) {
            pl->bondhealth = COOP_REVIVED_HEALTH;
            coopSetLocalDowned(false);
            coopShowLocal(1, "REVIVED");
        }
    } else {
        for (int i = 0; i < 4; i++) {
            bool beside = i != me && s_downed[i] && netSlotOccupied(i) && coopBeside(pl, g_playerPointers[i]);
            if (beside && !s_reviving_shown[i]) {
                char msg[64];
                const char *name = netGetSlotName(i);
                snprintf(msg, sizeof(msg), "REVIVING %s: STAY CLOSE", name && name[0] ? name : "A TEAMMATE");
                coopShowLocal(0, msg);
            }
            s_reviving_shown[i] = beside;
        }
    }
    if (netIsHost() && !s_all_down_ended) {
        int playing = 0, down = 0;
        for (int i = 0; i < 4; i++) {
            if (!netSlotOccupied(i) || netSlotIsSpectator(i) || !g_playerPointers[i] || !g_playerPointers[i]->prop) continue;
            playing++;
            if (s_downed[i]) down++;
        }
        if (playing > 0 && down == playing) {
            s_all_down_ended = true;
            COOP_LOG("every player is down: the mission has failed");
            netCoopMissionEnded(NET_COOP_RESULT_ALL_DOWN);
        }
    }
}

/* ---- Drop-in, drop-out and a host change ---- */

/*
 * Every headset knows which guard slots its mission's setup filled
 * (s_setup, at the first tick) and which hold a spawned guard (s_spawned:
 * the host's own spawns, or a client's from the host's NET_MSG_CHR_SPAWN).
 * A player joining a mission under way loads it fresh, with the setup's
 * guards; the host sends it a roster, only to it: the setup guards gone
 * here, and the guards spawned since. After a host change the new host's
 * slots are not the old host's: a returning player first gets the remap.
 */
#define COOP_AI_INTERVAL_US 2000000ull       /* the guards' AI state, every two seconds */
#define COOP_AI_RECORD_BYTES 32
#define COOP_AI_PER_PACKET 32
#define COOP_ROSTER_DELAY_US 1500000ull      /* after its STAGE_READY: it has ticked by then */
#define COOP_PLACE_AFTER_US 15000000ull      /* a mission older than this: a joiner starts beside a teammate */


static void coopDropInReset(void)
{
    memset(s_setup, 0, sizeof(s_setup));
    memset(s_spawned, 0, sizeof(s_spawned));
    memset(s_spawn_flags, 0, sizeof(s_spawn_flags));
    memset(s_ai, 0, sizeof(s_ai));
    memset(s_ai_bg, 0, sizeof(s_ai_bg));
    memset(s_roster_left, 0, sizeof(s_roster_left));
    s_setup_recorded = false;
    s_ai_sent_us = 0;
    s_stage_loaded_us = sysGetMicroseconds();
    s_await_remap = false;
    s_remap_valid = false;
    s_remap_count = 0;
    s_pending_spawns = 0;
}

/* The first tick after a load: the setup's guards */
static void coopRecordSetup(void)
{
    if (s_setup_recorded || !g_ChrSlots) return;
    s_setup_recorded = true;
    for (s32 slot = 0; slot < g_NumChrSlots && slot < COOP_MAX_SLOTS; slot++)
        s_setup[slot] = coopChrLive(&g_ChrSlots[slot]) && !s_spawned[slot];
}

static void coopWriteSpawn(struct netbuf *buf, ChrRecord *chr, s32 slot, AIRecord *ailist, s32 spawnflags)
{
    s32 global = FALSE;
    s32 listid = ailist ? chraiGetAIListID(ailist, &global) : -1;
    coopHeader(buf, NET_MSG_CHR_SPAWN);
    netbufWriteU16(buf, (u16)slot);
    netbufWriteS16(buf, chr->chrnum);
    netbufWriteS8(buf, chr->bodynum);
    netbufWriteS8(buf, chr->headnum);
    netbufWriteU32(buf, (u32)spawnflags);
    netbufWriteS32(buf, listid);
    netbufWriteCoord(buf, &chr->prop->pos);
    netbufWriteF32(buf, getsubroty(chr->model));
}

static void coopWriteRemove(struct netbuf *buf, s32 slot)
{
    coopHeader(buf, NET_MSG_CHR_REMOVE);
    netbufWriteU16(buf, (u16)slot);
    netbufWriteU8(buf, 1);    /* a roster's: a setup guard gone on the host */
}

/* The host: a player loaded the mission (fresh, or back after a host change) */
void netCoopPlayerJoined(int slot, int returning)
{
    if (!netCoopActive() || !netIsHost() || slot < 0 || slot >= 4 || slot == netGetLocalSlot()) return;
    s_roster_at_us[slot] = sysGetMicroseconds() + COOP_ROSTER_DELAY_US;
    s_roster_left[slot] = 2;   /* and once more, a few seconds on */
    s_roster_remap[slot] = s_remap_valid;   /* returning or not: one that came back late is not "returning" */
    s_roster_join[slot] = !returning;
    for (int i = 0; i < COOP_MAX_SLOTS; i++) s_sent[i].sent = false;   /* every guard's state, at once */
    s_mission_check_us = s_mission_sent_us = 0;
    s_ai_sent_us = 0;
    COOP_LOG("slot %d %s: roster in %.1f s", slot, returning ? "is back" : "joined", COOP_ROSTER_DELAY_US / 1000000.0);
}

static int coopBesideWhom(int joiner)
{
    s32 me = netGetLocalSlot();
    if (me >= 0 && me < 4 && me != joiner && coopTeammateUp(me)) return me;
    for (int i = 0; i < 4; i++)
        if (i != joiner && coopTeammateUp(i)) return i;
    return -1;
}

static void coopSendRoster(int slot)
{
    u8 raw[64];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    int removes = 0, spawns = 0;

    if (s_roster_remap[slot]) {
        u8 big[16 + COOP_MAX_SLOTS * 4];
        struct netbuf rb = { .data = big, .size = sizeof(big) };
        coopHeader(&rb, NET_MSG_CHR_REMAP);
        netbufWriteU16(&rb, (u16)s_remap_count);
        for (int i = 0; i < s_remap_count; i++) {
            netbufWriteU16(&rb, s_remap_old[i]);
            netbufWriteU16(&rb, s_remap_new[i]);
        }
        if (!rb.error) netCoopSendTo(slot, rb.data, rb.wp);
    }
    for (s32 s = 0; g_ChrSlots && s < g_NumChrSlots && s < COOP_MAX_SLOTS; s++) {
        ChrRecord *chr = &g_ChrSlots[s];
        bool live = coopChrLive(chr) && !(chr->hidden & CHRHIDDEN_REMOVE);
        if (s_setup[s] && (!live || s_spawned[s])) {
            coopWriteRemove(&buf, s);
            netCoopSendTo(slot, buf.data, buf.wp);
            removes++;
        }
        if (live && s_spawned[s]) {
            coopWriteSpawn(&buf, chr, s, chr->ailist, s_spawn_flags[s]);
            if (!buf.error) netCoopSendTo(slot, buf.data, buf.wp);
            spawns++;
        }
    }
    if (s_roster_join[slot] && sysGetMicroseconds() - s_stage_loaded_us > COOP_PLACE_AFTER_US) {
        int beside = coopBesideWhom(slot);
        if (beside >= 0) {
            coopHeader(&buf, NET_MSG_COOP_JOIN);
            netbufWriteU8(&buf, (u8)slot);
            netbufWriteU8(&buf, (u8)beside);
            netCoopSendTo(slot, buf.data, buf.wp);
        }
    }
    COOP_LOG("roster to slot %d: %d gone, %d spawned%s", slot, removes, spawns, s_roster_remap[slot] ? ", with the remap" : "");
}

/* The host, from netCoopHostTick: rosters due, and the AI state */
static void coopHostDropIn(u64 now)
{
    for (int i = 0; i < 4; i++) {
        if (!s_roster_left[i] || now < s_roster_at_us[i]) continue;
        if (!netSlotOccupied(i)) {
            s_roster_left[i] = 0;
            continue;
        }
        coopSendRoster(i);
        s_roster_left[i]--;
        s_roster_at_us[i] = now + 3500000ull;
        s_roster_remap[i] = false;   /* the second time, the maps are right */
        s_roster_join[i] = false;    /* and it has moved since */
    }
    if (now - s_ai_sent_us < COOP_AI_INTERVAL_US) return;
    s_ai_sent_us = now;
    {
        u8 raw[16 + COOP_AI_PER_PACKET * COOP_AI_RECORD_BYTES];
        struct netbuf buf = { .data = raw, .size = sizeof(raw) };
        int count = 0;
        s32 total = (g_ChrSlots ? (g_NumChrSlots < COOP_MAX_SLOTS ? g_NumChrSlots : COOP_MAX_SLOTS) : 0);
        s32 bg = g_ActiveChrs ? (g_ActiveChrsCount < COOP_BACKGROUND_MAX ? g_ActiveChrsCount : COOP_BACKGROUND_MAX) : 0;
        for (s32 n = 0; n < total + bg; n++) {
            ChrRecord *chr = n < total ? &g_ChrSlots[n] : &g_ActiveChrs[n - total];
            u16 id = n < total ? (u16)n : (u16)(0x8000 | (n - total));
            s32 global = FALSE;
            if (n < total && !coopChrLive(chr)) continue;
            if (!count) {
                coopHeader(&buf, NET_MSG_CHR_AI);
                netbufWriteU8(&buf, 0);   /* the count, filled in below */
            }
            netbufWriteU16(&buf, id);
            netbufWriteS16(&buf, chr->ailist ? (s16)chraiGetAIListID(chr->ailist, &global) : -1);
            netbufWriteU16(&buf, chr->aioffset);
            netbufWriteS16(&buf, chr->aireturnlist);
            netbufWriteU8(&buf, chr->morale);
            netbufWriteU8(&buf, chr->alertness);
            netbufWriteU8(&buf, chr->flags2);
            netbufWriteU8(&buf, chr->random);
            netbufWriteS32(&buf, chr->timer60);
            netbufWriteS16(&buf, chr->padpreset1);
            netbufWriteS16(&buf, chr->chrpreset1);
            netbufWriteS16(&buf, chr->chrseeshot);
            netbufWriteS16(&buf, chr->chrseedie);
            netbufWriteU32(&buf, (u32)chr->chrflags);
            netbufWriteU16(&buf, chr->hidden);
            netbufWriteS8(&buf, chr->sleep);
            netbufWriteU8(&buf, 0);
            if (++count == COOP_AI_PER_PACKET) {
                raw[8] = (u8)count;
                netCoopBroadcast(buf.data, buf.wp, false);
                count = 0;
            }
        }
        if (count) {
            raw[8] = (u8)count;
            netCoopBroadcast(buf.data, buf.wp, false);
        }
    }
}

static void coopReceiveAi(struct netbuf *b)
{
    u8 count = netbufReadU8(b);
    if (b->error || count == 0 || count > COOP_AI_PER_PACKET || netbufReadLeft(b) != count * (u32)COOP_AI_RECORD_BYTES) return;
    for (int i = 0; i < count; i++) {
        CoopAi a;
        u16 id = netbufReadU16(b);
        a.ailist = netbufReadS16(b);
        a.aioffset = netbufReadU16(b);
        a.aireturnlist = netbufReadS16(b);
        a.morale = netbufReadU8(b);
        a.alertness = netbufReadU8(b);
        a.flags2 = netbufReadU8(b);
        a.random = netbufReadU8(b);
        a.timer60 = netbufReadS32(b);
        a.padpreset1 = netbufReadS16(b);
        a.chrpreset1 = netbufReadS16(b);
        a.chrseeshot = netbufReadS16(b);
        a.chrseedie = netbufReadS16(b);
        a.chrflags = netbufReadU32(b);
        a.hidden = netbufReadU16(b);
        a.sleep = netbufReadS8(b);
        (void)netbufReadU8(b);
        a.valid = true;
        if (b->error) return;
        if (id & 0x8000) {
            if ((id & 0x7FFF) < COOP_BACKGROUND_MAX) s_ai_bg[id & 0x7FFF] = a;
        } else if (id < COOP_MAX_SLOTS) {
            s_ai[id] = a;
        }
    }
}

static void coopApplyAi(ChrRecord *chr, const CoopAi *a)
{
    AIRecord *list = a->ailist >= 0 ? ailistFindById(a->ailist) : NULL;
    if (list) {
        chr->ailist = list;
        chr->aioffset = a->aioffset;
    }
    chr->aireturnlist = a->aireturnlist;
    chr->morale = a->morale;
    chr->alertness = a->alertness;
    chr->flags2 = a->flags2;
    chr->random = a->random;
    chr->timer60 = a->timer60;
    chr->padpreset1 = a->padpreset1;
    chr->chrpreset1 = a->chrpreset1;
    chr->chrseeshot = a->chrseeshot;
    chr->chrseedie = a->chrseedie;
    chr->chrflags = (CHRFLAG)a->chrflags;
    chr->hidden = (u16)((chr->hidden & ~CHRHIDDEN_TIMER_ACTIVE) | (a->hidden & CHRHIDDEN_TIMER_ACTIVE));
    chr->sleep = 0;   /* its AI runs at the next tick */
}

/* A client whose host left (net_core.c netHostLost): elected, or rejoining another */
void netCoopHostLost(int oldhost, int elected)
{
    if (!netCoopActive()) return;
    netCoopSlotLeft(oldhost);
    if (!elected) {
        s_await_remap = true;   /* the new host's slots, from its remap */
        s_await_since_us = sysGetMicroseconds();
    }
    s_pending_spawns = 0;       /* the old host's slots */
    for (int i = 0; i < COOP_MAX_SLOTS; i++) s_puppet[i].valid = false;
    COOP_LOG("the host (slot %d) left: %s", oldhost, elected ? "taking the mission over" : "waiting for the new host");
}

/*
 * net_core.c netHostTakeOver: this headset runs the mission now. Its guards
 * were the old host's puppets; each takes up the old host's AI where the
 * last AI state (at most two seconds old) left it, its background lists
 * too, and plays on from where it stands. Its slots are the guards' names
 * from now on: the remap for the others, who knew the old host's.
 */
void netCoopBecameHost(void)
{
    int applied = 0, bgapplied = 0;
    if (!netCoopActive() || !g_ChrSlots) return;
    s_remap_count = 0;
    for (int h = 0; h < COOP_MAX_SLOTS; h++) {
        int local = s_local_of[h];
        if (local < 0 || local >= g_NumChrSlots) continue;
        ChrRecord *chr = &g_ChrSlots[local];
        if (!coopChrLive(chr)) continue;
        s_remap_old[s_remap_count] = (u16)h;
        s_remap_new[s_remap_count] = (u16)local;
        s_remap_count++;
        if (s_ai[h].valid) {
            coopApplyAi(chr, &s_ai[h]);
            applied++;
        }
        if (chr->actiontype == ACT_DIE) {
            /* mid-fall on the old host: down for good here */
            memset(&chr->act_dead, 0, sizeof(chr->act_dead));
            chr->actiontype = ACT_DEAD;
        }
        chrStopFiring(chr);
        chr->sleep = 0;
    }
    for (int i = 0; g_ActiveChrs && i < g_ActiveChrsCount && i < COOP_BACKGROUND_MAX; i++) {
        if (s_ai_bg[i].valid) {
            coopApplyAi(&g_ActiveChrs[i], &s_ai_bg[i]);
            bgapplied++;
        }
    }
    for (int i = 0; i < COOP_MAX_SLOTS; i++) {
        s_sent[i].sent = false;
        s_target[i].player = -1;
        s_target[i].until60 = 0;
    }
    s_mission_check_us = s_mission_sent_us = 0;
    s_ai_sent_us = 0;
    s_await_remap = false;
    s_remap_valid = true;
    COOP_LOG("now the host: %d guards and %d background lists take up their AI; %d guards in the remap",
             applied, bgapplied, s_remap_count);
}

/* A client back with the new host: its slots for the old host's */
static void coopReceiveRemap(struct netbuf *b)
{
    u16 n = netbufReadU16(b);
    static s16 local_of[COOP_MAX_SLOTS], host_of[COOP_MAX_SLOTS];
    if (b->error || n > COOP_MAX_SLOTS || netbufReadLeft(b) != n * 4u) return;
    if (!s_await_remap) return;   /* a fresh joiner: its setup's slots are the new host's already */
    for (int i = 0; i < COOP_MAX_SLOTS; i++) local_of[i] = host_of[i] = -1;
    for (int i = 0; i < n; i++) {
        u16 oldh = netbufReadU16(b), newh = netbufReadU16(b);
        if (oldh >= COOP_MAX_SLOTS || newh >= COOP_MAX_SLOTS) continue;
        int local = s_local_of[oldh];
        if (local < 0 || local >= COOP_MAX_SLOTS) continue;
        local_of[newh] = (s16)local;
        host_of[local] = (s16)newh;
    }
    if (b->error) return;
    memcpy(s_local_of, local_of, sizeof(s_local_of));
    memcpy(s_host_of, host_of, sizeof(s_host_of));
    memset(s_puppet, 0, sizeof(s_puppet));
    s_await_remap = false;
    COOP_LOG("remap from the new host: %d guards", n);
}

/* A joiner: the host's word to start beside a teammate */
static void coopReceiveJoin(struct netbuf *b)
{
    extern s32 gevrCoopPlaceBeside(s32 target);
    u8 joiner = netbufReadU8(b), beside = netbufReadU8(b);
    s32 prev;
    if (b->error || netbufReadLeft(b) || joiner != netGetLocalSlot() || beside >= 4 || beside == joiner) return;
    prev = get_cur_playernum();
    set_cur_player(joiner);
    if (gevrCoopPlaceBeside(beside)) COOP_LOG("joined beside slot %d", beside);
    set_cur_player(prev);
}

static void coopShowText(int top, int textid)
{
    s32 prev = get_cur_playernum();
    char *text = (char *)langGet(textid);
    if (!text) return;
    set_cur_player(netGetLocalSlot());
    if (top) hudmsgTopShow(text);
    else hudmsgBottomShow(text);
    set_cur_player(prev);
}

/* chrai.c AI_TextPrintBottom / AI_TextPrintTop on the host: shown to the
 * player the script ran as; every other headset is sent it, and the host's
 * own player sees it too when the script ran as another. */
void gevrCoopAiText(int top, int textid)
{
    u8 raw[16];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    if (!netCoopActive() || !netIsHost() || textid < 0 || textid > 0xFFFF) return;
    coopHeader(&buf, NET_MSG_COOP_TEXT);
    netbufWriteU8(&buf, top ? 1 : 0);
    netbufWriteU16(&buf, (u16)textid);
    netCoopBroadcast(buf.data, buf.wp, true);
    if (get_cur_playernum() != netGetLocalSlot()) coopShowText(top, textid);
}

static void coopReceiveText(struct netbuf *b)
{
    u8 top = netbufReadU8(b);
    u16 textid = netbufReadU16(b);
    if (b->error || netbufReadLeft(b) || top > 1) return;
    coopShowText(top, textid);
}

/* net_core.c netHandlePacket: the co-op messages */
void netCoopReceive(int type, int slot, int from_host, struct netbuf *b)
{
    if (!netCoopActive() || !netPlayersWereTicked()) return;
    switch (type) {
        case NET_MSG_COOP_MISSION: if (from_host && !netIsHost()) coopReceiveMission(b); break;
        case NET_MSG_COOP_TEXT: if (from_host && !netIsHost()) coopReceiveText(b); break;
        case NET_MSG_COOP_GRANT: if (from_host && !netIsHost()) coopReceiveGrant(b); break;
        case NET_MSG_COOP_EVENT: if (netIsHost()) coopReceiveEvent(slot, b); break;
        case NET_MSG_CHR_AI: if (from_host && !netIsHost()) coopReceiveAi(b); break;
        case NET_MSG_CHR_REMAP: if (from_host && !netIsHost()) coopReceiveRemap(b); break;
        case NET_MSG_COOP_JOIN: if (from_host && !netIsHost()) coopReceiveJoin(b); break;
        case NET_MSG_COOP_CINEMA: if (from_host && !netIsHost()) coopReceiveCinema(b); break;
        case NET_MSG_CHR_STATE: if (from_host && !netIsHost()) coopReceiveStates(b); break;
        case NET_MSG_CHR_SPAWN: if (from_host && !netIsHost()) coopReceiveSpawn(b); break;
        case NET_MSG_CHR_REMOVE: if (from_host && !netIsHost()) coopReceiveRemove(b); break;
        case NET_MSG_COOP_DAMAGE: if (from_host && !netIsHost()) coopApplyDamage(b); break;
        case NET_MSG_COOP_HIT: if (netIsHost()) coopApplyHit(slot, b); break;
        default: break;
    }
}
