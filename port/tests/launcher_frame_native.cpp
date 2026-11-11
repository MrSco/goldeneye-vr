// The launcher's frame (docs/launcher-unify.md) at Quest font size, with the
// real settings table (port/src/gevr_settings.c, stubbed by
// settings_table_stubs.h) and the production pages, update line and START
// row cut from vr_launcher.cpp by test_launcher_frame.py: every tab and
// sub-tab drawn at its fullest fits with no scrolling, every label fits its
// column, both tab rows fit, and START and the hint stay on the canvas.
#include "gevr_launcher_ui.h"
#include "gevr_settings_ui.h"
#include "gevr_romlayout.h"
#include "gevr_ghosts_ui.h"
#include "imgui/imgui_internal.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "settings_table_stubs.h"
#include "settings_fit.h"

static void check(bool ok, const char *what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}

// what the cut code reads from vr_launcher.cpp
struct UpdateStatus { std::string state, offered, message, installed; int progress = -1; bool testBuilds = false; };
static UpdateStatus upd;
static void gevrUpdaterCommand(const char *) {}
static unsigned long long VrCheatMask = ~0ULL;   // every cheat ticked
static int VrUnlockAll = 1;
static int customPages;
static void gevrHapticsPage(const ImVec4 &, const ImVec4 &, const ImVec4 &) { customPages++; }
static void gevrModsPage(unsigned, const ImVec4 &, const ImVec4 &, const ImVec4 &) { customPages++; }
// the Ghosts tab (gevr_ghosts_ui.h) at its fullest: a page of the board with the
// longest names the Friends service allows, your run ranked and refused, a
// downloaded run raced with the setting off, and a message that wraps
static void gevrGhostsPage(const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad) {
    static const char *const missions[] = { "Surface II", "Dam" };
    static GevrGhostsView v;
    v.missions = missions; v.missionCount = 2; v.difficulty = 1; v.plays = 2;
    v.haveBest = true; v.bestTime60 = 59 * 3600 + 59 * 60 + 59; v.bestFlags = 0x09; v.bestRank = 100; v.bestUnranked = true;
    v.racing = std::string(32, 'W') + " (GE-7K3QX2), 1:02:03.05"; v.racingOther = true; v.raceOn = false;
    v.state = GEVR_GHOSTS_LOADED;
    v.rows.clear();
    for (int i = 0; i < 100; i++) {
        GevrGhostRow r;
        r.id = i; r.time60 = 216000 + i; r.flags = i & 1; r.code = "7K3QX2"; r.name = std::string(32, 'W');
        r.mine = i == 1; r.racing = i == 2;
        v.rows.push_back(r);
    }
    v.page = 0;
    v.message = "Uploaded 60 runs: 58 on the boards. Friends is still signing in; try again in a moment, and a little more.";
    v.messageBad = true;
    gevrGhostsPageDraw(v, gold, good, bad);
}
// Multiplayer: its Host > Fun and Audio pages, cut from gevrMultiplayerPage, in
// its body (gevrLauncherBeginBody) above its status line and buttons
struct NetMatchConfig { int mode, scenario; };
static NetMatchConfig s_config;
static const NetMatchConfig *netGetMatchConfig() { return &s_config; }
static int VrMpMode, VrMpScenario, s_phase;
enum { NET_PHASE_WARMUP, NET_PHASE_IN_PROGRESS };
static int netGetPhase() { return s_phase; }
static int netVoiceHasPermission() { return 1; }
static int netVoiceCaptureFailed() { return 1; }   // the longest status
static bool netModePve(int mode) { return mode == 1 || mode == 2; }
static bool gevrConfigHasTeams(int, int scenario) { return scenario != 0; }
static bool gevrLobbyHasTeams(const NetMatchConfig *c) { return gevrConfigHasTeams(c->mode, c->scenario); }
static float mpBottom, mpLimit, mpRight, mpLimitX, mpButtons;
static void gevrMultiplayerPage(int sub, bool &, bool, const ImVec4 &, const ImVec4 &, const ImVec4 &) {
    customPages++;
    /* AUDIO_OPTIONS */
    gevrLauncherBeginBody("##mpbody");
    if (sub == GEVR_LSUB_HOST) {
        ImGui::Dummy(ImVec2(0, ImGui::GetFrameHeight()));   // Lobby, Match, Fun
        /* FUN_PAGE */
    } else if (sub == GEVR_LSUB_AUDIO) {
        audioOptions();
    }
    ImGuiWindow *w = ImGui::GetCurrentWindow();
    mpBottom = w->DC.CursorMaxPos.y; mpLimit = w->ClipRect.Max.y; mpRight = w->DC.CursorMaxPos.x; mpLimitX = w->ClipRect.Max.x;
    ImGui::EndChild();
    ImGui::Separator();
    gevrLauncherStatus("Waiting for joined players to ready up.");
    ImGui::Button("Launch", ImVec2(0, ImGui::GetFrameHeight() * 1.5f));
    mpButtons = ImGui::GetItemRectMax().y;
}

