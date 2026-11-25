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
extern int netLocalSlotCount(void);
extern int netLocalSeatOfSlot(int slot);
extern int gevrNetSlotIsLocalHuman(int slot);
extern void netLobbySetCharacterSlot(int slot, u8 chr_id);
void gevrCharSelOpen(s32 slot, s32 dropin);
void gevrStageSelOpen(s32 slot, s32 vote);
void gevrMpOptionsOpen(s32 slot, s32 from);
s32 gevrMpOptionsAvailable(s32 slot);
extern void netLobbySetLoadoutSlot(int slot, const u8 items[4]);
extern void netLobbySetTeamSlot(int slot, u8 team);
extern void netSetVoteSlot(int slot, int kind, int choice);
extern void netLobbySetReadySlot(int slot, bool ready);
extern bool netRequestRemoveLocalGuest(int slot);
extern void gevrNativePauseOpenPanel(void);
extern int netSlotReady(int slot);
extern int VrAlly;
extern int VrCampaign;
extern int gevrNativePauseOpen(void);
extern int netHordeActive(void);
extern int netInMatchLobby(void);
extern int netLobbyRuleOn(void);
extern int netHostCanEnterLobby(void);
extern int netLobbyReadyCount(int *humans);
extern int netSlotOfLocalSeat(int seat);
extern int netGetHostSlot(void);
extern bool netIsDedicated(void);
void viSetViewSize(s16 x, s16 y);          /* fr.h */
void viSetViewPosition(s16 left, s16 top);
static s32 gevrLobbySeatCount(void);
static s32 s_lobbyActing = -1;   /* the pre-match lobby's seat being ticked or drawn (gevrLobbyStageTick) */
static s32 menuActingSlot(void)
{
    s32 cur = get_cur_playernum();
    if (s_lobbyActing >= 0) return s_lobbyActing;   /* the title stage has no current player */
    /* Horde's split screen: the pause window over the views is the first player's (input.c) */
    if (netHordeActive() && netLocalSlotCount() > 1 && gevrNativePauseOpen()) return netGetLocalSlot();
    return gevrNetSlotIsLocalHuman(cur) ? cur : netGetLocalSlot();
}
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
typedef struct { float x, y; } GevrVec2;   /* XrVector2f's layout */
extern bool get_2d_input(int hand, const char *input_name, GevrVec2 *value);
extern float VrSfxVolume;
extern void gevrSndApplySfxVolume(u16 volume);
extern int netVoiceIsMuted(void);
extern void netVoiceSetMuted(int muted);
extern int netVoiceHasPermission(void);
extern int netVoiceCaptureFailed(void);
#include "net_game.h"
#include "gevr_mpoptions.h"
#include "assets/obseg/text/LtitleE.h"
#include "../../port/vr/gevr_pause_menu.h"
#include "glass.h"
#include <stdlib.h>
#include <string.h>
void mpwatchPlayBeep(void);
s32 mpwatchIsPlayerPressingRight(s32 player);
extern int netMpPlayerCount(int fallback);
extern int netGetConnectedPlayerCount(void);
/* The online score and kill tables' row pitch: the original 15 pixels up to
 * four players, closer past four so eight rows fit the watch's panel. */
static s32 gevrScoreRowStep(void)
{
    s32 occupied = 0;
    s32 slot;

    for (slot = 0; slot < MAX_PLAYER_COUNT; slot++)
    {
        if (netSlotOccupied(slot)) occupied++;
    }
    return occupied > 4 ? 12 : 15;
}
extern int netGetPlayingCount(void);
extern int netGetPhase(void);
extern void netSetLocalVote(int kind, int idx);
extern int netGetVote(int kind, int slot);
extern void netLobbySetCharacter(u8 chr_id);
extern void netLobbySetLoadout(const u8 items[4]);
extern unsigned long long VrMpFavStages;
extern unsigned VrMpFavSets;
extern s32 g_gameOverFlag;
enum { NET_SCENARIO_YOLT = 1, NET_SCENARIO_TLD = 2, NET_SCENARIO_MWTGG = 3 };
enum { NET_WEAPON_SET_CUSTOM_ROW = 14, NET_NEXT_VOTE_ROW = 0 };

/*
 * The pause menu's pages of rows: PAUSE holds the audio, LOBBY the match
 * (the settings the host changes, the votes, this player's character and
 * guns, the host's START MATCH and RETURN TO LOBBY). The left stick moves
 * the cursor up and down (the right stick click still steps it down), the
 * right stick changes the value in any direction (repeating after 18
 * ticks), A fires an action row. The others see a host-only row dim.
 * (User, 2026-09-30: a click that only went down was no way to navigate.)
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
    s32 (*shows)(const GevrMenuRow *r);   /* the rows this page draws of its table; NULL: every one */
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
static void rowVoiceModeValue(char *b,s32 n) { snprintf(b,n,"%s",netVoiceModeName(gevrNetConfigGet(CFG_VOICE_MODE))); }
static void rowVoiceModeStep(s32 dir) { gevrNetConfigSet(CFG_VOICE_MODE,gevrCycled(gevrNetConfigGet(CFG_VOICE_MODE),dir,2)); }
static s32 friendlyFireTeams(void) { return netScenarioHasTeams(gevrNetConfigGet(CFG_SCENARIO)); }
static void rowFriendlyFireValue(char *b,s32 n) { snprintf(b,n,"%s",gevrNetConfigGet(CFG_FRIENDLY_FIRE) ? "ON" : "OFF"); }
static void rowFriendlyFireStep(s32 dir) { gevrNetConfigSet(CFG_FRIENDLY_FIRE,gevrCycled(gevrNetConfigGet(CFG_FRIENDLY_FIRE),dir,2)); }
static s32 rowEqualizationHost(void) { return netIsHost(); }
static void rowEqualizationValue(char *b,s32 n) { snprintf(b,n,"%s",netGetHostEqualization(NULL) ? "ON" : "OFF"); }
static void rowEqualizationStep(s32 dir) { unsigned cap; int on=netGetHostEqualization(&cap);netSetHostEqualization(!on,cap); }
static void rowEqualizationCapValue(char *b,s32 n) { unsigned cap;netGetHostEqualization(&cap);snprintf(b,n,"%ums",cap); }
static void rowEqualizationCapStep(s32 dir) {
    const unsigned caps[]={20,40,50,60,80}; unsigned cap;int on=netGetHostEqualization(&cap),i=0;
    for(int k=0;k<5;k++) if(cap>=caps[k]) i=k;
    netSetHostEqualization(on,caps[gevrCycled(i,dir,5)]);
}
static const GevrMenuRow s_pauseRows[] = {
    { "HOST EQUALIZATION", GEVR_ROW_VALUE, 1, rowEqualizationHost, rowEqualizationValue, rowEqualizationStep, "R-STICK:ON/OFF" },
    { "HOST DELAY CAP", GEVR_ROW_VALUE, 1, rowEqualizationHost, rowEqualizationCapValue, rowEqualizationCapStep, "R-STICK:PICK" },
    { "MUSIC", GEVR_ROW_VALUE, 0, NULL, rowMusicValue, rowMusicStep, "R-STICK:ADJ" },
    { "SFX",   GEVR_ROW_VALUE, 0, NULL, rowSfxValue,   rowSfxStep,   "R-STICK:ADJ" },
    { "VOICE", GEVR_ROW_VALUE, 0, NULL, rowVoiceValue, rowVoiceStep, "R-STICK:ADJ" },
    { "VOICE MODE", GEVR_ROW_VALUE, 1, NULL, rowVoiceModeValue, rowVoiceModeStep, "R-STICK:PICK" },
    { "FRIENDLY FIRE", GEVR_ROW_VALUE, 1, friendlyFireTeams, rowFriendlyFireValue, rowFriendlyFireStep, "R-STICK:ON/OFF" },
    { "MIC",   GEVR_ROW_VALUE, 0, NULL, rowMicValue,   rowMicStep,   "R-STICK:ON/OFF" },
};

/* ---- LOBBY: the match ---- */
static s32 lobbyVoting(void) { return gevrNetConfigGet(CFG_NEXT_ROUND) == NET_NEXT_VOTE_ROW; }
static s32 lobbyNotGoldenGun(void) { return gevrNetConfigGet(CFG_SCENARIO) != NET_SCENARIO_MWTGG; }
static s32 lobbyNotYolt(void) { return gevrNetConfigGet(CFG_SCENARIO) != NET_SCENARIO_YOLT; }
static s32 lobbyCustomSet(void) { return lobbyNotGoldenGun() && gevrNetConfigGet(CFG_WEAPON_SET) == NET_WEAPON_SET_CUSTOM_ROW; }
static s32 lobbyWeaponVote(void) { return lobbyVoting() && lobbyNotGoldenGun(); }
static s32 lobbyLoadouts(void) { return gevrNetConfigGet(CFG_LOADOUTS) != 0; }
static s32 lobbyCanStart(void) { return netHostCanStartRound(); }
/* a round, a countdown (the lobby's too: there RETURN cancels it), or the Lobby rule's practice */
static s32 lobbyCanReturn(void) { return netIsHost() && (netGetPhase() == 2 || netCountdownSecondsLeft() > 0 || netHostCanEnterLobby()); }

