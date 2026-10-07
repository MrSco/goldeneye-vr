/*
 * Bots online: player slots the host runs, as GoldenEye's own split-screen
 * runs its players, with the controller filled in by an AI instead of a
 * person. The game moves them (MoveBond: walls, doors, steps), fires their
 * guns and judges their shots (the copy's view pass, lv.c gevrViewPass),
 * hurts, kills, respawns and scores them; the net layer sends each as an
 * owner sends its player (net_player_sync.c netSyncSendBotMove), so the
 * other headsets see an ordinary remote player.
 *
 * Perfect Dark's simulants are the model (bot.c): their target choice,
 * reaction time and aim error by difficulty. PD's own netplay runs no
 * simulants online, so the wiring is this port's.
 *
 * A bot's pad is 1.1 Honey's (joy.c gevrSlotPad): stick Y walks, C-left and
 * C-right strafe, Z fires, B opens doors and reloads. Its view is set
 * directly, as PD turns a simulant (bot.c botTick).
 */

#include <ultra64.h>
#include <math.h>
#include <string.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "bondview.h"
#include "player.h"
#include "gevr_bot.h"
#include "net_game.h"

extern bool netIsActive(void);
extern bool netIsHost(void);
extern s32 g_gevrStereo;
extern s32 g_ClockTimer;
extern void gevrBotRespawn(void);       /* bondview2.c */
extern void sysLogPrintf(s32 level, const char *fmt, ...);
extern PadRecord *g_Startpad[];
extern s32 startpadcount;
extern bool netSlotOccupied(int slot);
extern int bondinvHasInvItem(ITEM_IDS item);
extern s32 get_ammo_count_for_weapon(ITEM_IDS weapon);
extern s32 currentPlayerEquipWeaponWrapper(GUNHAND hand, s32 next_weapon);
extern PropRecord *chrpropGetActiveTail(void);
extern MPSCENARIOS get_scenario(void);
extern bool bondinvIsAliveWithFlag(void);
extern s32 stanTestLineUnobstructed(StandTile **pTile, f32 p_x, f32 p_z, f32 dest_x, f32 dest_z, s32 cdtypes,
                                    f32 unkHeight, f32 unkA, f32 unkB, f32 unkC);

/*
 * Perfect Dark's simulant difficulties (bot.c g_BotDifficulties), NTSC
 * ticks: the reaction (shootdelay: ticks of sight before the first shot),
 * the aim error's range in radians while the target is fresh (unk04, unk08)
 * and the ticks of sight that settle it (unk0c), the error turning adds
 * (unk10), and its floor (unk18). Meat to Dark.
 */
typedef struct GevrBotDifficulty {
    s32 shootdelay;
    f32 errmin, errmax;
    s32 settle;
    f32 turnerr;
    f32 cloakerr;
    f32 errfloor;
} GevrBotDifficulty;

static const GevrBotDifficulty s_difficulties[NET_BOT_DIFF_COUNT] = {
    /* meat */ { 90, 0.26175770163536f,  0.52351540327072f,  600, 10, 0.69802051782608f, 0.34901025891304f },
    /* easy */ { 60, 0.12215359508991f,  0.24430719017982f,  360, 10, 0.49733963608742f, 0.13960410654545f },
    /* norm */ { 30, 0.069802053272724f, 0.13960410654545f,  180, 4,  0.34901025891304f, 0.08725256472826f },
    /* hard */ { 15, 0.026175770908594f, 0.069802053272724f, 90,  2,  0.24430719017982f, 0.034901026636362f },
    /* perf */ { 0,  0,                  0.034901026636362f, 45,  1,  0.17450512945652f, 0 },
    /* dark */ { 0,  0,                  0,                  0,   0,  0.13960410654545f, 0 },
};

/* PD's distance modes (botcmd.c botcmdTickDistMode) */
enum { BOT_DIST_NONE = -1, BOT_DIST_BACKUP, BOT_DIST_OK, BOT_DIST_ADVANCE, BOT_DIST_GOTO };

/* PD's distance configs (botcmd.c g_BotDistConfigs): the closest and farthest
 * a sim attacks from with a weapon of the kind */
enum { BOT_DISTCFG_CLOSE, BOT_DISTCFG_PISTOL, BOT_DISTCFG_DEFAULT, BOT_DISTCFG_SHOOTEXPLOSIVE, BOT_DISTCFG_THROWEXPLOSIVE };
static const f32 s_distconfigs[][2] = {
    { 0, 120 }, { 300, 450 }, { 300, 600 }, { 600, 1200 }, { 450, 700 },
};

/*
 * Perfect Dark's weapon preferences (botinv.c g_AibotWeaponPreferences):
 * the score a sim gives a gun (unk00) and its distance config, taken from
 * the row of PD's own copy of each GoldenEye gun (its classic weapons: PP9i,
 * CC13, KL01313, KF7 Special, ZZT, DMC, AR53, RC-P45) or of PD's gun of the
 * same kind. The critical ammo is the row's criticalammopri. A score of 0:
 * a bot never picks it (mines, the watch's laser, gadgets).
 */
typedef struct GevrBotWeapon {
    u8 score;
    u8 distconfig;
    u8 critical;
} GevrBotWeapon;