/* CHEATS */
/* START_ROW */

static GevrLauncherTabs launcherTabs;
static float s_launcherFooterH;   // vr_launcher.cpp's, measured as it does below
static float pageBottom, pageLimit, pageRight, pageLimitX, pageWidth, startBottom, hintBottom;
static int pageTab = -1, pageSub = -1;

static void frame() {
    const ImVec4 gold(.88f, .69f, .25f, 1), good(.5f, .9f, .5f, 1), bad(.95f, .5f, .4f, 1);
    bool start = false;
    const bool romReady = true;
    const unsigned now = 1000;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1280, 960));
    ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // vr_launcher.cpp's header: the icon's height, then the ROM header, collapsed
    ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() * 2.2f));
    ImGui::Separator();
    ImGui::SetNextItemOpen(false, ImGuiCond_Always);
    ImGui::CollapsingHeader("ROM: GoldenEye 007 (USA) ok###rom");
    ImGui::Separator();
    pageTab = pageSub = -1;
    float footerTop = -1.0f;
    /* FRAME */
    ImGui::TextDisabled("Point and pull the trigger, or use the stick and A.");
    hintBottom = ImGui::GetItemRectMax().y;
    if (footerTop >= 0.0f) s_launcherFooterH = ImGui::GetCursorPosY() - footerTop;
    ImGui::End();
    ImGui::Render();
    (void)start;
}

static int sweep(const char *state) {
    const GevrSettingsApi api = gevrSettingsLive();
    int pages = 0;
    for (int tab = 0; tab < GEVR_LTAB_COUNT; tab++) {
        const int subs = gevrLauncherSubCount(tab);
        for (int sub = subs ? 0 : -1; sub < (subs ? subs : 0); sub++) {
            launcherTabs.open(tab, sub);
            frame(); frame(); frame();
            check(launcherTabs.top == tab && (sub < 0 || launcherTabs.sub[tab] == sub), "each tab and sub-tab opens");
            check(startBottom <= 960 - 4 || tab == GEVR_LTAB_MULTIPLAYER, "START stays on the canvas");
            check(hintBottom <= 960 - 4, "the hint line stays on the canvas");
            if (tab == GEVR_LTAB_MULTIPLAYER && (sub == GEVR_LSUB_HOST || sub == GEVR_LSUB_AUDIO)) {
                char what[160];
                std::snprintf(what, sizeof(what), "%s: Multiplayer > %s fits its body (content %.0f, body %.0f)", state,
                              gevrLauncherSubName(tab, sub), mpBottom, mpLimit);
                check(mpBottom <= mpLimit && mpRight <= mpLimitX + 1, what);
                check(mpButtons <= 960 - 4, "Multiplayer's buttons stay on the canvas");
                settingsFitControls(api, sub == GEVR_LSUB_HOST ? GEVR_LSEC_MP_FUN : GEVR_LSEC_MP_AUDIO, GEVR_SET_LAUNCHER,
                              1280 - ImGui::GetStyle().WindowPadding.x * 2, state);
                pages++;
                continue;
            }
            if (tab == GEVR_LTAB_MULTIPLAYER || (tab == GEVR_LTAB_CONTROLS && sub == GEVR_LSUB_HAPTICS) || tab == GEVR_LTAB_MODS)
                continue;   // their own pages (launcher_compact, the haptics box, test_launcher_mods)
            pages++;
            char what[160];
            std::snprintf(what, sizeof(what), "%s: %s%s%s fits without scrolling (content %.0f, page %.0f)", state, gevrLauncherTabName(tab),
                          sub >= 0 ? " > " : "", sub >= 0 ? gevrLauncherSubName(tab, sub) : "", pageBottom, pageLimit);
            check(pageTab == tab && pageSub == sub, "the page drawn is the tab's");
            // nor scrolls inside (v0.5.4 put the rules in a scrolling child that fit the page)
            for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
                if (w->WasActive && w->ScrollMax.y > 0.5f) {
                    std::fprintf(stderr, "%s scrolls by %.1f on %s (content %.0f, page %.0f)\n", w->Name,
                                 w->ScrollMax.y, what, pageBottom, pageLimit);
                    check(false, "no part of a page scrolls");
                }
            check(pageBottom <= pageLimit, what);
            std::snprintf(what, sizeof(what), "%s: %s%s%s fits the width", state, gevrLauncherTabName(tab), sub >= 0 ? " > " : "",
                          sub >= 0 ? gevrLauncherSubName(tab, sub) : "");
            check(pageRight <= pageLimitX + 1, what);
            // the table's labels fit their column (gevrSettingsSection's split)
            static const int lsec[GEVR_LTAB_COUNT][4] = {
                { GEVR_LSEC_PLAY }, { -1 }, { -1 }, { GEVR_LSEC_HANDS, GEVR_LSEC_GESTURES, GEVR_LSEC_THROWING, -1 },
                { GEVR_LSEC_COMFORT }, { GEVR_LSEC_SCREEN }, { GEVR_LSEC_RULES, GEVR_LSEC_ALLY, GEVR_LSEC_CHEATS }, { -1 } };
            const int section = lsec[tab][sub < 0 ? 0 : sub];
            if (section >= 0 && section != GEVR_LSEC_CHEATS) settingsFitControls(api, section, GEVR_SET_LAUNCHER, pageWidth, state);
        }
    }
    return pages;
}