/* the vote rows: this player's pick and how many share it, or the host's mode */
static void lobbyVoteValue(char *b, s32 n, s32 kind, const char *(*name)(int))
{
    s32 mode = gevrNetConfigGet(CFG_NEXT_ROUND);
    if (mode != NET_NEXT_VOTE_ROW)
    {
        snprintf(b, n, "%s", netNextRoundName(mode));
        return;
    }
    s32 vote = netGetVote(kind, menuActingSlot());
    s32 tally = 0;
    s32 slot;
    for (slot = 0; slot < MAX_PLAYER_COUNT; slot++)
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
    s32 me = menuActingSlot();
    s32 vote = netGetVote(0, me);
    s32 count = netStageCount();
    s32 tries = count + 1;
    if (!lobbyVoting()) return;
    do
    {
        vote += dir;
        if (vote >= count) vote = -1;
        if (vote < -1) vote = count - 1;
    } while (vote >= 0 && !netStageEligible(vote) && --tries > 0);
    netSetVoteSlot(me, 0, vote);
}
static void rowNextWeaponsValue(char *b, s32 n) { lobbyVoteValue(b, n, 1, netWeaponSetName); }
static void rowNextWeaponsStep(s32 dir)
{
    s32 me = menuActingSlot();
    s32 vote = netGetVote(1, me);
    s32 count = netWeaponSetCount();
    if (!lobbyVoting()) return;
    vote += dir;
    if (vote >= count) vote = -1;
    if (vote < -1) vote = count - 1;
    netSetVoteSlot(me, 1, vote);
}
static void rowNextRoundValue(char *b, s32 n) { snprintf(b, n, "%s", netNextRoundName(gevrNetConfigGet(CFG_NEXT_ROUND))); }
static void rowNextRoundStep(s32 dir) { gevrNetConfigSet(CFG_NEXT_ROUND, gevrCycled(gevrNetConfigGet(CFG_NEXT_ROUND), dir, 3)); }
static void rowScenarioValue(char *b, s32 n) { snprintf(b, n, "%s", netScenarioName(gevrNetConfigGet(CFG_SCENARIO))); }
static void rowScenarioStep(s32 dir)
{
    /* past the scenarios the stage or the party cannot take, which the setter refuses */
    s32 next = gevrNetConfigGet(CFG_SCENARIO);
    s32 k;

    for (k = 1; k < netScenarioCount(); k++)
    {
        next = gevrCycled(next, dir, netScenarioCount());
        gevrNetConfigSet(CFG_SCENARIO, next);
        if (gevrNetConfigGet(CFG_SCENARIO) == next) return;
    }
}
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
static void rowCharacterValue(char *b, s32 n) { snprintf(b, n, "%s", netCharacterName(gevrNetSlotChr(menuActingSlot()))); }
static void rowCharacterStep(s32 dir) { s32 me = menuActingSlot(); netLobbySetCharacterSlot(me, (u8)gevrCycled(gevrNetSlotChr(me), dir, netCharacterCount())); }
static void lobbyLoadoutValue(char *b, s32 n, s32 k)
{
    s32 item = gevrNetSlotLoadout(menuActingSlot(), k);
    snprintf(b, n, "%s", item ? netItemName(item) : "NONE");
}
static void lobbyLoadoutStep(s32 dir, s32 k)
{
    s32 me = menuActingSlot();
    u8 items[4];
    s32 i;
    s32 idx;
    for (i = 0; i < 4; i++) items[i] = (u8)gevrNetSlotLoadout(me, i);
    idx = netItemIndexOf(items[k]);
    idx = gevrCycled(idx < 0 ? 0 : idx, dir, netItemCount());
    items[k] = gevrNetItemAt(idx);
    netLobbySetLoadoutSlot(me, items);
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
    snprintf(b, n, "%s %s", netStageName(i), i >= 0 && ((VrMpFavStages >> i) & 1ull) ? "YES" : "NO");
}
static void rowFavMapStep(s32 dir)
{
    s32 i = lobbyFavStage();
    if (i < 0) return;
    VrMpFavStages ^= 1ull << i;
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
/*
 * The start's countdown, on the row and in the page's title: pressing START
 * MATCH used to swap the row for RETURN TO LOBBY with nothing else changing,
 * so the host had no idea a start was under way, and the natural next press
 * cancelled it (user, 2026-09-30). The HUD's own "MATCH STARTS IN" is hidden
 * while this menu is up.
 */
static void rowStartValue(char *b, s32 n)
{
    s32 secs = netCountdownSecondsLeft();
    if (secs > 0) snprintf(b, n, "- STARTING IN %d", secs);
    else snprintf(b,n,"%s",netGetConnectedPlayerCount() == 1 ? "- RESTART WARMUP" : netHostStartRequested() ? "- WAITING FOR READY" : netRoundRosterReady() ? "" : "- REQUEST READY");
}
static void rowReturnValue(char *b, s32 n)
{
    snprintf(b, n, "%s", netCountdownSecondsLeft() > 0 ? "- CANCELS THE START" : "");
}
/* The pre-match lobby's host rules (net_core.c netHostEnterLobby; docs/match-lobby.md) */
static void rowLobbyValue(char *b, s32 n) { snprintf(b, n, "%s", gevrNetConfigGet(CFG_LOBBY) ? "ON" : "OFF"); }
static void rowLobbyStep(s32 dir) { (void)dir; gevrNetConfigSet(CFG_LOBBY, !gevrNetConfigGet(CFG_LOBBY)); }
static void rowWarmupValue(char *b, s32 n) { snprintf(b, n, "%s", netWarmupRuleName(gevrNetConfigGet(CFG_WARMUP))); }
static void rowWarmupStep(s32 dir) { gevrNetConfigSet(CFG_WARMUP, gevrCycled(gevrNetConfigGet(CFG_WARMUP), dir, NET_WARMUP_RULE_COUNT)); }
/* Leave: A twice within 3 s (the first asks), as the watch's quit asks first */
extern u64 sysGetMicroseconds(void);
static u64 s_leaveAskedAt[MAX_PLAYER_COUNT];
static s32 rowLeaveAsked(s32 me) { return me >= 0 && me < MAX_PLAYER_COUNT && s_leaveAskedAt[me] && sysGetMicroseconds() - s_leaveAskedAt[me] < 3000000ull; }
static void rowLeaveValue(char *b, s32 n) { snprintf(b, n, "%s", rowLeaveAsked(menuActingSlot()) ? "- A AGAIN TO LEAVE" : ""); }
static void rowLeaveStep(s32 dir)
{
    s32 me = menuActingSlot();
    (void)dir;
    if (me < 0 || me >= MAX_PLAYER_COUNT) return;
    if (!rowLeaveAsked(me))
    {
        s_leaveAskedAt[me] = sysGetMicroseconds() | 1;
        return;
    }
    s_leaveAskedAt[me] = 0;
    if (netLocalSeatOfSlot(me) > 0)
    {
        netRequestRemoveLocalGuest(me);
        return;
    }
    gevrLobbySessionStopped();   /* the goodbye: the host frees the slot at once */
    gevrRestartToLauncher();
}
static s32 lobbyTeams(void) { return netScenarioHasTeams(gevrNetConfigGet(CFG_SCENARIO)); }
static s32 lobbyNoTeams(void) { return !lobbyTeams(); }   /* a team scenario takes its own size */
static void rowPlayersValue(char *b,s32 n) { snprintf(b,n,"%d",gevrNetConfigGet(CFG_MAX_PLAYERS)); }
static void rowPlayersStep(s32 dir)
{
    /* 2..8 on any map; past the counts below the lobby's connected slots, which the setter refuses */
    s32 count = MAX_PLAYER_COUNT - 1;
    s32 next = gevrNetConfigGet(CFG_MAX_PLAYERS) - 2;
    s32 k;

    for (k = 1; k < count; k++)
    {
        next = gevrCycled(next, dir, count);
        gevrNetConfigSet(CFG_MAX_PLAYERS, next + 2);
        if (gevrNetConfigGet(CFG_MAX_PLAYERS) == next + 2) return;
    }
}
static void rowTeamValue(char *b,s32 n) { snprintf(b,n,"%s",netTeamName(netGetSlotTeam(menuActingSlot()))); }
static void rowTeamStep(s32 dir) { s32 me = menuActingSlot(); netLobbySetTeamSlot(me, (u8)gevrCycled(netGetSlotTeam(me),dir,3)); }
/* Per-player health (protocol 29): the host's rule, and each player's own (the match's unless set) */
static s32 lobbyDeathmatch(void) { return netIsActive() && !gevrNetConfigPve(); }
static void rowHandicapsValue(char *b,s32 n) { snprintf(b,n,"%s",gevrNetConfigGet(CFG_HANDICAPS) ? "ON" : "OFF"); }
static void rowHandicapsStep(s32 dir) { (void)dir; gevrNetConfigSet(CFG_HANDICAPS, !gevrNetConfigGet(CFG_HANDICAPS)); }
static void rowMyHealthValue(char *b,s32 n)
{
    s32 me = menuActingSlot();
    s32 value = netGetSlotHandicap(me);

    snprintf(b,n,"%s%s",netHealthName(netSlotHandicapIndex(me)),netHandicapIsHosts(value) ? " (HOST)" : "");
}
static void rowMyHealthStep(s32 dir)
{
    s32 me = menuActingSlot();

    if (!netSlotHandicapEditable(me)) return;
    netLobbySetHandicapSlot(me, netHandicapPick(netSlotHandicapIndex(me) + dir, gevrNetConfigGet(CFG_HEALTH), netHealthCount()));
}
static s32 lobbyClient(void) { return !netIsHost(); }
static void rowReadyValue(char *b,s32 n) { snprintf(b,n,"%s",netSlotReady(menuActingSlot()) ? "READY" : "WAITING"); }
static void rowReadyStep(s32 dir) { s32 me = menuActingSlot(); (void)dir; netLobbySetReadySlot(me, !netSlotReady(me)); }
static s32 lobbySplitPrimary(void) { return netLocalSlotCount() > 1 && menuActingSlot() == netGetLocalSlot(); }
static s32 lobbySplitGuest(void) { return netLocalSlotCount() > 1 && netLocalSeatOfSlot(menuActingSlot()) > 0; }
/* the Multiplayer Options folder (below), from the LOBBY page: PREVIOUS comes back here */
static s32 lobbySetup(void) { return gevrMpOptionsAvailable(menuActingSlot()); }
static void rowSetupValue(char *b, s32 n) { snprintf(b, n, "%s", ""); }
static void rowSetupStep(s32 dir) { (void)dir; gevrMpOptionsOpen(menuActingSlot(), MENU_LOBBY); }
static void rowFullMenuValue(char *b, s32 n) { snprintf(b, n, "%s", ""); }
static void rowFullMenuStep(s32 dir) { (void)dir; gevrNativePauseOpenPanel(); }
static void rowDropOutValue(char *b, s32 n) { snprintf(b, n, "%s", ""); }
static void rowDropOutStep(s32 dir)
{
    s32 me = menuActingSlot();
    (void)dir;
    if (g_CurrentPlayer) g_CurrentPlayer->mpmenuon = FALSE;
    netRequestRemoveLocalGuest(me);
}
/* Bots: player slots the host runs (gevr_bot.c), changed between rounds only */
static s32 lobbyBots(void) { return gevrNetBotRowsEditable(); }
static s32 lobbyBotsOn(void) { return gevrNetBotRowsEditable() && gevrNetConfigGet(CFG_BOT_MODE) != NET_BOT_OFF; }
static s32 lobbyBotsFixed(void) { return gevrNetBotRowsEditable() && gevrNetConfigGet(CFG_BOT_MODE) == NET_BOT_FIXED; }
static void rowBotModeValue(char *b,s32 n) { snprintf(b,n,"%s",netBotModeName(gevrNetConfigGet(CFG_BOT_MODE))); }
static void rowBotModeStep(s32 dir) { gevrNetConfigSet(CFG_BOT_MODE,gevrCycled(gevrNetConfigGet(CFG_BOT_MODE),dir,NET_BOT_MODE_COUNT)); }
static void rowBotCountValue(char *b,s32 n) { snprintf(b,n,"%d",gevrNetConfigGet(CFG_BOT_COUNT)); }
static void rowBotCountStep(s32 dir) { gevrNetConfigSet(CFG_BOT_COUNT,gevrCycled(gevrNetConfigGet(CFG_BOT_COUNT)-1,dir,MAX_PLAYER_COUNT-1)+1); }
static void rowBotDiffValue(char *b,s32 n) { snprintf(b,n,"%s",netBotDifficultyName(gevrNetConfigGet(CFG_BOT_DIFFICULTY))); }
static void rowBotDiffStep(s32 dir) { gevrNetConfigSet(CFG_BOT_DIFFICULTY,gevrCycled(gevrNetConfigGet(CFG_BOT_DIFFICULTY),dir,NET_BOT_DIFF_COUNT)); }
/*
 * Horde (owner, 2026-11-24): the run arrives in a setup break and its first
 * wave waits for the host (net_core.c netHordeHostStart). The host's map
 * (Horde's own list, the photos filtered to it) and first wave are pending
 * config (CFG_HORDE_STAGE, CFG_HORDE_START_WAVE): a new wave applies at once
 * while nothing has started, a new map when START WAVE restarts the run on
 * it; during a run both wait for the retry. The folder reuses these rows.
 */
static s32 lobbyHorde(void) { return netHordeActive(); }
static s32 lobbyHordeCanStart(void) { return netHordeHostCanStart(); }
static void rowHordeMapValue(char *b, s32 n)
{
    s32 idx = netStageIndexOf((u8)gevrNetConfigGet(CFG_HORDE_STAGE));
    snprintf(b, n, "%s", idx >= 0 && idx < netHordeStageCount() ? netStageName(idx) : "");
}
static void rowHordeMapStep(s32 dir)
{
    s32 idx = netStageIndexOf((u8)gevrNetConfigGet(CFG_HORDE_STAGE));
    gevrNetConfigSet(CFG_HORDE_STAGE, gevrCycled(idx < 0 ? 0 : idx, dir, netHordeStageCount()));   /* the list position */
}
static void rowHordeWaveValue(char *b, s32 n) { snprintf(b, n, "%d", gevrNetConfigGet(CFG_HORDE_START_WAVE)); }
static void rowHordeWaveStep(s32 dir)
{
    gevrNetConfigSet(CFG_HORDE_START_WAVE, gevrCycled(gevrNetConfigGet(CFG_HORDE_START_WAVE) - 1, dir, NET_HORDE_MAX_START_WAVE) + 1);
}
static void rowHordeStartValue(char *b, s32 n) { snprintf(b, n, "%s", netHordeRestartPending() ? "- RESTART ON NEW MAP" : ""); }
static void rowHordeStartStep(s32 dir) { (void)dir; netHordeHostStart(); }
/* Co-op and Horde: the host's AI ally (gevr_settings.c's VrAlly, GoldenEye's
 * campaign only); a local setting the next mission or retry takes */
static s32 lobbyPve(void) { return gevrNetConfigPve(); }
static s32 lobbyAlly(void) { return gevrNetConfigPve() && VrCampaign == 0; }
static void rowAllyValue(char *b, s32 n) { snprintf(b, n, "%s", VrAlly ? "ON" : "OFF"); }
static void rowAllyStep(s32 dir) { (void)dir; VrAlly = !VrAlly; vrSettingsSave(); }
/* friendly fire: a team round's rule, and always co-op's and Horde's */
static s32 lobbyFriendlyFire(void) { return friendlyFireTeams() || lobbyPve(); }
static const GevrMenuRow s_lobbyRows[] = {
    { "MULTIPLAYER OPTIONS", GEVR_ROW_ACTION, 0, lobbySetup,       rowSetupValue,      rowSetupStep,       "A:OPEN" },
    { "FULL MENU / SETTINGS", GEVR_ROW_ACTION, 0, lobbySplitPrimary, rowFullMenuValue, rowFullMenuStep, "A:OPEN" },
    { "DROP OUT",        GEVR_ROW_ACTION, 0, lobbySplitGuest,   rowDropOutValue,    rowDropOutStep,     "A:LEAVE MATCH" },
    { "NEXT ROUND READY", GEVR_ROW_VALUE, 0, lobbyClient, rowReadyValue, rowReadyStep, "R-STICK:ON/OFF" },
    { "FRIENDLY FIRE", GEVR_ROW_VALUE, 1, lobbyFriendlyFire, rowFriendlyFireValue, rowFriendlyFireStep, "R-STICK:ON/OFF" },
    { "START WAVE",      GEVR_ROW_ACTION, 1, lobbyHordeCanStart, rowHordeStartValue, rowHordeStartStep, "A:START" },
    { "HORDE MAP",       GEVR_ROW_VALUE,  1, lobbyHorde,        rowHordeMapValue,   rowHordeMapStep,    "A/R-STICK:PICK" },
    { "STARTING WAVE",   GEVR_ROW_VALUE,  1, lobbyHorde,        rowHordeWaveValue,  rowHordeWaveStep,   "R-STICK:PICK" },
    { "AI ALLY",         GEVR_ROW_VALUE,  1, lobbyAlly,         rowAllyValue,       rowAllyStep,        "R-STICK:ON/OFF" },
    { "START MATCH",     GEVR_ROW_ACTION, 1, lobbyCanStart,     rowStartValue,      rowStartStep,       "A:START" },
    { "RETURN TO LOBBY", GEVR_ROW_ACTION, 1, lobbyCanReturn,    rowReturnValue,     rowReturnStep,      "A:RETURN" },
    { "NEXT ROUND",      GEVR_ROW_VALUE,  1, NULL,              rowNextRoundValue,  rowNextRoundStep,   "R-STICK:PICK" },
    { "LOBBY",           GEVR_ROW_VALUE,  1, lobbyDeathmatch,   rowLobbyValue,      rowLobbyStep,       "R-STICK:ON/OFF" },
    { "WARMUP",          GEVR_ROW_VALUE,  1, lobbyDeathmatch,   rowWarmupValue,     rowWarmupStep,      "R-STICK:PICK" },
    { "LEAVE",           GEVR_ROW_ACTION, 0, NULL,              rowLeaveValue,      rowLeaveStep,       "A:LEAVE" },
    { "NEXT MAP",        GEVR_ROW_VALUE,  0, lobbyVoting,       rowNextMapValue,    rowNextMapStep,     "A/R-STICK:VOTE" },
    { "NEXT WEAPONS",    GEVR_ROW_VALUE,  0, lobbyWeaponVote,       rowNextWeaponsValue, rowNextWeaponsStep, "R-STICK:VOTE" },
    { "MAP",             GEVR_ROW_VALUE,  1, NULL,              rowMapValue,        rowMapStep,         "A/R-STICK:PICK" },
    { "PLAYERS",         GEVR_ROW_VALUE,  1, lobbyNoTeams,      rowPlayersValue,    rowPlayersStep,     "R-STICK:PICK" },
    { "BOTS",            GEVR_ROW_VALUE,  1, lobbyBots,         rowBotModeValue,    rowBotModeStep,     "R-STICK:PICK" },
    { "BOT COUNT",       GEVR_ROW_VALUE,  1, lobbyBotsFixed,    rowBotCountValue,   rowBotCountStep,    "R-STICK:PICK" },
    { "BOT DIFFICULTY",  GEVR_ROW_VALUE,  1, lobbyBotsOn,       rowBotDiffValue,    rowBotDiffStep,     "R-STICK:PICK" },
    { "WEAPONS",         GEVR_ROW_VALUE,  1, lobbyNotGoldenGun, rowWeaponsValue,    rowWeaponsStep,     "R-STICK:PICK" },
    { "CUSTOM 1",        GEVR_ROW_VALUE,  1, lobbyCustomSet,    rowCustom0Value,    rowCustom0Step,     "R-STICK:PICK" },
    { "CUSTOM 2",        GEVR_ROW_VALUE,  1, lobbyCustomSet,    rowCustom1Value,    rowCustom1Step,     "R-STICK:PICK" },
    { "CUSTOM 3",        GEVR_ROW_VALUE,  1, lobbyCustomSet,    rowCustom2Value,    rowCustom2Step,     "R-STICK:PICK" },
    { "CUSTOM 4",        GEVR_ROW_VALUE,  1, lobbyCustomSet,    rowCustom3Value,    rowCustom3Step,     "R-STICK:PICK" },
    { "SCENARIO",        GEVR_ROW_VALUE,  1, NULL,              rowScenarioValue,   rowScenarioStep,    "R-STICK:PICK" },
    { "LENGTH",          GEVR_ROW_VALUE,  1, lobbyNotYolt,      rowLengthValue,     rowLengthStep,      "R-STICK:PICK" },
    { "HEALTH",          GEVR_ROW_VALUE,  1, NULL,              rowHealthValue,     rowHealthStep,      "R-STICK:PICK" },
    { "HANDICAPS",       GEVR_ROW_VALUE,  1, lobbyDeathmatch,   rowHandicapsValue,  rowHandicapsStep,   "R-STICK:ON/OFF" },
    { "DUAL WIELD",      GEVR_ROW_VALUE,  1, NULL,              rowDualValue,       rowDualStep,        "R-STICK:PICK" },
    { "LOADOUTS",        GEVR_ROW_VALUE,  1, NULL,              rowLoadoutsValue,   rowLoadoutsStep,    "R-STICK:ON/OFF" },
    { "YOUR TEAM",       GEVR_ROW_VALUE,  0, lobbyTeams,        rowTeamValue,       rowTeamStep,        "A/R-STICK:PICK" },
    { "YOUR HEALTH",     GEVR_ROW_VALUE,  0, lobbyDeathmatch,   rowMyHealthValue,   rowMyHealthStep,    "A/R-STICK:PICK" },
    { "CHARACTER",       GEVR_ROW_VALUE,  0, NULL,              rowCharacterValue,  rowCharacterStep,   "A/R-STICK:PICK" },
    { "LOADOUT 1",       GEVR_ROW_VALUE,  0, lobbyLoadouts,     rowLoadout0Value,   rowLoadout0Step,    "R-STICK:PICK" },
    { "LOADOUT 2",       GEVR_ROW_VALUE,  0, lobbyLoadouts,     rowLoadout1Value,   rowLoadout1Step,    "R-STICK:PICK" },
    { "LOADOUT 3",       GEVR_ROW_VALUE,  0, lobbyLoadouts,     rowLoadout2Value,   rowLoadout2Step,    "R-STICK:PICK" },
    { "LOADOUT 4",       GEVR_ROW_VALUE,  0, lobbyLoadouts,     rowLoadout3Value,   rowLoadout3Step,    "R-STICK:PICK" },
    { "FAV MAP",         GEVR_ROW_VALUE,  0, NULL,              rowFavMapValue,     rowFavMapStep,      "R-STICK:YES/NO" },
    { "FAV SET",         GEVR_ROW_VALUE,  0, NULL,              rowFavSetValue,     rowFavSetStep,      "R-STICK:YES/NO" },
};
/* The active round uses its latched copy; these controls edit the next round. */
static void funValue(char *b, s32 n, s32 flag) { snprintf(b,n,"%s",gevrNetConfigGet(CFG_FUN_FLAGS) & flag ? "ON" : "OFF"); }
static void funStep(s32 flag) { gevrNetConfigSet(CFG_FUN_FLAGS, gevrNetConfigGet(CFG_FUN_FLAGS) ^ flag); }
static void rowDkValue(char *b,s32 n) { funValue(b,n,NET_FUN_DK); }
static void rowPaintValue(char *b,s32 n) { funValue(b,n,NET_FUN_PAINTBALL); }
static void rowLineValue(char *b,s32 n) { funValue(b,n,NET_FUN_LINE); }
static void rowRadarValue(char *b,s32 n) { funValue(b,n,NET_FUN_NO_RADAR); }
static void rowTeleportValue(char *b,s32 n) { funValue(b,n,NET_FUN_NO_TELEPORT); }
static void rowDkStep(s32 dir) { funStep(NET_FUN_DK); }
static void rowPaintStep(s32 dir) { funStep(NET_FUN_PAINTBALL); }
static void rowLineStep(s32 dir) { funStep(NET_FUN_LINE); }
static void rowRadarStep(s32 dir) { funStep(NET_FUN_NO_RADAR); }
static void rowTeleportStep(s32 dir) { funStep(NET_FUN_NO_TELEPORT); }
/* hit immunity: none (every hit counts), short, GoldenEye's (net_rules.h) */
static void rowImmunityValue(char *b,s32 n) { const char *names[] = { "NONE", "SHORT", "GOLDENEYE" }; snprintf(b,n,"%s",names[netHitImmunity(gevrNetConfigGet(CFG_FUN_FLAGS))]); }
static void rowImmunityStep(s32 dir) { s32 f=gevrNetConfigGet(CFG_FUN_FLAGS); gevrNetConfigSet(CFG_FUN_FLAGS, netWithHitImmunity(f, gevrCycled(netHitImmunity(f),dir,3))); }
static void rowGunSizeValue(char *b,s32 n) { const char *names[] = { "NORMAL", "TINY", "BIG" }; snprintf(b,n,"%s",names[gevrNetConfigGet(CFG_GUN_SIZE)]); }
static void rowGunSizeStep(s32 dir) { gevrNetConfigSet(CFG_GUN_SIZE,gevrCycled(gevrNetConfigGet(CFG_GUN_SIZE),dir,3)); }
/* Movement speed is live, shared by humans and bots; the other fun rows latch next round. */
static void rowMovementSpeedValue(char *b,s32 n) { snprintf(b,n,"%d%%",netMovementSpeedPercent(netActiveMovementSpeed())); }
static void rowMovementSpeedStep(s32 dir) {
    s32 percent=netMovementSpeedPercent(gevrNetConfigGet(CFG_MOVEMENT_SPEED));
    gevrNetConfigSet(CFG_MOVEMENT_SPEED,netMovementSpeedMode(50+gevrCycled((percent-50)/25,dir,7)*25));
}
static const GevrMenuRow s_funRows[] = {
    { "DK MODE", GEVR_ROW_VALUE, 1, NULL, rowDkValue, rowDkStep, "R-STICK:ON/OFF" },
    { "PAINTBALL", GEVR_ROW_VALUE, 1, NULL, rowPaintValue, rowPaintStep, "R-STICK:ON/OFF" },
    { "LINE MODE", GEVR_ROW_VALUE, 1, NULL, rowLineValue, rowLineStep, "R-STICK:ON/OFF" },
    { "NO RADAR", GEVR_ROW_VALUE, 1, NULL, rowRadarValue, rowRadarStep, "R-STICK:ON/OFF" },
    { "NO TELEPORT", GEVR_ROW_VALUE, 1, NULL, rowTeleportValue, rowTeleportStep, "R-STICK:ON/OFF" },
    { "HIT IMMUNITY", GEVR_ROW_VALUE, 1, NULL, rowImmunityValue, rowImmunityStep, "R-STICK:PICK" },
    { "GUN SIZE", GEVR_ROW_VALUE, 1, NULL, rowGunSizeValue, rowGunSizeStep, "R-STICK:PICK" },
    { "MOVEMENT SPEED", GEVR_ROW_VALUE, 1, NULL, rowMovementSpeedValue, rowMovementSpeedStep, "LIVE:HOST CHOOSES" },
};
/*
 * The LOBBY page is the quick page between rounds (in-game match setup, plan
 * commit 9): the votes, READY, START MATCH, RETURN TO LOBBY, the Multiplayer
 * Options folder, DROP OUT and FULL MENU. The rest of s_lobbyRows (the match's
 * setup, the player's character, team, health and guns) is the folder's,
 * which views the same rows; the pause window keeps the favorites too
 * (gevrPauseInFolder). Co-op has no folder and no LOBBY page.
 */
static s32 gevrLobbyQuickRow(const GevrMenuRow *r)
{
    return r->step == rowSetupStep || r->step == rowFullMenuStep || r->step == rowDropOutStep || r->step == rowReadyStep ||
           r->step == rowStartStep || r->step == rowReturnStep || r->step == rowNextMapStep || r->step == rowNextWeaponsStep ||
           r->step == rowHordeStartStep;
}
static GevrMenuPage s_funPage = { s_funRows, sizeof(s_funRows) / sizeof(s_funRows[0]), 0, 0, NULL };
static GevrMenuPage s_pausePage = { s_pauseRows, sizeof(s_pauseRows) / sizeof(s_pauseRows[0]), 0, 0, NULL };
static GevrMenuPage s_lobbyPage = { s_lobbyRows, sizeof(s_lobbyRows) / sizeof(s_lobbyRows[0]), 0, 0, gevrLobbyQuickRow };
static s32 s_pageCursor[MAX_PLAYER_COUNT][3];
static s32 s_pageScroll[MAX_PLAYER_COUNT][3];

static s32 gevrMenuPageIdx(s32 mode)
{
    return mode == MENU_PAUSE ? 0 : mode == MENU_LOBBY ? 1 : mode == MENU_FUN ? 2 : -1;
}
static GevrMenuPage *gevrMenuPageFor(s32 mode)
{
    s32 idx = gevrMenuPageIdx(mode);
    s32 p = get_cur_playernum();
    GevrMenuPage *pg = idx == 0 ? &s_pausePage : idx == 1 ? &s_lobbyPage : idx == 2 ? &s_funPage : NULL;
    if (pg && p >= 0 && p < MAX_PLAYER_COUNT) {
        pg->cursor = s_pageCursor[p][idx];
        pg->scroll = s_pageScroll[p][idx];
    }
    return pg;
}
static void gevrMenuPageSave(s32 mode, const GevrMenuPage *pg)
{
    s32 idx = gevrMenuPageIdx(mode);
    s32 p = get_cur_playernum();
    if (pg && idx >= 0 && p >= 0 && p < MAX_PLAYER_COUNT) {
        s_pageCursor[p][idx] = pg->cursor;
        s_pageScroll[p][idx] = pg->scroll;
    }
}
static s32 gevrRowVisible(const GevrMenuRow *r) { return r->visible == NULL || r->visible(); }
static s32 gevrPageRowVisible(const GevrMenuPage *page, s32 i)
{
    const GevrMenuRow *r = &page->rows[i];
    return gevrRowVisible(r) && (page->shows == NULL || page->shows(r));
}
static s32 gevrRowEditable(const GevrMenuRow *r)
{
    /* YOUR HEALTH: the player's own under the host's rule (the host's seat always) */
    if (r->step == rowMyHealthStep) return netSlotHandicapEditable(menuActingSlot());
    return !r->hostonly || (netIsHost() && menuActingSlot() == netGetLocalSlot());
}

/* the cursor onto a visible row: dir steps to the next one that way, 0 keeps it or takes the nearest below */
static void gevrPageMoveCursor(GevrMenuPage *page, s32 dir)
{
    s32 tries = page->count;
    s32 c = page->cursor;
    if (dir != 0) c = gevrCycled(c, dir, page->count);
    while (tries-- > 0 && !gevrPageRowVisible(page, c))
    {
        c = gevrCycled(c, dir ? dir : 1, page->count);
    }
    page->cursor = c;
}

static void gevrMenuPagesTick(s32 player_num)
{
#if (defined(ANDROID) && !defined(GEVR_MOBILE)) || defined(GEVR_PCVR)   /* the headset build: the Quest, PC VR */
    if(gevrNativePauseOpen())return;
#endif
    static int last_rclick = 0;
    static s32 stick_direction[MAX_PLAYER_COUNT];
    static s32 stick_held_ticks[MAX_PLAYER_COUNT];
    static s32 nav_direction[MAX_PLAYER_COUNT];
    static s32 nav_held_ticks[MAX_PLAYER_COUNT];
    GevrMenuPage *page;
    s32 change = 0;
    s32 stick_y;
    s32 stick_x_value = 0;
    s32 direction;
    int rclick;

    if (!netIsActive() || player_num < 0 || player_num >= MAX_PLAYER_COUNT || !gevrNetSlotIsLocalHuman(player_num)) return;
    page = gevrMenuPageFor(g_CurrentPlayer->mpmenumode);
    if (page == NULL) return;

    if (player_num == netGetLocalSlot()) {
        rclick = get_button_state(1, "thumbstick_click") ? 1 : 0;
        /* Right stick click only: A also closes the pause menu. */
        if (rclick && !last_rclick)
        {
            gevrPageMoveCursor(page, 1);
            mpwatchPlayBeep();
        }
        last_rclick = rclick;
    }

    {
        GevrVec2 lstick = { 0.0f, 0.0f };
        GevrVec2 rstick = { 0.0f, 0.0f };
        float lx, ly, rx, ry;
        s32 nav = 0;
        if (player_num == netGetLocalSlot()) {
            get_2d_input(0, "thumbstick", &lstick);
            get_2d_input(1, "thumbstick", &rstick);
            lx = lstick.x < 0.0f ? -lstick.x : lstick.x;
            ly = lstick.y < 0.0f ? -lstick.y : lstick.y;
            rx = rstick.x < 0.0f ? -rstick.x : rstick.x;
            ry = rstick.y < 0.0f ? -rstick.y : rstick.y;
            nav = ly > 0.5f && ly > lx ? (lstick.y > 0.0f ? -1 : 1) : 0;
            stick_x_value = rx > 0.5f && rx > ry ? (rstick.x > 0.0f ? 1 : -1) : 0;
        }
        /* Flat gamepads & keyboard: D-pad Up/Down or C-Up/C-Down move cursor; D-pad Left/Right step value. */
        if (!nav) {
            u16 held = joyGetButtons(player_num, U_JPAD | D_JPAD | U_CBUTTONS | D_CBUTTONS);
            if (held & (U_JPAD | U_CBUTTONS)) nav = -1;
            else if (held & (D_JPAD | D_CBUTTONS)) nav = 1;
        }
        if (!stick_x_value) {
            u16 held = joyGetButtons(player_num, L_JPAD | R_JPAD);
            if (held & L_JPAD) stick_x_value = -1;
            else if (held & R_JPAD) stick_x_value = 1;
        }
        if (nav != nav_direction[player_num])
        {
            nav_direction[player_num] = nav;
            nav_held_ticks[player_num] = 0;
            if (nav)
            {
                gevrPageMoveCursor(page, nav);
                mpwatchPlayBeep();
            }
        }
        else if (nav != 0 && ++nav_held_ticks[player_num] >= 18 && (nav_held_ticks[player_num] - 18) % 6 == 0)
        {
            gevrPageMoveCursor(page, nav);
            mpwatchPlayBeep();
        }
    }

    stick_y = joyGetStickY(player_num);
    direction = stick_y > 30 ? 1 : stick_y < -30 ? -1 : stick_x_value;
    if (direction != stick_direction[player_num])
    {
        stick_direction[player_num] = direction;
        stick_held_ticks[player_num] = 0;
        change = direction;
    }
    else if (direction != 0 && ++stick_held_ticks[player_num] >= 18 && (stick_held_ticks[player_num] - 18) % 6 == 0)
    {
        change = direction;
    }
    gevrPageMoveCursor(page, 0);
    gevrMenuPageSave(g_CurrentPlayer->mpmenumode, page);
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

/*
 * The game's own screens in a match (Select Character, Select Stage below; a
 * page of the watch menu each, its mpmenumode in s_gevrMenuPanels): where one
 * is drawn, whether one is up (input.c), and leaving one. A new screen adopts
 * them by listing its mode there and drawing into gevrMenuPanelRect.
 *
 * On a flat screen the panel fills the player's view. In stereo the watch
 * menu goes on the head-locked HUD quad (lv.c: VR_HUD_CAPTURE_BEGIN_H at full
 * size; a 44 degree panel 1.4 m out, vr_openxr.cpp). Its capture is the eye
 * buffer's size and the game's screen covers it at one scale (gfx_pc.cpp
 * gfx_adjust_viewport_or_scissor: the texture rectangles, the portraits'
 * viewport and the scissor alike), so a 4:3 screen loses its sides on a
 * squarer eye; and the HUD's draws come out a ninth larger round the centre
 * (the vertex shader's w = 0.9). The panel takes the part that shows, less a
 * margin. sw x sh is the game's screen, fbw x fbh the eye buffer.
 */
#define GEVR_PANEL_HUD_SHOWN 0.85f   /* 0.9 of the covered screen shows; a margin inside that */

void gevrMenuPanelFit(s32 sw, s32 sh, s32 fbw, s32 fbh, s32 *x, s32 *y, s32 *w, s32 *h)
{
    f32 visw = (f32)sw;
    f32 vish = (f32)sh;

    if (sw > 0 && sh > 0 && fbw > 0 && fbh > 0)
    {
        f32 rx = (f32)fbw / (f32)sw;
        f32 ry = (f32)fbh / (f32)sh;
        f32 cover = rx > ry ? rx : ry;

        visw = (f32)fbw / cover;
        vish = (f32)fbh / cover;
    }
    *w = (s32)(visw * GEVR_PANEL_HUD_SHOWN);
    *h = (s32)(vish * GEVR_PANEL_HUD_SHOWN);
    *x = (sw - *w) / 2;
    *y = (sh - *h) / 2;
}

/* Where the current player's panel goes, in screen pixels */
void gevrMenuPanelRect(s32 *x, s32 *y, s32 *w, s32 *h)
{
    extern int vr_get_internal_render_width(void);
    extern int vr_get_internal_render_height(void);

    if (g_gevrStereo)
    {
        gevrMenuPanelFit(viGetX(), viGetY(), vr_get_internal_render_width(), vr_get_internal_render_height(), x, y, w, h);
        return;
    }
    *x = viGetViewLeft();
    *y = viGetViewTop();
    *w = viGetViewWidth();
    *h = viGetViewHeight();
}

/*
 * Select Character in a match, online: GoldenEye's own
 * multiplayer screen (front.c, the scrolling portraits and the name under
 * them) for one player, inside their own view (in VR on the head-locked HUD,
 * gevrMenuPanelRect) and driven by their own pad, while the others play on.
 * A player who drops in (input.c) gets it first;
 * the others open it with A on the LOBBY page's CHARACTER row (the solo
 * player from the pause window's Player tab). It is a page of the watch menu
 * (MENU_CHARSEL), so the player stands still as in any page of it. The choice
 * goes out as the Character row's does (netLobbySetCharacterSlot), and every
 * machine then gives that player the new body (bondview2.c
 * gevrNetBodyFollowsCharacter). B before choosing leaves it as it was.
 *
 * A player who dropped in is not in the fight until they choose: frozen
 * where they spawned and, on their own machine, which applies their damage,
 * unhurt (net_core.c netSetSlotChoosing). The body stays in the world (the
 * match spawned it on joining, as before) so nothing about joining changes
 * for the other machines.
 */
/*
 * The pre-match lobby (front.c, docs/match-lobby.md) shows the folder and its
 * screens on the title stage, where there are no players: a seat's menu state
 * (which screen is up) is then kept here, by slot, in place of its player's
 * mpmenuon and mpmenumode. Everything below reads and writes it through
 * gevrSeatMenu.
 */
typedef struct { s32 on; s32 mode; s32 centre; } GevrLobbySeat;
static GevrLobbySeat s_lobbySeat[MAX_PLAYER_COUNT];
typedef struct { s32 *on; s32 *mode; s32 *centre; struct player *pl; } GevrSeatMenu;
s32 gevrInMatchLobby(void) { return netIsActive() && netInMatchLobby() && bossGetStageNum() == LEVELID_TITLE; }
static s32 gevrSeatMenu(s32 slot, GevrSeatMenu *m)
{
    struct player *pl;

    if (slot < 0 || slot >= MAX_PLAYER_COUNT) return 0;
    if (gevrInMatchLobby())
    {
        if (!gevrNetSlotIsLocalHuman(slot)) return 0;
        m->on = &s_lobbySeat[slot].on;
        m->mode = &s_lobbySeat[slot].mode;
        m->centre = &s_lobbySeat[slot].centre;
        m->pl = NULL;
        return 1;
    }
    pl = g_playerPointers[slot];
    if (pl == NULL) return 0;
    m->on = &pl->mpmenuon;
    m->mode = &pl->mpmenumode;
    m->centre = &pl->mpjoywascentre;
    m->pl = pl;
    return 1;
}
/* slot's screen: mode when one is up, else -1 */
static s32 gevrSeatMode(s32 slot)
{
    GevrSeatMenu m;
    return gevrSeatMenu(slot, &m) && *m.on ? *m.mode : -1;
}
/* slot's menu up on mode; whether it has one to show it on */
static s32 gevrSeatShow(s32 slot, s32 mode)
{
    GevrSeatMenu m;
    if (!gevrSeatMenu(slot, &m)) return 0;
    *m.on = TRUE;
    *m.mode = mode;
    *m.centre = 1;
    return 1;
}
static void gevrSeatSetMode(s32 slot, s32 mode)
{
    GevrSeatMenu m;
    if (gevrSeatMenu(slot, &m)) *m.mode = mode;
}
/* slot's menu closed, into the match */
static void gevrSeatHide(s32 slot)
{
    GevrSeatMenu m;
    if (!gevrSeatMenu(slot, &m)) return;
    *m.on = FALSE;
    if (m.pl != NULL) m.pl->healthdisplaytime = 0;
}

static s32 s_charSelDropIn[MAX_PLAYER_COUNT];
static s32 s_charSelArmed[MAX_PLAYER_COUNT];
/* a game screen opened from the Multiplayer Options folder (MENU_SETUP) goes back to it, else to the LOBBY page */
static s32 s_panelToSetup[MAX_PLAYER_COUNT];
static s32 s_setupArmed[MAX_PLAYER_COUNT];
static s32 gevrPanelBack(s32 slot)
{
    if (slot < 0 || slot >= MAX_PLAYER_COUNT || !s_panelToSetup[slot]) return MENU_LOBBY;
    s_panelToSetup[slot] = 0;
    s_setupArmed[slot] = 0;   /* the A or B that left the screen is not the folder's */
    return MENU_SETUP;
}

s32 gevrCharSelIsOpen(s32 slot) { return gevrSeatMode(slot) == MENU_CHARSEL; }

/* An online player's watch is on a page of rows (PAUSE, LOBBY, FUN): a flat pad's sticks read differently there (input.c) */
s32 gevrMenuRowsOpen(s32 slot)
{
    struct player *pl;

    if (!netIsActive() || slot < 0 || slot >= MAX_PLAYER_COUNT) return 0;
    pl = g_playerPointers[slot];
    return pl != NULL && pl->mpmenuon && gevrMenuPageIdx(pl->mpmenumode) >= 0;
}

void gevrCharSelOpen(s32 slot, s32 dropin)
{
    if (!netIsActive() || slot < 0 || slot >= MAX_PLAYER_COUNT || !gevrNetSlotIsLocalHuman(slot)) return;
    if (!gevrSeatShow(slot, MENU_CHARSEL)) return;
    frontCharSelectBegin(slot, gevrNetSlotChr(slot), netCharacterCount());
    s_panelToSetup[slot] = 0;
    s_charSelDropIn[slot] = dropin;
    s_charSelArmed[slot] = 0;
    netSetSlotChoosing(slot, dropin);
}

static void gevrCharSelClose(s32 slot)
{
    netSetSlotChoosing(slot, 0);
    if ((s_charSelDropIn[slot] && !s_panelToSetup[slot]) || !netIsActive())
    {
        gevrSeatHide(slot);   /* into the match */
    }
    else
    {
        gevrSeatSetMode(slot, gevrPanelBack(slot));   /* back to the page it came from */
    }
    s_charSelDropIn[slot] = 0;
}

/* The panel's frame for the player ticking; whether it had the pad */
static s32 gevrCharSelTick(s32 player_num)
{
    s32 result;

    if (!gevrCharSelIsOpen(player_num)) return 0;
    if (!netIsActive() || !gevrNetSlotIsLocalHuman(player_num))
    {
        gevrCharSelClose(player_num);
        return 0;
    }
    /* The press that opened it (a drop-in's START, the row's A, the pause window's
     * click: the left button is Z, and a click released before this frame is still
     * this frame's press) is not a choice. */
    if (!s_charSelArmed[player_num])
    {
        const u16 opener = A_BUTTON | B_BUTTON | Z_TRIG | START_BUTTON;

        if (joyGetButtons(player_num, opener) || joyGetButtonsPressedThisFrame(player_num, opener)) return 1;
        s_charSelArmed[player_num] = 1;
    }
    result = frontCharSelectTick(player_num, netCharacterCount());
    if (result > 0)
    {
        netLobbySetCharacterSlot(player_num, (u8)frontCharSelectShown(player_num));
        gevrCharSelClose(player_num);
    }
    else if (result < 0)
    {
        mpwatchPlayBeep();
        gevrCharSelClose(player_num);
    }
    return 1;
}

/*
 * Select Stage in a match, online: GoldenEye's multiplayer
 * stage select (front.c: the rows of photos on their dark bands, the
 * highlighted one lit) for one player, in their own view and driven by their
 * pad, while the others play on, as Select Character above. A on the LOBBY
 * page's MAP row (the host) chooses the map with it, A on NEXT MAP (anyone,
 * while the next round is voted) casts that player's vote, its first tile,
 * the screen's Random photo, being No vote. Every listed stage has a tile
 * (net_match.c, more than the screen's eleven); one the party cannot play
 * is dimmed and passed over, as the rows pass over it. Each photo shows its
 * votes, and the player's own (their vote, the host's map) is framed. The
 * choice goes out as the row's step does (CFG_STAGE by list position,
 * netSetVoteSlot), so nothing on the wire changes; B leaves it as it was. The
 * solo player opens it from the pause window's Match tab. The stick still
 * steps the rows as before.
 */
#define GEVR_STAGESEL_TILES 65   /* No vote and the 64 stages a ballot holds (net_match.c) */
static s32 s_stageSelVote[MAX_PLAYER_COUNT];
static s32 s_stageSelArmed[MAX_PLAYER_COUNT];
static s32 s_stageSelLeaving[MAX_PLAYER_COUNT];   /* chosen or backed out: closes once the button is let go */

s32 gevrStageSelIsOpen(s32 slot) { return gevrSeatMode(slot) == MENU_STAGESEL; }

/* whether slot may still use its grid: the host's own seat for the map, anyone while voting */
static s32 gevrStageSelAllowed(s32 slot, s32 vote)
{
    if (!netIsActive() || !gevrNetSlotIsLocalHuman(slot)) return 0;
    return vote ? lobbyVoting() : netIsHost() && slot == netGetLocalSlot();
}

/* The grid's tiles for slot, as its rows offer them; the count */
static s32 gevrStageSelTiles(s32 slot, s32 vote, FrontStageTile *tiles)
{
    s32 horde = !vote && netHordeActive();   /* Horde's map: its own list, the first entries */
    s32 count = horde ? netHordeStageCount() : netStageCount();
    s32 voting = lobbyVoting();
    s32 mine = vote ? netGetVote(0, slot) : netStageIndexOf((u8)gevrNetConfigGet(horde ? CFG_HORDE_STAGE : CFG_STAGE));
    s32 n = 0;
    s32 i;
    s32 k;

    if (vote)
    {
        tiles[n].level = -1;
        tiles[n].name = "No vote";
        tiles[n].eligible = 1;
        tiles[n].full = 0;
        tiles[n].mine = mine < 0;
        tiles[n].votes = 0;
        n++;
    }
    for (i = 0; i < count && n < GEVR_STAGESEL_TILES; i++, n++)
    {
        const NetMatchStage *stage = netStage(i);
        s32 votes = 0;

        if (voting)
        {
            for (k = 0; k < MAX_PLAYER_COUNT; k++)
            {
                if (netGetVote(0, k) == i) votes++;
            }
        }
        tiles[n].level = (s32)netStageLevelId(stage->level_id);
        tiles[n].name = stage->name;
        tiles[n].eligible = netStageEligible(i) != 0;
        tiles[n].full = netStageIsFull(stage->level_id) != 0;
        tiles[n].mine = i == mine;
        tiles[n].votes = (u8)votes;
    }
    return n;
}

void gevrStageSelOpen(s32 slot, s32 vote)
{
    FrontStageTile tiles[GEVR_STAGESEL_TILES];
    GevrSeatMenu m;
    s32 n;
    s32 cursor = 0;
    s32 i;

    if (slot < 0 || slot >= MAX_PLAYER_COUNT || !gevrStageSelAllowed(slot, vote)) return;
    if (!gevrSeatMenu(slot, &m)) return;
    n = gevrStageSelTiles(slot, vote, tiles);
    for (i = 0; i < n; i++)
    {
        if (tiles[i].mine) cursor = i;
    }
    frontStageSelectBegin(slot, tiles, n, cursor);
    s_panelToSetup[slot] = 0;
    gevrSeatShow(slot, MENU_STAGESEL);
    s_stageSelVote[slot] = vote;
    s_stageSelArmed[slot] = 0;
    s_stageSelLeaving[slot] = 0;
}

static void gevrStageSelClose(s32 slot)
{
    GevrSeatMenu m;

    if (gevrSeatMenu(slot, &m) && *m.mode == MENU_STAGESEL) *m.mode = gevrPanelBack(slot);   /* back to the page it came from */
}

/* The grid's frame for the player ticking; whether it had the pad */
static s32 gevrStageSelTick(s32 player_num)
{
    const u16 opener = A_BUTTON | B_BUTTON | Z_TRIG | START_BUTTON;
    FrontStageTile tiles[GEVR_STAGESEL_TILES];
    s32 vote;
    s32 n;
    s32 result;

    if (!gevrStageSelIsOpen(player_num)) return 0;
    vote = s_stageSelVote[player_num];
    /* the host's map grid closes for a host no more; the vote's when the host stops the vote */
    if (!gevrStageSelAllowed(player_num, vote))
    {
        gevrStageSelClose(player_num);
        return 0;
    }
    /* The press that opened it (the row's A, the pause window's click) is not a choice, as Select Character's. */
    if (!s_stageSelArmed[player_num])
    {
        if (joyGetButtons(player_num, opener) || joyGetButtonsPressedThisFrame(player_num, opener)) return 1;
        s_stageSelArmed[player_num] = 1;
    }
    /*
     * Chosen or backed out: the grid stays (the choice framed) until the button
     * is let go, as Select Character's portrait grows. Closed at once, the press
     * still held reached what came back: the pause window's button that opened
     * the grid took the A and opened it again.
     */
    if (s_stageSelLeaving[player_num])
    {
        if (!joyGetButtons(player_num, opener)) gevrStageSelClose(player_num);
        return 1;
    }
    n = gevrStageSelTiles(player_num, vote, tiles);
    result = frontStageSelectTick(player_num, tiles, n);
    if (result > 0)
    {
        s32 pick = frontStageSelectShown(player_num);

        if (vote) netSetVoteSlot(player_num, 0, pick - 1);   /* the first tile: no vote */
        else gevrNetConfigSet(netHordeActive() ? CFG_HORDE_STAGE : CFG_STAGE, pick);   /* the setter takes the list position */
        s_stageSelLeaving[player_num] = 1;
    }
    else if (result < 0)
    {
        mpwatchPlayBeep();
        s_stageSelLeaving[player_num] = 1;
    }
    return 1;
}

static Gfx *gevrStageSelDraw(Gfx *gdl, s32 slot)
{
    FrontStageTile tiles[GEVR_STAGESEL_TILES];
    s32 vote = s_stageSelVote[slot];
    s32 n = gevrStageSelTiles(slot, vote, tiles);
    s32 x;
    s32 y;
    s32 w;
    s32 h;

    gevrMenuPanelRect(&x, &y, &w, &h);
    return frontStageSelectDraw(gdl, slot, tiles, n, vote ? "Vote for the Next Map" : "Select Map", vote ? "VOTE" : "PICK",
                                x, y, w, h);
}

/* The watch pages that are the game's own screens (gevrMenuPanelRect above) */
static const s32 s_gevrMenuPanels[] = { MENU_CHARSEL, MENU_STAGESEL, MENU_HANDICAP, MENU_TEAMSEL, MENU_SETUP };
static void gevrMpOptionsClose(s32 slot, s32 back);

s32 gevrMenuPanelIsOpen(s32 slot)
{
    s32 mode = gevrSeatMode(slot);
    u32 i;

    if (mode < 0) return 0;
    for (i = 0; i < sizeof(s_gevrMenuPanels) / sizeof(s_gevrMenuPanels[0]); i++)
    {
        if (mode == s_gevrMenuPanels[i]) return 1;
    }
    return 0;
}

/* slot's panel left unchanged, as B leaves it (the headset's Menu button, input.c); whether one was up */
s32 gevrMenuPanelCancel(s32 slot)
{
    if (!gevrMenuPanelIsOpen(slot)) return 0;
    if (gevrSeatMode(slot) == MENU_CHARSEL) gevrCharSelClose(slot);
    else if (gevrSeatMode(slot) == MENU_SETUP) gevrMpOptionsClose(slot, 1);
    else gevrSeatSetMode(slot, gevrPanelBack(slot));   /* back to the page it came from */
    return 1;
}

/*
 * Handicap and Teams in a match, online on a flat screen: GoldenEye's Select
 * Handicap and Select Teams screens (front.c) for one player, in their own
 * view and driven by their pad, while the others play on, as Select
 * Character and Select Stage above. A on the LOBBY page's YOUR HEALTH or
 * YOUR TEAM row opens one (the solo player: the pause window's Player tab);
 * the right stick still steps the rows. B leaves things as they were.
 *
 * Handicap: left and right step the health (Hero .. Rookie, the match's own
 * reading MATCH). A player changes their own while the host's HANDICAPS rule
 * is on and the host has not set it (net_rules.h NET_HANDICAP_*); the host's
 * own seat moves up and down the list and sets anyone's, a bot's too. A sends
 * every change made (netLobbySetHandicapSlot, netHostSetSlotHandicap). Like
 * the match's health it takes effect at the next load. Deathmatch only.
 *
 * Teams: the two sides with their players and open places, and those on
 * neither: who is on which team is the point of it. Left picks Red, right
 * Blue, down neither, up back to the current one; A sends it as the YOUR
 * TEAM row does (netLobbySetTeamSlot), so the team rules stay net_core.c's:
 * a full side refuses, a change unreadies, and the round plays its latched
 * teams until the next load.
 */
extern int netLobbySlotConnected(int slot);
static s32 s_hcapArmed[MAX_PLAYER_COUNT];
static s32 s_hcapLeaving[MAX_PLAYER_COUNT];
static s32 s_hcapCursor[MAX_PLAYER_COUNT];                    /* the target slot */
static s16 s_hcapPick[MAX_PLAYER_COUNT][MAX_PLAYER_COUNT];    /* the value chosen for each slot, -1 none yet */
static s32 s_teamSelArmed[MAX_PLAYER_COUNT];
static s32 s_teamSelLeaving[MAX_PLAYER_COUNT];
static s32 s_teamSelPick[MAX_PLAYER_COUNT];

static s32 gevrPanelOpen(s32 slot, s32 mode) { return gevrSeatMode(slot) == mode; }
s32 gevrHandicapIsOpen(s32 slot) { return gevrPanelOpen(slot, MENU_HANDICAP); }
s32 gevrTeamSelIsOpen(s32 slot) { return gevrPanelOpen(slot, MENU_TEAMSEL); }
static void gevrPanelClose(s32 slot, s32 mode)
{
    GevrSeatMenu m;

    if (gevrSeatMenu(slot, &m) && *m.mode == mode) *m.mode = gevrPanelBack(slot);   /* back to the page it came from */
}
/* The press that opened a panel (the row's A, the pause window's click) is not a choice; nor is the one that closes it */
static s32 gevrPanelHeld(s32 slot)
{
    const u16 opener = A_BUTTON | B_BUTTON | Z_TRIG | START_BUTTON;

    return joyGetButtons(slot, opener) || joyGetButtonsPressedThisFrame(slot, opener);
}

/* the host's own seat: anyone's health */
static s32 gevrHandicapHostSeat(s32 slot) { return netIsHost() && slot == netGetLocalSlot(); }
static s32 gevrHandicapValue(s32 me, s32 target)
{
    return s_hcapPick[me][target] >= 0 ? s_hcapPick[me][target] : netGetSlotHandicap(target);
}
/* the rule a value is read under: the host's seat's own picks count as the host's */
static s32 gevrHandicapRule(s32 me, s32 target)
{
    return gevrNetConfigGet(CFG_HANDICAPS) || (s_hcapPick[me][target] >= 0 && gevrHandicapHostSeat(me));
}
static s32 gevrHandicapMayChange(s32 me, s32 target)
{
    if (gevrHandicapHostSeat(me)) return netLobbySlotConnected(target);
    return target == me && netSlotHandicapEditable(me);
}

/* The screen's entries for slot, in slot order; the count. at: the cursor's entry */
static s32 gevrHandicapEntries(s32 me, FrontHandicapEntry *e, s32 *at)
{
    s32 match = gevrNetConfigGet(CFG_HEALTH);
    s32 n = 0;
    s32 i;

    *at = 0;
    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        s32 value;
        s32 rule;
        s32 index;

        if (!netLobbySlotConnected(i)) continue;
        value = gevrHandicapValue(me, i);
        rule = gevrHandicapRule(me, i);
        index = netHandicapResolve(value, rule, match, netHealthCount());
        e[n].name = netGetSlotName(i);
        e[n].label = netHealthName(index);
        e[n].index = (s8)index;
        e[n].match = value == NET_HANDICAP_MATCH || (!netHandicapIsHosts(value) && !rule);
        e[n].host = s_hcapPick[me][i] >= 0 ? gevrHandicapHostSeat(me) && value != NET_HANDICAP_MATCH : netHandicapIsHosts(value);
        e[n].editable = gevrHandicapMayChange(me, i);
        e[n].me = i == me;
        if (i == s_hcapCursor[me]) *at = n;
        n++;
    }
    return n;
}

void gevrHandicapOpen(s32 slot)
{
    GevrSeatMenu m;
    s32 i;

    if (slot < 0 || slot >= MAX_PLAYER_COUNT || !lobbyDeathmatch() || !gevrNetSlotIsLocalHuman(slot)) return;
    if (!gevrSeatMenu(slot, &m)) return;
    for (i = 0; i < MAX_PLAYER_COUNT; i++) s_hcapPick[slot][i] = -1;
    s_hcapCursor[slot] = slot;
    s_hcapArmed[slot] = 0;
    s_hcapLeaving[slot] = 0;
    s_panelToSetup[slot] = 0;
    frontPanelPadReset(slot);
    gevrSeatShow(slot, MENU_HANDICAP);
}

/* every change made: the host's seat as the host's, a player's own as theirs */
static void gevrHandicapSend(s32 me)
{
    s32 i;

    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        s32 value = s_hcapPick[me][i];

        if (value < 0 || !gevrHandicapMayChange(me, i)) continue;
        if (gevrHandicapHostSeat(me)) netHostSetSlotHandicap(i, value);
        else netLobbySetHandicapSlot(i, value);
    }
}

