#include <ultra64.h>
#include <stdio.h>
#include <music.h>
#include <bondgame.h>
#include <bondinv.h>
#include <bondconstants.h>
#include <boss.h>
#include <joy.h>
#include <random.h>
#include "textrelated.h"
#include "frametiming.h"
#include "player.h"
#include "mpmenu.h"
#include "gun.h"
#include "front.h"
#include "lv.h"
#include "language.h"
#include "mp_music.h"
#include "options.h"
#include "file.h"
#include "assets/obseg/text/LmpmenuE.h"
#ifdef REFRESH_PAL
#define MPMENU_YOFF 8   /* PAL: every text row sits 8 pixels lower */
#else
#define MPMENU_YOFF 0
#endif
#ifdef GEVR
extern s32 g_gevrStereo;
extern bool netIsActive(void);
extern bool netIsHost(void);
extern int netGetLocalSlot(void);
extern void netHostRoundEnded(void);
extern bool netSlotOccupied(int slot);
extern const char *netGetSlotName(int slot);
extern int netVoiceSlotSpeaking(unsigned char slot);
extern void gevrLobbySessionStopped(void);
extern void gevrRestartToLauncher(void);
extern float VrMusicVolume;
extern float VrVoiceVolume;
extern void vrSettingsSave(void);
extern bool get_button_state(int hand, const char *button_name);
extern float VrSfxVolume;
extern void gevrSndApplySfxVolume(u16 volume);
extern int netVoiceIsMuted(void);
extern void netVoiceSetMuted(int muted);
extern int netVoiceHasPermission(void);
extern int netVoiceCaptureFailed(void);
#include "net_game.h"
void mpwatchPlayBeep(void);
extern int netMpPlayerCount(int fallback);
extern int netGetConnectedPlayerCount(void);
extern int netGetPlayingCount(void);
extern int netGetPhase(void);
extern void netSetLocalVote(int kind, int idx);
extern int netGetVote(int kind, int slot);
extern void netLobbySetCharacter(u8 chr_id);
extern void netLobbySetLoadout(const u8 items[4]);
extern unsigned VrMpFavStages, VrMpFavSets;
extern s32 g_gameOverFlag;
enum { NET_SCENARIO_YOLT = 1, NET_SCENARIO_TLD = 2, NET_SCENARIO_MWTGG = 3 };
enum { NET_WEAPON_SET_CUSTOM_ROW = 14, NET_NEXT_VOTE_ROW = 0 };

/*
 * The pause menu's pages of rows: PAUSE holds the audio, LOBBY the match
 * (the settings the host changes, the votes, this player's character and
 * guns, the host's START MATCH and RETURN TO LOBBY). The right stick click
 * steps the cursor, up and down change the value (repeating after 18
 * ticks), A fires an action row. The others see a host-only row dim.
 */
enum { GEVR_ROW_VALUE, GEVR_ROW_ACTION };
typedef struct {
    const char *name;
    u8 kind;
    u8 hostonly;
    s32 (*visible)(void);             /* NULL: always */
    void (*value)(char *buf, s32 n);  /* the text after the name; NULL: none */
    void (*step)(s32 dir);            /* -1 down, +1 up; an action row's A is dir 0 */
    const char *hint;                 /* the legend's stick hint */
} GevrMenuRow;
typedef struct {
    const GevrMenuRow *rows;
    s32 count;
    s32 cursor;                       /* a row index */
    s32 scroll;                       /* the first row drawn, among the visible */
} GevrMenuPage;
#define GEVR_MENU_ROWS_SHOWN 8

static void gevrUpper(char *b)
{
    for (; *b; b++)
    {
        if (*b >= 'a' && *b <= 'z') *b -= 'a' - 'A';
    }
}

static s32 gevrCycled(s32 v, s32 dir, s32 count)
{
    v += dir;
    if (v < 0) v = count - 1;
    if (v >= count) v = 0;
    return v;
}

/* ---- PAUSE: the audio rows ---- */
static void rowMusicValue(char *b, s32 n) { snprintf(b, n, "%d%%", ((s32)get_mTrack2Vol() * 100 + 16383) / 32767); }
static void rowMusicStep(s32 dir)
{
    s32 volume = (s32)get_mTrack2Vol() + dir * 3277;
    if (volume < 0) volume = 0;
    if (volume > 32767) volume = 32767;
    set_mTrack2Vol((u16)volume);
    musicTrack1ApplySeqpVol((u16)volume);
    musicTrack3ApplySeqpVol((u16)volume);
    VrMusicVolume = (float)volume / 32767.0f;
    vrSettingsSave();
}
static void rowSfxValue(char *b, s32 n) { snprintf(b, n, "%d%%", (int)(VrSfxVolume * 100.0f + 0.5f)); }
static void rowSfxStep(s32 dir)
{
    /* Tenths, so the steps land on 0..100% exactly. */
    s32 tenths = (s32)(VrSfxVolume * 10.0f + 0.5f) + dir;
    if (tenths < 0) tenths = 0;
    if (tenths > 10) tenths = 10;
    VrSfxVolume = (float)tenths / 10.0f;
    gevrSndApplySfxVolume((u16)(VrSfxVolume * 32767.0f));
    vrSettingsSave();
}
static void rowVoiceValue(char *b, s32 n) { snprintf(b, n, "%d%%", (int)(VrVoiceVolume * 100.0f + 0.5f)); }
static void rowVoiceStep(s32 dir)
{
    VrVoiceVolume += dir * 0.10f;
    if (VrVoiceVolume < 0.0f) VrVoiceVolume = 0.0f;
    if (VrVoiceVolume > 1.0f) VrVoiceVolume = 1.0f;
    vrSettingsSave();
}
static void rowMicValue(char *b, s32 n)
{
    snprintf(b, n, "%s", netVoiceIsMuted() ? "MUTED" : !netVoiceHasPermission() ? "NO ACCESS" :
             netVoiceCaptureFailed() ? "UNAVAILABLE" : "ACTIVE");
}
static void rowMicStep(s32 dir)
{
    /* up = active, down = muted */
    if ((dir < 0) != (netVoiceIsMuted() != 0)) netVoiceSetMuted(dir < 0);
}
static const GevrMenuRow s_pauseRows[] = {
    { "MUSIC", GEVR_ROW_VALUE, 0, NULL, rowMusicValue, rowMusicStep, "R-STICK:ADJ" },
    { "SFX",   GEVR_ROW_VALUE, 0, NULL, rowSfxValue,   rowSfxStep,   "R-STICK:ADJ" },
    { "VOICE", GEVR_ROW_VALUE, 0, NULL, rowVoiceValue, rowVoiceStep, "R-STICK:ADJ" },
    { "MIC",   GEVR_ROW_VALUE, 0, NULL, rowMicValue,   rowMicStep,   "R-STICK:ON/OFF" },
};

/* ---- LOBBY: the match ---- */
static s32 lobbyVoting(void) { return gevrNetConfigGet(CFG_NEXT_ROUND) == NET_NEXT_VOTE_ROW; }
static s32 lobbyNotGoldenGun(void) { return gevrNetConfigGet(CFG_SCENARIO) != NET_SCENARIO_MWTGG; }
static s32 lobbyNotYolt(void) { return gevrNetConfigGet(CFG_SCENARIO) != NET_SCENARIO_YOLT; }
static s32 lobbyCustomSet(void) { return lobbyNotGoldenGun() && gevrNetConfigGet(CFG_WEAPON_SET) == NET_WEAPON_SET_CUSTOM_ROW; }
static s32 lobbyWeaponVote(void) { return lobbyVoting() && lobbyNotGoldenGun(); }
static s32 lobbyLoadouts(void) { return gevrNetConfigGet(CFG_LOADOUTS) != 0; }
static s32 lobbyCanStart(void) { return netIsHost() && (netGetPhase() == 1 || g_gameOverFlag) && netCountdownSecondsLeft() == 0; }
static s32 lobbyCanReturn(void) { return netIsHost() && (netGetPhase() == 2 || netCountdownSecondsLeft() > 0); }

