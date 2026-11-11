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
 *
 * Racing a ghost (gevr_ghost.c) brings the clock with it, as in Dab's Mod
 * ("a time trial without a time on screen is not one"), and the split
 * beside it: green ahead of the ghost, red behind (hudmsgRenderMissionTimer's
 * colours).
 */
#include <ultra64.h>
#include <stdio.h>
#include <string.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "bondview.h"
#include "gun.h"
#include "player.h"
#include "textrelated.h"
#include "hudaspect.h"
#include "gevr_livesplit.h"

extern bool netIsActive(void);
extern int netGetLocalSlot(void);
extern int netHordeActive(void);
extern s32 g_gevrStereo;
extern int VrSpeedrunTimer;   /* vr_settings_defaults.c */
extern s32 gevrGhostRacing(void);      /* gevr_ghost.c */
extern s32 gevrGhostHasSplit(void);
extern s32 gevrGhostSplit60(void);

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
/* this headset's own mission, and its clock running */
static s32 gevrSpeedrunClockRunning(void)
{
    if (g_CurrentPlayer == NULL)
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

s32 gevrSpeedrunTimerShown(void)
{
    return (VrSpeedrunTimer || gevrGhostRacing()) && gevrSpeedrunClockRunning();
}

/*
 * The debrief's stamp (front.c constructor_menu0D_missioncomplete): what a
 * moderator needs to see on a run's video and could not tell from it, as the
 * speedrun research listed it after Celeste's assist stamps, the Midas board's
 * frame-rate rule and the XBLA board's "any other difference": the build, the
 * platform, VR or flat, the art, the turning, auto-aim, the display, what was
 * added to the game, and the frame clock's steps (the port's equivalent of
 * Midas's FPS counter: whether the run had the long steps lag tricks need).
 *
 * Taken while the mission's clock runs, not at the debrief: a headset shows
 * the debrief flat on its screen. An addition counts if it was on at any time
 * in the run (the in-game panel can change it).
 */
extern int VrBeanModels, VrGexGuns, VrForcePull, VrAlly, VrThirdPerson, VrMovementSpeed;
extern int VrFastReinforcements, VrMinesStickToGuards, VrSmoothTurnSpeed, VrGhostRace, VrLiveSplit;
extern float VrUseSnapTurn;
extern float g_gevrDisplayHz;        /* vr_settings_defaults.c, from vr_openxr.cpp */
extern int g_gevrSpaceWarpOn;
extern s32 gevrBeanIsAvailable(void);
extern const char *netGetLocalAppVersion(void);
extern const char gevrBuildId[];
extern u8 g_CheatActivated[];

#define STAMP_MAXFIELDS 6            /* frametiming.c's cap on a step */

static struct {
    s32 valid;
    s32 stereo, third, bean, gex, snap, turn, autoaim, hz, spacewarp;
    s32 forcepull, ally, speed, fastreinf, mines, dk, paintball, linemode;
    s32 stepMax, stepCapped;
} s_run;

/* a mission's stage (not the menus', where the debrief reads it) starts the count */
void gevrSpeedrunStageReset(s32 stage)
{
    if (stage >= 0 && stage < LEVELID_MAX)
    {
        memset(&s_run, 0, sizeof(s_run));
    }
}

/* frametiming.c: each frame's step in fields, before the cap, while the clock runs */
void gevrSpeedrunStep(u32 fields)
{
    s32 step = fields > STAMP_MAXFIELDS ? STAMP_MAXFIELDS : (s32)fields;

    if (!is_timer_active || g_CameraMode != CAMERAMODE_FP)
    {
        return;
    }
    if (fields > STAMP_MAXFIELDS)
    {
        s_run.stepCapped++;
    }
    if (step > s_run.stepMax)
    {
        s_run.stepMax = step;
    }
}

static void gevrSpeedrunCapture(void)
{
    static const s32 speeds[] = { 100, 50, 75, 125, 150, 175, 200 };   /* net_rules.h NET_MOVE_* */

    s_run.valid = TRUE;
    s_run.stereo = g_gevrStereo != 0;
    s_run.third |= VrThirdPerson && !g_gevrStereo;
    s_run.bean |= VrBeanModels && gevrBeanIsAvailable();
    s_run.gex |= VrGexGuns != 0;
    s_run.snap = (s32)VrUseSnapTurn;
    s_run.turn = VrSmoothTurnSpeed;
    s_run.autoaim |= currentPlayerGetXAutoAimEnabled() != 0;
    if (g_gevrDisplayHz > 0.0f)
    {
        s_run.hz = (s32)(g_gevrDisplayHz + 0.5f);
    }
    s_run.spacewarp |= g_gevrSpaceWarpOn != 0;
    s_run.forcepull |= VrForcePull != 0;
    s_run.ally |= VrAlly != 0;
    if (VrMovementSpeed > 0 && VrMovementSpeed < (s32)(sizeof(speeds) / sizeof(speeds[0])))
    {
        s_run.speed = speeds[VrMovementSpeed];
    }
    s_run.fastreinf |= VrFastReinforcements != 0;
    s_run.mines |= VrMinesStickToGuards != 0;
    s_run.dk |= g_CheatActivated[CHEAT_DK_MODE] != 0;
    s_run.paintball |= g_CheatActivated[CHEAT_PAINTBALL] != 0;
    s_run.linemode |= g_CheatActivated[CHEAT_LINEMODE] != 0;
}

static const char *gevrSpeedrunPlatform(void)
{
#if defined(GEVR_IOS)
    return "iOS";
#elif defined(GEVR_MOBILE)
    return "Android";
#elif defined(GEVR_FRAME)
    return "Steam Frame";
#elif defined(ANDROID)
    return "Quest";
#elif defined(__linux__)
    return "Linux";
#elif defined(_WIN32)
    return "Windows";
#else
    return "PC";
#endif
}

/* the HUD font has capitals, figures and a little punctuation: the rest goes */
static void gevrStampFont(char *s)
{
    char *out = s;

    for (; *s; s++)
    {
        char c = (*s >= 'a' && *s <= 'z') ? (char)(*s - 'a' + 'A') : *s;

        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == ',' || c == ':' || c == '-'
            || c == '+' || c == '\n')
        {
            *out++ = c;
        }
    }
    *out = '\0';
}