static s32 gevrHandicapTick(s32 player_num)
{
    s32 match;
    s32 result;
    s32 dx;
    s32 dy;

    if (!gevrHandicapIsOpen(player_num)) return 0;
    if (!lobbyDeathmatch() || !gevrNetSlotIsLocalHuman(player_num))
    {
        gevrPanelClose(player_num, MENU_HANDICAP);
        return 0;
    }
    if (!s_hcapArmed[player_num])
    {
        if (gevrPanelHeld(player_num)) return 1;
        s_hcapArmed[player_num] = 1;
    }
    /* chosen or backed out: it stays until the button is let go, as the Select Stage grid */
    if (s_hcapLeaving[player_num])
    {
        if (!gevrPanelHeld(player_num)) gevrPanelClose(player_num, MENU_HANDICAP);
        return 1;
    }
    result = frontPanelPad(player_num, &dx, &dy);
    if (result > 0)
    {
        frontPanelChosenSound();
        gevrHandicapSend(player_num);
        s_hcapLeaving[player_num] = 1;
        return 1;
    }
    if (result < 0)
    {
        mpwatchPlayBeep();
        s_hcapLeaving[player_num] = 1;
        return 1;
    }
    match = gevrNetConfigGet(CFG_HEALTH);
    if (dy != 0 && gevrHandicapHostSeat(player_num))
    {
        /* the host's seat: the next player up or down the list */
        s32 next = s_hcapCursor[player_num];
        s32 k;

        for (k = 0; k < MAX_PLAYER_COUNT; k++)
        {
            next += dy;
            if (next < 0 || next >= MAX_PLAYER_COUNT) { next = s_hcapCursor[player_num]; break; }
            if (netLobbySlotConnected(next)) break;
        }
        if (next != s_hcapCursor[player_num])
        {
            s_hcapCursor[player_num] = next;
            mpwatchPlayBeep();
        }
    }
    if (!netLobbySlotConnected(s_hcapCursor[player_num])) s_hcapCursor[player_num] = player_num;
    if (dx != 0 && gevrHandicapMayChange(player_num, s_hcapCursor[player_num]))
    {
        s32 target = s_hcapCursor[player_num];
        s32 value = gevrHandicapValue(player_num, target);
        s32 rule = gevrHandicapRule(player_num, target);
        s32 shown = netHandicapResolve(value, rule, match, netHealthCount());
        s32 next = netHandicapStep(value, rule, match, netHealthCount(), dx);

        /* at either end of the table it stays */
        if (netHandicapResolve(next, 1, match, netHealthCount()) != shown)
        {
            s_hcapPick[player_num][target] = (s16)next;
            mpwatchPlayBeep();
        }
    }
    return 1;
}