/* the vote rows: this player's pick and how many share it, or the host's mode */
static void lobbyVoteValue(char *b, s32 n, s32 kind, const char *(*name)(int))
{
    s32 mode = gevrNetConfigGet(CFG_NEXT_ROUND);
    if (mode != NET_NEXT_VOTE_ROW)
    {
        snprintf(b, n, "%s", netNextRoundName(mode));
        return;
    }
    s32 vote = netGetVote(kind, netGetLocalSlot());
    s32 tally = 0;
    s32 slot;
    for (slot = 0; slot < 4; slot++)
    {
        if (vote >= 0 && netGetVote(kind, slot) == vote) tally++;
    }
    if (vote < 0) snprintf(b, n, "NO VOTE");
    else snprintf(b, n, "%s (%d)", name(vote), tally);
}
static void rowNextMapValue(char *b, s32 n) { lobbyVoteValue(b, n, 0, netStageName); }
static void rowNextMapStep(s32 dir)
{
    /* through the maps the party fits; past either end is no vote */
        s32 vote = netGetVote(0, netGetLocalSlot());
    s32 count = netStageCount();
    s32 tries = count + 1;
    if (!lobbyVoting()) return;
    do
    {
        vote += dir;
        if (vote >= count) vote = -1;
        if (vote < -1) vote = count - 1;
    } while (vote >= 0 && !netStageEligible(vote) && --tries > 0);
    netSetLocalVote(0, vote);
}
static void rowNextWeaponsValue(char *b, s32 n) { lobbyVoteValue(b, n, 1, netWeaponSetName); }
static void rowNextWeaponsStep(s32 dir)
{
    s32 vote = netGetVote(1, netGetLocalSlot());
    s32 count = netWeaponSetCount();
    if (!lobbyVoting()) return;
    vote += dir;
    if (vote >= count) vote = -1;
    if (vote < -1) vote = count - 1;
    netSetLocalVote(1, vote);
}
static void rowNextRoundValue(char *b, s32 n) { snprintf(b, n, "%s", netNextRoundName(gevrNetConfigGet(CFG_NEXT_ROUND))); }
static void rowNextRoundStep(s32 dir) { gevrNetConfigSet(CFG_NEXT_ROUND, gevrCycled(gevrNetConfigGet(CFG_NEXT_ROUND), dir, 3)); }
static void rowScenarioValue(char *b, s32 n) { snprintf(b, n, "%s", netScenarioName(gevrNetConfigGet(CFG_SCENARIO))); }
static void rowScenarioStep(s32 dir) { gevrNetConfigSet(CFG_SCENARIO, gevrCycled(gevrNetConfigGet(CFG_SCENARIO), dir, netScenarioCount())); }
static void rowLengthValue(char *b, s32 n) { snprintf(b, n, "%s", netGameLengthName(gevrNetConfigGet(CFG_GAME_LENGTH))); }
static void rowLengthStep(s32 dir)
{
    /* The Living Daylights takes the time limits only, as the game's own menu has it */
    s32 count = gevrNetConfigGet(CFG_SCENARIO) == NET_SCENARIO_TLD ? 4 : 7;
    gevrNetConfigSet(CFG_GAME_LENGTH, gevrCycled(gevrNetConfigGet(CFG_GAME_LENGTH), dir, count));
}
static void rowHealthValue(char *b, s32 n) { snprintf(b, n, "%s", netHealthName(gevrNetConfigGet(CFG_HEALTH))); }
static void rowHealthStep(s32 dir) { gevrNetConfigSet(CFG_HEALTH, gevrCycled(gevrNetConfigGet(CFG_HEALTH), dir, netHealthCount())); }
static void rowDualValue(char *b, s32 n) { snprintf(b, n, "%s", netDualWieldName(gevrNetConfigGet(CFG_DUAL_WIELD))); }
static void rowDualStep(s32 dir) { gevrNetConfigSet(CFG_DUAL_WIELD, gevrCycled(gevrNetConfigGet(CFG_DUAL_WIELD), dir, 3)); }
static void rowLoadoutsValue(char *b, s32 n) { snprintf(b, n, "%s", gevrNetConfigGet(CFG_LOADOUTS) ? "ON" : "OFF"); }
static void rowLoadoutsStep(s32 dir) { gevrNetConfigSet(CFG_LOADOUTS, !gevrNetConfigGet(CFG_LOADOUTS)); }
static void rowWeaponsValue(char *b, s32 n) { snprintf(b, n, "%s", netWeaponSetName(gevrNetConfigGet(CFG_WEAPON_SET))); }
static void rowWeaponsStep(s32 dir) { gevrNetConfigSet(CFG_WEAPON_SET, gevrCycled(gevrNetConfigGet(CFG_WEAPON_SET), dir, netWeaponSetCount())); }
static void rowMapValue(char *b, s32 n) { snprintf(b, n, "%s", netStageName(netStageIndexOf((u8)gevrNetConfigGet(CFG_STAGE)))); }
static void rowMapStep(s32 dir)
{
    s32 idx = netStageIndexOf((u8)gevrNetConfigGet(CFG_STAGE));
        s32 tries = netStageCount();
    do
    {
        idx = gevrCycled(idx < 0 ? 0 : idx, dir, netStageCount());
    } while (!netStageEligible(idx) && --tries > 0);
    gevrNetConfigSet(CFG_STAGE, idx);   /* the setter takes the list position */
}
static void lobbyCustomValue(char *b, s32 n, s32 k) { snprintf(b, n, "%s", netItemName(gevrNetConfigGet(CFG_CUSTOM0 + k))); }
static void lobbyCustomStep(s32 dir, s32 k)
{
    s32 idx = netItemIndexOf(gevrNetConfigGet(CFG_CUSTOM0 + k));
    idx = gevrCycled(idx < 0 ? 0 : idx, dir, netItemCount());
    gevrNetConfigSet(CFG_CUSTOM0 + k, gevrNetItemAt(idx));
}
static void rowCustom0Value(char *b, s32 n) { lobbyCustomValue(b, n, 0); }
static void rowCustom1Value(char *b, s32 n) { lobbyCustomValue(b, n, 1); }
static void rowCustom2Value(char *b, s32 n) { lobbyCustomValue(b, n, 2); }
static void rowCustom3Value(char *b, s32 n) { lobbyCustomValue(b, n, 3); }
static void rowCustom0Step(s32 dir) { lobbyCustomStep(dir, 0); }
static void rowCustom1Step(s32 dir) { lobbyCustomStep(dir, 1); }
static void rowCustom2Step(s32 dir) { lobbyCustomStep(dir, 2); }
static void rowCustom3Step(s32 dir) { lobbyCustomStep(dir, 3); }
static void rowCharacterValue(char *b, s32 n) { snprintf(b, n, "%s", netCharacterName(gevrNetSlotChr(netGetLocalSlot()))); }
static void rowCharacterStep(s32 dir) { netLobbySetCharacter((u8)gevrCycled(gevrNetSlotChr(netGetLocalSlot()), dir, netCharacterCount())); }
static void lobbyLoadoutValue(char *b, s32 n, s32 k)
{
    s32 item = gevrNetSlotLoadout(netGetLocalSlot(), k);
    snprintf(b, n, "%s", item ? netItemName(item) : "NONE");
}
static void lobbyLoadoutStep(s32 dir, s32 k)
{
    u8 items[4];
    s32 i;
    s32 idx;
    for (i = 0; i < 4; i++) items[i] = (u8)gevrNetSlotLoadout(netGetLocalSlot(), i);
    idx = netItemIndexOf(items[k]);
    idx = gevrCycled(idx < 0 ? 0 : idx, dir, netItemCount());
    items[k] = gevrNetItemAt(idx);
    netLobbySetLoadout(items);
}
static void rowLoadout0Value(char *b, s32 n) { lobbyLoadoutValue(b, n, 0); }
static void rowLoadout1Value(char *b, s32 n) { lobbyLoadoutValue(b, n, 1); }
static void rowLoadout2Value(char *b, s32 n) { lobbyLoadoutValue(b, n, 2); }
static void rowLoadout3Value(char *b, s32 n) { lobbyLoadoutValue(b, n, 3); }
static void rowLoadout0Step(s32 dir) { lobbyLoadoutStep(dir, 0); }
static void rowLoadout1Step(s32 dir) { lobbyLoadoutStep(dir, 1); }
static void rowLoadout2Step(s32 dir) { lobbyLoadoutStep(dir, 2); }
static void rowLoadout3Step(s32 dir) { lobbyLoadoutStep(dir, 3); }
/* the favorites: this headset's, for the shuffle and the playlist when it hosts;
 * the toggle is for the map or set the vote row points at (or the current one) */
static s32 lobbyFavStage(void)
{
    s32 v = lobbyVoting() ? netGetVote(0, netGetLocalSlot()) : -1;
    return v >= 0 ? v : netStageIndexOf((u8)gevrNetConfigGet(CFG_STAGE));
}
static s32 lobbyFavSet(void)
{
    s32 v = lobbyVoting() ? netGetVote(1, netGetLocalSlot()) : -1;
    return v >= 0 ? v : gevrNetConfigGet(CFG_WEAPON_SET);
}
static void rowFavMapValue(char *b, s32 n)
{
    s32 i = lobbyFavStage();
    snprintf(b, n, "%s %s", netStageName(i), i >= 0 && ((VrMpFavStages >> i) & 1u) ? "YES" : "NO");
}
static void rowFavMapStep(s32 dir)
{
    s32 i = lobbyFavStage();
    if (i < 0) return;
    VrMpFavStages ^= 1u << i;
    vrSettingsSave();
}
static void rowFavSetValue(char *b, s32 n)
{
    s32 i = lobbyFavSet();
    snprintf(b, n, "%s %s", netWeaponSetName(i), i >= 0 && ((VrMpFavSets >> i) & 1u) ? "YES" : "NO");
}
static void rowFavSetStep(s32 dir)
{
    s32 i = lobbyFavSet();
    if (i < 0) return;
    VrMpFavSets ^= 1u << i;
    vrSettingsSave();
}
static void rowStartStep(s32 dir) { netHostStartRoundNow(); }
static void rowReturnStep(s32 dir) { netHostReturnToLobby(); }
static const GevrMenuRow s_lobbyRows[] = {
    { "START MATCH",     GEVR_ROW_ACTION, 1, lobbyCanStart,     NULL,               rowStartStep,       "A:START" },
    { "RETURN TO LOBBY", GEVR_ROW_ACTION, 1, lobbyCanReturn,    NULL,               rowReturnStep,      "A:RETURN" },
    { "NEXT ROUND",      GEVR_ROW_VALUE,  1, NULL,              rowNextRoundValue,  rowNextRoundStep,   "R-STICK:PICK" },
    { "NEXT MAP",        GEVR_ROW_VALUE,  0, lobbyVoting,       rowNextMapValue,    rowNextMapStep,     "R-STICK:VOTE" },
    { "NEXT WEAPONS",    GEVR_ROW_VALUE,  0, lobbyWeaponVote,       rowNextWeaponsValue, rowNextWeaponsStep, "R-STICK:VOTE" },
    { "MAP",             GEVR_ROW_VALUE,  1, NULL,              rowMapValue,        rowMapStep,         "R-STICK:PICK" },
    { "WEAPONS",         GEVR_ROW_VALUE,  1, lobbyNotGoldenGun, rowWeaponsValue,    rowWeaponsStep,     "R-STICK:PICK" },
    { "CUSTOM 1",        GEVR_ROW_VALUE,  1, lobbyCustomSet,    rowCustom0Value,    rowCustom0Step,     "R-STICK:PICK" },
    { "CUSTOM 2",        GEVR_ROW_VALUE,  1, lobbyCustomSet,    rowCustom1Value,    rowCustom1Step,     "R-STICK:PICK" },
    { "CUSTOM 3",        GEVR_ROW_VALUE,  1, lobbyCustomSet,    rowCustom2Value,    rowCustom2Step,     "R-STICK:PICK" },
    { "CUSTOM 4",        GEVR_ROW_VALUE,  1, lobbyCustomSet,    rowCustom3Value,    rowCustom3Step,     "R-STICK:PICK" },
    { "SCENARIO",        GEVR_ROW_VALUE,  1, NULL,              rowScenarioValue,   rowScenarioStep,    "R-STICK:PICK" },
    { "LENGTH",          GEVR_ROW_VALUE,  1, lobbyNotYolt,      rowLengthValue,     rowLengthStep,      "R-STICK:PICK" },
    { "HEALTH",          GEVR_ROW_VALUE,  1, NULL,              rowHealthValue,     rowHealthStep,      "R-STICK:PICK" },
    { "DUAL WIELD",      GEVR_ROW_VALUE,  1, NULL,              rowDualValue,       rowDualStep,        "R-STICK:PICK" },
    { "LOADOUTS",        GEVR_ROW_VALUE,  1, NULL,              rowLoadoutsValue,   rowLoadoutsStep,    "R-STICK:ON/OFF" },
    { "CHARACTER",       GEVR_ROW_VALUE,  0, NULL,              rowCharacterValue,  rowCharacterStep,   "R-STICK:PICK" },
    { "LOADOUT 1",       GEVR_ROW_VALUE,  0, lobbyLoadouts,     rowLoadout0Value,   rowLoadout0Step,    "R-STICK:PICK" },
    { "LOADOUT 2",       GEVR_ROW_VALUE,  0, lobbyLoadouts,     rowLoadout1Value,   rowLoadout1Step,    "R-STICK:PICK" },
    { "LOADOUT 3",       GEVR_ROW_VALUE,  0, lobbyLoadouts,     rowLoadout2Value,   rowLoadout2Step,    "R-STICK:PICK" },
    { "LOADOUT 4",       GEVR_ROW_VALUE,  0, lobbyLoadouts,     rowLoadout3Value,   rowLoadout3Step,    "R-STICK:PICK" },
    { "FAV MAP",         GEVR_ROW_VALUE,  0, NULL,              rowFavMapValue,     rowFavMapStep,      "R-STICK:YES/NO" },
    { "FAV SET",         GEVR_ROW_VALUE,  0, NULL,              rowFavSetValue,     rowFavSetStep,      "R-STICK:YES/NO" },
};
static GevrMenuPage s_pausePage = { s_pauseRows, sizeof(s_pauseRows) / sizeof(s_pauseRows[0]), 0, 0 };
static GevrMenuPage s_lobbyPage = { s_lobbyRows, sizeof(s_lobbyRows) / sizeof(s_lobbyRows[0]), 0, 0 };

