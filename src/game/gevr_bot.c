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
} GevrBot;

static GevrBot s_bots[MAX_PLAYER_COUNT];

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

/* The bot's controller and view for this tick */
static void gevrBotThink(s32 slot, struct player *pl, GevrBot *bot, OSContPad *pad)
{
    coord3d aim;

    bot->routeticks += g_ClockTimer;
    if (bot->route.count == 0 || bot->routeticks > 60 * 20)
    {
        gevrBotPickRoam(pl, bot);
    }
    if (gevrBotNavNext(&bot->route, &pl->prop->pos, pl->prop->stan, &aim))
    {
        f32 turn = gevrBotTurnTo(bot, gevrBotHeading(&pl->prop->pos, &aim), BOT_TURN_PER_TICK * 2.0f);

        bot->verta *= 0.9f;
        if (turn < 60.0f && turn > -60.0f)
        {
            gevrBotMoveToward(pl, bot, &aim, pad);
        }
    }
    else
    {
        bot->route.count = 0;
    }
    gevrBotUnstick(slot, pl, bot, pad);
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