static Gfx *gevrHandicapDraw(Gfx *gdl, s32 slot)
{
    FrontHandicapEntry e[MAX_PLAYER_COUNT];
    s32 at;
    s32 n = gevrHandicapEntries(slot, e, &at);
    const char *note = !gevrHandicapMayChange(slot, slot) && !gevrHandicapHostSeat(slot)
                           ? (gevrNetConfigGet(CFG_HANDICAPS) ? "THE HOST SET YOURS" : "THE HOST SETS HEALTH")
                           : "NEXT ROUND";

    s32 x;
    s32 y;
    s32 w;
    s32 h;

    gevrMenuPanelRect(&x, &y, &w, &h);
    return frontHandicapDraw(gdl, slot, e, n, at, note, x, y, w, h);
}

/* Teams: the screen's entries, in slot order */
static s32 gevrTeamSelEntries(s32 me, FrontTeamEntry *e)
{
    s32 n = 0;
    s32 i;

    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        if (!netLobbySlotConnected(i)) continue;
        e[n].name = netGetSlotName(i);
        e[n].team = (s8)netGetSlotTeam(i);
        e[n].next = netGetPhase() == 2 && netGetSlotTeam(i) != netGetActiveSlotTeam(i);
        e[n].bot = gevrNetSlotIsBot(i) != 0;
        e[n].me = i == me;
        n++;
    }
    return n;
}

/* the players on team other than slot (-1: all); bots give way (net_core.c netUpdateBots) */
static s32 gevrTeamSelPlayers(s32 slot, s32 team)
{
    s32 count = 0;
    s32 i;

    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        if (i != slot && netLobbySlotConnected(i) && !gevrNetSlotIsBot(i) && netGetSlotTeam(i) == team) count++;
    }
    return count;
}
/* a side is full at its capacity (net_core.c netSetSlotTeam refuses more) */
static s32 gevrTeamSelFull(s32 slot, s32 team)
{
    if (team != NET_TEAM_RED && team != NET_TEAM_BLUE) return 0;
    return gevrTeamSelPlayers(slot, team) >= netTeamCapacity(gevrNetConfigGet(CFG_SCENARIO), team);
}
/* more on a side than it takes (a joiner takes a bot's place and its side): the round waits for a change */
static s32 gevrTeamSelOver(s32 team)
{
    return gevrTeamSelPlayers(-1, team) > netTeamCapacity(gevrNetConfigGet(CFG_SCENARIO), team);
}

void gevrTeamSelOpen(s32 slot)
{
    GevrSeatMenu m;

    if (slot < 0 || slot >= MAX_PLAYER_COUNT || !lobbyDeathmatch() || !lobbyTeams() || !gevrNetSlotIsLocalHuman(slot)) return;
    if (!gevrSeatMenu(slot, &m)) return;
    s_teamSelPick[slot] = netGetSlotTeam(slot);
    s_teamSelArmed[slot] = 0;
    s_teamSelLeaving[slot] = 0;
    s_panelToSetup[slot] = 0;
    frontPanelPadReset(slot);
    gevrSeatShow(slot, MENU_TEAMSEL);
}

static s32 gevrTeamSelTick(s32 player_num)
{
    s32 result;
    s32 pick;
    s32 dx;
    s32 dy;

    if (!gevrTeamSelIsOpen(player_num)) return 0;
    if (!lobbyDeathmatch() || !lobbyTeams() || !gevrNetSlotIsLocalHuman(player_num))
    {
        gevrPanelClose(player_num, MENU_TEAMSEL);
        return 0;
    }
    if (!s_teamSelArmed[player_num])
    {
        if (gevrPanelHeld(player_num)) return 1;
        s_teamSelArmed[player_num] = 1;
    }
    if (s_teamSelLeaving[player_num])
    {
        if (!gevrPanelHeld(player_num)) gevrPanelClose(player_num, MENU_TEAMSEL);
        return 1;
    }
    result = frontPanelPad(player_num, &dx, &dy);
    pick = s_teamSelPick[player_num];
    if (result > 0)
    {
        if (pick != netGetSlotTeam(player_num) && gevrTeamSelFull(player_num, pick))
        {
            mpwatchPlayBeep();   /* that side is full: stay and choose again */
            return 1;
        }
        frontPanelChosenSound();
        if (pick != netGetSlotTeam(player_num)) netLobbySetTeamSlot(player_num, (u8)pick);
        s_teamSelLeaving[player_num] = 1;
        return 1;
    }
    if (result < 0)
    {
        mpwatchPlayBeep();
        s_teamSelLeaving[player_num] = 1;
        return 1;
    }
    if (dx < 0) pick = NET_TEAM_RED;
    else if (dx > 0) pick = NET_TEAM_BLUE;
    else if (dy > 0) pick = NET_TEAM_NONE;
    else if (dy < 0) pick = netGetSlotTeam(player_num);
    if (pick != s_teamSelPick[player_num])
    {
        s_teamSelPick[player_num] = pick;
        mpwatchPlayBeep();
    }
    return 1;
}

static Gfx *gevrTeamSelDraw(Gfx *gdl, s32 slot)
{
    FrontTeamEntry e[MAX_PLAYER_COUNT];
    s32 n = gevrTeamSelEntries(slot, e);
    s32 scenario = gevrNetConfigGet(CFG_SCENARIO);
    s32 pick = s_teamSelPick[slot];
    s32 x;
    s32 y;
    s32 w;
    s32 h;
    const char *note = pick != netGetSlotTeam(slot) && gevrTeamSelFull(slot, pick) ? "THAT TEAM IS FULL"
                       : gevrTeamSelOver(NET_TEAM_RED) || gevrTeamSelOver(NET_TEAM_BLUE) ? "TOO MANY ON A TEAM" : "NEXT ROUND";

    gevrMenuPanelRect(&x, &y, &w, &h);

    return frontTeamSelectDraw(gdl, slot, e, n, pick, netTeamCapacity(scenario, NET_TEAM_RED), netTeamCapacity(scenario, NET_TEAM_BLUE),
                               note, x, y, w, h);
}

/*
 * The Multiplayer Options folder in a match, online deathmatch: GoldenEye's
 * own folder (menu0E, front.c frontMpOptionsDraw) in the player's view (in
 * stereo on the head-locked HUD, gevrMenuPanelRect), driven by their pad,
 * where the match is set up: the host's rows (Players, Scenario, Level, Game
 * Length, Weapons...) dimmed at every other seat as the original dimmed a
 * locked row, and each player's own (Character, Health, Teams, Loadout).
 * It is a fourth view over the LOBBY page's rows (s_lobbyRows): the same
 * callbacks and authority, so nothing about the match's rules or the wire
 * changes. The rows come from gevr_mpoptions.c.
 *
 * Up and down move between the rows and onto the START tab; left and right
 * step a value; A opens a row's screen (Select Character, the stage photos,
 * Handicap, Teams, the Scenario list, the Custom, Bots and Loadout sheets) or
 * steps its value as the original's A did; B is PREVIOUS: a sheet back to
 * the folder, the folder back to where it came from. START is the START tab:
 * the host's START MATCH, anyone else's READY. A screen opened from here
 * comes back here. In the warmup the player waits held and unhurt while it
 * is up (net_core.c netSetSlotChoosing, as a drop-in choosing a character);
 * in a round it is a page of the watch like any other.
 */
enum { SETUP_SHEET_MAIN, SETUP_SHEET_CUSTOM, SETUP_SHEET_BOTS, SETUP_SHEET_LOADOUT, SETUP_SHEET_SCENARIO, SETUP_SHEET_COUNT };
enum { SETUP_OPEN_NONE, SETUP_OPEN_SCENARIO, SETUP_OPEN_STAGES, SETUP_OPEN_VOTE, SETUP_OPEN_CHARSEL, SETUP_OPEN_HANDICAP,
       SETUP_OPEN_TEAMS, SETUP_OPEN_CUSTOM, SETUP_OPEN_BOTS, SETUP_OPEN_LOADOUT, SETUP_OPEN_FULLMENU, SETUP_OPEN_ACTION };
#define SETUP_START_TAB GEVR_SETUP_ROW_COUNT   /* the main sheet's cursor on the START tab */
#define SETUP_LINES 24
typedef struct {
    s32 id;                  /* the main sheet's GEVR_SETUP_*, a sheet's index */
    const char *brief;       /* a quarter view's label (NULL: the label) */
    char label[40];
    char value[48];
    void (*step)(s32 dir);   /* left / right; NULL: none */
    u8 editable;
    u8 open;                 /* SETUP_OPEN_* */
} GevrSetupLine;
/* the folder's own rows; the others' labels are the original folder's (TITLE_STR_77 .. 83: "Scenario:",
 * "Characters"): a value with a colon, a row that opens a screen without. brief: a quarter view's
 * label, beside a value that takes half its width ("CHARACTE." was the original's cut) */
static const struct { u16 text; const char *label; void (*step)(s32); u8 open; const char *brief; } s_setupItems[GEVR_SETUP_ROW_COUNT] = {
    [GEVR_SETUP_PLAYERS]       = { getStringID(LTITLE, TITLE_STR_77_PLAYERS), NULL, rowPlayersStep, SETUP_OPEN_NONE, "Players:" },
    [GEVR_SETUP_SCENARIO]      = { getStringID(LTITLE, TITLE_STR_78_SCENARIO), NULL, rowScenarioStep, SETUP_OPEN_SCENARIO, "Mode:" },
    [GEVR_SETUP_LEVEL]         = { getStringID(LTITLE, TITLE_STR_79_LEVEL), NULL, rowMapStep, SETUP_OPEN_STAGES, "Level:" },
    [GEVR_SETUP_NEXT_MAP]      = { 0, "Next Map:", rowNextMapStep, SETUP_OPEN_VOTE, "Map:" },
    [GEVR_SETUP_LENGTH]        = { getStringID(LTITLE, TITLE_STR_80_GAMELENGTH), NULL, rowLengthStep, SETUP_OPEN_NONE, "Length:" },
    [GEVR_SETUP_WEAPONS]       = { getStringID(LTITLE, TITLE_STR_81_WEAPONS), NULL, rowWeaponsStep, SETUP_OPEN_NONE, "Guns:" },
    [GEVR_SETUP_CUSTOM]        = { 0, "Custom Set", NULL, SETUP_OPEN_CUSTOM, "Custom" },
    [GEVR_SETUP_NEXT_WEAPONS]  = { 0, "Next Weapons:", rowNextWeaponsStep, SETUP_OPEN_NONE, "Guns:" },
    [GEVR_SETUP_CHARACTER]     = { getStringID(LTITLE, TITLE_STR_82_CHARACTER), NULL, rowCharacterStep, SETUP_OPEN_CHARSEL, "Char." },
    [GEVR_SETUP_HEALTH]        = { getStringID(LTITLE, TITLE_STR_83_HEALTH), NULL, NULL, SETUP_OPEN_HANDICAP, "Health" },
    [GEVR_SETUP_HANDICAPS]     = { 0, "Handicaps:", rowHandicapsStep, SETUP_OPEN_NONE, "Handicaps:" },
    [GEVR_SETUP_TEAMS]         = { 0, "Teams", rowTeamStep, SETUP_OPEN_TEAMS, "Team" },
    [GEVR_SETUP_DUAL_WIELD]    = { 0, "Dual Wield:", rowDualStep, SETUP_OPEN_NONE, "Dual:" },
    [GEVR_SETUP_LOADOUTS]      = { 0, "Loadouts:", rowLoadoutsStep, SETUP_OPEN_NONE, "Loadouts:" },
    [GEVR_SETUP_LOADOUT]       = { 0, "Your Loadout", NULL, SETUP_OPEN_LOADOUT, "My Guns" },
    [GEVR_SETUP_BOTS]          = { 0, "Bots", NULL, SETUP_OPEN_BOTS, "Bots" },
    [GEVR_SETUP_NEXT_ROUND]    = { 0, "Next Round:", rowNextRoundStep, SETUP_OPEN_NONE, "Next:" },
    [GEVR_SETUP_FRIENDLY_FIRE] = { 0, "Friendly Fire:", rowFriendlyFireStep, SETUP_OPEN_NONE, "Fr. Fire:" },
    [GEVR_SETUP_RETURN]        = { 0, "Return to Lobby", rowReturnStep, SETUP_OPEN_ACTION, "Lobby" },
    [GEVR_SETUP_DROP_OUT]      = { 0, "Drop Out", rowDropOutStep, SETUP_OPEN_ACTION, "Drop Out" },
    [GEVR_SETUP_FULL_MENU]     = { 0, "Full Menu", NULL, SETUP_OPEN_FULLMENU, "Full Menu" },
    [GEVR_SETUP_HORDE_MAP]     = { getStringID(LTITLE, TITLE_STR_79_LEVEL), NULL, rowHordeMapStep, SETUP_OPEN_STAGES, "Level:" },
    [GEVR_SETUP_STARTING_WAVE] = { 0, "Starting Wave:", rowHordeWaveStep, SETUP_OPEN_NONE, "Wave:" },
    [GEVR_SETUP_AI_ALLY]       = { 0, "AI Ally:", rowAllyStep, SETUP_OPEN_NONE, "Ally:" },
    [GEVR_SETUP_LOBBY]         = { 0, "Lobby:", rowLobbyStep, SETUP_OPEN_NONE, "Lobby:" },
    [GEVR_SETUP_WARMUP]        = { 0, "Warmup:", rowWarmupStep, SETUP_OPEN_NONE, "Warmup:" },
    [GEVR_SETUP_LEAVE]         = { 0, "Leave Match", rowLeaveStep, SETUP_OPEN_ACTION, "Leave" },
};
static s32 s_setupSheet[MAX_PLAYER_COUNT];
static s32 s_setupCursor[MAX_PLAYER_COUNT][SETUP_SHEET_COUNT];   /* the main sheet: a row id or SETUP_START_TAB; else an index */
static s32 s_setupFrom[MAX_PLAYER_COUNT];      /* the page PREVIOUS goes back to; -1: the menu closes */
static s32 s_setupLeaving[MAX_PLAYER_COUNT];   /* closes once the button that closed it is let go */
static s32 s_setupHeld[MAX_PLAYER_COUNT];      /* held in the warmup (netSetSlotChoosing) */
static s32 s_setupOwed[MAX_PLAYER_COUNT];      /* opens by itself at the next warmup's load (gevrMpOptionsAutoTick) */
static s32 s_setupStartQueued[MAX_PLAYER_COUNT];   /* a START input.c took for the folder (Horde: gevrMpOptionsQueueStart) */
extern const char *netGetLobbyCode(void);