static GevrMenuPage *gevrMenuPageFor(s32 mode)
{
    return mode == MENU_PAUSE ? &s_pausePage : mode == MENU_LOBBY ? &s_lobbyPage : NULL;
}
static s32 gevrRowVisible(const GevrMenuRow *r) { return r->visible == NULL || r->visible(); }
static s32 gevrRowEditable(const GevrMenuRow *r) { return !r->hostonly || netIsHost(); }

/* the cursor onto a visible row: dir steps to the next one that way, 0 keeps it or takes the nearest below */
static void gevrPageMoveCursor(GevrMenuPage *page, s32 dir)
{
    s32 tries = page->count;
    s32 c = page->cursor;
    if (dir != 0) c = gevrCycled(c, dir, page->count);
    while (tries-- > 0 && !gevrRowVisible(&page->rows[c]))
    {
        c = gevrCycled(c, dir ? dir : 1, page->count);
    }
    page->cursor = c;
}

static void gevrMenuPagesTick(s32 player_num)
{
    static int last_rclick = 0;
    static s32 stick_direction = 0;
    static s32 stick_held_ticks = 0;
    GevrMenuPage *page;
    s32 change = 0;
    s32 stick_y;
    s32 direction;
    int rclick;

    if (!netIsActive() || player_num != netGetLocalSlot()) return;
    page = gevrMenuPageFor(g_CurrentPlayer->mpmenumode);
    if (page == NULL) return;

    rclick = get_button_state(1, "thumbstick_click") ? 1 : 0;
    /* Right stick click only: A also closes the pause menu. */
    if (rclick && !last_rclick)
    {
        gevrPageMoveCursor(page, 1);
        mpwatchPlayBeep();
    }
    last_rclick = rclick;

    stick_y = joyGetStickY(player_num);
    direction = stick_y > 30 ? 1 : stick_y < -30 ? -1 : 0;
    if (direction != stick_direction)
    {
        stick_direction = direction;
        stick_held_ticks = 0;
        change = direction;
    }
    else if (direction != 0 && ++stick_held_ticks >= 18 && (stick_held_ticks - 18) % 6 == 0)
    {
        change = direction;
    }
    gevrPageMoveCursor(page, 0);
    if (change)
    {
        const GevrMenuRow *r = &page->rows[page->cursor];
        if (gevrRowEditable(r) && r->kind == GEVR_ROW_VALUE && r->step != NULL)
        {
            r->step(change);
            /* After the change, so a volume's beep plays at the new level. */
            mpwatchPlayBeep();
        }
    }
}

/* A on the LOBBY page: the selected action row */
static s32 gevrMenuPageAction(void)
{
    GevrMenuPage *page = gevrMenuPageFor(g_CurrentPlayer->mpmenumode);
    const GevrMenuRow *r;
    if (page == NULL) return 0;
    gevrPageMoveCursor(page, 0);
    r = &page->rows[page->cursor];
    if (!gevrRowVisible(r) || !gevrRowEditable(r) || r->kind != GEVR_ROW_ACTION || r->step == NULL) return 0;
    r->step(0);
    mpwatchPlayBeep();
    return 1;
}

static Gfx *gevrMenuPagesDraw(Gfx *gdl, s32 menu_top, s32 two_player_x_offset)
{
    GevrMenuPage *page = gevrMenuPageFor(g_CurrentPlayer->mpmenumode);
    char label[64];
    char value[40];
    s32 vis[32];
    s32 nvis = 0;
    s32 ci = 0;
    s32 i;
    s32 x;
    s32 y;
    s32 textwidth;
    s32 textheight;
    s32 shown;
    s32 y0;
    const GevrMenuRow *sel;

    if (!netIsActive() || page == NULL) return gdl;
    /* PAUSE: under the score block; LOBBY: the page's own top */
    shown = g_CurrentPlayer->mpmenumode == MENU_PAUSE ? page->count : GEVR_MENU_ROWS_SHOWN;
    y0 = g_CurrentPlayer->mpmenumode == MENU_PAUSE ? 116 : 37;
    gevrPageMoveCursor(page, 0);
    for (i = 0; i < page->count && nvis < 32; i++)
    {
        if (gevrRowVisible(&page->rows[i]))
        {
            if (i == page->cursor) ci = nvis;
            vis[nvis++] = i;
        }
    }
    if (page->scroll > ci) page->scroll = ci;
    if (page->scroll < ci - shown + 1) page->scroll = ci - shown + 1;
    if (page->scroll > nvis - shown) page->scroll = nvis - shown;
    if (page->scroll < 0) page->scroll = 0;

    for (i = 0; i < shown && page->scroll + i < nvis; i++)
    {
        const GevrMenuRow *r = &page->rows[vis[page->scroll + i]];
        const s32 selected = vis[page->scroll + i] == page->cursor;
        const s32 dim = !gevrRowEditable(r);
        value[0] = '\0';
        if (r->value != NULL) r->value(value, sizeof(value));
        snprintf(label, sizeof(label), "%s%s %s%s", selected ? "> " : "  ", r->name, value, selected ? " <" : "");
        gevrUpper(label);
        textMeasure(&textheight, &textwidth, label, ptrFontBankGothicChars, ptrFontBankGothic, 0);
        x = viGetViewLeft() + two_player_x_offset + 80 - (textwidth >> 1);
        y = menu_top + y0 + i * 13 + MPMENU_YOFF;
        gdl = textRender(gdl, &x, &y, label, ptrFontBankGothicChars, ptrFontBankGothic,
                         dim ? 0x00ff0080 : selected ? 0xa0ffa0f0 : 0x00ff00b0, viGetX(), viGetY(), 0, 0);
    }
    /* the legend: the selected row's hint, the click, and arrows when rows are out of view */
    sel = &page->rows[page->cursor];
    snprintf(label, sizeof(label), "%s%s  CLICK:NEXT%s", page->scroll > 0 ? "^ " : "",
             gevrRowEditable(sel) ? sel->hint : "HOST ONLY", page->scroll + shown < nvis ? " v" : "");
    textMeasure(&textheight, &textwidth, label, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    x = viGetViewLeft() + two_player_x_offset + 80 - (textwidth >> 1);
    y = menu_top + y0 + shown * 13 + 2 + MPMENU_YOFF;
    gdl = textRender(gdl, &x, &y, label, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viGetX(), viGetY(), 0, 0);
    return gdl;
}
#endif



// bss
s32 g_stopPlayFlag;

/**
 * g_gameOverFlag has two meanings. One is the standard true/false for whether the game is over.
 * The other is as a timer. Values >= 2 mean a countdown is still running.
 */
s32 g_gameOverFlag;

s32 chevron_glow;
s32 chevron_glow_timer;
s32 alt_gameover_msg;
s32 alt_gameover_msg_timer;
s32 g_pausedFlag;
s32 who_paused;

// data
u16 g_AwardNames[] = {
    getStringID(LMPMENU, MPMENU_STR_00_LEMMINGAWARD),getStringID(LMPMENU, MPMENU_STR_01_WHERESTHEAMMO),getStringID(LMPMENU, MPMENU_STR_02_WHERESTHEARMOR),getStringID(LMPMENU, MPMENU_STR_03_AC10AWARD),getStringID(LMPMENU, MPMENU_STR_04_MARKSMANSHIPAWARD),getStringID(LMPMENU, MPMENU_STR_05_MOSTPROFESSIONAL),
    getStringID(LMPMENU, MPMENU_STR_06_MOSTDEADLY),getStringID(LMPMENU, MPMENU_STR_07_MOSTLYHARMLESS),getStringID(LMPMENU, MPMENU_STR_08_MOSTCOWARD),getStringID(LMPMENU, MPMENU_STR_09_MOSTFRANTIC),getStringID(LMPMENU, MPMENU_STR_0A_MOSTHONORABLE),getStringID(LMPMENU, MPMENU_STR_0B_MOSTDISHONORABLE),
    getStringID(LMPMENU, MPMENU_STR_0C_SHORTESTINNINGS),getStringID(LMPMENU, MPMENU_STR_0D_LONGESTINNINGS),getStringID(LMPMENU, MPMENU_STR_0E_DOUBLEKILL),getStringID(LMPMENU, MPMENU_STR_0F_TRIPLEKILL),getStringID(LMPMENU, MPMENU_STR_10_QUADKILL)
};


s32 mpwatchMenuCanGoRight(void) 
{
    switch(g_CurrentPlayer->mpmenumode)
    {
        case MENU_GOWOC:
        case MENU_LOSSES:
        case MENU_KILLS:
        case MENU_PAUSE:
        case MENU_LOBBY:
            return 1;
        case MENU_EXIT:
        case MENU_EXIT_CONFIRM:
        case MENU_FINISHED:
            return 0;
        case MENU_SCORES:
#ifdef GEVR
            /* online the results lead on to the LOBBY page, where the next match is voted */
            if (netIsActive()) return 1;
#endif
            return g_gameOverFlag ? 0 : 1;
        default:
            return 0;
    }
}


s32 mpwatchMenuCanGoLeft(void) 
{
    switch(g_CurrentPlayer->mpmenumode)
    {
        case MENU_KILLS:
        case MENU_SCORES:
        case MENU_PAUSE:
        case MENU_LOBBY:
        case MENU_EXIT:
            return 1;
        case MENU_GOWOC:
        case MENU_EXIT_CONFIRM:
        case MENU_FINISHED:
            return 0;
        case MENU_LOSSES:
            return g_gameOverFlag ? 1 : 0;
        default:
#ifdef DEBUG
            // kill the process
            assert(1 == 0); // mpmenu.c, line87
#endif
        return 0;
    }
}


s32 mpwatchIsPlayerPressingRight(s32 player)
{
    s32 iVar3 = joyGetStickXInRange(player, -2, 1);

    if ((joyGetButtonsPressedThisFrame(player, R_JPAD|R_CBUTTONS)) || ((iVar3 >= 1  && (g_CurrentPlayer->mpjoywascentre)))) 
    {
        return 1;
    }

    return 0;
}

s32 mpwatchIsPlayerPressingLeft(s32 player)
{
    s32 iVar3 = joyGetStickXInRange(player, -2, 1);

    if ((joyGetButtonsPressedThisFrame(player, L_JPAD|L_CBUTTONS)) || ((iVar3 < -1 && (g_CurrentPlayer->mpjoywascentre)))) 
    {
        return 1;
    }

    return 0;
}


void mpwatchPlayBeep(void)
{
    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, 0);
}


void mpwatchUnpauseGame(void)
{
    g_stopPlayFlag = 0;
    g_gameOverFlag = 0;
    g_pausedFlag = 0;
}


/**
 * Returns the index (0-3) of the player with the highest value.
 */
s32 mpFindMaxInt(s32 numplayers, s32 value0, s32 value1, s32 value2, s32 value3)
{
#ifdef GEVR
    if (netIsActive()) {
        s32 values[4] = {value0, value1, value2, value3};
        s32 best = -1;
        for (s32 slot = 0; slot < numplayers; slot++)
            if (netSlotOccupied(slot) && (best < 0 || values[slot] > values[best])) best = slot;
        return best < 0 ? 0 : best;
    }
#endif
    s32 aux;
    s32 result;
 
    if ((value0 < value1) || ((value1 == value0 && ((randomGetNext() & 1))))) 
    {
        result = 1;
        aux = value1;
    }
    else 
    {
        result = 0;
        aux = value0;
    }
 
    if (numplayers >= 3) 
    {
 
        if ((aux < value2) || ((value2 == aux && ((randomGetNext() & 1))))) 
        {
            result = 2;
            aux = value2;
        }
 
        if (numplayers >= 4) 
        {
            if ((aux < value3) || ((value3 == aux && ((randomGetNext() & 1))))) {
                result = 3;
            }
        }
    }
 
    return result;
}


