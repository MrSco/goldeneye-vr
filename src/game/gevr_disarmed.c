/*
 * A guard force pull disarmed (docs/force-pull.md "After a disarm").
 *
 * GoldenEye never leaves a living guard without a gun: its fire actions all
 * need one, so a disarmed guard's AI list kept failing them and side-stepping
 * (headset, test.21: "wandering around aimlessly"). Perfect Dark has the
 * behaviour (gailists.c func0007_alerted: go to the gun, pick it up; else
 * hand combat, func000f_hand_combat: run in, punch, loop). Its decisions are
 * port/src/gevr_disarm.c (tested); this is the guard's side: what he sees,
 * where he runs and the punch's hit.
 *
 * GoldenEye has no punch or pick-up animation for guards. The punch is the
 * alarm press's reach (ANIM_extending_left_hand, frames 40-82 as
 * chrStartAlarmChooseAnimation plays it), landing at frame 64; at the gun he
 * stands a moment and it is in his hand, as chrai.c AI_ChrEquipObject does.
 *
 * Solo and the co-op host (guards run there; chrlvActionTick runs as the
 * guard's target player). Online he only punches: a gun on the floor is each
 * headset's own copy, so one taken on the host would stay on the clients.
 */
#include <ultra64.h>
#include <bondgame.h>
#include <bondaicommands.h>
#include <bondconstants.h>
#include <math.h>
#include "system.h"
#include "chr.h"
#include "chrai.h"
#include "chraction.h"
#include "propobj.h"
#include "model.h"
#include "gun.h"
#include "file.h"
#include "player.h"
#include "lv.h"
#include "bondview.h"
#include "stan.h"
#include <random.h>
#include "gevr_disarm.h"

extern bool netIsActive(void);
extern bool netIsHost(void);
extern int netHordeActive(void);
extern s32 plot_course_for_actor(ChrRecord *chr, coord3d *pos, StandTile *tile, SPEED speed);
extern bool chrIsNotDeadOrShot(ChrRecord *chr);
extern f32 get_distance_actor_to_position(ChrRecord *self, coord3d *pos);
extern f32 chrlvGetSubrotySideback(ChrRecord *self);
extern s32 gevrPullBusy(PropRecord *prop);   /* gevr_forcepull.c: in flight to a hand */
extern s32 gevrAllyIs(ChrRecord *chr);

#define GEVR_DISARMED_MAX        16
#define GEVR_DISARMED_DROP_TICKS 30     /* the drop waits for his own tick (chr.c objDrop) */
#define GEVR_DISARMED_REPLOT     30     /* a run's course is plotted again this often */
#define GEVR_DISARMED_HIT_FRAME  64.0f  /* the reach at its furthest */
#define GEVR_DISARMED_END_FRAME  82     /* the alarm press's end (chraction.c) */
#define GEVR_DISARMED_PUNCH_ARC  0.7854f   /* 45 degrees either side of his facing */
#define GEVR_DISARMED_STEP_CM    150.0f    /* a gun this far above or below his feet is on another floor */
/* one punch: a PP7 shot's damage, scaled as a guard's shot is (chrlvUpdateShotbondsum) */
#define GEVR_DISARMED_PUNCH_ITEM ITEM_WPPK

typedef struct
{
    ChrRecord *chr;
    s32 chrnum;
    s32 since;              /* g_GlobalTimer at the disarm */
    s32 started;            /* the state below is running (the gun has left his hand) */
    GevrDisarmState st;
    PropRecord *gun;        /* the gun he goes for */
    PropRecord *abandoned;  /* the one he gave up on */
    void *punch;            /* the punch's animation, while it plays */
    s32 hit;                /* this punch has had its hit frame */
    s32 replot;
    s32 last;               /* the last action, for the log */
} GevrDisarmed;

static GevrDisarmed s_disarmed[GEVR_DISARMED_MAX];

void gevrDisarmedReset(void)
{
    s32 i;

    for (i = 0; i < GEVR_DISARMED_MAX; i++)
    {
        s_disarmed[i].chr = NULL;
    }
}

static GevrDisarmed *gevrDisarmedOf(ChrRecord *chr)
{
    s32 i;

    for (i = 0; i < GEVR_DISARMED_MAX; i++)
    {
        if (s_disarmed[i].chr == chr && chr != NULL && s_disarmed[i].chrnum == chr->chrnum)
        {
            return &s_disarmed[i];
        }
    }
    return NULL;
}

