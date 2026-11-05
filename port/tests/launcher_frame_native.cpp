// The launcher's frame (docs/launcher-unify.md) at Quest font size, with the
// real settings table (port/src/gevr_settings.c, stubbed by
// settings_table_stubs.h) and the production pages, update line and START
// row cut from vr_launcher.cpp by test_launcher_frame.py: every tab and
// sub-tab drawn at its fullest fits with no scrolling, every label fits its
// column, both tab rows fit, and START and the hint stay on the canvas.
#include "gevr_launcher_ui.h"
#include "gevr_settings_ui.h"
#include "gevr_romlayout.h"
#include "imgui/imgui_internal.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "settings_table_stubs.h"

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
static void gevrMultiplayerPage(bool &, bool &, bool, const ImVec4 &, const ImVec4 &, const ImVec4 &) { customPages++; }

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

int main() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = ImVec2(1280, 960); io.DeltaTime = 1.f / 72; io.FontGlobalScale = 2.2f;
    ImGui::StyleColorsDark(); ImGui::GetStyle().ScaleAllSizes(2.2f); ImGui::GetStyle().WindowRounding = 0;
    unsigned char *px; int w, h; io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);

    // every row there is: flat screen with third person, ragdolls with
    // dismemberment, body slots, the ally; an update to offer; Bean's 120 Hz warning
    VrPlayMode = 0; VrThirdPerson = 1; VrRagdolls = VrDismemberment = 1; VrBodySlots = 1; VrAlly = 1;
    VrMotionThrowing = VrForcePull = 1; s_bean = 1; VrRefreshRate = 120; VrSpaceWarp = 0;
    upd.state = "available"; upd.offered = "0.5.15-test.99";
    upd.message = "Notes: a long line about what the update brings, longer than the row it shares with Update.";
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
            if (tab == GEVR_LTAB_MULTIPLAYER || (tab == GEVR_LTAB_CONTROLS && sub == GEVR_LSUB_HAPTICS) || tab == GEVR_LTAB_MODS)
                continue;   // their own pages (layout_compact, the haptics box, the packs)
            pages++;
            char what[160];
            std::snprintf(what, sizeof(what), "%s%s%s fits without scrolling (content %.0f, page %.0f)", gevrLauncherTabName(tab),
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
            std::snprintf(what, sizeof(what), "%s%s%s fits the width", gevrLauncherTabName(tab), sub >= 0 ? " > " : "",
                          sub >= 0 ? gevrLauncherSubName(tab, sub) : "");
            check(pageRight <= pageLimitX + 1, what);
            // the table's labels fit their column (gevrSettingsSection's split)
            static const int lsec[GEVR_LTAB_COUNT][4] = {
                { GEVR_LSEC_PLAY }, { -1 }, { GEVR_LSEC_HANDS, GEVR_LSEC_GESTURES, GEVR_LSEC_THROWING, -1 },
                { GEVR_LSEC_COMFORT }, { GEVR_LSEC_SCREEN }, { GEVR_LSEC_RULES, GEVR_LSEC_ALLY, GEVR_LSEC_CHEATS }, { -1 } };
            const int section = lsec[tab][sub < 0 ? 0 : sub];
            int ids[48];
            const int n = gevrSettingSectionRows(section, GEVR_SET_LAUNCHER, ids, 48);
            const int columns = n > GEVR_SETTINGS_ONE_COLUMN ? 2 : 1;
            const float half = (pageWidth - (columns > 1 ? ImGui::GetStyle().ItemSpacing.x * 4 : 0)) / columns;
            const float labelW = half * (columns > 1 ? 0.44f : 0.40f);
            for (int i = 0; i < n; i++) {
                GevrSettingInfo info{};
                api.info(ids[i], &info);
                std::snprintf(what, sizeof(what), "the label \"%s\" fits its column", info.label);
                check(ImGui::CalcTextSize(info.label).x + ImGui::GetStyle().CellPadding.x * 2 <= labelW, what);
            }
        }
    }
    check(pages == 9, "every settings page was measured");
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
    std::printf("PASS: launcher frame: %d settings pages, the tab rows, the update line and START fit at Quest font size\n", pages);
}