/**
 * Returns the index (0-3) of the player with the lowest value.
 */
s32 mpFindMinInt(s32 numplayers, s32 value0, s32 value1, s32 value2, s32 value3)
{
#ifdef GEVR
    if (netIsActive()) {
        s32 values[4] = {value0, value1, value2, value3};
        s32 best = -1;
        for (s32 slot = 0; slot < numplayers; slot++)
            if (netSlotOccupied(slot) && (best < 0 || values[slot] < values[best])) best = slot;
        return best < 0 ? 0 : best;
    }
#endif
    s32 aux;
    s32 result;
 
    if ((value1 < value0) || ((value1 == value0 && ((randomGetNext() & 1))))) 
    {
        result = 1;
        aux = value1;
    }
    else 
    {
        result = 0;
        aux = value0;
    }
 
    if (numplayers >= 3) 
    {
        if ((value2 < aux) || ((value2 == aux && ((randomGetNext() & 1)))))
        {
            result = 2;
            aux = value2;
        }
 
        if (numplayers >= 4) 
        {
            if ((value3 < aux) || ((value3 == aux && ((randomGetNext() & 1))))) 
            {
                result = 3;
            }
        }
    }
 
    return result;
}


/**
 * Returns the index of the player with the highest value.
 * 
 * @bug: aux is s32 so each time it's set to one of the values, the decimal portion gets truncated.
 * The first comparison is safe from this since it doesn't use aux, but the rest are impacted.
 */
s32 mpFindMaxFloat(s32 numplayers, f32 value0, f32 value1, f32 value2, f32 value3)
{
#ifdef GEVR
    if (netIsActive()) {
        f32 values[4] = {value0, value1, value2, value3};
        s32 best = -1;
        for (s32 slot = 0; slot < numplayers; slot++)
            if (netSlotOccupied(slot) && (best < 0 || values[slot] > values[best])) best = slot;
        return best < 0 ? 0 : best;
    }
#endif
    s32 aux;
    s32 result;
 
    if ((value0 < value1) || ((value1 == value0 && ((randomGetNext() & 1))))) 
    {
        aux = (s32) value1;
        result = 1;
    }
    else 
    {
        aux = (s32) value0;
        result = 0;
    }
 
    if (numplayers >= 3)
    {
        if ((aux < value2) || ((value2 == aux && ((randomGetNext() & 1))))) 
        {
            aux = (s32) value2;
            result = 2;
        }
 
        if (numplayers >= 4)
        {
            if ((aux < value3) || ((value3 == aux && ((randomGetNext() & 1))))) 
            {
                result = 3;
            }
        }
    }
 
    return result;
}


/**
 * Returns the index of the player with the lowest value.
 * 
 * Also suffers from the same bug as the above function.
 */
s32 mpFindMinFloat(s32 numplayers, f32 value0, f32 value1, f32 value2, f32 value3)
{
#ifdef GEVR
    if (netIsActive()) {
        f32 values[4] = {value0, value1, value2, value3};
        s32 best = -1;
        for (s32 slot = 0; slot < numplayers; slot++)
            if (netSlotOccupied(slot) && (best < 0 || values[slot] < values[best])) best = slot;
        return best < 0 ? 0 : best;
    }
#endif
    s32 aux;
    s32 result;
 
    if ((value1 < value0) || ((value1 == value0 && ((randomGetNext() & 1))))) 
    {
        aux = (s32) value1;
        result = 1;
    }
    else 
    {
        aux = (s32) value0;
        result = 0;
    }
 
    if (numplayers >= 3)
    {
        if ((value2 < aux) || ((value2 == aux && ((randomGetNext() & 1))))) 
        {
            aux = (s32) value2;
            result = 2;
        }
 
        if (numplayers >= 4)
        {
            if ((value3 < aux) || ((value3 == aux && ((randomGetNext() & 1))))) 
            {
                result = 3;
            }
        }
    }
 
    return result;
}


void pauseAndLockControls(void) 
{
    lvlSetControlsLockedFlag(1);
    g_pausedFlag = TRUE;
}


bool disablePlayerActionsWhenPausedOrInMpMenu(void)
{
    if (getPlayerCount() == 1)
    {
        return TRUE;
    }

    if (g_stopPlayFlag)
    {
        return FALSE;
    }

    if (g_CurrentPlayer->mpmenuon)
    {
        return FALSE;
    }

    return TRUE;
}


void mpwatchSetStopPlayFlag(void)
{
    g_stopPlayFlag = TRUE;
}



void mpCalculateAwards(bool gameoverdelay)
{
#ifdef GEVR
    if (netIsActive()) {
        if (g_gameOverFlag) return;
        netHostRoundEnded();
    }
#endif
    s32 player_count;
    s32 i;
    s32 j;
    s32 weapon_choice_2;
    s32 weapon_choice_1;
    s32 prev_player_num;
    s32 duration;

    struct AwardMetrics metrics[4] = {0};

    player_count = getPlayerCount();
    duration = getMissiontimer();

    sndDeactivateAllSfxByFlag_1();
    set_missionstate(MISSION_STATE_0);

    musicTrack1ApplySeqpVol(sub_GAME_7F0C0BF0());
    g_musicXTrack1Fade = 0;
    musicTrack1Play(M_INTROSWOOSH);

    pauseAndLockControls();

    if (gameoverdelay != 0)
    {
        g_gameOverFlag = (PAL ? 250 : 300);
    }
    else
    {
        g_gameOverFlag = 1;
    }

    alt_gameover_msg = 1;

    // Possible copy/paste error: these are the timer values for the chevron glow
    alt_gameover_msg_timer =  (PAL ? 16 : 20);

    chevron_glow = 0;
    chevron_glow_timer = 0;

    prev_player_num = get_cur_playernum();

    for (i = 0; i < player_count; i++)
    {
#ifdef GEVR
        if (netIsActive() && !netSlotOccupied(i)) continue;
#endif

        set_cur_player(i);

        g_CurrentPlayer->mpmenuon = TRUE;
        g_CurrentPlayer->mpmenumode = MENU_SCORES;
        g_CurrentPlayer->ptr_text_first_mp_award = 0;
        g_CurrentPlayer->ptr_text_second_mp_award = 0;

        bondinvGetWeaponOfChoice(&weapon_choice_1, &weapon_choice_2);
        store_favorite_weapon_current_player((u32) weapon_choice_1, (u32) weapon_choice_2);

        metrics[i].num_shots = get_curplayer_shot_register(SHOT_REGISTER_TOTAL);
        metrics[i].num_headshots = get_curplayer_shot_register(SHOT_REGISTER_HEAD);
        metrics[i].num_kills = 0;
        metrics[i].num_deaths = 0;
        metrics[i].num_suicides = 0;

        for (j = 0; j < get_selected_num_players(); j++)
        {
            metrics[i].num_deaths += g_playerPlayerData[j].kill_counts[i];
            if (i == j)
            {
                metrics[i].num_suicides += g_playerPlayerData[i].kill_counts[j];
            }
            else
            {
                metrics[i].num_kills += g_playerPlayerData[i].kill_counts[j];
            }
        }

        metrics[i].num_kills += g_playerPlayerData[i].kill_count;

        metrics[i].ks_ratio = metrics[i].num_kills * 100.0f / (metrics[i].num_shots + 1.0f);
        metrics[i].kd_ratio = metrics[i].num_kills * 100.0f / (metrics[i].num_deaths + 1.0f);
        metrics[i].damage_to_backside = g_playerPlayerData[i].damage_to_backside;
        metrics[i].time_other_players_on_screen = g_playerPlayerData[i].time_other_players_on_screen;
        metrics[i].avg_km_per_hour = g_playerPlayerData[i].distance_traveled / 100000.0f / ((duration + 1) / (3600.0f * 60.0f));
        metrics[i].body_armor_pickups = g_playerPlayerData[i].body_armor_pickups;
        metrics[i].awards = 0;
        metrics[i].longest_inning = g_playerPlayerData[i].longest_inning;
        metrics[i].shortest_inning = g_playerPlayerData[i].shortest_inning;
    }

    set_cur_player(prev_player_num);

    // Choose which players are eligible for which awards
    i = mpFindMaxInt(player_count, metrics[0].num_suicides, metrics[1].num_suicides, metrics[2].num_suicides, metrics[3].num_suicides);

    if (metrics[i].num_suicides > 0)
    {
        metrics[i].awards |= AWARD_MOSTSUICIDAL;
    }

    i = mpFindMinInt(player_count, metrics[0].num_shots, metrics[1].num_shots, metrics[2].num_shots, metrics[3].num_shots);

    if (metrics[i].num_shots < 100)
    {
        metrics[i].awards |= AWARD_WHONEEDSAMMO;
    }

    i = mpFindMinFloat(player_count, metrics[0].body_armor_pickups, metrics[1].body_armor_pickups, metrics[2].body_armor_pickups, metrics[3].body_armor_pickups);

    if (metrics[i].body_armor_pickups <= 2.0f)
    {
        metrics[i].awards |= AWARD_WHERESTHEARMOUR;
    }

    i = mpFindMaxFloat(player_count, metrics[0].body_armor_pickups, metrics[1].body_armor_pickups, metrics[2].body_armor_pickups, metrics[3].body_armor_pickups);

    if (metrics[i].body_armor_pickups > 6.0f)
    {
        metrics[i].awards |= AWARD_ACNEGATIVE10;
    }

    i = mpFindMaxInt(player_count, metrics[0].num_headshots, metrics[1].num_headshots, metrics[2].num_headshots, metrics[3].num_headshots);

    if (metrics[i].num_headshots > 0)
    {
        metrics[i].awards |= AWARD_MARKSMANSHIP;
    }

    i = mpFindMaxFloat(player_count, metrics[0].ks_ratio, metrics[1].ks_ratio, metrics[2].ks_ratio, metrics[3].ks_ratio);

    if (metrics[i].ks_ratio > 0.0f)
    {
        metrics[i].awards |= AWARD_MOSTPROFESSIONAL;
    }

    i = mpFindMaxFloat(player_count, metrics[0].kd_ratio, metrics[1].kd_ratio, metrics[2].kd_ratio, metrics[3].kd_ratio);

    if (metrics[i].kd_ratio > 0.0f)
    {
        metrics[i].awards |= AWARD_MOSTDEADLY;
    }

    i = mpFindMinFloat(player_count, metrics[0].kd_ratio, metrics[1].kd_ratio, metrics[2].kd_ratio, metrics[3].kd_ratio);
    metrics[i].awards |= AWARD_MOSTHARMLESS;

    i = mpFindMinInt(player_count, metrics[0].time_other_players_on_screen, metrics[1].time_other_players_on_screen, metrics[2].time_other_players_on_screen, metrics[3].time_other_players_on_screen);
    metrics[i].awards |= AWARD_MOSTCOWARDLY;

    i = mpFindMaxFloat(player_count, metrics[0].avg_km_per_hour, metrics[1].avg_km_per_hour, metrics[2].avg_km_per_hour, metrics[3].avg_km_per_hour);

    if (metrics[i].avg_km_per_hour > 10.0f)
    {
        metrics[i].awards |= AWARD_MOSTFRANTIC;
    }

    i = mpFindMinInt(player_count, metrics[0].damage_to_backside, metrics[1].damage_to_backside, metrics[2].damage_to_backside, metrics[3].damage_to_backside);
    metrics[i].awards |= AWARD_MOSTHONORABLE;

    i = mpFindMaxInt(player_count, metrics[0].damage_to_backside, metrics[1].damage_to_backside, metrics[2].damage_to_backside, metrics[3].damage_to_backside);

    if (metrics[i].damage_to_backside > 0 && (metrics[i].awards & AWARD_MOSTHONORABLE) == 0)
    {
        metrics[i].awards |= AWARD_MOSTDISHONORABLE;
    }

    i = mpFindMaxInt(player_count, metrics[0].longest_inning, metrics[1].longest_inning, metrics[2].longest_inning, metrics[3].longest_inning);

    if (metrics[i].longest_inning > 0)
    {
        metrics[i].awards |= AWARD_LONGESTINNINGS;
    }

    i = mpFindMinInt(player_count, metrics[0].shortest_inning, metrics[1].shortest_inning, metrics[2].shortest_inning, metrics[3].shortest_inning);

    if (metrics[i].shortest_inning > 0)
    {
        metrics[i].awards |= AWARD_SHORTESTINNINGS;
    }

    for (i = 0; i < player_count; i++)
    {
#ifdef GEVR
        if (netIsActive() && !netSlotOccupied(i)) continue;
#endif
        if (g_playerPlayerData[i].most_killed_one_time == 4)
        {
            metrics[i].awards |= AWARD_QUADKILL;
        }
        else if (g_playerPlayerData[i].most_killed_one_time == 3)
        {
            metrics[i].awards |= AWARD_TRIPLEKILL;
        }
        else if (g_playerPlayerData[i].most_killed_one_time == 2)
        {
            metrics[i].awards |= AWARD_DOUBLEKILL;
        }
    }

    // For each player, choose which two awards to actually give them.
    // Note that the first award checked is quad kill, but after that the awards
    // are checked randomly. So if a player has quad kill they'll definitely see
    // it on the endscreen, but this is not the case for triple kill or any
    // other awards.
    for (i = 0; i < player_count; i++)
    {
#ifdef GEVR
        if (netIsActive() && !netSlotOccupied(i)) continue;
#endif
        s32 numdone = 0;
        s32 awardindex = 16;

        while (numdone == 0)
        {
            if (metrics[i].awards & (1 << awardindex))
            {
                metrics[i].awards &= ~(1 << awardindex);
                g_playerPointers[i]->ptr_text_first_mp_award = (char *) langGet(g_AwardNames[awardindex]);
                numdone = 1;
            }

            if (metrics[i].awards == 0)
            {
                numdone = 1;
            }

            awardindex = randomGetNext() % 17;
        }

        while (numdone < 2)
        {
            awardindex = randomGetNext() % 17;

            if (metrics[i].awards & (1 << awardindex))
            {
                metrics[i].awards &= ~(1 << awardindex);
                g_playerPointers[i]->ptr_text_second_mp_award = (char *) langGet(g_AwardNames[awardindex]);
                numdone = 2;
            }

            if (metrics[i].awards == 0)
            {
                numdone = 2;
            }
        }
    }
}