static s32 gevrSetupHostSeat(s32 slot) { return netIsHost() && slot == netGetLocalSlot(); }

/* the folder's row state at slot (gevr_mpoptions.c) */
static s32 gevrSetupRows(s32 slot, u8 *ids, u8 *hostonly)
{
    GevrSetupState s;

    s.mode = !gevrNetConfigPve() ? GEVR_SETUP_MODE_DEATHMATCH : gevrNetConfigCoop() ? GEVR_SETUP_MODE_COOP : GEVR_SETUP_MODE_HORDE;
    s.host = gevrSetupHostSeat(slot);
    s.playing = netGetPhase() == 2;
    s.teams = lobbyTeams();
    /* the lobby's first gathering is the host's choice; after a match it is the vote's */
    s.voting = lobbyVoting() && (!gevrInMatchLobby() || netVoteRequested());
    s.goldengun = !lobbyNotGoldenGun();
    s.yolt = !lobbyNotYolt();
    s.custom = lobbyCustomSet();
    s.loadouts = lobbyLoadouts();
    s.can_return = lobbyCanReturn();
    s.split = netLocalSlotCount() > 1;
    s.guest = netLocalSeatOfSlot(slot) > 0;
    s.ally = lobbyAlly();
    s.lobby = gevrInMatchLobby();
    s.lobby_rule = gevrNetConfigGet(CFG_LOBBY) != 0;
    return gevrMpOptionsRowSet(&s, ids, hostonly);
}

/* whether slot can have the folder: an online deathmatch's or Horde run's local player, in a stage */
s32 gevrMpOptionsAvailable(s32 slot)
{
    u8 ids[GEVR_SETUP_ROW_COUNT];
    u8 hostonly[GEVR_SETUP_ROW_COUNT];

    if (!netIsActive() || slot < 0 || slot >= MAX_PLAYER_COUNT || !gevrNetSlotIsLocalHuman(slot)) return 0;
    /* the pre-match lobby's seats on the title stage, else a player in a stage */
    if (!gevrInMatchLobby() &&
        (g_playerPointers[slot] == NULL || bossGetStageNum() == LEVELID_TITLE || (gevrCoopActive() && !netHordeActive()))) return 0;
    return gevrSetupRows(slot, ids, hostonly) > 0;
}

/*
 * The folder is a split screen's menu, in each player's own view. One player
 * on the screen, flat or in a headset, has the full pause window instead,
 * with the whole setup on it (owner, 2026-11-25, Quest: "it shows the small
 * pause panel... it should show full panel if not in splitscreen").
 */
static s32 gevrMpOptionsAsMenu(void) { return netLocalSlotCount() > 1 || gevrInMatchLobby(); }   /* the lobby: always the folder */

/* where the folder opens by itself (arriving, RETURN TO LOBBY, Horde's setup
 * break): a split screen's folder, or one player's full pause window */
static void gevrMpOptionsOrPause(s32 slot)
{
    struct player *pl = slot >= 0 && slot < MAX_PLAYER_COUNT ? g_playerPointers[slot] : NULL;

    if (pl == NULL || pl->mpmenuon) return;
    if (gevrMpOptionsAsMenu())
    {
        gevrMpOptionsOpen(slot, -1);
        return;
    }
    pl->mpmenuon = TRUE;   /* the pause window shows over any page but the game's screens */
    pl->mpmenumode = MENU_LOBBY;
}

s32 gevrMpOptionsIsOpen(s32 slot) { return gevrSeatMode(slot) == MENU_SETUP; }

static void gevrSetupLine(GevrSetupLine *l, s32 id, const char *label, void (*value)(char *, s32), void (*step)(s32),
                          s32 editable, s32 open)
{
    /* the watch's capitals in the folder's words */
    static const char *const words[][2] = { { "ON", "On" }, { "OFF", "Off" }, { "NO VOTE", "No vote" }, { "NONE", "None" } };
    u32 k;

    l->id = id;
    l->brief = NULL;
    snprintf(l->label, sizeof(l->label), "%s", label);
    l->value[0] = '\0';
    if (value != NULL) value(l->value, sizeof(l->value));
    for (k = 0; k < sizeof(words) / sizeof(words[0]); k++)
    {
        if (!strcmp(l->value, words[k][0])) snprintf(l->value, sizeof(l->value), "%s", words[k][1]);
    }
    l->step = step;
    l->editable = editable != 0;
    l->open = (u8)open;
}

/* a LOBBY row by its step, for a sheet's rows */
static const GevrMenuRow *gevrLobbyRowOf(void (*step)(s32))
{
    u32 i;

    for (i = 0; i < sizeof(s_lobbyRows) / sizeof(s_lobbyRows[0]); i++)
    {
        if (s_lobbyRows[i].step == step) return &s_lobbyRows[i];
    }
    return NULL;
}

static void rowSetupEmpty(char *b, s32 n) { if (n > 0) b[0] = '\0'; }
static void rowMyLoadoutValue(char *b, s32 n)
{
    s32 me = menuActingSlot();
    s32 k;
    s32 any = 0;

    for (k = 0; k < 4; k++) any |= gevrNetSlotLoadout(me, k) != 0;
    snprintf(b, n, "%s", any ? netItemName(gevrNetSlotLoadout(me, 0)) : "NONE");
}

/* The sheet's lines for slot (its own player: menuActingSlot) and their count */
static s32 gevrSetupLines(s32 slot, s32 sheet, GevrSetupLine *out)
{
    static void (*const customs[4])(s32) = { rowCustom0Step, rowCustom1Step, rowCustom2Step, rowCustom3Step };
    static void (*const loadouts[4])(s32) = { rowLoadout0Step, rowLoadout1Step, rowLoadout2Step, rowLoadout3Step };
    static void (*const bots[3])(s32) = { rowBotModeStep, rowBotCountStep, rowBotDiffStep };
    static const char *const botLabels[3] = { "Bots:", "Bot Count:", "Difficulty:" };
    const s32 host = gevrSetupHostSeat(slot);
    char label[40];
    s32 n = 0;
    s32 i;

    switch (sheet)
    {
        case SETUP_SHEET_CUSTOM:
            for (i = 0; i < 4; i++)
            {
                const GevrMenuRow *r = gevrLobbyRowOf(customs[i]);
                snprintf(label, sizeof(label), "Custom %d:", (int)i + 1);
                if (r != NULL) gevrSetupLine(&out[n++], i, label, r->value, r->step, host, SETUP_OPEN_NONE);
            }
            return n;
        case SETUP_SHEET_LOADOUT:
            for (i = 0; i < 4; i++)
            {
                const GevrMenuRow *r = gevrLobbyRowOf(loadouts[i]);
                snprintf(label, sizeof(label), "Gun %d:", (int)i + 1);
                if (r != NULL) gevrSetupLine(&out[n++], i, label, r->value, r->step, 1, SETUP_OPEN_NONE);
            }
            return n;
        case SETUP_SHEET_BOTS:
            for (i = 0; i < 3; i++)
            {
                const GevrMenuRow *r = gevrLobbyRowOf(bots[i]);
                if (r != NULL && gevrRowVisible(r)) gevrSetupLine(&out[n++], i, botLabels[i], r->value, r->step, host, SETUP_OPEN_NONE);
            }
            return n;
        case SETUP_SHEET_SCENARIO:
            for (i = 0; i < netScenarioCount() && n < SETUP_LINES; i++)
            {
                gevrSetupLine(&out[n++], i, netScenarioName(i), NULL, NULL, host && gevrNetScenarioAccepted(i), SETUP_OPEN_ACTION);
            }
            return n;
        default:
            break;
    }
    {
        u8 ids[GEVR_SETUP_ROW_COUNT];
        u8 hostonly[GEVR_SETUP_ROW_COUNT];
        s32 count = gevrSetupRows(slot, ids, hostonly);

        for (i = 0; i < count && n < SETUP_LINES; i++)
        {
            const s32 id = ids[i];
            void (*step)(s32) = s_setupItems[id].step;
            void (*value)(char *, s32) = rowSetupEmpty;
            const GevrMenuRow *r = step != NULL ? gevrLobbyRowOf(step) : NULL;
            const char *name = s_setupItems[id].label != NULL ? s_setupItems[id].label : (const char *)langGet(s_setupItems[id].text);
            s32 editable = !hostonly[i] || host;

            if (r != NULL) value = r->value;
            switch (id)
            {
                case GEVR_SETUP_HEALTH:
                    /* the host's: the match's health; anyone else's: their own */
                    if (host) { value = rowHealthValue; step = rowHealthStep; }
                    else { value = rowMyHealthValue; step = netSlotHandicapEditable(slot) ? rowMyHealthStep : NULL; }
                    break;
                case GEVR_SETUP_CUSTOM: value = rowSetupEmpty; break;
                case GEVR_SETUP_LOADOUT: value = rowMyLoadoutValue; break;
                case GEVR_SETUP_BOTS: value = rowBotModeValue; break;
                case GEVR_SETUP_RETURN:
                    value = rowReturnValue;
                    if (gevrInMatchLobby()) name = "Cancel Start";   /* the lobby's countdown */
                    break;
                case GEVR_SETUP_LEAVE: value = rowLeaveValue; break;
                default: break;
            }
            gevrSetupLine(&out[n], id, name, value, step, editable, s_setupItems[id].open);
            out[n++].brief = s_setupItems[id].brief;
        }
    }
    return n;
}

/* the main sheet's cursor as an index (count: the START tab), from the row id kept */
static s32 gevrSetupCursorIndex(s32 slot, s32 sheet, const GevrSetupLine *lines, s32 count)
{
    s32 c = s_setupCursor[slot][sheet];
    s32 i;

    if (sheet != SETUP_SHEET_MAIN) return c < 0 ? 0 : c >= count ? count - 1 : c;
    if (c == SETUP_START_TAB) return count;
    for (i = 0; i < count; i++)
    {
        if (lines[i].id == c) return i;
    }
    /* the row went (a scenario without it): the next one that stayed */
    for (i = 0; i < count; i++)
    {
        if (lines[i].id > c) return i;
    }
    return count;
}

/* Horde's START tab is its setup break's: START WAVE for the host, WAITING FOR HOST elsewhere */
static s32 gevrSetupHasStartTab(void) { return !netHordeActive() || netHordeSetupBreak(); }

/* The START tab: the host's START MATCH (an empty warmup restarts with the
 * changes), anyone else's READY; Horde's START WAVE (or the restart on the
 * new map); whether it can be pressed now. A word may give a shorter form
 * after a '|' for a short tab (front.c frontMpOptionsTab). */
static const char *gevrSetupStartTab(s32 slot, s32 *usable)
{
    static char word[48];

    if (netHordeActive())
    {
        *usable = gevrSetupHostSeat(slot) && netHordeHostCanStart();
        if (!gevrSetupHostSeat(slot)) return "WAITING FOR HOST|WAITING|WAIT";
        if (netHordeRestartPending()) return "RESTART ON NEW MAP|RESTART";
        snprintf(word, sizeof(word), "START WAVE %d|WAVE %d", gevrNetConfigGet(CFG_HORDE_START_WAVE), gevrNetConfigGet(CFG_HORDE_START_WAVE));
        return word;
    }
    if (gevrSetupHostSeat(slot))
    {
        *usable = lobbyCanStart();
        return (const char *)langGet(getStringID(LTITLE, TITLE_STR_04_START));
    }
    /* a team scenario's player readies once on a team (net_core.c netLobbySetReadySlot) */
    *usable = netSlotReady(slot) || !lobbyTeams() || netGetSlotTeam(slot) != NET_TEAM_NONE;
    return "READY";
}

static void gevrSetupStartPress(s32 slot)
{
    s32 usable;

    gevrSetupStartTab(slot, &usable);
    if (!usable)
    {
        mpwatchPlayBeep();
        return;
    }
    if (netHordeActive()) rowHordeStartStep(0);
    else if (gevrSetupHostSeat(slot)) rowStartStep(0);
    else netLobbySetReadySlot(slot, !netSlotReady(slot));
    frontPanelChosenSound();
}

/*
 * The line along the bottom: what START does now, the countdown, the lobby
 * code. A pad's START presses the START tab; the headset's Menu leaves the
 * folder (input.c), so there it is the tab, reached with the stick and A.
 */
static void gevrSetupInfo(s32 slot, char *b, s32 n, s32 narrow)
{
    const char *code = netGetLobbyCode();
    const char *go = g_gevrStereo ? "START TAB: " : "START: ";
    char state[64];
    s32 secs = netCountdownSecondsLeft();

    if (netHordeActive())
    {
        const s32 wave = gevrNetConfigGet(CFG_HORDE_START_WAVE);

        if (!netHordeSetupBreak())
        {
            /* a run under way: the host's changes wait for Retry Horde */
            snprintf(state, sizeof(state), "%s", gevrSetupHostSeat(slot) ? (narrow ? "NEXT RUN" : "CHANGES APPLY ON RETRY") : "");
        }
        else if (!gevrSetupHostSeat(slot))
        {
            snprintf(state, sizeof(state), "%s", "WAITING FOR HOST");
        }
        else if (netHordeRestartPending())
        {
            snprintf(state, sizeof(state), "%s%s", go, narrow ? "NEW MAP" : "RESTART ON THE NEW MAP");
        }
        else if (narrow)
        {
            snprintf(state, sizeof(state), "%sWAVE %d", go, (int)wave);
        }
        else
        {
            snprintf(state, sizeof(state), "%sBEGIN WAVE %d", go, (int)wave);
        }
    }
    else if (secs > 0)
    {
        snprintf(state, sizeof(state), narrow || !gevrSetupHostSeat(slot) ? "STARTING IN %d" : "STARTING IN %d - CHANGES UN-READY ALL", (int)secs);
    }
    else if (gevrSetupHostSeat(slot))
    {
        const char *what = netGetPhase() == 2 ? NULL
                           : netGetConnectedPlayerCount() == 1 && gevrInMatchLobby() ? (narrow ? "PRACTICE" : "PRACTICE ON THE MAP")
                           : netGetConnectedPlayerCount() == 1 ? (narrow ? "RESTART" : "RESTART WARMUP WITH CHANGES")
                           : netHostStartRequested() ? NULL
                           /* a START with changes reloads the warmup first (net_core.c netTryHostStartRequest) */
                           : netWarmupSettingsPending() ? (narrow ? "LOAD" : "LOAD THE CHANGES INTO THE WARMUP")
                           : netRoundRosterReady() ? (narrow ? "MATCH" : "BEGIN THE MATCH")
                           : (narrow ? "ASK READY" : "REQUEST READY");

        if (what != NULL) snprintf(state, sizeof(state), "%s%s", go, what);
        else snprintf(state, sizeof(state), "%s", netGetPhase() == 2 ? (narrow ? "NEXT ROUND" : "CHANGES APPLY NEXT ROUND") : "WAITING FOR READY");
    }
    else if (netSlotReady(slot))
    {
        snprintf(state, sizeof(state), "%s", narrow ? "READY" : "READY - WAITING FOR THE HOST");
    }
    else
    {
        s32 usable;
        gevrSetupStartTab(slot, &usable);
        if (!usable) snprintf(state, sizeof(state), "%s", narrow ? "CHOOSE A TEAM" : "CHOOSE A TEAM, THEN READY");
        else if (netGetPhase() == 2 && narrow) snprintf(state, sizeof(state), "%s", "NEXT ROUND");
        else snprintf(state, sizeof(state), "%s%s", go, netGetPhase() == 2 ? "READY FOR THE NEXT ROUND" : narrow ? "READY" : "READY UP");
    }
    if (gevrInMatchLobby() && secs <= 0 && !netHordeActive() && gevrLobbySeatCount() > 1)
    {
        /* the lobby: how many are READY (one player's screen lists them beside the folder) */
        s32 humans = 0;
        s32 ready = netLobbyReadyCount(&humans);
        char tally[64];

        snprintf(tally, sizeof(tally), "%s %d/%d", state, (int)ready, (int)humans);
        snprintf(state, sizeof(state), "%s", tally);
    }
    if (!narrow && code != NULL && code[0]) snprintf(b, n, state[0] ? "%s   CODE %s" : "%sCODE %s", state, code);
    else snprintf(b, n, "%s", state);
}

void gevrMpOptionsOpen(s32 slot, s32 from)
{
    if (!gevrMpOptionsAvailable(slot)) return;
    s_setupSheet[slot] = SETUP_SHEET_MAIN;
    s_setupFrom[slot] = from;
    s_setupArmed[slot] = 0;
    s_setupLeaving[slot] = 0;
    s_setupOwed[slot] = 0;
    s_setupStartQueued[slot] = 0;
    s_panelToSetup[slot] = 0;
    frontPanelPadReset(slot);
    gevrSeatShow(slot, MENU_SETUP);
}

/* back: to the page it came from (or closed); else closed */
static void gevrMpOptionsClose(s32 slot, s32 back)
{
    s_setupLeaving[slot] = 0;
    if (gevrInMatchLobby()) return;   /* the lobby's folder stays up: there is no match under it */
    if (back && s_setupFrom[slot] >= 0)
    {
        gevrSeatSetMode(slot, s_setupFrom[slot]);
        return;
    }
    gevrSeatHide(slot);   /* into the match */
    gevrSeatSetMode(slot, MENU_STATUS);
}

/* A sheet's row's screen or sheet; whether one opened */
static s32 gevrSetupOpen(s32 slot, const GevrSetupLine *l)
{
    struct player *pl = g_playerPointers[slot];

    switch (l->open)
    {
        case SETUP_OPEN_SCENARIO:
            if (!l->editable) return 0;
            s_setupSheet[slot] = SETUP_SHEET_SCENARIO;
            s_setupCursor[slot][SETUP_SHEET_SCENARIO] = gevrNetConfigGet(CFG_SCENARIO);
            return 1;
        case SETUP_OPEN_CUSTOM:
        case SETUP_OPEN_BOTS:
        case SETUP_OPEN_LOADOUT:
            /* the host's sheets read-only elsewhere */
            s_setupSheet[slot] = l->open == SETUP_OPEN_CUSTOM ? SETUP_SHEET_CUSTOM : l->open == SETUP_OPEN_BOTS ? SETUP_SHEET_BOTS : SETUP_SHEET_LOADOUT;
            s_setupCursor[slot][s_setupSheet[slot]] = 0;
            return 1;
        case SETUP_OPEN_STAGES:
        case SETUP_OPEN_VOTE:
            if (!l->editable) return 0;
            gevrStageSelOpen(slot, l->open == SETUP_OPEN_VOTE);
            if (!gevrStageSelIsOpen(slot)) return 0;
            break;
        case SETUP_OPEN_CHARSEL:
            gevrCharSelOpen(slot, 0);
            if (!gevrCharSelIsOpen(slot)) return 0;
            break;
        case SETUP_OPEN_HANDICAP:
            gevrHandicapOpen(slot);
            if (!gevrHandicapIsOpen(slot)) return 0;
            break;
        case SETUP_OPEN_TEAMS:
            gevrTeamSelOpen(slot);
            if (!gevrTeamSelIsOpen(slot)) return 0;
            break;
        case SETUP_OPEN_FULLMENU:
            /* one player: the pause window (any page but the game's screens); a split screen's first seat: the full panel */
            if (netLocalSlotCount() > 1) gevrNativePauseOpenPanel();
            else if (pl != NULL) pl->mpmenumode = MENU_LOBBY;
            return 1;
        default:
            return 0;
    }
    s_panelToSetup[slot] = 1;   /* the screen comes back here */
    return 1;
}

