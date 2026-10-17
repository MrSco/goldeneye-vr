/* #194: a campaign companion using GoldenEye's NPC movement and combat.
 * No imported ROM scripts, stage flags or reserved mission character IDs.
 * Preferences are captured on load; death does not spawn a replacement. */
#include <ultra64.h>
#include <bondgame.h>
#include <bondaicommands.h>
#include "gevr_ally.h"
#include "chr.h"
#include "chraction.h"
#include "objecthandler.h"
#include "player.h"
#include "lv.h"
#include "bondview.h"
#include "gevr_romlayout.h"
#include "net_coop.h"
#include "net_horde.h"
#include "stan.h"
#include <math.h>

extern bool netIsActive(void);
extern bool netIsHost(void);
extern int netGetLocalSlot(void);
extern int netPlayerIsSpectator(int slot);
extern int netLobbySlotConnected(int slot);
extern s32 g_StageNum, g_ClockTimer;
extern int VrAlly, VrAllyWeapon, VrAllyAggression, VrAllyHealth, VrAllyDamage, VrAllyInvincible;
extern PropRecord *chrSpawnAtCoord(s32 body, s32 head, coord3d *pos, StandTile *tile,
                                  f32 angle, AIRecord *ai, s32 flags);
extern s32 plot_course_for_actor(ChrRecord *chr, coord3d *pos, StandTile *tile, SPEED speed);
extern s32 chrIsNotDeadOrShot(ChrRecord *chr);

static ChrRecord *s_ally;
static s32 s_identity, s_spawned, s_wait, s_think, s_target;
static s32 s_enabled, s_weapon, s_aggression, s_health, s_invincible, s_configKnown;
static f32 s_damage, s_shotScale = 1.0f;

static const struct { s32 item, model; } s_weapons[] = {
    { ITEM_WPPK, PROP_CHRWPPK }, { ITEM_WPPKSIL, PROP_CHRWPPKSIL },
    { ITEM_TT33, PROP_CHRTT33 }, { ITEM_AK47, PROP_CHRKALASH },
    { ITEM_MP5K, PROP_CHRMP5K }, { ITEM_UZI, PROP_CHRUZI },
    { ITEM_M16, PROP_CHRM16 }, { ITEM_FNP90, PROP_CHRFNP90 },
    { ITEM_SHOTGUN, PROP_CHRSHOTGUN }, { ITEM_AUTOSHOT, PROP_CHRAUTOSHOT }
};

static s32 gevrAllyClamp(s32 value, s32 low, s32 high)
{
    return value < low ? low : value > high ? high : value;
}

void gevrAllyReset(void)
{
    s_ally = NULL;
    s_identity = -1;
    s_spawned = s_wait = s_think = 0;
    s_target = -1;
    s_shotScale = 1.0f;
    /* Clients must never substitute their saved preferences for the host's. */
    s_enabled = VrAlly != 0 && (!netIsActive() || netIsHost());
    s_configKnown = !netIsActive() || netIsHost();
    s_weapon = gevrAllyClamp(VrAllyWeapon, 0, 9);
    s_aggression = gevrAllyClamp(VrAllyAggression, 0, 2);
    s_health = gevrAllyClamp(VrAllyHealth, 25, 1000);
    s_damage = gevrAllyClamp(VrAllyDamage, 25, 400) * 0.01f;
    s_invincible = VrAllyInvincible != 0;
}

s32 gevrAllyIs(ChrRecord *chr)
{
    /* A faded body can give its pool slot to a reinforcement. */
    return chr != NULL && chr == s_ally && chr->model != NULL && chr->chrnum == s_identity;
}

void gevrAllyStop(void)
{
    if (!gevrAllyIs(s_ally)) return;
    chrSetFiring(s_ally, GUNRIGHT, FALSE);
    chrSetFiring(s_ally, GUNLEFT, FALSE);
    check_set_actor_standing_still(s_ally, 0, 0);
}

static s32 gevrAllyAuthority(void)
{
    return gevrRomCampaign() == 0 && (netIsActive() ? gevrCoopHostGuards() : getPlayerCount() == 1);
}

static s32 gevrAllyPlayerUp(s32 slot)
{
    struct player *pl = slot >= 0 && slot < MAX_PLAYER_COUNT ? g_playerPointers[slot] : NULL;
    return pl && pl->prop && pl->prop->stan && !pl->bonddead
        && (!netIsActive() || (netLobbySlotConnected(slot) && !netPlayerIsSpectator(slot) && !gevrCoopDowned(slot)));
}