void mpwatchMenuTick(void)
{
    s32 player_num;
    s32 player_count;
    s32 x_centered;
    s32 menu_count;
    s32 i;

    player_num = get_cur_playernum();
    player_count = getPlayerCount();
    x_centered = joyGetStickXInRange(player_num, -2, 1);

    // The player in shuffled position 0 drives down g_gameOverFlag which is both a flag and a timer.
    if (!get_player_position_in_shuffled(player_num) && (g_gameOverFlag >= 2))
    {
        g_gameOverFlag -= speedgraphframes;

        if (g_gameOverFlag <= 0) 
        { 
            g_gameOverFlag = 1;
        }
    }

    if (player_count != 1)
    {
        // If a player has their pause menu up when they die and the game isn't over, turn their menu off. 
        if ((g_CurrentPlayer->bonddead) && (!g_gameOverFlag))
        {
            g_CurrentPlayer->mpmenuon = FALSE;
            g_CurrentPlayer->healthdisplaytime = 0;
            return;
        }

        if (g_gameOverFlag < 2)
        {
            if (get_player_position_in_shuffled(player_num) == 0)
            {
                chevron_glow_timer += speedgraphframes;
                alt_gameover_msg_timer += speedgraphframes;

                // Toggle chevron glow
                if (chevron_glow_timer >= (PAL ? 16 : 20))
                {
                    chevron_glow_timer -= (PAL ? 16 : 20);
                    chevron_glow = !chevron_glow;
                }

                // Flip between "GAME OVER" and "START TO EXIT" text.
                if (alt_gameover_msg_timer >= (PAL ? 100 : 120))
                {
                    alt_gameover_msg_timer -= (PAL ? 100: 120);
                    alt_gameover_msg = !alt_gameover_msg;
                }
            }

            if (g_playerPerm->most_killed_one_life < g_CurrentPlayer->kills_this_life)
            {
                g_playerPerm->most_killed_one_life = g_CurrentPlayer->kills_this_life;
            }

            if (g_playerPerm->longest_inning < (getMissiontimer() - g_CurrentPlayer->lifestarttime60))
            {
                g_playerPerm->longest_inning = getMissiontimer() - g_CurrentPlayer->lifestarttime60;
            }

            if (g_CurrentPlayer->mpmenuon != FALSE)
            {
#ifdef GEVR
                gevrMenuPagesTick(player_num);
#endif
                if (mpwatchIsPlayerPressingRight(player_num) && mpwatchMenuCanGoRight())
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpmenumode++;
#ifdef GEVR
                    if (g_CurrentPlayer->mpmenumode == MENU_LOBBY && !netIsActive()) g_CurrentPlayer->mpmenumode++;
#endif
                }
                else if (mpwatchIsPlayerPressingLeft(player_num) && mpwatchMenuCanGoLeft())
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpmenumode--;
#ifdef GEVR
                    if (g_CurrentPlayer->mpmenumode == MENU_LOBBY && !netIsActive()) g_CurrentPlayer->mpmenumode--;
#endif
                }
                else if (mpwatchIsPlayerPressingRight(player_num) && (g_CurrentPlayer->mpmenumode == MENU_EXIT_CONFIRM))
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpquitconfirm = 1;
                }
                else if (mpwatchIsPlayerPressingLeft(player_num) && (g_CurrentPlayer->mpmenumode == MENU_EXIT_CONFIRM))
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpquitconfirm = 0;
                }
#ifdef GEVR
                else if (!netIsActive() && joyGetButtonsPressedThisFrame(player_num, A_BUTTON) && (g_CurrentPlayer->mpmenumode == MENU_PAUSE))
#else
                else if (joyGetButtonsPressedThisFrame(player_num, A_BUTTON) && (g_CurrentPlayer->mpmenumode == MENU_PAUSE))
#endif
                {
                    mpwatchPlayBeep();
                    if (!g_pausedFlag)
                    {
                        g_pausedFlag = 1;
                        who_paused = get_cur_playernum();
                        lvlSetControlsLockedFlag(1);
                    }
                    else if (get_cur_playernum() == who_paused)
                    {
                        g_pausedFlag = 0;
                        lvlSetControlsLockedFlag(0);
                    }
                }
                else if (g_CurrentPlayer->mpmenumode == MENU_FINISHED)
                {
                    if (joyGetButtonsPressedThisFrame(player_num, B_BUTTON))
                    {
                        mpwatchPlayBeep();
                        g_CurrentPlayer->mpmenuon = TRUE;
                        g_CurrentPlayer->mpmenumode = MENU_SCORES;
                    }
                }
#ifdef GEVR
                else if (netIsActive() && g_CurrentPlayer->mpmenumode == MENU_LOBBY &&
                         joyGetButtonsPressedThisFrame(player_num, A_BUTTON | B_BUTTON | START_BUTTON))
                {
                    /* A fires the selected action row; B or START closes the menu (not at the results) */
                    if (joyGetButtonsPressedThisFrame(player_num, A_BUTTON))
                    {
                        if (player_num == netGetLocalSlot()) gevrMenuPageAction();
                    }
                    else if (!g_gameOverFlag)
                    {
                        mpwatchPlayBeep();
                        g_CurrentPlayer->mpmenuon = FALSE;
                        g_CurrentPlayer->healthdisplaytime = (PAL ? 50 : 60);
                        if (get_cur_playernum() == who_paused)
                        {
                            g_pausedFlag = 0;
                            lvlSetControlsLockedFlag(0);
                        }
                    }
                }
#endif
                else if (((joyGetButtonsPressedThisFrame(player_num, A_BUTTON | START_BUTTON)) && ((((g_CurrentPlayer->mpmenumode != MENU_EXIT)) && (g_CurrentPlayer->mpmenumode != MENU_EXIT_CONFIRM)) || ((g_CurrentPlayer->mpmenumode == MENU_EXIT_CONFIRM) && (g_CurrentPlayer->mpquitconfirm != 1)))) || (joyGetButtonsPressedThisFrame(player_num, B_BUTTON)))
                {
                    mpwatchPlayBeep();

                    if (g_gameOverFlag)
                    {
#ifdef GEVR
                        if (netIsActive()) {
                            /* The host: A or START goes on to the next match (a
                             * countdown the LOBBY page votes through), B returns
                             * everyone to the lobby. The others wait; the header
                             * says so. Everyone stays on the results meanwhile. */
                            if (netIsHost() && player_num == netGetLocalSlot()) {
                                if (joyGetButtonsPressedThisFrame(player_num, B_BUTTON)) netHostReturnToLobby();
                                else netHostContinue();
                            }
                            return;
                        }
#endif
                        menu_count = 0;
                        g_CurrentPlayer->mpmenumode = MENU_FINISHED;

                        for (i = 0; i < player_count; i++)
                        {
                            if (g_playerPointers[i]->mpmenumode == MENU_FINISHED)
                            {
                                menu_count++;
                            }
                        }

                        if (menu_count == player_count)
                        {
                            bossSetLoadedStage(LEVELID_TITLE);
                        }
                    }
                    else
                    {
                        g_CurrentPlayer->mpmenuon = FALSE;
                        g_CurrentPlayer->healthdisplaytime = (PAL ? 50 : 60);

                        if (get_cur_playernum() == who_paused)
                        {
                            g_pausedFlag = 0;
                            lvlSetControlsLockedFlag(0);
                        }
                    }
                }
                else if ((joyGetButtonsPressedThisFrame(player_num, A_BUTTON | START_BUTTON)) && (g_CurrentPlayer->mpmenumode == MENU_EXIT))
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpmenumode = MENU_EXIT_CONFIRM;
                    g_CurrentPlayer->mpquitconfirm = 0;
                }
                else if (joyGetButtonsPressedThisFrame(player_num, A_BUTTON | START_BUTTON))
                {
                    if ((g_CurrentPlayer->mpmenumode == MENU_EXIT_CONFIRM) && (g_CurrentPlayer->mpquitconfirm == 1))
                    {
                        mpwatchPlayBeep();
#ifdef GEVR
                        if (netIsActive()) {
                            gevrLobbySessionStopped();
                            gevrRestartToLauncher();
                            return;
                        }
#endif
                        g_CurrentPlayer->mpmenuon = FALSE;
                        g_CurrentPlayer->healthdisplaytime = 0;
                        mpCalculateAwards(FALSE);
                    }
                }

                if ((x_centered == 0) || (x_centered == -1))
                {
                    g_CurrentPlayer->mpjoywascentre = 1;
                    return;
                }

                g_CurrentPlayer->mpjoywascentre = 0;
                return;
            }

            if (joyGetButtonsPressedThisFrame(player_num, START_BUTTON))
            {
                mpwatchPlayBeep();
                g_CurrentPlayer->mpmenuon = TRUE;
                g_CurrentPlayer->mpmenumode = MENU_SCORES;
                g_CurrentPlayer->mpjoywascentre = 1;
                g_CurrentPlayer->apparenthealth = g_CurrentPlayer->bondhealth;
                g_CurrentPlayer->apparentarmour = g_CurrentPlayer->bondarmour;
            }
        }
    }
}