/* force pull took the gun from his hand (gevr_forcepull.c, here or a client's ask on the host) */
void gevrDisarmedMark(ChrRecord *chr)
{
    GevrDisarmed *d;
    s32 i, oldest = 0;

    if (chr == NULL || chr->prop == NULL || gevrAllyIs(chr))
    {
        return;
    }
    d = gevrDisarmedOf(chr);
    if (d == NULL)
    {
        for (i = 0; i < GEVR_DISARMED_MAX; i++)
        {
            if (s_disarmed[i].chr == NULL)
            {
                break;
            }
            if ((s32) (s_disarmed[i].since - s_disarmed[oldest].since) < 0)
            {
                oldest = i;
            }
        }
        d = &s_disarmed[i < GEVR_DISARMED_MAX ? i : oldest];
    }
    d->chr = chr;
    d->chrnum = chr->chrnum;
    d->since = g_GlobalTimer;
    d->started = FALSE;
    d->gun = d->abandoned = NULL;
    d->punch = NULL;
    d->hit = FALSE;
    d->replot = 0;
    d->last = -1;
    sysLogPrintf(LOG_NOTE, "disarmed: guard %d lost his gun", chr->chrnum);
}

static void gevrDisarmedClear(GevrDisarmed *d, const char *why)
{
    sysLogPrintf(LOG_NOTE, "disarmed: guard %d done (%s)", d->chrnum, why);
    d->chr = NULL;
}

static s32 gevrDisarmedGunItem(s32 item)
{
    s32 as = GEVR_ITEM_AS(item);

    return as >= ITEM_WPPK && as <= ITEM_LASER;   /* the guns a guard holds and fires; no launchers */
}

/* a gun lying free, landed, that no hand's pull and no other disarmed guard has */
static s32 gevrDisarmedGunFree(const GevrDisarmed *d, PropRecord *prop)
{
    ObjectRecord *obj;
    s32 i;

    if (prop == NULL || prop->type != PROP_TYPE_WEAPON || prop->weapon == NULL || (obj = prop->obj) == NULL
        || obj->type != PROPDEF_COLLECTABLE || prop->parent != NULL || prop->stan == NULL
        || !(prop->flags & PROPFLAG_ENABLED) || prop->timetoregen > 0 || (obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE)
        || !gevrDisarmedGunItem(prop->weapon->weaponnum) || gevrPullBusy(prop) || prop == d->abandoned)
    {
        return FALSE;
    }
    if ((obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) && obj->projectile != NULL
        && (obj->projectile->flags & PROJECTILEFLAG_AIRBORNE))
    {
        return FALSE;   /* still falling */
    }
    for (i = 0; i < GEVR_DISARMED_MAX; i++)
    {
        if (&s_disarmed[i] != d && s_disarmed[i].chr != NULL && s_disarmed[i].gun == prop)
        {
            return FALSE;
        }
    }
    return TRUE;
}

/* in sight along the floor (the walk-over rule force pull uses): the tile walk is clear and ends on its tile */
static s32 gevrDisarmedCanReach(PropRecord *from, PropRecord *to)
{
    StandTile *stan = from->stan;

    if (stan == NULL || to->stan == NULL)
    {
        return FALSE;
    }
    return stanTestLineUnobstructed(&stan, from->pos.x, from->pos.z, to->pos.x, to->pos.z, CDTYPE_DOORS, 30.0f, 30.0f, 0.0f, 1.0f) != 0
        && stan == to->stan;
}

static f32 gevrDisarmedFlat(const PropRecord *a, const PropRecord *b)
{
    f32 dx = b->pos.x - a->pos.x, dz = b->pos.z - a->pos.z;

    return sqrtf(dx * dx + dz * dz);
}

static PropRecord *gevrDisarmedFindGun(GevrDisarmed *d)
{
    PropRecord *me = d->chr->prop, *best = NULL, *prop;
    f32 bestDist = GEVR_DISARM_SEARCH_CM;

    for (prop = chrpropGetActiveTail(); prop != NULL; prop = prop->prev)
    {
        f32 dist;

        if (!gevrDisarmedGunFree(d, prop) || (dist = gevrDisarmedFlat(me, prop)) >= bestDist
            || fabsf(prop->pos.y - d->chr->ground) > GEVR_DISARMED_STEP_CM || !gevrDisarmedCanReach(me, prop))
        {
            continue;
        }
        best = prop;
        bestDist = dist;
    }
    return best;
}

