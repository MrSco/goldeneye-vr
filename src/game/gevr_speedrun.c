/*
 * Speedrun timer (launcher Play > Display "Speedrun timer", VrSpeedrunTimer).
 *
 * Ported from Dab's Mod's GoldenEye mission timer (port/src/gehud.c
 * geHudRenderMissionTimer, hudmsg.c hudmsgIsMissionTimerVisible). Dab had to
 * rebuild GoldenEye's clock for Perfect Dark's engine (gecinema.c: it counts
 * from when Bond has control and stops in the level's cutscenes); this port
 * runs GoldenEye's own, so the time shown is the mission_timer the debrief
 * prints (getMissiontimer), unchanged: 1/60 s fields while Bond is in
 * first-person control, none while paused, while controls are locked or in a
 * scripted cutscene. Only its readout is new, to the hundredth.
 *
 * Dab set it in GoldenEye's countdown figures (Bank Gothic outlined in grey,
 * a digit to eight units and nine either side of a colon), which here are the
 * countdown's own (propobj.c countdownTimerRender), in the lower left corner
 * under the bottom messages' line, where GoldenEye draws nothing, so pickup
 * text and objectives stay where GoldenEye puts them (Dab's F3
 * 20261001-121337: a timer on the raised line pushed them up).
 *
 * Stereo: on the head-locked HUD panel with the messages and the countdown
 * (VR_HUD_CAPTURE_*_H, as countdownTimerRender since issue #42).
 */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "bondview.h"
#include "gun.h"
#include "player.h"
#include "textrelated.h"
#include "hudaspect.h"

extern bool netIsActive(void);
extern int netGetLocalSlot(void);
extern int netHordeActive(void);
extern s32 g_gevrStereo;
extern int VrSpeedrunTimer;   /* vr_settings_defaults.c */

/* the countdown's columns from its first digit's centre: m m : s s : h h */
static const s8 s_columns[8] = { 0, 8, 17, 26, 34, 43, 52, 60 };

/* left of the first digit's centre, as the bottom messages' 0x1E margin */
#define TIMER_LEFT   (0x1E + 4)
/* the digits' foot, above the view's bottom edge */
#define TIMER_BOTTOM 2

/*
 * Whether the clock is up this frame: asked for; this headset's own mission
 * (solo, or co-op, where the time is the local player's, bondview2.c); and
 * the mission's clock is running, so not in an intro, a cutscene or with the
 * controls locked (hud_hide_and_lock_controls_and_pause_mission_time).
 */
s32 gevrSpeedrunTimerShown(void)
{
    if (!VrSpeedrunTimer || g_CurrentPlayer == NULL)
    {
        return FALSE;
    }
    if (netIsActive())
    {
        if (!gevrCoopActive() || netHordeActive() || get_cur_playernum() != netGetLocalSlot())
        {
            return FALSE;
        }
    }
    else if (getPlayerCount() != 1)
    {
        return FALSE;
    }
    return g_CameraMode == CAMERAMODE_FP && is_timer_active && !g_CurrentPlayer->bonddead;
}

Gfx *gevrSpeedrunTimerRender(Gfx *gdl)
{
    s32 time;
    s32 mins;
    s32 secs;
    s32 hundredths;
    s32 digits[8];
    s32 x;
    s32 y;
    s32 i;

    if (!gevrSpeedrunTimerShown())
    {
        return gdl;
    }

    time = getMissiontimer();
    mins = time / 3600;
    secs = (time / 60) % 60;
    hundredths = ((time % 60) * 100) / 60;

    digits[0] = (mins / 10) % 10;
    digits[1] = mins % 10;
    digits[3] = secs / 10;
    digits[4] = secs % 10;
    digits[6] = hundredths / 10;
    digits[7] = hundredths % 10;

    x = viGetViewLeft() + TIMER_LEFT;
    y = viGetViewTop() + viGetViewHeight() - TIMER_BOTTOM;
    if (g_gevrStereo)
    {
        /* the panel shows the view's middle; full size, its left fifth is cut
         * off (the minutes were, at the 0x1E margin), so in as far as the stats
         * readout's left edge */
        x = viGetViewLeft() + (viGetViewWidth() * 22) / 100 + 4;
    }

    PORT_HUD_ASPECT(gdl, GE_HUD_ASPECT_LEFT);
    gdl = microcode_constructor(gdl);
    if (g_gevrStereo)
    {
        gDPNoOpTag(gdl++, 0x56570000); /* VR_HUD_CAPTURE_BEGIN_H */
        /* full size, as the countdown (#42): in the panel's own corner, not
         * pulled in toward the middle of the view with the messages */
        gDPNoOpTag(gdl++, 0x56590000); /* VR_HUD_FULL_SIZE_BEGIN */
    }

    /* an hour and more of minutes: the hundreds before the first column */
    if (mins >= 100)
    {
        gdl = gunDrawHudInteger(gdl, (mins / 100) % 10, x - 8, HUDHALIGN_MIDDLE, y, HUDVALIGN_BOTTOM, 1);
    }
    for (i = 0; i < 8; i++)
    {
        if (i == 2 || i == 5)
        {
            gdl = gunDrawHudString(gdl, (s8 *)":\n", x + s_columns[i], HUDHALIGN_MIDDLE, y, HUDVALIGN_BOTTOM, 1);
        }
        else
        {
            gdl = gunDrawHudInteger(gdl, digits[i], x + s_columns[i], HUDHALIGN_MIDDLE, y, HUDVALIGN_BOTTOM, 1);
        }
    }

    gdl = combiner_bayer_lod_perspective(gdl);
    if (g_gevrStereo)
    {
        gDPNoOpTag(gdl++, 0x56590001); /* VR_HUD_FULL_SIZE_END */
        gDPNoOpTag(gdl++, 0x56570001); /* VR_HUD_CAPTURE_END_H */
    }
    PORT_HUD_ASPECT(gdl, GE_HUD_ASPECT_NONE);
    return gdl;
}