Gfx *display_text_for_playerdata_on_MP_menu(Gfx *gdl, s32 x, s32 y, s32 points, TEXTCOLORS text_color) {

    s32 textX;
    s32 textY;
    s32 textwidth;
    s32 textheight;
    s32 unused;
    char text[32];
    s16 viX;
    s32 viY;

    snprintf(text, sizeof(text), "%d", points);

    textMeasure(&textheight, &textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);

    textX = x - (textwidth >> 1);
    textY = y;

    switch (text_color) 
    {
        case GREEN_NORMAL:
            viX = viGetX();
            viY = viGetY();
            gdl = textRender(gdl, &textX, &textY, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xFF00B0, viX, viY, 0, 0);
            break;

        case GREEN_HIGHLIGHT:
            viX = viGetX();
            viY = viGetY();
            gdl = textRenderOutlined(gdl, &textX, &textY, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xA0FFA0F0, 0x7000A0, viX, viY, 0, 0);
            break;

        case RED_NORMAL:
            viX = viGetX();
            viY = viGetY();
            gdl = textRender(gdl, &textX, &textY, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xFF4040B0, viX, viY, 0, 0);
            break;

        case RED_HIGHLIGHT:
            viX = viGetX();
            viY = viGetY();
            gdl = textRenderOutlined(gdl, &textX, &textY, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xFFA0A0F0, 0x700000A0, viX, viY, 0, 0);
            break;

        case BLUE_NORMAL:
            viX = viGetX();
            viY = viGetY();
            gdl = textRender(gdl, &textX, &textY, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x4040FFB0, viX, viY, 0, 0);
            break;

        case BLUE_HIGHLIGHT:
            viX = viGetX();
            viY = viGetY();
            gdl = textRenderOutlined(gdl, &textX, &textY, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xA0A0FFF0, 0x70A0, viX, viY, 0, 0);
            break;
    }

    return gdl;
}


//rodata
/*8005BC20*/
const char ascii_MP_watch_menu_BLANK[] = "";
const char ascii_MP_watch_menu_left_chevron[] = "<";
const char ascii_MP_watch_menu_right_chevron[] = ">";
const char ascii_pnum_KILLS[] = "%s%d %s";
const char ascii_pnum_LOSSES[] = "%s%d %s";


s32 get_points_for_mp_player(s32 playernum)
{
    s32 team_or_token;
    s32 player_count;
    s32 i;
    s32 j;
    s32 points;

    // The have_token_or_goldengun field is also treated as a team identifier.
    team_or_token = g_playerPlayerData[playernum].have_token_or_goldengun;

    player_count = getPlayerCount();

    points = 0;

    switch (get_scenario())
    {
        case SCENARIO_NORMAL:
        case SCENARIO_MWTGG:
        case SCENARIO_LTK:
            for (i = 0; i < player_count; i++)
            {
                if (i != playernum)
                {
                    points += g_playerPlayerData[playernum].kill_counts[i];
                }
                else
                {
                    points -= g_playerPlayerData[i].kill_counts[playernum];
                }
            }

            points += g_playerPlayerData[playernum].kill_count;

            points += g_playerPlayerData[playernum].killed_gg_owner_count * (netMpPlayerCount(player_count) - 2);
            break;

        case SCENARIO_YOLT:
            points = MAX_PLAYER_COUNT - g_playerPlayerData[playernum].order_out_in_yolt;
            break;

        case SCENARIO_TLD:
            points = g_playerPlayerData[playernum].flag_counter;
            break;
            
        case SCENARIO_2v2:
        case SCENARIO_3v1:
        case SCENARIO_2v1:
            for (i = 0; i < player_count; i++)
            {
                if (g_playerPlayerData[i].have_token_or_goldengun == team_or_token)
                {
                    for (j = 0; j < player_count; j++)
                    {
                        if (g_playerPlayerData[j].have_token_or_goldengun != team_or_token)
                        {
                            points += g_playerPlayerData[i].kill_counts[j];
                        }
                        else
                        {
                            points -= g_playerPlayerData[i].kill_counts[j];
                        }
                    }
                }
            }
            break;

        default:
            break;
    }

    return points;
}


void write_playerrank_to_buffer(char *buffer, s32 playernum)
{
    s32 scenario;
    s32 count;
    s32 scores[4];
    s32 players[4];
    s32 tmp;
    s32 i;
    s32 j;

    scenario = get_scenario();
    count = getPlayerCount();

    for (i = 0; i < count; i++)
    {
        scores[i] = get_points_for_mp_player(i);
        players[i] = i;
    }

    for (j = 0; j < count; j++)
    {
        for (i = 0; i < (count - 1); i++)
        {
            if (scores[i] < scores[i + 1])
            {
                tmp = scores[i + 1];
                scores[i + 1] = scores[i];
                scores[i] = tmp;
                tmp = players[i + 1];
                players[i + 1] = players[i];
                players[i] = tmp;
            }
        }

    }

    for (i = 0; i < count; i++)
    {
        if (playernum == players[i])
        {
            break;
        }
    }

    for (j = 0; j <= i; j++)
    {
        if (scores[j] == scores[i])
        {
            break;
        }
    }

    switch (j)
    {
        case 0:
            snprintf(buffer, 64, "%s", langGet(getStringID(LMPMENU, MPMENU_STR_11_RANK1ST))); /* Rank: 1st */
            break;
        case 1:
            snprintf(buffer, 64, "%s", langGet(getStringID(LMPMENU, MPMENU_STR_12_RANK2ND))); /* Rank: 2nd */
            break;
        case 2:
            if ((scenario != SCENARIO_2v2) && (scenario != SCENARIO_2v1))
            {
                snprintf(buffer, 64, "%s", langGet(getStringID(LMPMENU, MPMENU_STR_13_RANK3RD))); /* Rank: 3rd */
            }
            else
            {
                snprintf(buffer, 64, "%s", langGet(getStringID(LMPMENU, MPMENU_STR_12_RANK2ND))); /* Rank: 2nd */
            }
            break;
        case 3:
            if (scenario != SCENARIO_3v1)
            {
                snprintf(buffer, 64, "%s", langGet(getStringID(LMPMENU, MPMENU_STR_14_RANK4TH))); /* Rank: 4th */
            }
            else
            {
                snprintf(buffer, 64, "%s", langGet(getStringID(LMPMENU, MPMENU_STR_12_RANK2ND))); /* Rank: 2nd */
            }
            break;
    }

}


s32 mpwatchShouldDisplayRank(s32 param_1)
{
    switch(get_scenario())
    {
        case SCENARIO_NORMAL:
        case SCENARIO_TLD:
        case SCENARIO_MWTGG:
        case SCENARIO_LTK:
        case SCENARIO_2v2:
        case SCENARIO_3v1:
        case SCENARIO_2v1:
            return 1;
        case SCENARIO_YOLT:
            return param_1 ? 0 : 1;
        default:
#ifdef DEBUG
            osSyncPrintf("Invalid scenario %d!", get_scenario());
#endif
        do {} while (1);
    }
}

s32 mpwatchShouldDisplayScore(s32 param_1)
{
    switch(get_scenario())
    {
        case SCENARIO_NORMAL:
        case SCENARIO_MWTGG:
        case SCENARIO_LTK:
        case SCENARIO_2v2:
        case SCENARIO_3v1:
        case SCENARIO_2v1:
            return 1;
        break;
        case SCENARIO_YOLT:
        case SCENARIO_TLD:
            return 0;
        break;
        default:
#ifdef DEBUG
            osSyncPrintf("Invalid scenario %d!", get_scenario());
#endif
            do {} while (1);
    }
}


/**
 * Draws the text for the multiplayer pause menu, post-game screens, and the "press start" prompt shown after death.
 * 
 * When the match is running it works through a horizontal carousel of pages starting at the "Score" page.
 * Losses << Kills << Score >> Pause >> Exit
 * 
 * When the game is over it displays these screens:
 * WOC/Awards << Losses << Kills << Score
 *
 * With the menu closed, nothing is drawn unless the player is dead and their
 * death animation has finished, in which case the continue prompt is centred
 * in the viewport. That's suppressed in YOLT once two of the player's lives are
 * gone since there is nothing to continue to.
 *
 * @param gdl display list to append to
 * @return the advanced display list pointer
 */
Gfx *mp_watch_menu_display(Gfx *gdl)
{
    s32 curplayernum;
    s32 player_count;
    s32 x;
    s32 y;
    s32 k;
    s32 textwidth;
    s32 textheight;
    s32 m;
    s32 h1;
    s32 h2;
    char rankbuffer[64];
    s32 two_player_x_offset;
    s32 menu_top;
    char *text;
    s32 scores[4];
    s32 i;
    TEXTCOLORS current_colour;
    TEXTCOLORS same_team_colour;
    TEXTCOLORS other_team_colour;
    MPSCENARIOS scenario;
    s32 fav_textheight;
    s32 fav_textwidth;
    s32 fav_x_offset;
    s32 x3;
    s32 y3;
    s32 textwidth3;
    s32 textheight3;
    char *text3;
    s32 self_paused;
    s32 total_kills_against_current;
    s16 x2;
    s16 viewleft;
    s32 colour;
    s32 q;
 
    curplayernum = get_cur_playernum();
    player_count = getPlayerCount();
    self_paused = 0;
 
    if (player_count == 1)
    {
        return gdl;
    }
 
    if (g_CurrentPlayer->mpmenuon)
    {
        gdl = microcode_constructor(gdl);
        menu_top = viGetViewTop();

#ifdef GEVR
        if (netIsActive())
        {
            /* Online uses four logical slots but one full-width view. The
             * original watch layout occupies a 160px split-screen region;
             * centre it in the view in both stereo and 2D mode. */
            two_player_x_offset = (viGetViewWidth() - 160) / 2;
            if (two_player_x_offset < 0) two_player_x_offset = 0;
            menu_top += (viGetViewHeight() - 150) / 2;
            if (menu_top < viGetViewTop()) menu_top = viGetViewTop();
        }
        else
#endif
        if (player_count == 2)
        {
            two_player_x_offset = 80;
        }
        else
        {
            two_player_x_offset = 0;
        }

        switch (g_CurrentPlayer->mpmenumode)
        {
            case MENU_GOWOC:
            case MENU_LOSSES:
            case MENU_KILLS:
            case MENU_SCORES:
                if (!g_gameOverFlag)
                {
                    text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_15_PLAY)); /* PLAY */
                }
#ifdef GEVR
                else if (netIsActive() && (netCountdownSecondsLeft() > 0 || !alt_gameover_msg))
                {
                    /* online: the countdown to the next match, or whose move it is */
                    static char gameover[32];
                    if (netCountdownSecondsLeft() > 0)
                    {
                        snprintf(gameover, sizeof(gameover), "NEXT MATCH IN %d", netCountdownSecondsLeft());
                    }
                    else
                    {
                        strcpy(gameover, netIsHost() ? "A:CONTINUE  B:LOBBY" : "WAITING FOR HOST");
                    }
                    text = gameover;
                }
#endif
                else
                {
                    if (alt_gameover_msg)
                    {
                        text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_16_GAMEOVER)); /* GAME OVER */
                    }
                    else
                    {
                        text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_17_STARTTTOEXIT)); /* START TO EXIT */
                    }
                }
                break;
            case MENU_FINISHED:
                text = (char *) ascii_MP_watch_menu_BLANK;
                break;