static s32 gevrDisarmedTakeGun(GevrDisarmed *d)
{
    PropRecord *prop = d->gun;
    ChrRecord *chr = d->chr;

    if (!gevrDisarmedGunFree(d, prop))
    {
        return FALSE;
    }
    /* chrai.c AI_ChrEquipObject: off the floor and into his hand */
    chrpropDeregisterRooms(prop);
    chrpropDelist(prop);
    chrpropDisable(prop);
    if (!chrEquipWeapon((WeaponObjRecord *) prop->obj, chr))
    {
        chrpropReparent(prop, chr->prop);
    }
    sysLogPrintf(LOG_NOTE, "disarmed: guard %d picked up item %d", chr->chrnum, prop->weapon->weaponnum);
    return chr->weapons_held[GUNRIGHT] != NULL || chr->weapons_held[GUNLEFT] != NULL;
}

/* the punch at its hit frame: still in reach, in front and in sight (PD chrPunchInflictDamage) */
static void gevrDisarmedHit(GevrDisarmed *d, PropRecord *bond)
{
    ChrRecord *chr = d->chr;
    f32 angle, damage;

    if (bond == NULL || g_CurrentPlayer == NULL || g_CurrentPlayer->bonddead
        || gevrDisarmedFlat(chr->prop, bond) > GEVR_DISARM_REACH_CM || !gevrDisarmedCanReach(chr->prop, bond))
    {
        sysLogPrintf(LOG_NOTE, "disarmed: guard %d punched and missed", chr->chrnum);
        return;
    }
    angle = get_distance_actor_to_position(chr, &bond->pos);
    if (angle > GEVR_DISARMED_PUNCH_ARC && angle < M_TAU_F - GEVR_DISARMED_PUNCH_ARC)
    {
        sysLogPrintf(LOG_NOTE, "disarmed: guard %d punched and missed (not in front)", chr->chrnum);
        return;
    }
    damage = 0.125f * gunItemGetDestructionAmount(GEVR_DISARMED_PUNCH_ITEM) * g_AiDamageModifier * get_007_damage_mod();
    /* as a guard's shot lands (co-op: forwarded to the target's headset in record_damage_kills) */
    bondviewCallRecordDamageKills(damage, chrlvGetSubrotySideback(chr), -1, 1);
    sysLogPrintf(LOG_NOTE, "disarmed: guard %d punched the player for %.3f", chr->chrnum, damage);
}

static void gevrDisarmedRun(GevrDisarmed *d, coord3d *pos, StandTile *stan)
{
    ChrRecord *chr = d->chr;

    d->replot -= g_ClockTimer;
    if (chr->actiontype != ACT_GOPOS || d->replot <= 0)
    {
        plot_course_for_actor(chr, pos, stan, SPEED_RUN);
        d->replot = GEVR_DISARMED_REPLOT;
    }
}

static void gevrDisarmedFace(ChrRecord *chr, s32 type)
{
    if (chr->actiontype != ACT_STAND || chr->act_stand.face_entitytype != type)
    {
        check_set_actor_standing_still(chr, type, 0);
    }
}