static const GevrBotWeapon s_weapons[ITEM_REMOTEMINE + 1] = {
    [ITEM_UNARMED]       = { 13,  BOT_DISTCFG_CLOSE, 0 },
    [ITEM_FIST]          = { 13,  BOT_DISTCFG_CLOSE, 0 },
    [ITEM_KNIFE]         = { 20,  BOT_DISTCFG_CLOSE, 0 },              /* combat knife */
    [ITEM_THROWKNIFE]    = { 20,  BOT_DISTCFG_CLOSE, 0 },
    [ITEM_WPPK]          = { 56,  BOT_DISTCFG_PISTOL, 10 },            /* PP9i */
    [ITEM_WPPKSIL]       = { 52,  BOT_DISTCFG_PISTOL, 10 },            /* Falcon 2 silencer */
    [ITEM_TT33]          = { 56,  BOT_DISTCFG_PISTOL, 10 },            /* CC13 */
    [ITEM_SKORPION]      = { 56,  BOT_DISTCFG_DEFAULT, 10 },           /* KL01313 */
    [ITEM_AK47]          = { 124, BOT_DISTCFG_DEFAULT, 30 },           /* KF7 Special */
    [ITEM_UZI]           = { 116, BOT_DISTCFG_DEFAULT, 30 },           /* ZZT */
    [ITEM_MP5K]          = { 124, BOT_DISTCFG_DEFAULT, 30 },           /* DMC */
    [ITEM_MP5KSIL]       = { 120, BOT_DISTCFG_DEFAULT, 30 },
    [ITEM_SPECTRE]       = { 120, BOT_DISTCFG_DEFAULT, 50 },           /* Cyclone */
    [ITEM_M16]           = { 156, BOT_DISTCFG_DEFAULT, 30 },           /* AR53 */
    [ITEM_FNP90]         = { 164, BOT_DISTCFG_DEFAULT, 40 },           /* RC-P45 */
    [ITEM_SHOTGUN]       = { 140, BOT_DISTCFG_PISTOL, 8 },
    [ITEM_AUTOSHOT]      = { 144, BOT_DISTCFG_PISTOL, 8 },
    [ITEM_SNIPERRIFLE]   = { 28,  BOT_DISTCFG_DEFAULT, 10 },
    [ITEM_RUGER]         = { 68,  BOT_DISTCFG_PISTOL, 8 },             /* DY357 Magnum */
    [ITEM_GOLDENGUN]     = { 180, BOT_DISTCFG_PISTOL, 6 },             /* DY357-LX */
    [ITEM_SILVERWPPK]    = { 60,  BOT_DISTCFG_PISTOL, 10 },
    [ITEM_GOLDWPPK]      = { 180, BOT_DISTCFG_PISTOL, 6 },
    [ITEM_LASER]         = { 112, BOT_DISTCFG_DEFAULT, 0 },
    [ITEM_GRENADELAUNCH] = { 160, BOT_DISTCFG_SHOOTEXPLOSIVE, 1 },      /* rocket launcher's row */
    [ITEM_ROCKETLAUNCH]  = { 160, BOT_DISTCFG_SHOOTEXPLOSIVE, 1 },
    [ITEM_GRENADE]       = { 36,  BOT_DISTCFG_THROWEXPLOSIVE, 2 },
};

/* PD's simulant turn (bot.c botTick): 0.0616 rad, 3.5 degrees, a tick */
#define BOT_TURN_PER_TICK 3.529f
#define BOT_STICK_FULL 70

typedef struct GevrBot {
    OSContPad pad;          /* this frame's controller */
    u16 pressed;            /* the buttons new this frame */
    s32 started;            /* the view below holds the bot's */
    f32 theta, verta;       /* the view the bot wants: degrees, the game's vv_theta/vv_verta */
    s32 stereo;             /* g_gevrStereo, put back after the bot's tick */
    s32 deadticks;          /* 60 Hz ticks since it died */
    GevrBotRoute route;     /* where it is walking */
    s32 routeticks;         /* since the route was planned */
    coord3d goal;           /* the route's end */
    coord3d lastpos;        /* for the stuck check */
    s32 stillticks;         /* walking without getting anywhere */
    s32 unstickticks;       /* sidestepping out of it */
    s32 unstickdir;
    /* Perfect Dark's aibot fields (bot.c), by the same names */
    s32 frame60;            /* the bot's own clock, 60 Hz ticks */
    s32 queryslot;          /* the player the next sight check is for */
    s8 insight[MAX_PLAYER_COUNT];
    s32 lastseen60[MAX_PLAYER_COUNT];
    f32 distance[MAX_PLAYER_COUNT];
    s32 target;             /* a slot, -1 none */
    s32 targetinsight;
    s32 shootdelaytimer60;
    f32 targetinsighttemperature;
    u32 random3;
    s32 random3ttl60;
    f32 extraanglebase, extraanglerate, extraangle;   /* radians */
    f32 speedtheta;         /* PD's turn speed measure, for the turning error */
    s32 distmode;
    s32 distmodettl60;
    s32 weapon;             /* the gun it wants in hand */
    s32 changeguntimer60;   /* PD's: no change again before this runs out */
    PropRecord *gotoprop;   /* a pickup it is fetching (PD's gotoprop) */
    s32 gotottl60;
    s32 fleettl60;          /* Flag Tag: until it picks a new place to run to */
} GevrBot;

static GevrBot s_bots[MAX_PLAYER_COUNT];

/* A stage loaded: the bots start over (their pickups and routes were the last stage's) */
void gevrBotStageLoaded(void)
{
    memset(s_bots, 0, sizeof(s_bots));
    gevrBotNavForget();
}