/* Prefer the nearest living teammate, never a downed/spectating host. */
static s32 gevrAllyPlayer(const PropRecord *near)
{
    s32 best = -1;
    f32 bestd = 3.4e38f;
    for (s32 i = 0; i < getPlayerCount(); ++i) {
        if (!gevrAllyPlayerUp(i)) continue;
        PropRecord *p = g_playerPointers[i]->prop;
        f32 x = near ? near->pos.x - p->pos.x : 0;
        f32 y = near ? near->pos.y - p->pos.y : 0;
        f32 z = near ? near->pos.z - p->pos.z : 0;
        f32 d = x*x + y*y*4 + z*z;
        if (d < bestd) { best = i; bestd = d; }
    }
    return best;
}

static s32 gevrAllyPlaying(void)
{
    return !lvlGetControlsLockedFlag() && (g_CameraMode == CAMERAMODE_FP
        || (netIsActive() && (g_CameraMode == CAMERAMODE_NONE
            || g_CameraMode == CAMERAMODE_DEATH_CAM_SP || g_CameraMode == CAMERAMODE_DEATH_CAM_MP)))
        && (!netHordeActive() || (netHordeRunning() && !gevrHordeDefeated()));
}

static void gevrAllyConfigure(ChrRecord *chr)
{
    if (!s_configKnown) return;
    chr->chrflags |= CHRFLAG_CAN_SHOOT_CHRS | CHRFLAG_NO_AUTOAIM;
    if (s_invincible) chr->chrflags |= CHRFLAG_INVINCIBLE;
    else chr->chrflags &= ~CHRFLAG_INVINCIBLE;
    chr->maxdamage = 40.0f * s_health * 0.01f;
    chr->accuracyrating = 20;
    chr->flags2 |= FLAGS2_DONT_POINT_AT_BOND;
}

void gevrAllyBecameHost(void)
{
    if (!gevrAllyIs(s_ally) || chrIsDead(s_ally) || !s_configKnown) return;
    /* Puppets carry animation/action tags, not the NPC action union's path
     * and attack internals. Start a fresh action without changing health. */
    chrlvMergeKneelToStand(s_ally, 0.0f);
    gevrAllyConfigure(s_ally);
    s_think = s_wait = 0;
    s_target = -1;
}

void gevrAllyAttach(ChrRecord *chr, u32 flags)
{
    if (!(flags & GEVR_ALLY_SPAWN_FLAG) || !chr || !chr->model || chr->chrnum < 0
        || chr->bodynum != BODY_Natalya_Jungle_Fatigues || chr->headnum != HEAD_Natalya_Jungle_Fatigues) return;
    s_ally = chr;
    s_identity = chr->chrnum;
    s_spawned = TRUE;
    gevrAllyConfigure(chr);
}

void gevrAllyGetState(GevrAllyState *state)
{
    *state = (GevrAllyState){s_enabled, s_weapon, s_aggression, s_health,
        (s32)(s_damage * 100.0f + 0.5f), s_invincible, s_spawned, s_identity};
}

s32 gevrAllyStateValid(const GevrAllyState *state)
{
    return state && (state->enabled == 0 || state->enabled == 1)
        && state->weapon >= 0 && state->weapon <= 9
        && state->aggression >= 0 && state->aggression <= 2
        && state->health >= 25 && state->health <= 1000
        && state->damage >= 25 && state->damage <= 400
        && (state->invincible == 0 || state->invincible == 1)
        && (state->spawned == 0 || state->spawned == 1)
        && state->identity >= -1 && state->identity <= 32767
        && (state->spawned ? state->enabled && state->identity >= 0 : state->identity == -1);
}

void gevrAllyApplyState(const GevrAllyState *state)
{
    if (!gevrAllyStateValid(state)) return;
    s_enabled = state->enabled; s_weapon = state->weapon; s_aggression = state->aggression;
    s_configKnown = TRUE;
    s_health = state->health; s_damage = state->damage * 0.01f; s_invincible = state->invincible;
    s_spawned = state->spawned;
    /* The controller pointer arrives with the normal NPC spawn/roster. Keep
     * the lifecycle even when the body has gone, so takeover cannot revive it. */
    if (s_ally && s_identity != state->identity) s_ally = NULL;
    s_identity = state->identity;
    if (gevrAllyIs(s_ally)) gevrAllyConfigure(s_ally);
}