/* The folder's frame for the player ticking; whether it had the pad */
static s32 gevrMpOptionsTick(s32 player_num)
{
    GevrSetupLine lines[SETUP_LINES];
    s32 sheet;
    s32 count;
    s32 total;
    s32 cursor;
    s32 result;
    s32 queued;
    s32 tab;
    s32 dx;
    s32 dy;

    if (!gevrMpOptionsIsOpen(player_num)) return 0;
    if (!gevrMpOptionsAvailable(player_num))
    {
        gevrMpOptionsClose(player_num, 0);
        return 0;
    }
    if (!s_setupArmed[player_num])
    {
        if (gevrPanelHeld(player_num)) return 1;
        s_setupArmed[player_num] = 1;
        frontPanelPadReset(player_num);
    }
    if (s_setupLeaving[player_num])
    {
        if (!gevrPanelHeld(player_num)) gevrMpOptionsClose(player_num, 1);
        return 1;
    }
    sheet = s_setupSheet[player_num];
    count = gevrSetupLines(player_num, sheet, lines);
    if (sheet != SETUP_SHEET_MAIN && count == 0)
    {
        s_setupSheet[player_num] = SETUP_SHEET_MAIN;   /* the bots' sheet once a round starts */
        return 1;
    }
    tab = sheet == SETUP_SHEET_MAIN && gevrSetupHasStartTab();
    total = count + tab;
    cursor = gevrSetupCursorIndex(player_num, sheet, lines, count);
    if (cursor >= total) cursor = total - 1;   /* Horde's tab went with the setup break */
    queued = s_setupStartQueued[player_num];
    s_setupStartQueued[player_num] = 0;
    if (sheet == SETUP_SHEET_MAIN && (queued || joyGetButtonsPressedThisFrame(player_num, START_BUTTON)))
    {
        if (!tab)
        {
            /* no START tab (a Horde run under way): START leaves, as it leaves the watch */
            mpwatchPlayBeep();
            gevrMpOptionsClose(player_num, 0);
            return 1;
        }
        s_setupCursor[player_num][sheet] = SETUP_START_TAB;
        gevrSetupStartPress(player_num);
        return 1;
    }
    result = frontPanelPad(player_num, &dx, &dy);
    if (result < 0)
    {
        mpwatchPlayBeep();
        if (sheet != SETUP_SHEET_MAIN) s_setupSheet[player_num] = SETUP_SHEET_MAIN;
        else s_setupLeaving[player_num] = 1;
        return 1;
    }
    if (dy != 0 && total > 0)
    {
        cursor = gevrCycled(cursor, dy, total);
        mpwatchPlayBeep();
    }
    if (cursor < count && dx != 0 && lines[cursor].editable && lines[cursor].step != NULL &&
        lines[cursor].open != SETUP_OPEN_ACTION)
    {
        lines[cursor].step(dx);
        mpwatchPlayBeep();
    }
    else if (cursor < count && dx != 0)
    {
        mpwatchPlayBeep();
    }
    if (sheet == SETUP_SHEET_MAIN) s_setupCursor[player_num][sheet] = cursor < count ? lines[cursor].id : SETUP_START_TAB;
    else s_setupCursor[player_num][sheet] = cursor;
    if (result > 0)
    {
        const GevrSetupLine *l = cursor < count ? &lines[cursor] : NULL;

        if (l == NULL)
        {
            gevrSetupStartPress(player_num);
        }
        else if (sheet == SETUP_SHEET_SCENARIO)
        {
            /* the list: a scenario the match can take, then back to the folder */
            if (!l->editable)
            {
                mpwatchPlayBeep();
                return 1;
            }
            gevrNetConfigSet(CFG_SCENARIO, l->id);
            frontPanelChosenSound();
            s_setupSheet[player_num] = SETUP_SHEET_MAIN;
        }
        else if (l->open == SETUP_OPEN_ACTION)
        {
            if (l->editable && l->step != NULL)
            {
                l->step(0);
                frontPanelChosenSound();
            }
            else mpwatchPlayBeep();
        }
        else if (l->open != SETUP_OPEN_NONE)
        {
            if (gevrSetupOpen(player_num, l))
            {
                mpwatchPlayBeep();
                s_setupArmed[player_num] = 0;   /* back from a sheet or a screen: the press that opened it is not the next choice */
            }
            else
            {
                mpwatchPlayBeep();
            }
        }
        else if (l->editable && l->step != NULL)
        {
            l->step(1);   /* A steps the value forward, as the original's A cycled the game length and the weapons */
            mpwatchPlayBeep();
        }
        else
        {
            mpwatchPlayBeep();
        }
    }
    return 1;
}

static Gfx *gevrMpOptionsDraw(Gfx *gdl, s32 slot)
{
    static const char *const titles[SETUP_SHEET_COUNT] = { NULL, "Custom Weapons", "Bots", "Your Loadout", NULL };
    GevrSetupLine lines[SETUP_LINES];
    FrontOptionRow rows[SETUP_LINES];
    char info[96];
    const char *start = NULL;
    s32 usable = 1;
    s32 sheet = s_setupSheet[slot];
    s32 count = gevrSetupLines(slot, sheet, lines);
    s32 cursor = gevrSetupCursorIndex(slot, sheet, lines, count);
    s32 i;
    s32 x;
    s32 y;
    s32 w;
    s32 h;

    if (sheet == SETUP_SHEET_MAIN && !gevrSetupHasStartTab() && cursor >= count) cursor = count - 1;
    gevrMenuPanelRect(&x, &y, &w, &h);
    gevrSetupInfo(slot, info, sizeof(info), w < 220);
    if (sheet == SETUP_SHEET_SCENARIO)
    {
        const char *names[SETUP_LINES];
        u8 eligible[SETUP_LINES];

        for (i = 0; i < count; i++)
        {
            names[i] = lines[i].label;
            eligible[i] = lines[i].editable;
        }
        return frontScenarioSelectDraw(gdl, slot, names, eligible, count, cursor, gevrNetConfigGet(CFG_SCENARIO), info, x, y, w, h);
    }
    for (i = 0; i < count; i++)
    {
        /* a quarter view (the layout's compact, front.c frontMpOptionsLayout): the short labels */
        rows[i].label = w < 220 && lines[i].brief != NULL ? lines[i].brief : lines[i].label;
        rows[i].value = lines[i].value;
        rows[i].dim = !lines[i].editable;   /* the host's, at another seat: the original's locked row */
        rows[i].more = lines[i].open != SETUP_OPEN_NONE && lines[i].open != SETUP_OPEN_ACTION;
    }
    if (sheet == SETUP_SHEET_MAIN && gevrSetupHasStartTab()) start = gevrSetupStartTab(slot, &usable);
    return frontMpOptionsDraw(gdl, slot, titles[sheet], rows, count, cursor, start, !usable, info, x, y, w, h);
}

/*
 * Every frame for each local player: in the warmup, a player at the folder
 * (or a screen it opened) waits held and unhurt, as a drop-in choosing a
 * character does; let go when they leave it or the round begins.
 */
static void gevrMpOptionsHoldTick(s32 slot)
{
    s32 want = 0;

    if (slot < 0 || slot >= MAX_PLAYER_COUNT) return;
    if (netIsActive() && gevrNetSlotIsLocalHuman(slot) && netGetPhase() == 1 && !gevrNetConfigPve())
    {
        want = gevrMpOptionsIsOpen(slot) || (gevrMenuPanelIsOpen(slot) && s_panelToSetup[slot]);
    }
    if (want)
    {
        netSetSlotChoosing(slot, 1);
        s_setupHeld[slot] = 1;
    }
    else if (s_setupHeld[slot])
    {
        s_setupHeld[slot] = 0;
        if (!(gevrCharSelIsOpen(slot) && s_charSelDropIn[slot])) netSetSlotChoosing(slot, 0);
    }
}

/*
 * The folder opens by itself (owner, 2026-11-24): on arriving in the match
 * (the first warmup this machine loads after hosting or joining it) and after
 * the host's RETURN TO LOBBY, for each local player; and when the host asks
 * for the next round's votes, on NEXT MAP for each one who has not voted
 * yet (at a match's end once they leave the results: right or START there).
 * Not every warmup between rounds: the round notice says what is wanted.
 */
static unsigned s_setupSession;   /* net_core.c netSessionCount: the session the openings below are for */
static unsigned s_setupLoads;     /* the stage load count an owed opening waits to pass */
static unsigned s_setupReturns;   /* netLobbyReturnCount seen */
static s32 s_setupVote;           /* netVoteRequested seen */

static void gevrSetupOweAll(unsigned loads)
{
    s32 i;

    s_setupLoads = loads;
    for (i = 0; i < MAX_PLAYER_COUNT; i++) s_setupOwed[i] = gevrNetSlotIsLocalHuman(i) != 0;
}

/* the vote's folder, on NEXT MAP; over another page of the watch (PREVIOUS goes back to it), not over a game screen */
static void gevrMpOptionsVoteOpen(s32 slot)
{
    struct player *pl = slot >= 0 && slot < MAX_PLAYER_COUNT ? g_playerPointers[slot] : NULL;

    if (pl == NULL || !gevrMpOptionsAvailable(slot)) return;
    if (!gevrMpOptionsAsMenu())
    {
        gevrMpOptionsOrPause(slot);   /* one player: the pause window's Match tab has the vote */
        return;
    }
    if (gevrMenuPanelIsOpen(slot) && !gevrMpOptionsIsOpen(slot)) return;
    if (!gevrMpOptionsIsOpen(slot)) gevrMpOptionsOpen(slot, pl->mpmenuon ? pl->mpmenumode : -1);
    if (!gevrMpOptionsIsOpen(slot)) return;
    s_setupSheet[slot] = SETUP_SHEET_MAIN;
    s_setupCursor[slot][SETUP_SHEET_MAIN] = GEVR_SETUP_NEXT_MAP;
}

/*
 * Horde: each local player's folder opens by itself in a run's setup break
 * (the first wave waits for the host's START WAVE): on arriving, and after
 * Retry Horde or a restart on another map lands in a new one; once per stage
 * load, so a player who leaves it plays on. START WAVE ends the break: the
 * folders close for the countdown to the first wave (a game screen opened
 * from one stays, its player choosing).
 */
static unsigned s_setupHordeLoads;   /* the stage load whose setup break owed its openings */
static s32 s_setupHordeBreak;        /* netHordeSetupBreak seen */

static void gevrMpOptionsHordeAutoTick(s32 slot)
{
    struct player *pl;
    s32 brk = netHordeSetupBreak();
    s32 i;

    if (netSessionCount() != s_setupSession)
    {
        s_setupSession = netSessionCount();
        s_setupHordeLoads = netStageLoadCount() - 1;
        s_setupHordeBreak = 0;
    }
    if (s_setupHordeBreak && !brk)
    {
        for (i = 0; i < MAX_PLAYER_COUNT; i++)
        {
            s_setupOwed[i] = 0;
            if (gevrNetSlotIsLocalHuman(i) && gevrMpOptionsIsOpen(i)) gevrMpOptionsClose(i, 0);
        }
    }
    s_setupHordeBreak = brk;
    if (netHordeSetupBreak() && netStageLoadCount() != s_setupHordeLoads)
    {
        s_setupHordeLoads = netStageLoadCount();
        for (i = 0; i < MAX_PLAYER_COUNT; i++) s_setupOwed[i] = gevrNetSlotIsLocalHuman(i) != 0;
    }
    if (slot < 0 || slot >= MAX_PLAYER_COUNT || !s_setupOwed[slot]) return;
    pl = g_playerPointers[slot];
    if (pl == NULL || pl->bonddead || !netHordeSetupBreak()) return;
    s_setupOwed[slot] = 0;
    gevrMpOptionsOrPause(slot);
}

static void gevrMpOptionsAutoTick(s32 slot)
{
    struct player *pl;
    s32 vote;
    s32 i;

    if (!netIsActive()) return;
    if (netHordeActive())
    {
        gevrMpOptionsHordeAutoTick(slot);
        return;
    }
    if (gevrNetConfigPve()) return;
    if (netSessionCount() != s_setupSession)
    {
        /* arriving: the stage loaded now counts (or, joining a round, the next warmup's) */
        s_setupSession = netSessionCount();
        s_setupReturns = netLobbyReturnCount();
        s_setupVote = netVoteRequested();
        gevrSetupOweAll(netStageLoadCount() - 1);
    }
    if (netLobbyReturnCount() != s_setupReturns)
    {
        s_setupReturns = netLobbyReturnCount();
        gevrSetupOweAll(netStageLoadCount());   /* after the reload into the lobby's warmup */
    }
    vote = netVoteRequested();
    if (vote != s_setupVote)
    {
        s_setupVote = vote;
        for (i = 0; vote && !g_gameOverFlag && i < MAX_PLAYER_COUNT; i++)
        {
            if (gevrNetSlotIsLocalHuman(i) && netGetVote(0, i) < 0) gevrMpOptionsVoteOpen(i);
        }
    }
    if (slot < 0 || slot >= MAX_PLAYER_COUNT || !s_setupOwed[slot]) return;
    pl = g_playerPointers[slot];
    /* the first frame of a warmup past that load, the player alive (a round in progress: still owed) */
    if (pl == NULL || netStageLoadCount() == s_setupLoads || netGetPhase() != 1 || pl->bonddead || g_gameOverFlag) return;
    s_setupOwed[slot] = 0;
    gevrMpOptionsOrPause(slot);
}

/* the results of a match: right (or START, but the host's, which continues) leaves them for the folder, on NEXT MAP while voting */
static s32 gevrMpOptionsFromResults(s32 slot)
{
    struct player *pl = slot >= 0 && slot < MAX_PLAYER_COUNT ? g_playerPointers[slot] : NULL;

    if (pl == NULL || !g_gameOverFlag || pl->mpmenumode != MENU_SCORES || !gevrMpOptionsAvailable(slot) || !gevrMpOptionsAsMenu()) return 0;
    if (!mpwatchIsPlayerPressingRight(slot) &&
        !(joyGetButtonsPressedThisFrame(slot, START_BUTTON) && !gevrSetupHostSeat(slot))) return 0;
    mpwatchPlayBeep();
    gevrMpOptionsOpen(slot, MENU_SCORES);
    if (lobbyVoting()) s_setupCursor[slot][SETUP_SHEET_MAIN] = GEVR_SETUP_NEXT_MAP;
    return gevrMpOptionsIsOpen(slot);
}

/* A split screen's new seat (input.c): in the warmup (Horde: the setup break),
 * the folder on its Select Character, which comes back to it; in a round,
 * Select Character as a drop-in */
void gevrMpOptionsDropIn(s32 slot)
{
    if ((netHordeActive() ? netHordeSetupBreak() : netGetPhase() == 1) && gevrMpOptionsAvailable(slot))
    {
        gevrMpOptionsOpen(slot, -1);
        s_setupCursor[slot][SETUP_SHEET_MAIN] = GEVR_SETUP_CHARACTER;
        gevrCharSelOpen(slot, 0);
        if (gevrCharSelIsOpen(slot))
        {
            s_panelToSetup[slot] = 1;
            return;
        }
    }
    gevrCharSelOpen(slot, 1);
}

/* the pause window's Match tab (vr_pause_menu.cpp): PREVIOUS comes back to the window, a page of the watch */
void gevrMpOptionsFromPause(s32 slot) { gevrMpOptionsOpen(slot, MENU_LOBBY); }

/* START with the watch closed (mpwatchMenuTick; the headset's Menu, input.c): in
 * an online deathmatch's warmup the folder opens, and in a Horde run's setup
 * break, or at any time with a split screen (Horde has no LOBBY page: the
 * folder is each player's menu); whether it did */
s32 gevrMpOptionsStartPressed(s32 slot)
{
    if (!gevrMpOptionsAvailable(slot) || !gevrMpOptionsAsMenu()) return 0;   /* one player: the pause window */
    if (netHordeActive() ? !netHordeSetupBreak() && netLocalSlotCount() < 2 : netGetPhase() != 1) return 0;
    gevrMpOptionsOpen(slot, -1);
    return gevrMpOptionsIsOpen(slot);
}

/* Horde (input.c): a pad's START, which the campaign watch must not see, for the folder up at slot: its START tab */
void gevrMpOptionsQueueStart(s32 slot)
{
    if (slot >= 0 && slot < MAX_PLAYER_COUNT && gevrMpOptionsIsOpen(slot)) s_setupStartQueued[slot] = 1;
}

/*
 * The pre-match lobby (front.c MENU_MP_LOBBY, docs/match-lobby.md): the title
 * stage, no players and no map; each local player's folder in their own part
 * of the screen (one: the whole of it, beside the list of who is in the lobby
 * and READY; two: halves; three or four: quarters, as the match's views),
 * driven by their own pad as in a match. The folder stays up: B on it does
 * nothing, and a screen it opens comes back to it. Leaving is the folder's
 * Leave Match row. The host's START loads the map (net_core.c).
 */
static s32 gevrLobbySeatCount(void)
{
    s32 n = netLocalSlotCount();
    return n < 1 ? 1 : n > 4 ? 4 : n;
}

/* seat k's part of the w x h screen */
static void gevrLobbySeatRect(s32 k, s32 n, s32 sw, s32 sh, s32 *x, s32 *y, s32 *w, s32 *h)
{
    *x = 0;
    *y = 0;
    *w = sw;
    *h = sh;
    if (n == 2)
    {
        *h = sh / 2;
        *y = k * *h;
    }
    else if (n > 2)
    {
        *w = sw / 2;
        *h = sh / 2;
        *x = (k % 2) * *w;
        *y = (k / 2) * *h;
    }
}

void gevrLobbyStageReset(void)
{
    s32 i;

    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        s_lobbySeat[i].on = 0;
        s_lobbySeat[i].mode = MENU_SETUP;
        s_setupOwed[i] = 0;   /* the match's own openings are not the lobby's */
        s_panelToSetup[i] = 0;
        s_leaveAskedAt[i] = 0;
    }
    /* back in a stage after the lobby: no opening owed for arriving (the lobby was the arrival) */
    s_setupSession = netSessionCount();
    s_setupReturns = netLobbyReturnCount();
    s_setupVote = netVoteRequested();
}

void gevrLobbyStageTick(void)
{
    s32 n = gevrLobbySeatCount();
    s32 k;
    s32 i;

    if (!gevrInMatchLobby()) return;
    /* the openings the match would owe on arriving: the lobby was the arrival */
    s_setupSession = netSessionCount();
    s_setupReturns = netLobbyReturnCount();
    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        s_setupOwed[i] = 0;
        if (!gevrNetSlotIsLocalHuman(i)) s_lobbySeat[i].on = 0;   /* a guest who dropped out */
    }
    for (k = 0; k < n; k++)
    {
        s32 slot = netSlotOfLocalSeat(k);

        if (slot < 0 || slot >= MAX_PLAYER_COUNT || !gevrNetSlotIsLocalHuman(slot)) continue;
        s_lobbyActing = slot;
        if (!s_lobbySeat[slot].on) gevrMpOptionsOpen(slot, -1);
        if (!gevrCharSelTick(slot) && !gevrStageSelTick(slot) && !gevrHandicapTick(slot) && !gevrTeamSelTick(slot))
        {
            gevrMpOptionsTick(slot);
        }
        s_lobbyActing = -1;
    }
}

/* the players in the lobby, the host first, then in slot order */
static s32 gevrLobbyRoster(FrontRosterEntry *e)
{
    s32 host = netGetHostSlot();
    s32 n = 0;
    s32 pass;
    s32 i;

    for (pass = 0; pass < 2; pass++)
    {
        for (i = 0; i < MAX_PLAYER_COUNT && n < MAX_PLAYER_COUNT; i++)
        {
            const char *name;

            if ((i == host) != (pass == 0) || !netLobbySlotConnected(i)) continue;
            name = netGetSlotName(i);
            if (i == host && netIsDedicated()) continue;   /* a dedicated server's own seat plays no one */
            e[n].name = name != NULL ? name : "";
            e[n].character = netCharacterName(gevrNetSlotChr(i));
            e[n].bot = gevrNetSlotIsBot(i) != 0;
            e[n].host = i == host && !e[n].bot;
            e[n].ready = netSlotReady(i) != 0;
            e[n].me = gevrNetSlotIsLocalHuman(i) != 0;
            n++;
        }
    }
    return n;
}

static Gfx *gevrLobbySeatDraw(Gfx *gdl, s32 slot, s32 x, s32 y, s32 w, s32 h)
{
    viSetViewPosition((s16)x, (s16)y);
    viSetViewSize((s16)w, (s16)h);
    if (gevrCharSelIsOpen(slot)) return frontCharSelectDraw(gdl, slot, netCharacterCount(), x, y, w, h);
    if (gevrStageSelIsOpen(slot)) return gevrStageSelDraw(gdl, slot);
    if (gevrHandicapIsOpen(slot)) return gevrHandicapDraw(gdl, slot);
    if (gevrTeamSelIsOpen(slot)) return gevrTeamSelDraw(gdl, slot);
    if (gevrMpOptionsIsOpen(slot)) return gevrMpOptionsDraw(gdl, slot);
    return gdl;
}

Gfx *gevrLobbyStageDraw(Gfx *gdl)
{
    const s32 sw = viGetX();
    const s32 sh = viGetY();
    s32 n = gevrLobbySeatCount();
    s32 k;

    if (!gevrInMatchLobby()) return gdl;
    for (k = 0; k < n; k++)
    {
        s32 slot = netSlotOfLocalSeat(k);
        s32 x;
        s32 y;
        s32 w;
        s32 h;

        if (slot < 0 || slot >= MAX_PLAYER_COUNT || !gevrNetSlotIsLocalHuman(slot)) continue;
        gevrLobbySeatRect(k, n, sw, sh, &x, &y, &w, &h);
        if (n == 1)
        {
            /* one player: the folder, and beside it who is in the lobby and READY */
            FrontRosterEntry roster[MAX_PLAYER_COUNT];
            s32 humans = 0;
            s32 ready = netLobbyReadyCount(&humans);
            s32 rw = w * 2 / 5;
            char note[32];

            snprintf(note, sizeof(note), "%d OF %d READY", (int)ready, (int)humans);
            gdl = frontLobbyRosterDraw(gdl, roster, gevrLobbyRoster(roster), "Players", note, x + w - rw, y, rw, h);
            w -= rw;
        }
        s_lobbyActing = slot;
        gdl = gevrLobbySeatDraw(gdl, slot, x, y, w, h);
        s_lobbyActing = -1;
    }
    viSetViewPosition(0, 0);
    viSetViewSize((s16)sw, (s16)sh);
    return gdl;
}