#ifdef GEVR
            case MENU_LOBBY:
                text = (char *) "LOBBY";
                break;
#endif
            case MENU_PAUSE:
                if (g_pausedFlag)
                {
                    text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_18_PAUSED)); /* PAUSED */
                    if (get_cur_playernum() == who_paused)
                    {
                        self_paused = 1;
                    }
                }
                else
                {
                    text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_19_PAUSE)); /* PAUSE */
                }
                break;
            case MENU_EXIT:
            case MENU_EXIT_CONFIRM:
                text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_1A_EXIT)); /* EXIT */
                x = (viGetViewLeft() + two_player_x_offset) + 65;
                break;
        }
 
        textMeasure(&textheight, &textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
        x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
        y = (menu_top - (textheight >> 1)) + (22 + MPMENU_YOFF);
 
        if (self_paused)
        {
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRenderOutlined(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xa0ffa0f0, 0x007000a0, viewleft, h1, 0, 0);
        }
        else
        {
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
        }
 
        if (mpwatchMenuCanGoLeft())
        {
            viewleft = viGetViewLeft();
    
            /**
             *  Colour is reused to store a pixel offset here. Efforts to introduce a new variable to store the offset resulted in stack problem I could not solve,
             *  so it's possible Rare really did reuse one variable for this. Perhaps it was not named "colour" but something more generic like "tmp."
             */ 
            colour = g_gameOverFlag ? 10 : 0, x = ((viewleft + two_player_x_offset) - colour) + 40;
    
            if (g_gameOverFlag)
            {
                x -= 8;
            }
 
            y = menu_top + (22 + MPMENU_YOFF);
 
            if (!chevron_glow)
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, (char *) ascii_MP_watch_menu_left_chevron, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
            else
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRenderOutlined(gdl, &x, &y, (char *) ascii_MP_watch_menu_left_chevron, ptrFontBankGothicChars, ptrFontBankGothic, 0xa0ffa0f0, 0x007000a0, viewleft, h1, 0, 0);
            }
        }
 
        if (mpwatchMenuCanGoRight())
        {
            viewleft = viGetViewLeft();

            // Colour is again used to store a pixel offset.
            colour = g_gameOverFlag ? 10 : 0, x = ((colour + 112) + viewleft) + two_player_x_offset;
 
            if (g_gameOverFlag)
            {
                x += 8;
            }
 
            y = menu_top + (22 + MPMENU_YOFF);
 
            if (!chevron_glow)
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, (char *) ascii_MP_watch_menu_right_chevron, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
            else
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRenderOutlined(gdl, &x, &y, (char *) ascii_MP_watch_menu_right_chevron, ptrFontBankGothicChars, ptrFontBankGothic, 0xa0ffa0f0, 0x007000a0, viewleft, h1, 0, 0);
            }
        }
 
        if ((g_CurrentPlayer->mpmenumode == MENU_SCORES) || (g_CurrentPlayer->mpmenumode == MENU_PAUSE))
        {
            if (player_count > 0)
            {
                i = 0;
 
                do
                {
                    scores[i] = get_points_for_mp_player(i);
                    i++;
                }
                while (i != player_count);
            }
 
            fav_x_offset = (g_gameOverFlag == 0); 
 
            if (fav_x_offset) 
            { 
                fav_x_offset = (g_stopPlayFlag == 0); 
            }
 
            if (mpwatchShouldDisplayRank(fav_x_offset))
            {
                write_playerrank_to_buffer(rankbuffer, curplayernum);
                textMeasure(&textheight, &textwidth, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0);
 
                x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
                y = (menu_top - (textheight >> 1)) + (37 + MPMENU_YOFF);
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }

            fav_x_offset = (g_gameOverFlag == 0); 
 
            if (fav_x_offset) 
            { 
                fav_x_offset = (g_stopPlayFlag == 0); 
            }
 
#ifdef GEVR
            if (netIsActive() || mpwatchShouldDisplayScore(fav_x_offset))
#else
            if (mpwatchShouldDisplayScore(fav_x_offset))
#endif
            {
                scenario = get_scenario();
                text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_1B_SCORES)); /* SCORES */
                textMeasure(&textheight, &textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
                y = (menu_top - (textheight >> 1)) + (53 + MPMENU_YOFF);
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
 
                if (((((scenario == SCENARIO_2v2) || (scenario == SCENARIO_3v1)) || (scenario == SCENARIO_2v1)) || (scenario == SCENARIO_TLD)) || (scenario == SCENARIO_MWTGG))
                {
                    if (g_playerPlayerData[curplayernum].have_token_or_goldengun == 0)
                    {
                        current_colour = RED_HIGHLIGHT;
                        same_team_colour = RED_NORMAL;
                        other_team_colour = BLUE_NORMAL;
                    }
                    else
                    {
                        current_colour = BLUE_HIGHLIGHT;
                        same_team_colour = BLUE_NORMAL;
                        other_team_colour = RED_NORMAL;
                    }
                }
                else
                {
                    current_colour = GREEN_HIGHLIGHT;
                    same_team_colour = GREEN_NORMAL;
                    other_team_colour = GREEN_NORMAL;
                }
 
#ifdef GEVR
                if (netIsActive())
                {
                    s32 row = 0;
                    for (i = 0; i < player_count; i++)
                    {
                        if (!netSlotOccupied(i)) continue;
                        char entry[32];
                        const char *name = netGetSlotName(i);
                        u32 row_colour;
                        if (!name || !name[0]) name = "Player";
                        snprintf(entry, sizeof(entry), "%s%s  %d",
                                 netVoiceSlotSpeaking((unsigned char)i) ? ">)) " : "",
                                 name, scores[i]);
                        x = (viGetViewLeft() + two_player_x_offset) + 53;
                        y = menu_top + (70 + MPMENU_YOFF) + row * 15;
                        viewleft = viGetX();
                        h1 = viGetY();
                        colour = i == curplayernum ? current_colour : same_team_colour;
                        /* TEXTCOLORS is an enum, not a packed RGBA value.
                         * Passing 0..5 to textRender made every row transparent. */
                        switch ((TEXTCOLORS)colour)
                        {
                            case RED_NORMAL: row_colour = 0xFF4040B0; break;
                            case RED_HIGHLIGHT: row_colour = 0xFFA0A0F0; break;
                            case BLUE_NORMAL: row_colour = 0x4040FFB0; break;
                            case BLUE_HIGHLIGHT: row_colour = 0xA0A0FFF0; break;
                            case GREEN_HIGHLIGHT: row_colour = 0xA0FFA0F0; break;
                            default: row_colour = 0x00FF00B0; break;
                        }
                        gdl = textRender(gdl, &x, &y, entry, ptrFontBankGothicChars,
                                         ptrFontBankGothic, row_colour, viewleft, h1, 0, 0);
                        row++;
                    }
                }
                else
#endif
                if (player_count == 2)
                {
                    viewleft = viGetViewLeft();
                    x2 = menu_top;

                    if (curplayernum == 0)
                    {
                        colour = current_colour;
                    }
                    else
                    {
                        q = g_playerPlayerData[0].have_token_or_goldengun == g_playerPlayerData[curplayernum].have_token_or_goldengun ? same_team_colour : other_team_colour;
                        colour = q;
                    }
 
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, x2 + (70 + MPMENU_YOFF), scores[0], colour);
                    viewleft = viGetViewLeft();
                    x2 = menu_top;

                    curplayernum == 1 ? (colour = current_colour) : (q = g_playerPlayerData[1].have_token_or_goldengun == g_playerPlayerData[curplayernum].have_token_or_goldengun ? same_team_colour : other_team_colour, colour = q);

                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, x2 + (86 + MPMENU_YOFF), scores[1], colour);
                }
                else
                {
                    viewleft = viGetViewLeft();
                    x2 = menu_top;
 
                    if (curplayernum == 0)
                    {
                        colour = current_colour;
                    }
                    else
                    {
                        q = g_playerPlayerData[0].have_token_or_goldengun == g_playerPlayerData[curplayernum].have_token_or_goldengun ? same_team_colour : other_team_colour;
                        colour = q;
                    }
 
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, x2 + (70 + MPMENU_YOFF), scores[0], colour);
                    viewleft = viGetViewLeft();
                    x2 = menu_top;
 
                    if (curplayernum == 1)
                    {
                        colour = current_colour;
                    }
                    else
                    {
                        q = g_playerPlayerData[1].have_token_or_goldengun == g_playerPlayerData[curplayernum].have_token_or_goldengun ? same_team_colour : other_team_colour;
                        colour = q;
                    }
 
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, x2 + (70 + MPMENU_YOFF), scores[1], colour);
                    viewleft = viGetViewLeft();
                    x2 = menu_top;
 
                    if (curplayernum == 2)
                    {
                        colour = current_colour;
                    }
                    else
                    {
                        q = g_playerPlayerData[2].have_token_or_goldengun == g_playerPlayerData[curplayernum].have_token_or_goldengun ? same_team_colour : other_team_colour;
                        colour = q;
                    }
 
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, x2 + (86 + MPMENU_YOFF), scores[2], colour);
 
                    if (player_count == 4)
                    {
                        viewleft = viGetViewLeft();
                        x2 = menu_top;

                        curplayernum == 3 ? (colour = current_colour) : (q = g_playerPlayerData[3].have_token_or_goldengun == g_playerPlayerData[curplayernum].have_token_or_goldengun ? same_team_colour : other_team_colour, colour = q);
                        gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, x2 + (86 + MPMENU_YOFF), scores[3], colour);
                    }
                }
            }

            // Keep colour live through this branch to reproduce the original register allocation.
            if (colour);
        }
        else if (g_CurrentPlayer->mpmenumode == MENU_KILLS)
        {
            fav_x_offset = (g_gameOverFlag == 0); 

            if (fav_x_offset) 
            { 
                fav_x_offset = (g_stopPlayFlag == 0); 
            } 

            if (mpwatchShouldDisplayRank(fav_x_offset))
            {
                write_playerrank_to_buffer(rankbuffer, curplayernum);
                textMeasure(&textheight, &textwidth, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
                y = (menu_top - (textheight >> 1)) + (37 + MPMENU_YOFF);
                viewleft = viGetX(); h1 = viGetY();
                gdl = textRender(gdl, &x, &y, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
 
            char *ptext = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_1C_P)); /* P */
            char *counttext;
 
            // Must remain a comma expression for matching
            counttext = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_1D_KILLS)), /* KILLS */
                sprintf(rankbuffer, ascii_pnum_KILLS, ptext, curplayernum + 1, counttext); /* -> "P<n> KILLS" */
 
            textMeasure(&textheight, &textwidth, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
            y = (menu_top - (textheight >> 1)) + (53 + MPMENU_YOFF);
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRender(gdl, &x, &y, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
#ifdef GEVR
            if (netIsActive())
            {
                s32 row = 0;
                for (i = 0; i < player_count; i++)
                {
                    if (!netSlotOccupied(i) || i == curplayernum) continue;
                    char entry[32];
                    snprintf(entry, sizeof(entry), "P%d  %d", i + 1,
                             g_playerPlayerData[curplayernum].kill_counts[i]);
                    x = (viGetViewLeft() + two_player_x_offset) + 53;
                    y = menu_top + (70 + MPMENU_YOFF) + row * 15;
                    viewleft = viGetX(); h1 = viGetY();
                    gdl = textRender(gdl, &x, &y, entry, ptrFontBankGothicChars,
                                     ptrFontBankGothic, GREEN_NORMAL, viewleft, h1, 0, 0);
                    row++;
                }
            }
            else
#endif
            if (player_count == 2)
            {
                if (curplayernum != 0)
                {
                    viewleft = viGetViewLeft(); h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[0], GREEN_NORMAL);
                }
                if (curplayernum != 1)
                {
                    viewleft = viGetViewLeft(); h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[1], GREEN_NORMAL);
                }
            }
            else
            {
                if (curplayernum != 0)
                {
                    viewleft = viGetViewLeft(); h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[0], GREEN_NORMAL);
                }
                if (curplayernum != 1)
                {
                    viewleft = viGetViewLeft(); h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[1], GREEN_NORMAL);
                }
                if (curplayernum != 2)
                {
                    viewleft = viGetViewLeft(); h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[2], GREEN_NORMAL);
                }
                if ((player_count == 4) && (curplayernum != 3))
                {
                    viewleft = viGetViewLeft(); h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[3], GREEN_NORMAL);
                }
            }
        }
        else if (g_CurrentPlayer->mpmenumode == MENU_LOSSES)
        {
            fav_x_offset = (g_gameOverFlag == 0); 

            if (fav_x_offset) 
            { 
                fav_x_offset = (g_stopPlayFlag == 0); 
            } 

            if (mpwatchShouldDisplayRank(fav_x_offset))
            {
                write_playerrank_to_buffer(rankbuffer, curplayernum);
                textMeasure(&textheight, &textwidth, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
                y = (menu_top - (textheight >> 1)) + (37 + MPMENU_YOFF);
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
 
            char *ptext = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_1C_P)); /* P */
            char *counttext;
 
            // Must remain a comma expression for matching.
            counttext = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_1E_LOSSES)), /* LOSSES */
                sprintf(rankbuffer, ascii_pnum_LOSSES, ptext, curplayernum + 1, counttext); /* -> "P<n> LOSSES" */
 
            textMeasure(&textheight, &textwidth, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
            y = (menu_top - (textheight >> 1)) + (53 + MPMENU_YOFF);
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRender(gdl, &x, &y, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0xff4040b0, viewleft, h1, 0, 0);
#ifdef GEVR
            if (netIsActive())
            {
                s32 row = 0;
                for (i = 0; i < player_count; i++)
                {
                    if (!netSlotOccupied(i)) continue;
                    s32 losses = g_playerPlayerData[i].kill_counts[curplayernum];
                    if (i == curplayernum && losses == 0) continue;
                    char entry[32];
                    snprintf(entry, sizeof(entry), "P%d  %d", i + 1, losses);
                    x = (viGetViewLeft() + two_player_x_offset) + 53;
                    y = menu_top + (70 + MPMENU_YOFF) + row * 15;
                    viewleft = viGetX(); h1 = viGetY();
                    gdl = textRender(gdl, &x, &y, entry, ptrFontBankGothicChars,
                                     ptrFontBankGothic, i == curplayernum ? RED_HIGHLIGHT : GREEN_NORMAL,
                                     viewleft, h1, 0, 0);
                    row++;
                }
            }
            else
#endif
            if (player_count == 2)
            {
                if (curplayernum != 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[0].kill_counts[curplayernum], GREEN_NORMAL);
                }
                else if (g_playerPlayerData[0].kill_counts[0] > 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[0].kill_counts[curplayernum], RED_HIGHLIGHT);
                }
                if (curplayernum != 1)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[1].kill_counts[curplayernum], GREEN_NORMAL);
                }
                else if (g_playerPlayerData[1].kill_counts[1] > 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[1].kill_counts[curplayernum], RED_HIGHLIGHT);
                }
            }
            else
            {
                if (curplayernum != 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[0].kill_counts[curplayernum], GREEN_NORMAL);
                }
                else if (g_playerPlayerData[0].kill_counts[0] > 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[0].kill_counts[curplayernum], RED_HIGHLIGHT);
                }

                if (curplayernum != 1)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[1].kill_counts[curplayernum], GREEN_NORMAL);
                }
                else if (g_playerPlayerData[1].kill_counts[1] > 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[1].kill_counts[curplayernum], RED_HIGHLIGHT);
                }

                if (curplayernum != 2)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[2].kill_counts[curplayernum], GREEN_NORMAL);
                }
                else if (g_playerPlayerData[2].kill_counts[2] > 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = menu_top;
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[2].kill_counts[curplayernum], RED_HIGHLIGHT);
                }

                if (player_count == 4)
                {
                    if (curplayernum != 3)
                    {
                        viewleft = viGetViewLeft(); 
                        h1 = menu_top;
                        gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[3].kill_counts[curplayernum], GREEN_NORMAL);
                    }
                    else if (g_playerPlayerData[3].kill_counts[3] > 0)
                    {
                        viewleft = viGetViewLeft(); 
                        h1 = menu_top;
                        gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[3].kill_counts[curplayernum], RED_HIGHLIGHT);
                    }
                }
            }
        }
        else if (g_CurrentPlayer->mpmenumode == MENU_GOWOC)
        {
            fav_x_offset = two_player_x_offset;

            if (player_count >= 3)
            {
                if (curplayernum & 1)
                {
                    fav_x_offset = two_player_x_offset - 7;
                }
                else
                {
                    fav_x_offset = two_player_x_offset + 7;
                }
            }
 
            text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_1F_WEAPONOFCHOICE)); /* Weapon of choice: */
            textMeasure(&fav_textheight, &fav_textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + fav_x_offset) - (fav_textwidth >> 1)) + 80;
            y = (menu_top - (fav_textheight >> 1)) + (37 + MPMENU_YOFF);
            viewleft = viGetX(); h1 = viGetY();
            gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            text = frontGetPlayersFavoriteWeaponInHand(curplayernum, 0);
            textMeasure(&fav_textheight, &fav_textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + fav_x_offset) - (fav_textwidth >> 1)) + 80;
            x2 = menu_top;
 
            if (j_text_trigger) 
            { 
                i = 4; 
            } 
            else 
            { 
                i = 0; 
            } 

            y = ((i + (u32) x2) - (fav_textheight >> 1)) + (53 + MPMENU_YOFF);
 
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
 
            if (g_CurrentPlayer->ptr_text_first_mp_award)
            {
                text = (char *) g_CurrentPlayer->ptr_text_first_mp_award;
                textMeasure(&fav_textheight, &fav_textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                x = ((viGetViewLeft() + fav_x_offset) - (fav_textwidth >> 1)) + 80;
                y = (menu_top - (fav_textheight >> 1)) + (75 + MPMENU_YOFF);
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }

            if (g_CurrentPlayer->ptr_text_second_mp_award)
            {
                text = (char *) g_CurrentPlayer->ptr_text_second_mp_award;
                textMeasure(&fav_textheight, &fav_textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                x = ((viGetViewLeft() + fav_x_offset) - (fav_textwidth >> 1)) + 80;
                y = (menu_top - (fav_textheight >> 1)) + (88 + MPMENU_YOFF);
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
        }

        if (g_CurrentPlayer->mpmenumode == MENU_EXIT_CONFIRM)
        {
            text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_20_CANCEL)); /* cancel */
            textMeasure(&textheight, &textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 54;
            y = (menu_top - (textheight >> 1)) + (54 + MPMENU_YOFF);
 
            if (g_CurrentPlayer->mpquitconfirm == 0)
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRenderOutlined(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xa0ffa0f0, 0x007000a0, viewleft, h1, 0, 0);
            }
            else
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
 
            text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_21_CONFIRM)); /* confirm */
            textMeasure(&textheight, &textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 104;
            y = (menu_top - (textheight >> 1)) + (54 + MPMENU_YOFF);
 
            if (g_CurrentPlayer->mpquitconfirm == 1)
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRenderOutlined(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xa0ffa0f0, 0x007000a0, viewleft, h1, 0, 0);
            }
            else
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
        }