static s32 gevrAllyMission(void)
{
    switch (g_StageNum) {
    case LEVELID_DAM: case LEVELID_FACILITY: case LEVELID_RUNWAY:
    case LEVELID_SURFACE: case LEVELID_BUNKER1: case LEVELID_SILO:
    case LEVELID_FRIGATE: case LEVELID_SURFACE2: case LEVELID_BUNKER2:
    case LEVELID_STATUE: case LEVELID_ARCHIVES: case LEVELID_STREETS:
    case LEVELID_DEPOT: case LEVELID_TRAIN: case LEVELID_JUNGLE:
    case LEVELID_CONTROL: case LEVELID_CAVERNS: case LEVELID_CRADLE:
    case LEVELID_AZTEC: case LEVELID_EGYPT:
        return TRUE;
    default:
        return FALSE;
    }
}

/* Within a metre of a player (any slot, local or remote), on that floor. */
static s32 gevrAllyNearPlayer(const coord3d *pos)
{
    for (s32 i = 0; i < getPlayerCount(); ++i) {
        struct player *pl = g_playerPointers[i];
        if (!pl || !pl->prop) continue;
        f32 x = pl->prop->pos.x - pos->x, y = pl->prop->pos.y - pos->y, z = pl->prop->pos.z - pos->z;
        if (x*x + z*z < 100.0f*100.0f && y > -200.0f && y < 200.0f) return TRUE;
    }
    return FALSE;
}

/* Where she appears: a ring around the player she follows, reached in a
 * straight line, clear of walls, objects and characters, and a metre from
 * every player. GoldenEye's own spawn check (chrAdjustPosForSpawn) skips the
 * current player's body and only steps 60 units, so spawning "at Bond" put
 * her inside a player at Dam's co-op start, who couldn't move until she died. */
static s32 gevrAllySpawnSpot(const PropRecord *bond, coord3d *spot, StandTile **stan, f32 *angle)
{
    static const f32 radii[] = { 150.0f, 250.0f, 110.0f };
    for (s32 r = 0; r < (s32)(sizeof(radii) / sizeof(radii[0])); ++r) {
        for (s32 i = 0; i < 8; ++i) {
            f32 a = i * 0.7853982f;
            coord3d test = bond->pos;
            StandTile *tile = bond->stan;
            test.x += sinf(a) * radii[r];
            test.z += cosf(a) * radii[r];
            if (!stanTestLineUnobstructed(&tile, bond->pos.x, bond->pos.z, test.x, test.z,
                                          CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER, 0.0f, 1.0f, 0.0f, 1.0f)
                || stanTestVolume(&tile, test.x, test.z, 30.0f,
                                  CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER,
                                  0.0f, 1.0f) >= 0
                || gevrAllyNearPlayer(&test)) continue;
            *spot = test;
            *stan = tile;
            *angle = a < 3.1415927f ? a + 3.1415927f : a - 3.1415927f; /* facing the player */
            return TRUE;
        }
    }
    return FALSE;
}

void gevrAllyTick(void)
{
    PropRecord *bond, *prop;
    coord3d spot;
    StandTile *stan;
    f32 angle;
    s32 player;
    if (!s_enabled || s_spawned || !gevrAllyAuthority() || !gevrAllyMission()
        || g_ClockTimer <= 0 || !gevrAllyPlaying()) return;
    player = gevrAllyPlayer(NULL);
    if (player < 0) return;
    bond = g_playerPointers[player]->prop;
    if (s_wait > 0) { s_wait -= g_ClockTimer; return; }
    /* No clear spot yet (a crowded start room): wait and try again rather
     * than spawn on someone. Allow on-screen placement: she joins in view. */
    if (!gevrAllySpawnSpot(bond, &spot, &stan, &angle)) { s_wait = 60; return; }
    prop = chrSpawnAtCoord(BODY_Natalya_Jungle_Fatigues, HEAD_Natalya_Jungle_Fatigues,
                          &spot, stan, angle, NULL, 0x10 | GEVR_ALLY_SPAWN_FLAG);
    if (prop == NULL) { s_wait = 60; return; }
    s_ally = prop->chr;
    s_identity = s_ally->chrnum; /* allocated identity, never a mission ID */
    s_spawned = TRUE;
    gevrAllyConfigure(s_ally);
    chrGiveWeapon(s_ally, s_weapons[s_weapon].model, s_weapons[s_weapon].item,
                  netHordeActive() ? PROPFLAG_AIUNDROPPABLE : 0);
    /* Announce only after role, durability and weapon are configured. */
    gevrCoopChrSpawned(s_ally, NULL, 0x10 | GEVR_ALLY_SPAWN_FLAG);
}

static f32 gevrAllyDistanceSq(const PropRecord *a, const PropRecord *b)
{
    f32 x = a->pos.x - b->pos.x, y = a->pos.y - b->pos.y, z = a->pos.z - b->pos.z;
    return x*x + y*y + z*z;
}