/* A on the LOBBY page: the selected action row */
static s32 gevrMenuPageAction(void)
{
    GevrMenuPage *page = gevrMenuPageFor(g_CurrentPlayer->mpmenumode);
    const GevrMenuRow *r;
    if (page == NULL) return 0;
    gevrPageMoveCursor(page, 0);
    gevrMenuPageSave(g_CurrentPlayer->mpmenumode, page);
    r = &page->rows[page->cursor];
    if (r->step == rowCharacterStep && gevrRowVisible(r))
    {
        /* A on CHARACTER: the portraits (the stick still steps the name) */
        gevrCharSelOpen(menuActingSlot(), 0);
        mpwatchPlayBeep();
        return 1;
    }
    if ((r->step == rowMyHealthStep || r->step == rowTeamStep) && gevrRowVisible(r))
    {
        /* A on YOUR HEALTH or YOUR TEAM: the game's Handicap or Teams screen (the stick still steps the row) */
        if (r->step == rowMyHealthStep) gevrHandicapOpen(menuActingSlot());
        else gevrTeamSelOpen(menuActingSlot());
        if (gevrHandicapIsOpen(menuActingSlot()) || gevrTeamSelIsOpen(menuActingSlot()))
        {
            mpwatchPlayBeep();
            return 1;
        }
    }
    if ((r->step == rowMapStep || r->step == rowNextMapStep || r->step == rowHordeMapStep) && gevrRowVisible(r) && gevrRowEditable(r))
    {
        /* A on MAP or HORDE MAP (the host) or NEXT MAP (the vote): the photos (the stick still steps the row) */
        gevrStageSelOpen(menuActingSlot(), r->step == rowNextMapStep);
        if (gevrStageSelIsOpen(menuActingSlot()))
        {
            mpwatchPlayBeep();
            return 1;
        }
    }
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
    /* Scores and status have their own pages; audio and lobby rows start at the top. */
    shown = g_CurrentPlayer->mpmenumode == MENU_PAUSE ? page->count : GEVR_MENU_ROWS_SHOWN;
    y0 = 37;
    /*
     * As many rows as the view holds above the legend. A split screen's view
     * is about 110 high, and eight rows and the legend (150) ran past its
     * foot, the legend unseen; the rest scroll, the legend's arrows show it.
     */
    {
        s32 room = viGetViewTop() + viGetViewHeight() - (menu_top + y0 + MPMENU_YOFF) - 14;
        s32 fit = room / 13;
        if (fit < 1) fit = 1;
        if (shown > fit) shown = fit;
    }
    gevrPageMoveCursor(page, 0);
    for (i = 0; i < page->count && nvis < 32; i++)
    {
        if (gevrPageRowVisible(page, i))
        {
            if (i == page->cursor) ci = nvis;
            vis[nvis++] = i;
        }
    }
    if (page->scroll > ci) page->scroll = ci;
    if (page->scroll < ci - shown + 1) page->scroll = ci - shown + 1;
    if (page->scroll > nvis - shown) page->scroll = nvis - shown;
    if (page->scroll < 0) page->scroll = 0;
    gevrMenuPageSave(g_CurrentPlayer->mpmenumode, page);

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
        if (textwidth > viGetViewWidth() - 4)
        {
            /* a quarter of the screen: without the cursor's marks (its colour still shows it) */
            snprintf(label, sizeof(label), "%s %s", r->name, value);
            gevrUpper(label);
            textMeasure(&textheight, &textwidth, label, ptrFontBankGothicChars, ptrFontBankGothic, 0);
        }
        x = viGetViewLeft() + two_player_x_offset + 80 - (textwidth >> 1);
        if (x < viGetViewLeft() + 2) x = viGetViewLeft() + 2;
        y = menu_top + y0 + i * 13 + MPMENU_YOFF;
        gdl = textRender(gdl, &x, &y, label, ptrFontBankGothicChars, ptrFontBankGothic,
                         dim ? 0x00ff0080 : selected ? 0xa0ffa0f0 : 0x00ff00b0, viGetX(), viGetY(), 0, 0);
    }
    /* the legend: the selected row's hint, the click, and arrows when rows are out of view */
    sel = &page->rows[page->cursor];
    snprintf(label, sizeof(label), "%sL-STICK:ROW  %s%s", page->scroll > 0 ? "^ " : "",
             gevrRowEditable(sel) ? sel->hint : "HOST ONLY", page->scroll + shown < nvis ? " v" : "");
    textMeasure(&textheight, &textwidth, label, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    if (textwidth > viGetViewWidth() - 4)
    {
        /* a quarter of the screen holds only the row's own hint (moving the cursor needs none) */
        snprintf(label, sizeof(label), "%s%s%s", page->scroll > 0 ? "^ " : "",
                 gevrRowEditable(sel) ? sel->hint : "HOST ONLY", page->scroll + shown < nvis ? " v" : "");
        textMeasure(&textheight, &textwidth, label, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    }
    x = viGetViewLeft() + two_player_x_offset + 80 - (textwidth >> 1);
    if (x < viGetViewLeft() + 2) x = viGetViewLeft() + 2;
    y = menu_top + y0 + shown * 13 + 2 + MPMENU_YOFF;
    gdl = textRender(gdl, &x, &y, label, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viGetX(), viGetY(), 0, 0);
    return gdl;
}
/* Native launcher-style pause window: reuse the watch's exact callbacks and authority. */
static const GevrMenuRow *gevrPauseRow(int id)
{
    if (id >= 200 && id < 200 + (int)(sizeof(s_funRows)/sizeof(s_funRows[0]))) return &s_funRows[id-200];
    if (id >= 100 && id < 100 + (int)(sizeof(s_pauseRows)/sizeof(s_pauseRows[0]))) return &s_pauseRows[id-100];
    if (id >= 0 && id < (int)(sizeof(s_lobbyRows)/sizeof(s_lobbyRows[0]))) return &s_lobbyRows[id];
    return NULL;
}
static int gevrPauseGroup(const GevrMenuRow *r)
{
    const char *n=r->name;
    if (!strcmp(n,"NEXT ROUND") || !strcmp(n,"NEXT MAP") || !strcmp(n,"NEXT WEAPONS") || !strcmp(n,"MAP") || !strcmp(n,"WEAPONS") || !strcmp(n,"SCENARIO")) return GEVR_PAUSE_MATCH;
    if (!strcmp(n,"HORDE MAP") || !strcmp(n,"STARTING WAVE")) return GEVR_PAUSE_MATCH;
    if (!strcmp(n,"NEXT ROUND READY") || !strcmp(n,"YOUR TEAM") || !strcmp(n,"YOUR HEALTH") || !strcmp(n,"CHARACTER") || !strncmp(n,"LOADOUT ",8) || !strcmp(n,"FAV MAP") || !strcmp(n,"FAV SET")) return GEVR_PAUSE_PLAYER;
    if (!strcmp(n,"MUSIC") || !strcmp(n,"SFX") || !strcmp(n,"VOICE") || !strcmp(n,"VOICE MODE") || !strcmp(n,"MIC")) return GEVR_PAUSE_AUDIO;
    return GEVR_PAUSE_RULES;
}
/* Co-op's and Horde's own rows (the host's): Horde's map and first wave on
 * Match, friendly fire and the AI ally on Rules (the launcher's PvE options) */
static int gevrPausePveRow(const GevrMenuRow *r)
{
    return r->step == rowHordeMapStep || r->step == rowHordeWaveStep || r->step == rowAllyStep ||
           (r->step == rowFriendlyFireStep && r->visible == lobbyFriendlyFire);
}
/* field, choice count, and current index for the game's own named choices */
static int gevrPauseChoiceInfo(const GevrMenuRow *r, int *selected)
{
    const char *n=r->name; int field=-1,count=0;
    if (!strcmp(n,"MAP")) { *selected=netStageIndexOf((u8)gevrNetConfigGet(CFG_STAGE)); return netStageCount(); }
    if (!strcmp(n,"HORDE MAP")) { *selected=netStageIndexOf((u8)gevrNetConfigGet(CFG_HORDE_STAGE)); return netHordeStageCount(); }
    if (!strcmp(n,"STARTING WAVE")) { *selected=gevrNetConfigGet(CFG_HORDE_START_WAVE)-1; return NET_HORDE_MAX_START_WAVE; }
    if (!strcmp(n,"NEXT MAP")) { *selected=netGetVote(0,netGetLocalSlot())+1; return netStageCount()+1; }
    if (!strcmp(n,"NEXT WEAPONS")) { *selected=netGetVote(1,netGetLocalSlot())+1; return netWeaponSetCount()+1; }
    if (!strcmp(n,"PLAYERS")) { *selected=gevrNetConfigGet(CFG_MAX_PLAYERS)-2; return 7; }
    if (!strcmp(n,"BOT COUNT")) { *selected=gevrNetConfigGet(CFG_BOT_COUNT)-1; return MAX_PLAYER_COUNT-1; }
    if (!strcmp(n,"BOTS")) {field=CFG_BOT_MODE;count=NET_BOT_MODE_COUNT;}
    if (!strcmp(n,"BOT DIFFICULTY")) {field=CFG_BOT_DIFFICULTY;count=NET_BOT_DIFF_COUNT;}
    if (!strcmp(n,"CHARACTER")) { *selected=gevrNetSlotChr(netGetLocalSlot()); return netCharacterCount(); }
    if (!strcmp(n,"YOUR TEAM")) { *selected=netGetSlotTeam(netGetLocalSlot()); return 3; }
    if (!strcmp(n,"YOUR HEALTH")) { *selected=netSlotHandicapIndex(netGetLocalSlot()); return netHealthCount(); }
    if (!strncmp(n,"LOADOUT ",8)) { *selected=netItemIndexOf(gevrNetSlotLoadout(netGetLocalSlot(),n[8]-'1')); return netItemCount(); }
    if (!strncmp(n,"CUSTOM ",7)) { *selected=netItemIndexOf(gevrNetConfigGet(CFG_CUSTOM0+n[7]-'1')); return netItemCount(); }
    if (!strcmp(n,"WEAPONS")) {field=CFG_WEAPON_SET;count=netWeaponSetCount();}
    if (!strcmp(n,"SCENARIO")) {field=CFG_SCENARIO;count=netScenarioCount();}
    if (!strcmp(n,"LENGTH")) {field=CFG_GAME_LENGTH;count=gevrNetConfigGet(CFG_SCENARIO)==NET_SCENARIO_TLD?4:7;}
    if (!strcmp(n,"HEALTH")) {field=CFG_HEALTH;count=netHealthCount();}
    if (!strcmp(n,"DUAL WIELD")) {field=CFG_DUAL_WIELD;count=3;}
    if (!strcmp(n,"NEXT ROUND")) {field=CFG_NEXT_ROUND;count=3;}
    if (!strcmp(n,"VOICE MODE")) {field=CFG_VOICE_MODE;count=2;}
    if (!strcmp(n,"MOVEMENT SPEED")) { *selected=(netMovementSpeedPercent(netActiveMovementSpeed())-50)/25;return 7; }
    if (!strcmp(n,"GUN SIZE")) {field=CFG_GUN_SIZE;count=3;}
    if (!strcmp(n,"HIT IMMUNITY")) { *selected=netHitImmunity(gevrNetConfigGet(CFG_FUN_FLAGS)); return 3; }
    if (!strcmp(n,"HOST DELAY CAP")) {
        unsigned cap;const unsigned caps[]={20,40,50,60,80};netGetHostEqualization(&cap);*selected=0;
        for(int i=0;i<5;i++) if(cap>=caps[i]) *selected=i;
        return 5;
    }
    if(field>=0) *selected=gevrNetConfigGet(field);
    return count;
}
/*
 * The match's rows the Multiplayer Options folder holds (an online
 * deathmatch's, a Horde run's): the pause window leaves them to it, opened
 * from its Match and Player tabs (vr_pause_menu.cpp GEVR_PAUSE_SETUP), and
 * keeps the votes, READY and the favorites (the LOBBY page's quick rows), its
 * own rules (host equalization, the fun rules, movement speed) and the audio.
 * Co-op has no folder: its rows stay.
 */
static int gevrPauseInFolder(int id, const GevrMenuRow *r)
{
    if (id >= 100 || gevrLobbyQuickRow(r) || r->step == rowFavMapStep || r->step == rowFavSetStep) return 0;
    return gevrMpOptionsAsMenu() && gevrMpOptionsAvailable(netGetLocalSlot());   /* one player: the window has them all */
}
int gevrPauseReadField(int tab,int index,GevrPauseField *out)
{
    int seen=0;
    for(int id=0;id<200+(int)(sizeof(s_funRows)/sizeof(s_funRows[0]));id++) {
        const GevrMenuRow *r=gevrPauseRow(id);if(!r || r->kind==GEVR_ROW_ACTION || gevrPauseGroup(r)!=tab || !gevrRowVisible(r)) continue;
        if(gevrPauseInFolder(id,r)) continue;
        if(gevrCoopActive() && !gevrPausePveRow(r)) {
            /* Campaign rules come from the mission: no deathmatch ballots, teams or kits. */
            if(tab==GEVR_PAUSE_MATCH || (tab==GEVR_PAUSE_PLAYER && strcmp(r->name,"CHARACTER")) ||
               (tab==GEVR_PAUSE_RULES && id<100)) continue;
        }
        /* FRIENDLY FIRE is in both old pages; show it once under Rules. */
        if(id>=100 && !strcmp(r->name,"FRIENDLY FIRE")) continue;
        if(seen++!=index) continue;
        memset(out,0,sizeof(*out));out->id=id;out->editable=gevrRowEditable(r);
        snprintf(out->label,sizeof(out->label),"%s",r->name);
        if(r->value) r->value(out->value,sizeof(out->value));
        out->count=gevrPauseChoiceInfo(r,&out->selected);
        out->kind=out->count?GEVR_PAUSE_CHOICE:(!strcmp(r->name,"MUSIC") || !strcmp(r->name,"SFX") || !strcmp(r->name,"VOICE"))?GEVR_PAUSE_SLIDER:
            (!strcmp(out->value,"ON") || !strcmp(out->value,"OFF") || !strcmp(r->name,"MIC") || !strcmp(r->name,"NEXT ROUND READY") || !strcmp(r->name,"FAV MAP") || !strcmp(r->name,"FAV SET"))?GEVR_PAUSE_TOGGLE:GEVR_PAUSE_STEP;
        if(out->kind==GEVR_PAUSE_TOGGLE) out->selected=!strcmp(out->value,"ON") || !strcmp(out->value,"ACTIVE") || !strcmp(out->value,"READY") || strstr(out->value," YES")!=NULL;
        if(out->kind==GEVR_PAUSE_SLIDER) out->selected=atoi(out->value);
        return 1;
    }
    return 0;
}
const char *gevrPauseChoice(int id,int index)
{
    const GevrMenuRow *r=gevrPauseRow(id);if(!r)return ""; const char*n=r->name;int selected,count=gevrPauseChoiceInfo(r,&selected);
    if(index<0 || index>=count)return "";
    if(!strcmp(n,"NEXT MAP") || !strcmp(n,"NEXT WEAPONS")) {if(!index)return "No vote";index--;return !strcmp(n,"NEXT MAP")?netStageName(index):netWeaponSetName(index);}
    if(!strcmp(n,"PLAYERS")) { const char*names[]={"2","3","4","5","6","7","8"};return names[index]; }
    if(!strcmp(n,"BOT COUNT")) { const char*names[]={"1","2","3","4","5","6","7"};return names[index]; }
    if(!strcmp(n,"BOTS"))return netBotModeName(index);
    if(!strcmp(n,"BOT DIFFICULTY"))return netBotDifficultyName(index);
    if(!strcmp(n,"MAP") || !strcmp(n,"HORDE MAP"))return netStageName(index);
    if(!strcmp(n,"STARTING WAVE")) { static char wave[16];snprintf(wave,sizeof(wave),"%d",index+1);return wave; }
    if(!strcmp(n,"WEAPONS"))return netWeaponSetName(index);
    if(!strcmp(n,"SCENARIO"))return netScenarioName(index);
    if(!strcmp(n,"LENGTH"))return netGameLengthName(index);
    if(!strcmp(n,"HEALTH"))return netHealthName(index);
    if(!strcmp(n,"DUAL WIELD"))return netDualWieldName(index);
    if(!strcmp(n,"NEXT ROUND"))return netNextRoundName(index);
    if(!strcmp(n,"VOICE MODE"))return netVoiceModeName(index);
    if(!strcmp(n,"CHARACTER"))return netCharacterName(index);
    if(!strcmp(n,"YOUR TEAM"))return netTeamName(index);
    if(!strcmp(n,"YOUR HEALTH")) {
        /* the match's own health says so: picking it follows the host's */
        static char health[48];
        if(index!=gevrNetConfigGet(CFG_HEALTH))return netHealthName(index);
        snprintf(health,sizeof(health),"%s (match)",netHealthName(index));return health;
    }
    if(!strncmp(n,"CUSTOM ",7) || !strncmp(n,"LOADOUT ",8))return netItemName(gevrNetItemAt(index));
    if(!strcmp(n,"MOVEMENT SPEED")) {const char*names[]={"50%","75%","100% (default)","125%","150%","175%","200%"};return names[index];}
    if(!strcmp(n,"GUN SIZE")) {const char*names[]={"Normal","Tiny","Big"};return names[index];}
    if(!strcmp(n,"HIT IMMUNITY")) {const char*names[]={"None: every hit counts","Short: a quarter second","GoldenEye: up to a second"};return names[index];}
    if(!strcmp(n,"HOST DELAY CAP")) {const char*names[]={"20 ms","40 ms","50 ms","60 ms","80 ms"};return names[index];}
    return "";
}
void gevrPauseSelect(int id,int index)
{
    const GevrMenuRow*r=gevrPauseRow(id);int current,count;
    if(!r || !gevrRowVisible(r) || !gevrRowEditable(r) || !r->step)return;
    count=gevrPauseChoiceInfo(r,&current);if(index<0 || index>=count || index==current)return;
    if(!strcmp(r->name,"MAP") && !netStageEligible(index))return;
    if(!strcmp(r->name,"NEXT MAP")) {if(index && !netStageEligible(index-1))return;netSetLocalVote(0,index-1);return;}
    if(!strcmp(r->name,"NEXT WEAPONS")) {netSetLocalVote(1,index-1);return;}
    if(!strcmp(r->name,"SCENARIO"))gevrNetConfigSet(CFG_SCENARIO,index);
    else if(!strcmp(r->name,"HORDE MAP"))gevrNetConfigSet(CFG_HORDE_STAGE,index);
    else if(!strcmp(r->name,"STARTING WAVE"))gevrNetConfigSet(CFG_HORDE_START_WAVE,index+1);
    else if(!strcmp(r->name,"PLAYERS"))gevrNetConfigSet(CFG_MAX_PLAYERS,index+2);
    else if(!strcmp(r->name,"BOTS"))gevrNetConfigSet(CFG_BOT_MODE,index);
    else if(!strcmp(r->name,"BOT COUNT"))gevrNetConfigSet(CFG_BOT_COUNT,index+1);
    else if(!strcmp(r->name,"BOT DIFFICULTY"))gevrNetConfigSet(CFG_BOT_DIFFICULTY,index);
    else r->step(index-current);
    mpwatchPlayBeep();
}
void gevrPauseStep(int id,int direction)
{
    const GevrMenuRow*r=gevrPauseRow(id);
    if(r && gevrRowVisible(r) && gevrRowEditable(r) && r->kind==GEVR_ROW_VALUE && r->step) {r->step(direction<0?-1:1);mpwatchPlayBeep();}
}
void gevrPauseVolume(int id,int percent)
{
    const GevrMenuRow*r=gevrPauseRow(id);if(!r || !gevrRowEditable(r))return;
    if(percent<0)percent=0;if(percent>100)percent=100;
    if(!strcmp(r->name,"MUSIC")) {set_mTrack2Vol((u16)((percent*32767+50)/100));musicTrack1ApplySeqpVol(get_mTrack2Vol());musicTrack3ApplySeqpVol(get_mTrack2Vol());VrMusicVolume=(float)percent/100;}
    else if(!strcmp(r->name,"SFX")) {VrSfxVolume=(float)percent/100;gevrSndApplySfxVolume((u16)((percent*32767+50)/100));}
    else if(!strcmp(r->name,"VOICE"))VrVoiceVolume=(float)percent/100;
    else return;
    vrSettingsSave();
}
int gevrPauseActionAvailable(const char*label)
{
    for(int i=0;i<(int)(sizeof(s_lobbyRows)/sizeof(s_lobbyRows[0]));i++) {
        const GevrMenuRow*r=&s_lobbyRows[i];if(!strcmp(r->name,label))return gevrRowVisible(r)&&gevrRowEditable(r);
    }
    return 0;
}
void gevrPauseAction(const char*label)
{
    if(!gevrPauseActionAvailable(label))return;
    for(int i=0;i<(int)(sizeof(s_lobbyRows)/sizeof(s_lobbyRows[0]));i++) if(!strcmp(s_lobbyRows[i].name,label)) {s_lobbyRows[i].step(0);mpwatchPlayBeep();return;}
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
        case MENU_STATUS:
        case MENU_FUN:
        case MENU_LOBBY:
            return 1;
        case MENU_EXIT:
        case MENU_EXIT_CONFIRM:
        case MENU_FINISHED:
#ifdef GEVR
        case MENU_CHARSEL:
        case MENU_STAGESEL:
        case MENU_HANDICAP:
        case MENU_TEAMSEL:
        case MENU_SETUP:
#endif
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
        case MENU_STATUS:
        case MENU_FUN:
        case MENU_LOBBY:
        case MENU_EXIT:
            return 1;
        case MENU_GOWOC:
        case MENU_EXIT_CONFIRM:
        case MENU_FINISHED:
#ifdef GEVR
        case MENU_CHARSEL:
        case MENU_STAGESEL:
        case MENU_HANDICAP:
        case MENU_TEAMSEL:
        case MENU_SETUP:
#endif
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



#ifdef GEVR
/*
 * Online up to eight players take part (MAX_PLAYER_COUNT), past the four
 * values the finders above take: the award goes to the occupied slot with the
 * highest (sign 1) or lowest (sign -1) value, the lowest slot on a tie. first
 * is the metric's field in metrics[0]; the others follow a struct apart.
 */
static s32 gevrAwardPickInt(s32 numplayers, const s32 *first, s32 sign)
{
    s32 best = -1;
    s32 slot;

    for (slot = 0; slot < numplayers; slot++)
    {
        s32 value = *(const s32 *)((const u8 *)first + slot * sizeof(struct AwardMetrics));
        s32 held = best < 0 ? 0 : *(const s32 *)((const u8 *)first + best * sizeof(struct AwardMetrics));

        if (netSlotOccupied(slot) && !netPlayerIsSpectator(slot) && (best < 0 || (sign > 0 ? value > held : value < held))) best = slot;
    }
    return best < 0 ? 0 : best;
}

static s32 gevrAwardPickFloat(s32 numplayers, const f32 *first, s32 sign)
{
    s32 best = -1;
    s32 slot;

    for (slot = 0; slot < numplayers; slot++)
    {
        f32 value = *(const f32 *)((const u8 *)first + slot * sizeof(struct AwardMetrics));
        f32 held = best < 0 ? 0 : *(const f32 *)((const u8 *)first + best * sizeof(struct AwardMetrics));

        if (netSlotOccupied(slot) && !netPlayerIsSpectator(slot) && (best < 0 || (sign > 0 ? value > held : value < held))) best = slot;
    }
    return best < 0 ? 0 : best;
}

#define MP_AWARD(find, kind, sign, field) (netIsActive() ? gevrAwardPick##kind(player_count, &metrics[0].field, sign) \
    : find(player_count, metrics[0].field, metrics[1].field, metrics[2].field, metrics[3].field))
#else
#define MP_AWARD(find, kind, sign, field) find(player_count, metrics[0].field, metrics[1].field, metrics[2].field, metrics[3].field)
#endif

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

    struct AwardMetrics metrics[MAX_PLAYER_COUNT] = {0};

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

        metrics[i].num_kills += g_playerPlayerData[i].gevr_score_bank;

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
    i = MP_AWARD(mpFindMaxInt, Int, 1, num_suicides);

    if (metrics[i].num_suicides > 0)
    {
        metrics[i].awards |= AWARD_MOSTSUICIDAL;
    }

    i = MP_AWARD(mpFindMinInt, Int, -1, num_shots);

    if (metrics[i].num_shots < 100)
    {
        metrics[i].awards |= AWARD_WHONEEDSAMMO;
    }

    i = MP_AWARD(mpFindMinFloat, Float, -1, body_armor_pickups);

    if (metrics[i].body_armor_pickups <= 2.0f)
    {
        metrics[i].awards |= AWARD_WHERESTHEARMOUR;
    }

    i = MP_AWARD(mpFindMaxFloat, Float, 1, body_armor_pickups);

    if (metrics[i].body_armor_pickups > 6.0f)
    {
        metrics[i].awards |= AWARD_ACNEGATIVE10;
    }

    i = MP_AWARD(mpFindMaxInt, Int, 1, num_headshots);

    if (metrics[i].num_headshots > 0)
    {
        metrics[i].awards |= AWARD_MARKSMANSHIP;
    }

    i = MP_AWARD(mpFindMaxFloat, Float, 1, ks_ratio);

    if (metrics[i].ks_ratio > 0.0f)
    {
        metrics[i].awards |= AWARD_MOSTPROFESSIONAL;
    }

    i = MP_AWARD(mpFindMaxFloat, Float, 1, kd_ratio);

    if (metrics[i].kd_ratio > 0.0f)
    {
        metrics[i].awards |= AWARD_MOSTDEADLY;
    }

    i = MP_AWARD(mpFindMinFloat, Float, -1, kd_ratio);
    metrics[i].awards |= AWARD_MOSTHARMLESS;

    i = MP_AWARD(mpFindMinInt, Int, -1, time_other_players_on_screen);
    metrics[i].awards |= AWARD_MOSTCOWARDLY;

    i = MP_AWARD(mpFindMaxFloat, Float, 1, avg_km_per_hour);

    if (metrics[i].avg_km_per_hour > 10.0f)
    {
        metrics[i].awards |= AWARD_MOSTFRANTIC;
    }

    i = MP_AWARD(mpFindMinInt, Int, -1, damage_to_backside);
    metrics[i].awards |= AWARD_MOSTHONORABLE;

    i = MP_AWARD(mpFindMaxInt, Int, 1, damage_to_backside);

    if (metrics[i].damage_to_backside > 0 && (metrics[i].awards & AWARD_MOSTHONORABLE) == 0)
    {
        metrics[i].awards |= AWARD_MOSTDISHONORABLE;
    }

    i = MP_AWARD(mpFindMaxInt, Int, 1, longest_inning);

    if (metrics[i].longest_inning > 0)
    {
        metrics[i].awards |= AWARD_LONGESTINNINGS;
    }

    i = MP_AWARD(mpFindMinInt, Int, -1, shortest_inning);

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

#ifdef GEVR
    /* Select Character (co-op too, whose watch is the solo pause): nothing else
     * reads the pad meanwhile. After the game-over timer, which the first player drives. */
    gevrMpOptionsAutoTick(player_num);
    gevrMpOptionsHoldTick(player_num);
    if (gevrCharSelTick(player_num)) return;
    if (gevrStageSelTick(player_num)) return;
    if (gevrHandicapTick(player_num)) return;
    if (gevrTeamSelTick(player_num)) return;
    if (gevrMpOptionsTick(player_num)) return;
#endif

#ifdef GEVR
    /* co-op (#94): the solo watch is the pause menu (bondview2.c) */
    if (player_count != 1 && !gevrCoopActive())
#else
    if (player_count != 1)
#endif
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
                if (gevrMpOptionsFromResults(player_num))
                {
                    /* the results left for the folder */
                }
                else
#endif
                if (mpwatchIsPlayerPressingRight(player_num) && mpwatchMenuCanGoRight())
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpmenumode++;
#ifdef GEVR
                    while ((g_CurrentPlayer->mpmenumode == MENU_LOBBY || g_CurrentPlayer->mpmenumode == MENU_FUN) && !netIsActive()) g_CurrentPlayer->mpmenumode++;
#endif
                }
                else if (mpwatchIsPlayerPressingLeft(player_num) && mpwatchMenuCanGoLeft())
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpmenumode--;
#ifdef GEVR
                    while ((g_CurrentPlayer->mpmenumode == MENU_LOBBY || g_CurrentPlayer->mpmenumode == MENU_FUN) && !netIsActive()) g_CurrentPlayer->mpmenumode--;
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
                else if (netIsActive() && gevrNetSlotIsLocalHuman(player_num) &&
                         (g_CurrentPlayer->mpmenumode == MENU_LOBBY || g_CurrentPlayer->mpmenumode == MENU_FUN) &&
                         joyGetButtonsPressedThisFrame(player_num, A_BUTTON) && gevrMenuPageAction())
                {
                    /* Selected action consumed A; other buttons use the normal close/results path. */
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
                            if (netLocalSeatOfSlot(player_num) > 0) {
                                g_CurrentPlayer->mpmenuon = FALSE;
                                netRequestRemoveLocalGuest(player_num);
                                return;
                            }
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
#ifdef GEVR
                /* online deathmatch's warmup: START opens the Multiplayer Options folder */
                if (gevrMpOptionsStartPressed(player_num)) return;
#endif
                g_CurrentPlayer->mpmenuon = TRUE;
                g_CurrentPlayer->mpmenumode =
#ifdef GEVR
                    (netIsActive() && netLocalSlotCount() > 1) ? MENU_LOBBY : MENU_STATUS;
#else
                    MENU_SCORES;
#endif
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

            /* the score bank: kills against players who have left (net_core.c netForgetPlayerScore) */
            points += g_playerPlayerData[playernum].gevr_score_bank;

            points += g_playerPlayerData[playernum].killed_gg_owner_count * (netMpPlayerCount(player_count) - 2);
            break;

        case SCENARIO_YOLT:
#ifdef GEVR
            /* the game's four as the base, so two to four players score as they
             * always did; past four, the party size keeps the scores positive */
            points = MAX(4, netMpPlayerCount(player_count)) - g_playerPlayerData[playernum].order_out_in_yolt;
#else
            points = MAX_PLAYER_COUNT - g_playerPlayerData[playernum].order_out_in_yolt;
#endif
            break;

        case SCENARIO_TLD:
            points = g_playerPlayerData[playernum].flag_counter;
            break;
            
        case SCENARIO_2v2:
        case SCENARIO_3v1:
        case SCENARIO_2v1:
#ifdef GEVR
            if (netIsActive()) return netTeamScore(team_or_token);
#endif
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
    s32 scores[MAX_PLAYER_COUNT];
    s32 players[MAX_PLAYER_COUNT];
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

#ifdef GEVR
    /*
     * Teammates share their team's score, so a team is first or second; the
     * cases below say so by the game's team sizes. Online 3v3 and 4v4 play as
     * its 2v2 (net_rules.h netGameScenario), where a third or fifth place
     * would read "4th" or past the ROM's ranks.
     */
    if (netIsActive() && j > 0 && ((scenario == SCENARIO_2v2) || (scenario == SCENARIO_3v1) || (scenario == SCENARIO_2v1)))
    {
        j = 1;
    }
#endif
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
#ifdef GEVR
        default:
            /* online past four players: the ROM's text stops at 4th */
            snprintf(buffer, 64, "Rank: %dth", j + 1);
            break;
#endif
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
#ifdef GEVR
            /* retail hangs here; a scenario added later shows no rank (#95) */
            return 0;
#else
        do {} while (1);
#endif
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
#ifdef GEVR
            /* retail hangs here; a scenario added later shows no score (#95) */
            return 0;
#else
            do {} while (1);
#endif
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
#if defined(GEVR) && ((defined(ANDROID) && !defined(GEVR_MOBILE)) || defined(GEVR_PCVR))
    if (gevrNativePauseOpen()) return gdl;
#endif

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
    s32 scores[MAX_PLAYER_COUNT];
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

#ifdef GEVR
    if (gevrCharSelIsOpen(curplayernum))
    {
        s32 px;
        s32 py;
        s32 pw;
        s32 ph;

        gevrMenuPanelRect(&px, &py, &pw, &ph);
        return frontCharSelectDraw(gdl, curplayernum, netCharacterCount(), px, py, pw, ph);
    }
    if (gevrStageSelIsOpen(curplayernum))
    {
        return gevrStageSelDraw(gdl, curplayernum);
    }
    if (gevrHandicapIsOpen(curplayernum))
    {
        return gevrHandicapDraw(gdl, curplayernum);
    }
    if (gevrTeamSelIsOpen(curplayernum))
    {
        return gevrTeamSelDraw(gdl, curplayernum);
    }
    if (gevrMpOptionsIsOpen(curplayernum))
    {
        return gevrMpOptionsDraw(gdl, curplayernum);
    }
#endif
 
#ifdef GEVR
    if (player_count == 1 || gevrCoopActive())   /* co-op (#94): the solo watch instead */
#else
    if (player_count == 1)
#endif
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
            case MENU_STATUS:
                text = "STATUS";
                break;
            case MENU_FUN:
                text = netGetPhase() == 2 ? "FUN - NEXT ROUND" : "FUN";
                break;
            case MENU_LOBBY:
            {
                /* the start's countdown, for everyone at the menu */
                static char lobbyTitle[32];
                s32 secs = netCountdownSecondsLeft();
                if (secs > 0) snprintf(lobbyTitle, sizeof(lobbyTitle), "STARTING IN %d", secs);
                else snprintf(lobbyTitle, sizeof(lobbyTitle), "LOBBY");
                text = lobbyTitle;
                break;
            }
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
 
        if ((g_CurrentPlayer->mpmenumode == MENU_SCORES) ||
#ifdef GEVR
            (!netIsActive() && g_CurrentPlayer->mpmenumode == MENU_PAUSE))
#else
            (g_CurrentPlayer->mpmenumode == MENU_PAUSE))