int main() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = ImVec2(1280, 960); io.DeltaTime = 1.f / 72; io.FontGlobalScale = 2.2f;
    ImGui::StyleColorsDark(); ImGui::GetStyle().ScaleAllSizes(2.2f); ImGui::GetStyle().WindowRounding = 0;
    unsigned char *px; int w, h; io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);

    // every row there is: ragdolls with dismemberment, body slots, the ally,
    // motion throw and force pull; an update to offer; Bean's 120 Hz warning
    VrRagdolls = VrDismemberment = 1; VrBodySlots = 1; VrAlly = 1;
    VrMotionThrowing = VrForcePull = 1; s_bean = 1; VrRefreshRate = 120; VrSpaceWarp = 0;
    upd.state = "available"; upd.offered = "0.5.15-test.99";
    upd.message = "Notes: a long line about what the update brings, longer than the row it shares with Update.";
    int pages = 0;
    // flat screen with third person's camera; stereo (third person greyed: "Flat screen only")
    VrPlayMode = 0; VrThirdPerson = 1;
    pages += sweep("flat, third person");
    VrPlayMode = 1;
    pages += sweep("stereo");
    // online: a co-op host (the live rules), a deathmatch joiner (the host's, greyed with their reason)
    VrPlayMode = 0; s_net = 1; s_host = 1; s_coop = 1; s_config.mode = 1; s_phase = NET_PHASE_IN_PROGRESS;
    pages += sweep("co-op host");
    s_host = 0; s_coop = 0; s_config.mode = 0; s_config.scenario = 1; s_netSpeed = 4;
    pages += sweep("deathmatch joiner");
    check(pages == 4 * 12, "every settings page, Ghosts, and Multiplayer's Fun and Audio, in four states");
    // both tab rows fit without shrinking
    ImGuiContext &g = *ImGui::GetCurrentContext();
    int bars = 0;
    for (int t = 0; t < g.TabBars.GetMapSize(); t++)
        if (ImGuiTabBar *bar = g.TabBars.TryGetMapData(t)) {
            bars++;
            check(bar->WidthAllTabsIdeal <= bar->BarRect.GetWidth() + 1, "a tab row's names fit without shrinking");
        }
    check(bars >= 3, "the top row and the Controls and Rules rows were drawn");
    ImGui::DestroyContext();
    std::printf("PASS: launcher frame: %d pages (flat, stereo, co-op host, joiner), their controls and reasons, "
                "the tab rows, the update line and START fit at Quest font size\n", pages);
}
