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

extern bool netIsActive(void);
extern s32 g_StageNum, g_ClockTimer;
extern int VrAlly, VrAllyWeapon, VrAllyAggression, VrAllyHealth, VrAllyDamage, VrAllyInvincible;
extern PropRecord *chrSpawnAtCoord(s32 body, s32 head, coord3d *pos, StandTile *tile,
                                  f32 angle, AIRecord *ai, s32 flags);
extern s32 plot_course_for_actor(ChrRecord *chr, coord3d *pos, StandTile *tile, SPEED speed);
extern s32 chrIsNotDeadOrShot(ChrRecord *chr);

static ChrRecord *s_ally;
static s32 s_identity, s_spawned, s_wait, s_think, s_target;
static s32 s_enabled, s_weapon, s_aggression, s_health, s_invincible;
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
    s_enabled = VrAlly != 0;
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

static s32 gevrAllySolo(void)
{
    return !netIsActive() && getPlayerCount() == 1 && gevrRomCampaign() == 0;
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

void gevrAllyTick(void)
{
    PropRecord *bond, *prop;
    if (!s_enabled || s_spawned || !gevrAllySolo() || !gevrAllyMission()
        || g_ClockTimer <= 0 || g_CameraMode != CAMERAMODE_FP
        || lvlGetControlsLockedFlag() || g_CurrentPlayer == NULL
        || g_CurrentPlayer->bonddead) return;
    bond = g_CurrentPlayer->prop;
    if (bond == NULL || bond->stan == NULL) return;
    if (s_wait > 0) { s_wait -= g_ClockTimer; return; }
    /* The spawn routine searches around Bond and checks floor and blockers.
     * Allow on-screen placement, otherwise small starting rooms never fit. */
    prop = chrSpawnAtCoord(BODY_Natalya_Jungle_Fatigues, HEAD_Natalya_Jungle_Fatigues,
                          &bond->pos, bond->stan, 0.0f, NULL, 0x10);
    if (prop == NULL) { s_wait = 60; return; }
    s_ally = prop->chr;
    s_identity = s_ally->chrnum; /* allocated identity, never a mission ID */
    s_spawned = TRUE;
    s_ally->chrflags |= CHRFLAG_CAN_SHOOT_CHRS | CHRFLAG_NO_AUTOAIM;
    if (s_invincible) s_ally->chrflags |= CHRFLAG_INVINCIBLE;
    s_ally->maxdamage = 40.0f * s_health * 0.01f;
    s_ally->accuracyrating = 20;
    s_ally->flags2 |= FLAGS2_DONT_POINT_AT_BOND;
    chrGiveWeapon(s_ally, s_weapons[s_weapon].model, s_weapons[s_weapon].item, 0);
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
    s32 i;
    if (!gevrAllyIs(chr)) return FALSE;
    if (chrIsDead(chr) || !chrIsNotDeadOrShot(chr)) return TRUE;
    if (!gevrAllySolo() || g_CameraMode != CAMERAMODE_FP || lvlGetControlsLockedFlag() || !g_CurrentPlayer
        || g_CurrentPlayer->bonddead || !g_CurrentPlayer->prop) {
        check_set_actor_standing_still(chr, 0, 0);
        return TRUE;
    }
    bond = g_CurrentPlayer->prop;
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