/* The host runs this slot */
static s32 gevrBotRuns(s32 slot)
{
    return slot >= 0 && slot < MAX_PLAYER_COUNT && netIsActive() && netIsHost() &&
           gevrNetSlotIsBot(slot) && gevrNetOwnsSlot(slot) && g_playerPointers[slot] != NULL;
}

/* joy.c: a bot's controller, for any slot; NULL when the slot is not a bot */
OSContPad *gevrBotPad(s32 slot, u16 *pressed)
{
    if (!gevrBotRuns(slot))
    {
        return NULL;
    }
    if (pressed != NULL)
    {
        *pressed = s_bots[slot].pressed;
    }
    return &s_bots[slot].pad;
}

/* The player's view, and what the game derives from it, as the move tick leaves them */
static void gevrBotSetView(struct player *pl, f32 theta, f32 verta)
{
    while (theta >= 360.0f) theta -= 360.0f;
    while (theta < 0.0f) theta += 360.0f;
    if (verta > 80.0f) verta = 80.0f;
    if (verta < -80.0f) verta = -80.0f;
    pl->vv_theta = theta;
    pl->vv_verta = verta;
    pl->vv_verta360 = verta < 0 ? verta + 360.0f : verta;
    pl->vv_costheta = cosf(theta * (M_PI_F / 180.0f));
    pl->vv_sintheta = sinf(theta * (M_PI_F / 180.0f));
    pl->vv_cosverta = cosf(verta * (M_PI_F / 180.0f));
    pl->vv_sinverta = sinf(verta * (M_PI_F / 180.0f));
    pl->speedtheta = 0.0f;
    pl->speedverta = 0.0f;
    pl->field_488.theta_transform.x = -pl->vv_sintheta;
    pl->field_488.theta_transform.y = 0.0f;
    pl->field_488.theta_transform.z = pl->vv_costheta;
    pl->field_488.applied_view.x = -pl->vv_sintheta * pl->vv_cosverta;
    pl->field_488.applied_view.y = pl->vv_sinverta;
    pl->field_488.applied_view.z = pl->vv_costheta * pl->vv_cosverta;
    pl->field_488.applied_view2.x = pl->vv_sintheta * pl->vv_sinverta;
    pl->field_488.applied_view2.y = pl->vv_cosverta;
    pl->field_488.applied_view2.z = -pl->vv_costheta * pl->vv_sinverta;
}

static u32 s_botRandom = 0x2545F491u;

/* The bots' own numbers: the game's generator is the match's, shared by seed */
static u32 gevrBotRandom(void)
{
    s_botRandom ^= s_botRandom << 13;
    s_botRandom ^= s_botRandom >> 17;
    s_botRandom ^= s_botRandom << 5;
    return s_botRandom;
}

static f32 gevrBotWrap(f32 degrees)
{
    while (degrees >= 180.0f) degrees -= 360.0f;
    while (degrees < -180.0f) degrees += 360.0f;
    return degrees;
}

/* The view turned toward theta (degrees) at the turn rate; the turn left */
static f32 gevrBotTurnTo(GevrBot *bot, f32 theta, f32 rate)
{
    f32 diff = gevrBotWrap(theta - bot->theta);
    f32 step = rate * (g_ClockTimer > 0 ? g_ClockTimer : 1);

    if (diff > step) bot->theta += step;
    else if (diff < -step) bot->theta -= step;
    else bot->theta = theta;
    return diff;
}

/* The heading to a point: the game's theta (forward is -sin, cos in x, z) */
static f32 gevrBotHeading(const coord3d *from, const coord3d *to)
{
    return atan2f(-(to->x - from->x), to->z - from->z) * (180.0f / M_PI_F);
}

/*
 * Walk toward a point with the view where it is: stick Y for the part of the
 * way ahead, C-left/C-right for the part across (the right is -cos, -sin in
 * x, z: radar.c).
 */
static void gevrBotMoveToward(struct player *pl, GevrBot *bot, const coord3d *to, OSContPad *pad)
{
    f32 dx = to->x - pl->prop->pos.x;
    f32 dz = to->z - pl->prop->pos.z;
    f32 len = sqrtf(dx * dx + dz * dz);
    f32 rad = bot->theta * (M_PI_F / 180.0f);
    f32 ahead;
    f32 across;

    if (len < 1.0f)
    {
        return;
    }
    ahead = (dx * -sinf(rad) + dz * cosf(rad)) / len;
    across = (dx * -cosf(rad) + dz * -sinf(rad)) / len;
    if (ahead > 0.2f || ahead < -0.2f)
    {
        s32 stick = (s32)(ahead * BOT_STICK_FULL * 1.4f);

        pad->stick_y = (s8)(stick > BOT_STICK_FULL ? BOT_STICK_FULL : stick < -BOT_STICK_FULL ? -BOT_STICK_FULL : stick);
    }
    if (across > 0.38f) pad->button |= R_CBUTTONS;
    else if (across < -0.38f) pad->button |= L_CBUTTONS;
}

/* A start pad to walk to while there is nothing better (PD's sims roam the
 * same way, to pads, when they have no target: bot.c MA_AIBOTGOTOPOS) */