#endif
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
                    x=(viGetViewLeft()+two_player_x_offset)+5; y=menu_top+65+MPMENU_YOFF;
                    gdl=textRender(gdl,&x,&y,"NAME",ptrFontBankGothicChars,ptrFontBankGothic,0x00FF00B0,viGetX(),viGetY(),0,0);
                    x=(viGetViewLeft()+two_player_x_offset)+115; y=menu_top+65+MPMENU_YOFF;
                    gdl=textRender(gdl,&x,&y,"PTS",ptrFontBankGothicChars,ptrFontBankGothic,0x00FF00B0,viGetX(),viGetY(),0,0);
                    x=(viGetViewLeft()+two_player_x_offset)+146; y=menu_top+65+MPMENU_YOFF;
                    gdl=textRender(gdl,&x,&y,"PING MS",ptrFontBankGothicChars,ptrFontBankGothic,0x00FF00B0,viGetX(),viGetY(),0,0);
                    for (i = 0; i < player_count; i++)
                    {
                        if (!netLobbySlotConnected(i)) continue;
                        char entry[32];
                        s32 row_y, name_width, name_height;
                        const char *name = netGetSlotName(i);
                        u32 row_colour;
                        if (!name || !name[0]) name = "Player";
                        snprintf(entry, sizeof(entry), "%.15s", name);
                        /* Fit the name independently of speaking and numeric columns. */
                        textMeasure(&name_height,&name_width,entry,ptrFontBankGothicChars,ptrFontBankGothic,0);
                        while (name_width > 88 && strlen(entry) > 1) {
                            entry[strlen(entry)-1]='\0';
                            textMeasure(&name_height,&name_width,entry,ptrFontBankGothicChars,ptrFontBankGothic,0);
                        }
                        x = (viGetViewLeft() + two_player_x_offset) + 5;
                        y = row_y = menu_top + (80 + MPMENU_YOFF) + row * gevrScoreRowStep();
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
                        x=(viGetViewLeft()+two_player_x_offset)+97; y=row_y;
                        gdl=textRender(gdl,&x,&y,netVoiceSlotSpeaking((unsigned char)i) ? ">))" : "",ptrFontBankGothicChars,ptrFontBankGothic,row_colour,viewleft,h1,0,0);
                        snprintf(entry,sizeof(entry),"%d",scores[i]);
                        textMeasure(&name_height,&name_width,entry,ptrFontBankGothicChars,ptrFontBankGothic,0);
                        x=(viGetViewLeft()+two_player_x_offset)+137-name_width; y=row_y;
                        gdl=textRender(gdl,&x,&y,entry,ptrFontBankGothicChars,ptrFontBankGothic,row_colour,viewleft,h1,0,0);
                        int ping=netGetSlotPing(i);
                        if (ping < 0) snprintf(entry,sizeof(entry),"--"); else snprintf(entry,sizeof(entry),"%d",ping);
                        textMeasure(&name_height,&name_width,entry,ptrFontBankGothicChars,ptrFontBankGothic,0);
                        x=(viGetViewLeft()+two_player_x_offset)+181-name_width; y=row_y;
                        gdl=textRender(gdl,&x,&y,entry,ptrFontBankGothicChars,ptrFontBankGothic,row_colour,viewleft,h1,0,0);
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
                    y = menu_top + (70 + MPMENU_YOFF) + row * gevrScoreRowStep();
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
                    y = menu_top + (70 + MPMENU_YOFF) + row * gevrScoreRowStep();
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
#ifdef GEVR
#if (defined(ANDROID) && !defined(GEVR_MOBILE)) || defined(GEVR_PCVR)   /* the headset build: the Quest, PC VR */
    /* The native pause window owns these gauges while it is visible. */
    if (gevrNativePauseOpen()) return FALSE;
#endif
    if (g_CurrentPlayer->mpmenuon) return !g_gameOverFlag && g_CurrentPlayer->mpmenumode == MENU_STATUS;
#endif
    return g_gameOverFlag ? FALSE : (g_CurrentPlayer->mpmenuon | (g_CurrentPlayer->healthdisplaytime > 0));
}


s32 checkGamePaused(void)
{
#ifdef GEVR
    { extern int netHordeActive(void); extern int netGetConnectedPlayerCount(void);
      extern int gevrHordeDefeated(void);
      if (netHordeActive() && (gevrHordeDefeated() || (netGetConnectedPlayerCount()==1 && gevrNativePauseOpen()))) return TRUE; }
#endif
    return g_pausedFlag;
}

#ifdef GEVR
static void gevrPauseGaugesOf(f32 health,f32 armour,GevrPauseGaugeVertex vertices[92])
{
    struct damage_display_val gauges[92];
    /* Reuse the original pause/watch arcs, including shading and fill. */
    hudMakeDamageSegments(gauges,46,-1,health);
    hudMakeDamageSegments(gauges+46,46,1,armour);
    for(int i=0;i<92;i++) {
        vertices[i].x=gauges[i].pos.x;
        vertices[i].y=gauges[i].pos.z;
        vertices[i].r=gauges[i].colour.r;
        vertices[i].g=gauges[i].colour.g;
        vertices[i].b=gauges[i].colour.b;
        vertices[i].a=gauges[i].colour.a;
    }
}
void gevrPauseLocalGauges(GevrPauseGaugeVertex vertices[92])
{
    int slot=netGetLocalSlot();
    f32 health=0,armour=0;
    if(slot>=0 && slot<MAX_PLAYER_COUNT && g_playerPointers[slot]) {
        health=g_playerPointers[slot]->bondhealth;
        armour=g_playerPointers[slot]->bondarmour;
    }
    gevrPauseGaugesOf(health,armour,vertices);
}
void gevrPausePlayerStats(int slot,int*points,int*kills,int*losses)
{
    *points=*kills=*losses=0;
    if(slot<0 || slot>=MAX_PLAYER_COUNT || !netSlotOccupied(slot))return;
    *points=get_points_for_mp_player(slot);
    for(int i=0;i<MAX_PLAYER_COUNT;i++) {
        if(i!=slot)*kills+=g_playerPlayerData[slot].kill_counts[i];
        *losses+=g_playerPlayerData[i].kill_counts[slot];
    }
}
/* The in-game panel's arcs (hold Menu, vr_pause_menu.cpp): this headset's
 * player online, player 1 in solo play; empty in the menus. */
void gevrPanelGauges(GevrPauseGaugeVertex vertices[92])
{
    struct player *pl=netIsActive()?NULL:g_playerPointers[0];
    if(netIsActive()) {
        gevrPauseLocalGauges(vertices);
        return;
    }
    gevrPauseGaugesOf(pl && bossGetStageNum()!=LEVELID_TITLE?pl->bondhealth:0,
                      pl && bossGetStageNum()!=LEVELID_TITLE?pl->bondarmour:0,vertices);
}
/* The pause menu's match clock: time left in a timed match (lv.c warns at one
 * minute and ends it at g_MpTime), time played in any other. Both count in
 * frames from the round's start, the host's clock on every headset. */
int gevrPauseMatchClock(char*text,unsigned size)
{
    extern s32 D_80048394,g_MpTime;
#if defined(VERSION_EU)
    const s32 rate=50;
#else
    const s32 rate=60;
#endif
    s32 frames=g_MpTime>0?g_MpTime-D_80048394:D_80048394;
    if(frames<0)frames=0;
    const s32 seconds=(frames+(g_MpTime>0?rate-1:0))/rate;   /* time left rounds up: 0:00 is the end */
    snprintf(text,size,g_MpTime>0?"%d:%02d left":"%d:%02d played",seconds/60,seconds%60);
    return g_MpTime>0?seconds:-1;
}
#endif
