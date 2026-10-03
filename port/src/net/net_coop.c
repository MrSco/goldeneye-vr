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
extern Model *retrieve_header_for_body_and_head(s32 body, s32 head, u32 bitflags);
extern void chrlvMergeKneelToStand(ChrRecord *self, f32 mergetime);
extern bool netSlotOccupied(int slot);
extern bool netSlotIsSpectator(int slot);

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

void netCoopHostTick(void)
{
    u8 raw[16 + NET_CHR_STATES_PER_PACKET * NET_CHR_STATE_BYTES];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    u64 now = sysGetMicroseconds();
    int count = 0;

    if (!netCoopActive() || !netIsHost() || !g_ChrSlots || now - s_last_send_us < COOP_SEND_INTERVAL_US) return;
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
    s32 global = FALSE;
    s32 slot = coopSlotOf(chr);
    s32 listid = ailist ? chraiGetAIListID(ailist, &global) : -1;

    if (!netCoopActive() || !netIsHost() || slot < 0 || !chr->prop || !chr->model) return;
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_CHR_SPAWN);
    netbufWriteU8(&buf, (uint8_t)netGetLocalSlot());
    netbufWriteU16(&buf, (u16)slot);
    netbufWriteS16(&buf, chr->chrnum);
    netbufWriteS8(&buf, chr->bodynum);
    netbufWriteS8(&buf, chr->headnum);
    netbufWriteU32(&buf, (u32)spawnflags);
    netbufWriteS32(&buf, listid);
    netbufWriteCoord(&buf, &chr->prop->pos);
    netbufWriteF32(&buf, getsubroty(chr->model));
    s_sent[slot < COOP_MAX_SLOTS ? slot : 0].sent = false;
    netCoopBroadcast(buf.data, buf.wp, true);
    COOP_LOG("spawn tx: slot %d chrnum %d body %d head %d", slot, chr->chrnum, chr->bodynum, chr->headnum);
}

/* chr.c chrTick on the host: a guard about to be freed (CHRHIDDEN_REMOVE) */
void gevrCoopChrRemoved(ChrRecord *chr)
{
    u8 raw[16];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    s32 slot = coopSlotOf(chr);

    if (!netCoopActive() || !netIsHost() || slot < 0) return;
    if (slot < COOP_MAX_SLOTS) s_sent[slot].sent = false;
    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_CHR_REMOVE);
    netbufWriteU8(&buf, (uint8_t)netGetLocalSlot());
    netbufWriteU16(&buf, (u16)slot);
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

/* chraction.c handles_shot_actors: a guard's bullet hit a player's body */
void gevrCoopGuardHitPlayer(s32 target, f32 damage, f32 vx, f32 vz)
{
    s32 prev = get_cur_playernum();
    if (!netIsHost() || target < 0 || target >= 4 || !g_playerPointers[target]) return;
    if (target != netGetLocalSlot()) {
        if (netSlotOccupied(target)) coopSendGuardDamage(target, damage, vx, vz);
        return;
    }
    set_cur_player(target);
    record_damage_kills(damage, vx, vz, -1, 1);
    set_cur_player(prev);
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

/* Revive (#94) is still to come: until then a player is never down, only dead */
s32 gevrCoopDowned(s32 player)
{
    (void)player;
    return FALSE;
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

static void coopReceiveSpawn(struct netbuf *b)
{
    u16 hostslot = netbufReadU16(b);
    s16 chrnum = netbufReadS16(b);
    s8 body = netbufReadS8(b);
    s8 head = netbufReadS8(b);
    u32 flags = netbufReadU32(b);
    s32 listid = netbufReadS32(b);
    coord3d pos;
    f32 angle;
    netbufReadCoord(b, &pos);
    angle = netbufReadF32(b);
    if (b->error || netbufReadLeft(b) || hostslot >= COOP_MAX_SLOTS || !isfinite(pos.x) || !isfinite(pos.y) ||
        !isfinite(pos.z) || !isfinite(angle) || !g_ChrSlots) return;
    ChrRecord *existing = coopLocalChr(hostslot);
    if (existing && coopChrLive(existing) && !(existing->hidden & CHRHIDDEN_REMOVE) &&
        existing->bodynum == body && existing->headnum == head) {
        existing->chrnum = chrnum;   /* already here: the setup's, or made before */
        return;
    }
    Model *header = retrieve_header_for_body_and_head(body, head, flags);
    coord3d at = pos;
    f32 y;
    StandTile *stan = stanFindTileBelowPos(&at, NULL, &y);
    AIRecord *ailist = listid >= 0 ? ailistFindById(listid) : NULL;
    PropRecord *prop = header && stan ? chrAllocate(header, &pos, angle, stan, ailist) : NULL;
    if (!prop || !prop->chr) {
        COOP_LOG("spawn rx: slot %d body %d head %d: could not be made here", hostslot, body, head);
        return;
    }
    chrpropActivateThisFrame(prop);
    chrpropEnable(prop);
    prop->chr->headnum = head;
    prop->chr->bodynum = body;
    prop->chr->chrnum = chrnum;
    s32 local = coopSlotOf(prop->chr);
    if (s_local_of[hostslot] >= 0 && s_local_of[hostslot] < COOP_MAX_SLOTS && s_host_of[s_local_of[hostslot]] == hostslot)
        s_host_of[s_local_of[hostslot]] = -1;
    s_local_of[hostslot] = (s16)local;
    if (local >= 0 && local < COOP_MAX_SLOTS) {
        if (s_host_of[local] >= 0 && s_host_of[local] < COOP_MAX_SLOTS && s_local_of[s_host_of[local]] == local &&
            s_host_of[local] != hostslot)
            s_local_of[s_host_of[local]] = -1;
        s_host_of[local] = (s16)hostslot;
    }
    COOP_LOG("spawn rx: host slot %d is slot %d here (chrnum %d)", hostslot, local, chrnum);
}

static void coopReceiveRemove(struct netbuf *b)
{
    u16 hostslot = netbufReadU16(b);
    if (b->error || netbufReadLeft(b) || hostslot >= COOP_MAX_SLOTS) return;
    ChrRecord *chr = coopLocalChr(hostslot);
    s32 local = s_local_of[hostslot];
    if (chr && coopChrLive(chr)) chr->hidden |= CHRHIDDEN_REMOVE;   /* freed at its next chrTick */
    s_puppet[hostslot].valid = false;
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
        if (dying) chr->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
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

/* net_core.c netHandlePacket: the co-op messages */
void netCoopReceive(int type, int slot, int from_host, struct netbuf *b)
{
    if (!netCoopActive() || !netPlayersWereTicked()) return;
    switch (type) {
        case NET_MSG_CHR_STATE: if (from_host && !netIsHost()) coopReceiveStates(b); break;
        case NET_MSG_CHR_SPAWN: if (from_host && !netIsHost()) coopReceiveSpawn(b); break;
        case NET_MSG_CHR_REMOVE: if (from_host && !netIsHost()) coopReceiveRemove(b); break;
        case NET_MSG_COOP_DAMAGE: if (from_host && !netIsHost()) coopApplyDamage(b); break;
        case NET_MSG_COOP_HIT: if (netIsHost()) coopApplyHit(slot, b); break;
        default: break;
    }
}