static s32 gevrBotPickRoam(struct player *pl, GevrBot *bot)
{
    s32 tries;

    for (tries = 0; tries < 4 && startpadcount > 0; tries++)
    {
        PadRecord *roam = g_Startpad[gevrBotRandom() % startpadcount];

        if (roam == NULL || roam->stan == NULL)
        {
            continue;
        }
        if (gevrBotNavPlan(&bot->route, pl->prop->stan, &pl->prop->pos, roam->stan, &roam->pos))
        {
            bot->goal = roam->pos;
            bot->routeticks = 0;
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * Not getting anywhere while walking: a closed door (B opens it, as a
 * player's B does), else a sidestep and a new route.
 */
static void gevrBotUnstick(s32 slot, struct player *pl, GevrBot *bot, OSContPad *pad)
{
    f32 dx = pl->prop->pos.x - bot->lastpos.x;
    f32 dz = pl->prop->pos.z - bot->lastpos.z;
    s32 moving = pad->stick_y != 0 || (pad->button & (L_CBUTTONS | R_CBUTTONS));

    if (bot->unstickticks > 0)
    {
        bot->unstickticks -= g_ClockTimer;
        pad->stick_y = -BOT_STICK_FULL / 2;
        pad->button = (pad->button & ~(L_CBUTTONS | R_CBUTTONS)) | (bot->unstickdir ? R_CBUTTONS : L_CBUTTONS);
        return;
    }
    if (!moving || dx * dx + dz * dz > 2.0f * 2.0f * (g_ClockTimer > 0 ? g_ClockTimer : 1))
    {
        bot->stillticks = 0;
        bot->lastpos = pl->prop->pos;
        return;
    }
    bot->stillticks += g_ClockTimer;
    if (bot->stillticks >= 20 && bot->stillticks < 80 && (bot->stillticks / 10) % 2 == 0)
    {
        pad->button |= B_BUTTON;
    }
    if (bot->stillticks >= 80)
    {
        sysLogPrintf(1, "bots: slot %d stuck at %.0f,%.0f,%.0f; sidestepping, new route",
                     slot, pl->prop->pos.x, pl->prop->pos.y, pl->prop->pos.z);
        bot->stillticks = 0;
        bot->unstickticks = 30;
        bot->unstickdir = gevrBotRandom() & 1;
        bot->route.count = 0;
    }
}

/* A player the bot may fight now: in the level, alive, a foe */
static s32 gevrBotFoe(s32 slot, s32 other)
{
    struct player *op;

    if (other < 0 || other >= MAX_PLAYER_COUNT || other == slot || !netSlotOccupied(other) || !gevrNetBotFoes(slot, other))
    {
        return FALSE;
    }
    op = g_playerPointers[other];
    return op != NULL && op->prop != NULL && op->prop->stan != NULL && !op->bonddead;
}

/*
 * Line of sight, as a guard sees Bond (chraction.c chrCanSeeBond): the
 * floor walks unbroken from eye to eye, past no door, object or opaque
 * scenery, and ends on the other's tile. Players do not block it.
 */
static s32 gevrBotCanSee(struct player *pl, struct player *op)
{
    StandTile *tile = pl->prop->stan;
    f32 height = pl->eyeheight - 20.0f;

    if (height < 40.0f)
    {
        height = 40.0f;
    }
    return stanTestLineUnobstructed(&tile, pl->prop->pos.x, pl->prop->pos.z, op->prop->pos.x, op->prop->pos.z,
                                    CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE,
                                    height, height, 0.0f, 1.0f)
        && tile == op->prop->stan;
}

/* PD's botSetTarget: the reaction clock runs while the target is in sight */
static void gevrBotSetTarget(GevrBot *bot, s32 target)
{
    bot->targetinsight = target >= 0 && bot->insight[target];
    if (bot->target != target)
    {
        bot->target = target;
        bot->shootdelaytimer60 = 0;
        bot->route.count = 0;
    }
    else if (bot->targetinsight)
    {
        bot->shootdelaytimer60 += g_ClockTimer;
    }
    else
    {
        bot->shootdelaytimer60 -= g_ClockTimer;
        if (bot->shootdelaytimer60 < 0) bot->shootdelaytimer60 = 0;
    }
}

/* PD's bot0f192a74: the aim error, wide while the target is fresh in sight,
 * settling as it stays there, unsettled by turning */
static void gevrBotAimError(GevrBot *bot, s32 diff)
{
    const GevrBotDifficulty *d = &s_difficulties[diff];
    f32 lo;
    f32 hi;
    f32 turning;
    s32 i;

    bot->random3ttl60 -= g_ClockTimer;
    if (bot->random3ttl60 <= 0)
    {
        bot->random3 = gevrBotRandom();
        bot->random3ttl60 = 20 + gevrBotRandom() % 20;
    }
    bot->targetinsighttemperature += bot->targetinsight ? g_ClockTimer : -g_ClockTimer;
    turning = d->turnerr * bot->speedtheta * g_ClockTimer;
    bot->targetinsighttemperature -= turning < 0 ? -turning : turning;
    if (bot->targetinsighttemperature > bot->shootdelaytimer60) bot->targetinsighttemperature = bot->shootdelaytimer60;
    if (bot->targetinsighttemperature < 0) bot->targetinsighttemperature = 0;
    if (bot->targetinsighttemperature >= d->settle)
    {
        bot->targetinsighttemperature = d->settle;
        lo = hi = 0.0f;
    }
    else
    {
        f32 left = (d->settle - bot->targetinsighttemperature) / d->settle;

        lo = d->errmin * left;
        hi = d->errmax * left;
    }
    if (hi < d->errfloor) hi = d->errfloor;
    bot->extraanglebase = (hi - lo) * (bot->random3 & 0xffff) * 0.000015259021893144f + lo;
    if (bot->random3 & 0x10000) bot->extraanglebase = -bot->extraanglebase;
    for (i = 0; i < g_ClockTimer * 4; i++)
    {
        bot->extraanglerate = bot->extraanglerate * 0.97500002384186f + bot->extraanglebase;
    }
    bot->extraangle = bot->extraanglerate * 0.024999976158142f;
}

/*
 * PD's botChooseGeneralTarget: one sight check a tick, round the players;
 * keep a target in sight, else the nearest foe in sight, else (Meat and Easy
 * at once, the others when no one is in sight) the nearest foe anywhere.
 */
static void gevrBotChooseTarget(s32 slot, struct player *pl, GevrBot *bot, s32 diff)
{
    s32 order[MAX_PLAYER_COUNT];
    s32 count = 0;
    s32 i;
    s32 j;
    s32 nearest = -1;
    s32 q;

    q = bot->queryslot = (bot->queryslot + 1) % MAX_PLAYER_COUNT;
    if (gevrBotFoe(slot, q))
    {
        struct player *op = g_playerPointers[q];
        f32 dx = op->prop->pos.x - pl->prop->pos.x;
        f32 dy = op->prop->pos.y - pl->prop->pos.y;
        f32 dz = op->prop->pos.z - pl->prop->pos.z;

        bot->distance[q] = sqrtf(dx * dx + dy * dy + dz * dz);
        bot->insight[q] = gevrBotCanSee(pl, op);
    }
    else
    {
        bot->insight[q] = FALSE;
    }
    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        if (!gevrBotFoe(slot, i))
        {
            bot->insight[i] = FALSE;
            continue;
        }
        if (bot->insight[i]) bot->lastseen60[i] = bot->frame60;
        for (j = count; j > 0 && bot->distance[order[j - 1]] > bot->distance[i]; j--) order[j] = order[j - 1];
        order[j] = i;
        count++;
    }
    gevrBotAimError(bot, diff);
    if (bot->target >= 0 && !gevrBotFoe(slot, bot->target))
    {
        bot->target = -1;
    }
    if (bot->target >= 0 && bot->insight[bot->target])
    {
        gevrBotSetTarget(bot, bot->target);
        return;
    }
    for (i = 0; i < count; i++)
    {
        if (bot->insight[order[i]])
        {
            gevrBotSetTarget(bot, order[i]);
            return;
        }
        if (nearest < 0) nearest = order[i];
    }
    if (bot->target < 0 || diff <= NET_BOT_EASY)
    {
        gevrBotSetTarget(bot, nearest);
        return;
    }
    gevrBotSetTarget(bot, bot->target);
}

static const GevrBotWeapon *gevrBotWeaponPref(s32 item)
{
    static const GevrBotWeapon none = { 0, BOT_DISTCFG_DEFAULT, 0 };

    return item >= 0 && item <= ITEM_REMOTEMINE ? &s_weapons[item] : &none;
}

/* The ammo the bot has for a gun: what is loaded and what is carried (none for fists and knives) */
static s32 gevrBotAmmo(struct player *pl, s32 item)
{
    s32 ammo;

    if (gevrBotWeaponPref(item)->distconfig == BOT_DISTCFG_CLOSE && item != ITEM_THROWKNIFE)
    {
        return 1;
    }
    ammo = get_ammo_count_for_weapon(item);
    if (pl->hands[GUNRIGHT].weaponnum == item)
    {
        ammo += pl->hands[GUNRIGHT].weapon_ammo_in_magazine;
    }
    return ammo;
}

/*
 * PD's botinvTick, simplified to GoldenEye's one inventory: the best scored
 * gun it has with ammo, swapped in the game's own way (the draw animation
 * plays), not again for a second.
 */
static void gevrBotChooseWeapon(struct player *pl, GevrBot *bot)
{
    s32 best = ITEM_UNARMED;
    s32 item;

    if (bondinvIsAliveWithFlag())
    {
        /* Flag Tag: the flag is the hand's (lv.c puts it back) */
        bot->weapon = ITEM_TOKEN;
        return;
    }

    for (item = ITEM_FIST; item <= ITEM_REMOTEMINE; item++)
    {
        if (s_weapons[item].score == 0 || s_weapons[item].score <= s_weapons[best].score)
        {
            continue;
        }
        if (bondinvHasInvItem(item) && gevrBotAmmo(pl, item) > 0)
        {
            best = item;
        }
    }
    bot->weapon = best;
    if (bot->changeguntimer60 > 0)
    {
        bot->changeguntimer60 -= g_ClockTimer;
        return;
    }
    if (pl->hands[GUNRIGHT].weaponnum != best && best != ITEM_UNARMED)
    {
        currentPlayerEquipWeaponWrapper(GUNRIGHT, best);
        bot->changeguntimer60 = 60;
    }
}

/*
 * PD's botFindPickup: the nearest gun lying on the floor that scores above
 * what the bot has, or, when it is short of ammo (PD's criticalammopri) or
 * hurt, an ammo crate or body armour; ANY when it has nothing else to do.
 */
static PropRecord *gevrBotFindPickup(struct player *pl, GevrBot *bot, s32 any)
{
    const GevrBotWeapon *have = gevrBotWeaponPref(bot->weapon);
    s32 short_of_ammo = have->critical > 0 && gevrBotAmmo(pl, bot->weapon) <= have->critical;
    s32 hurt = pl->bondhealth < 0.5f && pl->bondarmour <= 0.0f;
    PropRecord *best = NULL;
    f32 bestdist = 3000.0f * 3000.0f;
    PropRecord *prop;

    for (prop = chrpropGetActiveTail(); prop != NULL; prop = prop->prev)
    {
        s32 wanted = FALSE;
        f32 dx;
        f32 dz;
        f32 d;

        if (prop->timetoregen > 0 || prop->parent != NULL || prop->stan == NULL || !(prop->flags & PROPFLAG_ENABLED))
        {
            continue;
        }
        if (prop->type == PROP_TYPE_WEAPON && prop->weapon != NULL)
        {
            s32 item = prop->weapon->weaponnum;

            wanted = gevrBotWeaponPref(item)->score > have->score || any ||
                     (short_of_ammo && item == bot->weapon);
        }
        else if (prop->type == PROP_TYPE_OBJ && prop->obj != NULL)
        {
            wanted = (prop->obj->type == PROPDEF_AMMO && (short_of_ammo || any)) ||
                     (prop->obj->type == PROPDEF_ARMOUR && (hurt || any));
        }
        if (!wanted)
        {
            continue;
        }
        dx = prop->pos.x - pl->prop->pos.x;
        dz = prop->pos.z - pl->prop->pos.z;
        d = dx * dx + dz * dz;
        if (d < bestdist)
        {
            bestdist = d;
            best = prop;
        }
    }
    return best;
}

/*
 * The scenario's prize (PD's hold-the-briefcase branch, bot.c
 * botTickUnpaused, as the model): Flag Tag's flag, the Golden Gun. NULL
 * when it is not lying on the floor; *holder is who has it, or -1.
 */
static PropRecord *gevrBotPrize(s32 item, s32 *holder)
{
    PropRecord *prop;
    s32 i;

    *holder = -1;
    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        struct player *op = g_playerPointers[i];

        if (netSlotOccupied(i) && op != NULL && !op->bonddead && op->hands[GUNRIGHT].weaponnum == item)
        {
            *holder = i;
            return NULL;
        }
    }
    for (prop = chrpropGetActiveTail(); prop != NULL; prop = prop->prev)
    {
        if (prop->type == PROP_TYPE_WEAPON && prop->weapon != NULL && prop->weapon->weaponnum == item &&
            prop->parent == NULL && prop->stan != NULL && prop->timetoregen <= 0 && (prop->flags & PROPFLAG_ENABLED))
        {
            return prop;
        }
    }
    return NULL;
}

/* Flag Tag with the flag: run to the start pad farthest from the nearest foe */
static void gevrBotFlee(s32 slot, struct player *pl, GevrBot *bot)
{
    s32 nearest = -1;
    f32 neardist = 0;
    f32 bestdist = -1;
    PadRecord *best = NULL;
    s32 i;

    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        if (gevrBotFoe(slot, i) && (nearest < 0 || bot->distance[i] < neardist))
        {
            nearest = i;
            neardist = bot->distance[i];
        }
    }
    for (i = 0; i < startpadcount; i++)
    {
        PadRecord *pad = g_Startpad[i];
        f32 dx;
        f32 dz;
        f32 d;

        if (pad == NULL || pad->stan == NULL)
        {
            continue;
        }
        if (nearest < 0)
        {
            d = (f32)(gevrBotRandom() % 1000);
        }
        else
        {
            dx = pad->pos.x - g_playerPointers[nearest]->prop->pos.x;
            dz = pad->pos.z - g_playerPointers[nearest]->prop->pos.z;
            d = dx * dx + dz * dz;
        }
        if (d > bestdist)
        {
            bestdist = d;
            best = pad;
        }
    }
    if (best != NULL)
    {
        gevrBotNavPlan(&bot->route, pl->prop->stan, &pl->prop->pos, best->stan, &best->pos);
        bot->routeticks = 0;
    }
}

/* PD's botcmdTickDistMode (botcmd.c), the distances by the gun in hand */
static s32 gevrBotDistMode(GevrBot *bot, s32 diff)
{
    const f32 *limits = s_distconfigs[gevrBotWeaponPref(bot->weapon)->distconfig];
    f32 mindist = limits[0];
    f32 maxdist = limits[1];
    f32 dist = bot->distance[bot->target];

    if (diff == NET_BOT_MEAT) mindist *= 0.35f;
    else if (diff == NET_BOT_EASY) mindist *= 0.5f;
    if (bot->distmode == BOT_DIST_BACKUP) mindist += 25.0f;
    else if (bot->distmode == BOT_DIST_ADVANCE || bot->distmode == BOT_DIST_GOTO) maxdist -= 25.0f;
    if (dist < mindist) return bot->targetinsight ? BOT_DIST_BACKUP : BOT_DIST_ADVANCE;
    if (dist < maxdist) return bot->targetinsight ? BOT_DIST_OK : BOT_DIST_ADVANCE;
    if (dist < 4500.0f) return BOT_DIST_ADVANCE;
    return BOT_DIST_GOTO;
}

/* Walk the route toward a goal, turning to face along it unless told where to look */
static void gevrBotWalkRoute(s32 slot, struct player *pl, GevrBot *bot, OSContPad *pad, s32 face)
{
    coord3d aim;

    if (!gevrBotNavNext(&bot->route, &pl->prop->pos, pl->prop->stan, &aim))
    {
        bot->route.count = 0;
        return;
    }
    if (face)
    {
        f32 turn = gevrBotTurnTo(bot, gevrBotHeading(&pl->prop->pos, &aim), BOT_TURN_PER_TICK * 2.0f);

        bot->verta *= 0.9f;
        if (turn > 60.0f || turn < -60.0f)
        {
            return;
        }
    }
    gevrBotMoveToward(pl, bot, &aim, pad);
    (void)slot;
}

/* The bot's controller and view for this tick */
static void gevrBotThink(s32 slot, struct player *pl, GevrBot *bot, OSContPad *pad)
{
    s32 diff = gevrNetBotDifficulty();
    f32 oldtheta = bot->theta;

    s32 scenario = get_scenario();
    s32 prizeitem = scenario == SCENARIO_TLD ? ITEM_TOKEN : scenario == SCENARIO_MWTGG ? ITEM_GOLDENGUN : ITEM_UNARMED;
    PropRecord *prize = NULL;
    s32 holder = -1;

    bot->frame60 += g_ClockTimer;
    bot->routeticks += g_ClockTimer;
    gevrBotChooseTarget(slot, pl, bot, diff);
    gevrBotChooseWeapon(pl, bot);

    if (prizeitem != ITEM_UNARMED)
    {
        prize = gevrBotPrize(prizeitem, &holder);
        if (holder == slot && prizeitem == ITEM_TOKEN)
        {
            /* the flag scores by the second: keep it away from everyone, no gun to fire */
            bot->fleettl60 -= g_ClockTimer;
            if (bot->route.count == 0 || bot->fleettl60 <= 0)
            {
                gevrBotFlee(slot, pl, bot);
                bot->fleettl60 = 60 * 4;
            }
            bot->gotoprop = NULL;
            gevrBotWalkRoute(slot, pl, bot, pad, TRUE);
            gevrBotUnstick(slot, pl, bot, pad);
            return;
        }
        if (holder >= 0 && gevrBotFoe(slot, holder))
        {
            /* a foe has the prize: it is the target, seen or not */
            gevrBotSetTarget(bot, holder);
        }
        if (prize != NULL && bot->gotoprop != prize &&
            gevrBotNavPlan(&bot->route, pl->prop->stan, &pl->prop->pos, prize->stan, &prize->pos))
        {
            /* the prize on the floor beats any other pickup */
            bot->gotoprop = prize;
            bot->gotottl60 = 60 * 20;
            bot->routeticks = 0;
            bot->distmode = BOT_DIST_NONE;
        }
    }

    /*
     * PD's main loop order (bot.c botTickUnpaused): a gun better than the
     * one in hand, or ammo it is short of, comes first unless a foe is in
     * sight and armed against; with nothing to fight, any pickup.
     */
    if (bot->gotoprop != NULL &&
        (bot->gotoprop->timetoregen > 0 || bot->gotoprop->parent != NULL || bot->gotottl60 <= 0 ||
         !(bot->gotoprop->flags & PROPFLAG_ENABLED)))
    {
        bot->gotoprop = NULL;
        bot->route.count = 0;
    }
    if (bot->gotoprop == NULL && (bot->target < 0 || !bot->targetinsight || bot->weapon <= ITEM_FIST))
    {
        PropRecord *pickup = gevrBotFindPickup(pl, bot, bot->target < 0);

        if (pickup != NULL && gevrBotNavPlan(&bot->route, pl->prop->stan, &pl->prop->pos, pickup->stan, &pickup->pos))
        {
            bot->gotoprop = pickup;
            bot->gotottl60 = 60 * 15;
            bot->routeticks = 0;
            bot->distmode = BOT_DIST_NONE;
        }
    }
    if (bot->gotoprop != NULL)
    {
        bot->gotottl60 -= g_ClockTimer;
        if (bot->gotoprop != prize && bot->target >= 0 && bot->targetinsight && bot->weapon > ITEM_FIST)
        {
            bot->gotoprop = NULL;
            bot->route.count = 0;
        }
    }

    if (bot->gotoprop != NULL)
    {
        if (bot->target >= 0 && bot->targetinsight)
        {
            /* fetching under fire: shoot back while it walks */
            struct player *op = g_playerPointers[bot->target];
            f32 heading = gevrBotHeading(&pl->prop->pos, &op->prop->pos) + bot->extraangle * (180.0f / M_PI_F);
            f32 off = gevrBotTurnTo(bot, heading, BOT_TURN_PER_TICK);

            if (bot->shootdelaytimer60 >= s_difficulties[diff].shootdelay && off < 45.0f && off > -45.0f)
            {
                pad->button |= Z_TRIG;
            }
            gevrBotWalkRoute(slot, pl, bot, pad, FALSE);
        }
        else
        {
            gevrBotWalkRoute(slot, pl, bot, pad, TRUE);
        }
        if (bot->route.count == 0)
        {
            bot->gotoprop = NULL;
        }
    }
    else if (bot->target >= 0)
    {
        struct player *op = g_playerPointers[bot->target];
        s32 mode = gevrBotDistMode(bot, diff);

        if (bot->targetinsight)
        {
            /* face the target, off by the aim error (bot.c botTick); pitch straight at its chest */
            f32 dx = op->prop->pos.x - pl->prop->pos.x;
            f32 dz = op->prop->pos.z - pl->prop->pos.z;
            f32 dy = (op->prop->pos.y - 30.0f) - pl->prop->pos.y;
            f32 flat = sqrtf(dx * dx + dz * dz);
            f32 heading = gevrBotHeading(&pl->prop->pos, &op->prop->pos) + bot->extraangle * (180.0f / M_PI_F);
            f32 off = gevrBotTurnTo(bot, heading, BOT_TURN_PER_TICK);

            bot->verta = atan2f(dy, flat > 1.0f ? flat : 1.0f) * (180.0f / M_PI_F);
            /* PD's fire rule (bot.c botTickUnpaused): in sight, reacted, within 45 degrees */
            if (bot->shootdelaytimer60 >= s_difficulties[diff].shootdelay && off < 45.0f && off > -45.0f)
            {
                pad->button |= Z_TRIG;
            }
        }
        if (mode != bot->distmode || bot->distmodettl60 <= 0)
        {
            bot->distmode = mode;
            bot->distmodettl60 = 60;
            if (mode == BOT_DIST_ADVANCE || mode == BOT_DIST_GOTO)
            {
                if (gevrBotNavPlan(&bot->route, pl->prop->stan, &pl->prop->pos, op->prop->stan, &op->prop->pos))
                {
                    bot->routeticks = 0;
                }
            }
            else
            {
                bot->route.count = 0;
            }
        }
        bot->distmodettl60 -= g_ClockTimer;
        if (mode == BOT_DIST_BACKUP)
        {
            coord3d away = pl->prop->pos;

            away.x += pl->prop->pos.x - op->prop->pos.x;
            away.z += pl->prop->pos.z - op->prop->pos.z;
            gevrBotMoveToward(pl, bot, &away, pad);
        }
        else if (mode != BOT_DIST_OK)
        {
            gevrBotWalkRoute(slot, pl, bot, pad, !bot->targetinsight);
        }
    }
    else
    {
        bot->distmode = BOT_DIST_NONE;
        if (bot->route.count == 0 || bot->routeticks > 60 * 20)
        {
            gevrBotPickRoam(pl, bot);
        }
        gevrBotWalkRoute(slot, pl, bot, pad, TRUE);
    }
    gevrBotUnstick(slot, pl, bot, pad);
    /* PD's speedtheta: the turn, radians a tick, scaled as bot.c scales it */
    bot->speedtheta = gevrBotWrap(bot->theta - oldtheta) * (M_PI_F / 180.0f) / (g_ClockTimer > 0 ? g_ClockTimer : 1) * 16.236389160156f;
}

/*
 * Before the bot's move tick (boss.c), as the current player: its brain fills
 * the pad the tick reads, its view is set, and the tick runs the flat game's
 * paths, the headset's being the local player's alone. Returns whether the
 * slot is a bot this tick (gevrBotTickEnd's argument).
 */
s32 gevrBotTickBegin(s32 slot)
{
    struct player *pl;
    GevrBot *bot;
    OSContPad pad;

    if (!gevrBotRuns(slot) || g_playerPointers[slot]->prop == NULL)
    {
        if (slot >= 0 && slot < MAX_PLAYER_COUNT)
        {
            s_bots[slot].started = FALSE;
        }
        return FALSE;
    }
    pl = g_playerPointers[slot];
    bot = &s_bots[slot];
    if (!bot->started)
    {
        memset(bot, 0, sizeof(*bot));
        bot->started = TRUE;
        bot->target = -1;
        bot->distmode = BOT_DIST_NONE;
        bot->theta = pl->vv_theta;
        bot->verta = 0.0f;
    }
    memset(&pad, 0, sizeof(pad));
    if (pl->bonddead)
    {
        /* PD's simulant fades out and is back after 90 ticks (chraction.c chrTickDead) */
        bot->deadticks += g_ClockTimer;
        if (bot->deadticks >= 90)
        {
            gevrBotRespawn();
        }
        if (!pl->bonddead)
        {
            bot->theta = pl->vv_theta;
            bot->verta = 0.0f;
            bot->route.count = 0;
            bot->target = -1;
            bot->distmode = BOT_DIST_NONE;
            bot->targetinsighttemperature = 0;
        }
    }
    else
    {
        bot->deadticks = 0;
        gevrBotThink(slot, pl, bot, &pad);
    }
    bot->pressed = pad.button & ~bot->pad.button;
    bot->pad = pad;
    pl->cur_player_control_type_0 = CONTROLLER_CONFIG_HONEY;
    if (!pl->bonddead)
    {
        gevrBotSetView(pl, bot->theta, bot->verta);
    }
    bot->stereo = g_gevrStereo;
    g_gevrStereo = FALSE;
    return TRUE;
}

/* After the bot's move tick: the headset's mode back, and the bot's own view
 * over the auto-aim and look-ahead the tick applied */
void gevrBotTickEnd(s32 slot, s32 began)
{
    struct player *pl;

    if (!began)
    {
        return;
    }
    g_gevrStereo = s_bots[slot].stereo;
    pl = g_playerPointers[slot];
    if (pl != NULL && !pl->bonddead)
    {
        gevrBotSetView(pl, s_bots[slot].theta, s_bots[slot].verta);
    }
}