#ifdef GEVR
        gdl = gevrMenuPagesDraw(gdl, menu_top, two_player_x_offset);
#endif
        gdl = combiner_bayer_lod_perspective(gdl);
    }
    else if (((((g_CurrentPlayer->bonddead) && (g_CurrentPlayer->deathanimfinished)) && (g_CurrentPlayer->redbloodfinished)) && (!g_stopPlayFlag)) && (!g_gameOverFlag))
    {
        total_kills_against_current = 0;
 
        for (m = 0; m < player_count; m++)
        {
            total_kills_against_current += g_playerPlayerData[m].kill_counts[curplayernum];
        }
 
        if ((get_scenario() != SCENARIO_YOLT) || (total_kills_against_current < 2))
        {
            gdl = bgScissorCurrentPlayerViewDefault(gdl);
            gdl = microcode_constructor(gdl);
            text3 = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_22_PRESSSTART_LF)); /* press start */
            textMeasure(&textheight3, &textwidth3, text3, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x2 = viGetViewLeft();
            x3 = (x2 + (viGetViewWidth() >> 1)) - (textwidth3 >> 1);
            x2 = viGetViewTop();
            y3 = (x2 + (viGetViewHeight() >> 1)) - (textheight3 >> 1);
#ifndef VERSION_US
            gdl = microcode_constructor_related_to_menus(gdl, x3 - 1, y3 - 1, x3 + textwidth3 + 1, y3 + textheight3 + 1, 0);
#endif
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRender(gdl, &x3, &y3, text3, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            gdl = combiner_bayer_lod_perspective(gdl);
        }
    }
 
    return gdl;
}


s32 mpwatchShouldDisplayGauges(void)
{
    return g_gameOverFlag ? FALSE : (g_CurrentPlayer->mpmenuon | (g_CurrentPlayer->healthdisplaytime > 0));
}


s32 checkGamePaused(void) 
{
    return g_pausedFlag;
}