static s32 gevrAllyEnemy(ChrRecord *chr)
{
    if (!chr || chr == s_ally || !chr->model || !chr->prop || !chr->prop->stan
        || chr->prop->type != PROP_TYPE_CHR || chrIsDead(chr)
        || chr->actiontype == ACT_SURRENDER || chr->chrnum < 0
        || (chr->hidden & CHRHIDDEN_REMOVE)
        || (chr->chrflags & (CHRFLAG_HIDDEN | CHRFLAG_COUNT_DEATH_AS_CIVILIAN | CHRFLAG_CAN_SHOOT_CHRS))
        || (!chr->weapons_held[0] && !chr->weapons_held[1])) return FALSE;
    /* Armed story allies must not be treated as enemies. Mishkin shares a
     * body with ordinary guards; his named head disambiguates them. */
    switch (chr->bodynum) {
    case BODY_Natalya_Skirt: case BODY_Natalya_Jungle_Fatigues:
    case BODY_Boris: case BODY_Valentin_: case BODY_Trevelyan_006:
    case BODY_Scientist_1_Male: case BODY_Scientist_2_Female:
    case BODY_Civilian_1_Female: case BODY_Civilian_2: case BODY_Civilian_3: case BODY_Civilian_4:
        return FALSE;
    case BODY_Trevelyan_Janus:
        return g_StageNum == LEVELID_CRADLE;
    default:
        return chr->headnum != HEAD_Male_Mishkin;
    }
}

s32 gevrAllyThink(ChrRecord *chr)
{
    ChrRecord *target = NULL;
    PropRecord *bond;
    f32 best, distance;
    s32 i, player;
    if (!gevrAllyIs(chr)) return FALSE;
    if (chrIsDead(chr) || !chrIsNotDeadOrShot(chr)) return TRUE;
    player = gevrAllyPlayer(chr->prop);
    if (!s_configKnown || !gevrAllyAuthority() || !gevrAllyPlaying() || player < 0) {
        check_set_actor_standing_still(chr, 0, 0);
        return TRUE;
    }
    bond = g_playerPointers[player]->prop;
    if (!bond->stan) return TRUE;
    s_think -= g_ClockTimer;
    if (s_think > 0) return TRUE;
    s_think = 15;
    distance = gevrAllyDistanceSq(chr->prop, bond);
    /* Stay with Bond rather than hunting across the stage. */
    best = s_aggression == 0 ? 600.0f*600.0f : s_aggression == 1 ? 1200.0f*1200.0f : 1800.0f*1800.0f;
    if (distance < 1000.0f*1000.0f) {
        for (i = 0; i < get_numguards(); ++i) {
            ChrRecord *enemy = &g_ChrSlots[i];
            f32 d;
            if (!gevrAllyEnemy(enemy)) continue;
            /* Cautious companions only engage guards already alerted. */
            if (s_aggression == 0 && enemy->alertness < 100) continue;
            d = gevrAllyDistanceSq(chr->prop, enemy->prop);
            if (d < best && check_if_position_in_same_room(chr, &enemy->prop->pos, enemy->prop->stan)) {
                target = enemy;
                best = d;
            }
        }
    }
    if (target) {
        if (chr->actiontype != ACT_ATTACK || s_target != target->chrnum) {
            check_set_actor_standing_still(chr, 0, 0);
            actor_aim_at_actor(chr, TARGET_CHR, target->chrnum);
        }
        s_target = target->chrnum;
    } else {
        s_target = -1;
        if (distance > 250.0f*250.0f) {
            /* Use the same NPC pathfinder without the enemy crowd limit in
             * chrGoToBond, so fast reinforcements don't stop her following. */
            if (chr->actiontype != ACT_GOPOS || s_wait <= 0) {
                check_set_actor_standing_still(chr, 0, 0);
                plot_course_for_actor(chr, &bond->pos, bond->stan, SPEED_RUN);
                s_wait = 60;
            }
            s_wait -= 15;
        } else {
            check_set_actor_standing_still(chr, 0, 0);
        }
    }
    return TRUE;
}

/* Scope the multiplier to the existing NPC hit calculation. Other NPCs and
 * Bond keep their normal damage. Also reject incidental hits on allies. */
void gevrAllyHitGuard(ChrRecord *shooter, ChrRecord *victim, coord3d *direction, s32 weapon)
{
    f32 previous = s_shotScale;
    if (gevrAllyIs(shooter)) {
        if (!gevrAllyEnemy(victim)) return;
        s_shotScale = s_damage;
    }
    handles_shot_actors(victim, 0xF, direction, weapon, FALSE);
    s_shotScale = previous;
}

f32 gevrAllyDamageScale(void)
{
    return s_shotScale;
}