static s32 gevrStampWidth(const char *text)
{
    s32 w = 0;
    s32 h = 0;

    textMeasure(&h, &w, (char *)text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    return w;
}

static Gfx *gevrStampText(Gfx *gdl, const char *text, s32 x, s32 y)
{
    char line[112];

    snprintf(line, sizeof(line), "%s\n", text);
    gevrStampFont(line);
    return textRender(gdl, &x, &y, line, ptrFontBankGothicChars, ptrFontBankGothic, 0x404040FF, viGetX(), viGetY(), 0, 0);
}

/*
 * The debrief draws the stamp (front.c) in the room its page leaves, in the
 * HUD font, grey: the build and the platform in the left column under the
 * kill total, beside the limb and other hits; what the run was played with
 * across the foot of the page, most telling first (the rules, the frame
 * clock's steps, auto-aim, the turning, the display, the art), and what does
 * not fit is counted ("+2 more") rather than dropped unseen. rowy is the
 * page's first row of hits and rowh its rows' spacing; coop is the party's
 * kills line, which takes the foot.
 */
Gfx *gevrSpeedrunStampDraw(Gfx *gdl, s32 rowy, s32 rowh, s32 coop)
{
    char items[12][32];
    char text[112];
    s32 count = 0;
    s32 shown;
    s32 len;
    s32 foot;
    s32 h = 0;
    s32 w = 0;
    s32 i;

    if (!(VrSpeedrunTimer || VrGhostRace || VrLiveSplit) || !s_run.valid)
    {
        return gdl;
    }
    textMeasure(&h, &w, "A\n", ptrFontBankGothicChars, ptrFontBankGothic, 0);

    /* the left column: the build ("v0.5.15 (5a3b786)", else its hash) and the platform */
    {
        const char *version = netGetLocalAppVersion();
        char build[16];

        snprintf(build, sizeof(build), "%s", gevrBuildId);
        build[strcspn(build, " ")] = '\0';
        gdl = gevrStampText(gdl, version[0] ? version : build, 0x37, rowy + 2 * rowh + 2);
        snprintf(text, sizeof(text), "%s, %s%s", gevrSpeedrunPlatform(), s_run.stereo ? "VR" : "flat",
                 s_run.third ? ", 3rd" : "");
        gdl = gevrStampText(gdl, text, 0x37, rowy + 3 * rowh + 2);
    }

    /* the foot: what the run was played with */
    foot = rowy + (coop ? 5 : 4) * rowh + 2;
    if (foot + h > viGetY())
    {
        return gdl;
    }
#define STAMP_ITEM(...) (count < 12 ? (snprintf(items[count], sizeof(items[count]), __VA_ARGS__), count++) : 0)
    if (s_run.forcepull) STAMP_ITEM("force pull");
    if (s_run.ally) STAMP_ITEM("ally");
    if (s_run.speed) STAMP_ITEM("speed %d", s_run.speed);
    if (s_run.fastreinf) STAMP_ITEM("fast reinf");
    if (s_run.mines) STAMP_ITEM("sticky mines");
    if (s_run.dk) STAMP_ITEM("DK mode");
    if (s_run.paintball) STAMP_ITEM("paintball");
    if (s_run.linemode) STAMP_ITEM("line mode");
    if (count == 0) STAMP_ITEM("original rules");
    STAMP_ITEM("step %d cap %d", s_run.stepMax, s_run.stepCapped);
    STAMP_ITEM("%s", s_run.autoaim ? "auto-aim" : "no auto-aim");
    if (s_run.stereo)
    {
        if (s_run.snap > 0) STAMP_ITEM("snap %d", s_run.snap);
        else STAMP_ITEM("smooth %d", s_run.turn);
    }
    if (s_run.hz > 0) STAMP_ITEM("%d hz%s", s_run.hz, s_run.spacewarp ? " sw" : "");
    STAMP_ITEM("%s%s", s_run.bean ? "bean" : "n64", s_run.gex ? " gex" : "");
#undef STAMP_ITEM

    /* as many as fit the page's width, then how many did not */
    text[0] = '\0';
    len = 0;
    for (shown = 0; shown < count; shown++)
    {
        char tryline[112];
        char more[16];

        snprintf(more, sizeof(more), ", +%d more", count - shown - 1);
        snprintf(tryline, sizeof(tryline), "%s%s%s%s", text, len ? ", " : "", items[shown], shown + 1 < count ? more : "");
        gevrStampFont(tryline);
        if (0x37 + gevrStampWidth(tryline) > viGetX() - 0x30)
        {
            break;
        }
        len += snprintf(text + len, sizeof(text) - len, "%s%s", len ? ", " : "", items[shown]);
    }
    if (shown < count)
    {
        snprintf(text + len, sizeof(text) - len, ", +%d more", count - shown);
    }
    return gevrStampText(gdl, text, 0x37, foot);
}

/* gunDrawHudString's outlined digits in a colour of their own, left aligned on y */
static Gfx *gevrHudStringColour(Gfx *gdl, char *text, s32 x, s32 y, u32 colour)
{
    s32 w = 0;
    s32 h = 0;
    s32 x1;
    s32 y1;

    textMeasure(&h, &w, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    x1 = x;
    y1 = y - h;
    return textRenderOutlined(gdl, &x1, &y1, text, ptrFontBankGothicChars, ptrFontBankGothic, colour, 0x646464FF,
                              (s32)viGetX(), viGetY(), 0, 0);
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

    /* LiveSplit gets the clock whether or not it is drawn (gevr_livesplit.c),
     * and the debrief's stamp what the run is played on */
    gevrLiveSplitFrame(gevrSpeedrunClockRunning(), getMissiontimer());
    if (gevrSpeedrunClockRunning())
    {
        gevrSpeedrunCapture();
    }
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

    /* the split against the ghost, after the hundredths */
    if (gevrGhostHasSplit())
    {
        s32 split = gevrGhostSplit60();
        s32 a = split < 0 ? -split : split;
        char text[24];

        if (a >= 3600)
        {
            snprintf(text, sizeof(text), "%c%d:%02d.%02d\n", split < 0 ? '-' : '+', a / 3600, (a / 60) % 60, ((a % 60) * 100) / 60);
        }
        else
        {
            snprintf(text, sizeof(text), "%c%d.%02d\n", split < 0 ? '-' : '+', a / 60, ((a % 60) * 100) / 60);
        }
        gdl = gevrHudStringColour(gdl, text, x + s_columns[7] + 12, y, split < 0 ? 0x40FF40FF : 0xFF6040FF);
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