s32 gevrDisarmedThink(ChrRecord *chr)
{
    static const char *names[] = { "idle", "face", "go to gun", "at gun", "take gun", "give up gun", "chase", "punch", "wait" };
    GevrDisarmed *d = gevrDisarmedOf(chr);
    PropRecord *bond;
    f32 gunCm = -1.0f, playerCm = -1.0f;
    s32 clear = FALSE, front = FALSE, striking, act;
    s32 held;

    if (d == NULL)
    {
        return FALSE;
    }
    if (netIsActive() && (!netIsHost() || netHordeActive()))
    {
        return FALSE;   /* guards are the host's; a client's copy is its puppet */
    }
    if (chr->model == NULL || chr->prop == NULL || chrIsDead(chr) || chr->actiontype == ACT_SURRENDER
        || (chr->hidden & CHRHIDDEN_REMOVE))
    {
        gevrDisarmedClear(d, "dead, gone or surrendered");
        return FALSE;
    }
    held = chr->weapons_held[GUNRIGHT] != NULL || chr->weapons_held[GUNLEFT] != NULL;
    if (!d->started)
    {
        if (held && (s32) (g_GlobalTimer - d->since) < GEVR_DISARMED_DROP_TICKS)
        {
            return FALSE;   /* still letting go of it */
        }
        if (held)
        {
            gevrDisarmedClear(d, "still armed");
            return FALSE;
        }
        d->started = TRUE;
        gevrDisarmInit(&d->st, !netIsActive());
    }
    else if (held)
    {
        gevrDisarmedClear(d, "armed again");
        return FALSE;
    }
    if (!chrIsNotDeadOrShot(chr))
    {
        d->punch = NULL;
        return TRUE;   /* hit: his flinch plays out */
    }

    striking = d->punch != NULL && chr->actiontype == ACT_ANIM && objecthandlerGetModelAnim(chr->model) == d->punch
        && modelGetAnimFrame(chr->model) < (f32) GEVR_DISARMED_END_FRAME;
    bond = getCurrentPlayerProp();
    if (striking && !d->hit && modelGetAnimFrame(chr->model) >= GEVR_DISARMED_HIT_FRAME)
    {
        d->hit = TRUE;
        gevrDisarmedHit(d, bond);
    }
    if (!striking)
    {
        d->punch = NULL;
    }

    if (bond != NULL && bond->stan != NULL && g_CurrentPlayer != NULL && !g_CurrentPlayer->bonddead)
    {
        playerCm = gevrDisarmedFlat(chr->prop, bond);
        clear = gevrDisarmedCanReach(chr->prop, bond);
        {
            f32 angle = get_distance_actor_to_position(chr, &bond->pos);
            front = angle < GEVR_DISARMED_PUNCH_ARC || angle > M_TAU_F - GEVR_DISARMED_PUNCH_ARC;
        }
    }
    if (d->st.phase == GEVR_DISARM_FETCH && d->gun != NULL && !gevrDisarmedGunFree(d, d->gun))
    {
        d->gun = NULL;   /* taken or gone: look again */
    }
    if (gevrDisarmWantsGun(&d->st))
    {
        if (d->gun == NULL)
        {
            d->gun = gevrDisarmedFindGun(d);
        }
        gunCm = d->gun != NULL ? gevrDisarmedFlat(chr->prop, d->gun) : -1.0f;
    }

    act = gevrDisarmStep(&d->st, g_ClockTimer, gunCm, playerCm, clear, front, striking);
    if (act != d->last)
    {
        sysLogPrintf(LOG_NOTE, "disarmed: guard %d %s (gun %.0f cm, player %.0f cm)", chr->chrnum,
                     act >= 0 && act < (s32) (sizeof(names) / sizeof(names[0])) ? names[act] : "?", gunCm, playerCm);
        d->last = act;
    }
    if (d->st.phase == GEVR_DISARM_MELEE && act != GEVR_DISARM_GIVE_UP_GUN)
    {
        d->gun = NULL;
    }
    switch (act)
    {
        case GEVR_DISARM_IDLE:
            return FALSE;   /* no one to fight: his own AI */
        case GEVR_DISARM_FACE:
            gevrDisarmedFace(chr, TARGET_BOND);
            break;
        case GEVR_DISARM_GO_GUN:
            gevrDisarmedRun(d, &d->gun->pos, d->gun->stan);
            break;
        case GEVR_DISARM_AT_GUN:
            gevrDisarmedFace(chr, 0);
            break;
        case GEVR_DISARM_TAKE_GUN:
            if (gevrDisarmedTakeGun(d))
            {
                gevrDisarmedClear(d, "armed again");
                return FALSE;   /* his own AI takes it from here: it has a gun to fire */
            }
            d->abandoned = d->gun;
            d->gun = NULL;
            break;
        case GEVR_DISARM_GIVE_UP_GUN:
            d->abandoned = d->gun;
            d->gun = NULL;
            gevrDisarmedFace(chr, TARGET_BOND);
            break;
        case GEVR_DISARM_CHASE:
            gevrDisarmedRun(d, &bond->pos, bond->stan);
            break;
        case GEVR_DISARM_STRIKE:
            if (check_if_able_to_then_perform_animation(chr, ANIM_extending_left_hand, 40, GEVR_DISARMED_END_FRAME,
                                                        (randomGetNext() & 1) ? ANIM_MIRROR : 0, 8))
            {
                modelSetAnimSpeed(chr->model, 1.0f, 0.0f);   /* the alarm press's pace: a jab, not a reach */
                d->punch = objecthandlerGetModelAnim(chr->model);
                d->hit = FALSE;
            }
            break;
        case GEVR_DISARM_WAIT:
        default:
            break;
    }
    return TRUE;
}
