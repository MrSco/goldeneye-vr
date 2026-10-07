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
#include "net_game.h"

extern bool netIsActive(void);
extern bool netIsHost(void);
extern s32 g_gevrStereo;
extern s32 g_ClockTimer;
extern void gevrBotRespawn(void);       /* bondview2.c */

typedef struct GevrBot {
    OSContPad pad;          /* this frame's controller */
    u16 pressed;            /* the buttons new this frame */
    s32 started;            /* the view below holds the bot's */
    f32 theta, verta;       /* the view the bot wants: degrees, the game's vv_theta/vv_verta */
    s32 stereo;             /* g_gevrStereo, put back after the bot's tick */
    s32 deadticks;          /* 60 Hz ticks since it died */
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

/* The bot's controller and view for this tick */
static void gevrBotThink(s32 slot, struct player *pl, GevrBot *bot, OSContPad *pad)
{
    (void)slot;
    (void)pl;
    (void)bot;
    (void)pad;
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
