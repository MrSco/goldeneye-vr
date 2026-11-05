#include "gevr_launcher_ui.h"
#include "imgui/imgui_internal.h"
#include <cstdio>
#include <vector>
#include <cstdlib>
static int selected = 20, disabledRow = -1;
static std::vector<std::pair<int, ImVec2>> visible;
static ImVec2 combo;
static const char *nameAt(int n) {
    static char names[64][16];
    std::snprintf(names[n], 16, "Choice %02d", n);
    return names[n];
}
static void check(bool ok, const char *what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}
static void frame() {
    visible.clear();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(800, 600));
    ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetNextItemWidth(240);
    combo = ImGui::GetCursorScreenPos();
    combo.x += 120;
    combo.y += ImGui::GetFrameHeight() / 2;
    gevrNamedCombo("choice", 64, nameAt, &selected, true, [](int n) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImRect clip = ImGui::GetCurrentWindow()->ClipRect;
        p.x += 50;
        p.y += ImGui::GetTextLineHeight() / 2;
        if (p.y > clip.Min.y + 2 && p.y < clip.Max.y - 2)
            visible.emplace_back(n, p);
        return n == disabledRow;
    });
    ImGui::End();
    ImGui::Render();
}
static void click(ImVec2 p) {
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(p.x, p.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
}
static void openCombo() {
    click(combo);
    frame();
    frame();
    check(visible.size() > 3, "combo popup visible");
}
static void reset() {
    ImGui::DestroyContext();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    unsigned char *pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    selected = 20;
    disabledRow = -1;
    frame();
    frame();
}
static GevrLauncherSession session;
static bool romReady = false, romError = false, romOpen = false, debugClicked = false;
static ImVec2 romHeader, debugButton, tabButtons[GEVR_LTAB_COUNT], subButtons[4];
static int activeTab = -1, activeSub = -1;
static GevrLauncherTabs tabs;
static ImVec2 tabCentre(ImGuiTabBar *b, int i) {
    if (!b || i >= b->Tabs.Size) return ImVec2(-1000, -1000);
    return ImVec2(b->BarRect.Min.x + b->Tabs[i].Offset + b->Tabs[i].Width * .5f, (b->BarRect.Min.y + b->BarRect.Max.y) * .5f);
}
static void settingsFrame() {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    debugClicked = gevrLauncherDebugButton(session);
    debugButton = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()).GetCenter();
    romOpen = gevrLauncherRomHeader(session, romReady, romError, "ROM###rom");
    romHeader = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()).GetCenter();
    if (romOpen) {
        gevrLauncherBeginRomDetails();
        ImGui::TextUnformatted("Choose a ROM or fix the selection error.");
        ImGui::EndChild();
    }
    gevrLauncherTabRows(tabs, [&](int tab, int sub) {
        activeTab = tab;
        activeSub = sub;
        // inside a sub-tab the open bar is the sub-tab row
        if (sub >= 0)
            for (int i = 0; i < gevrLauncherSubCount(tab); i++)
                subButtons[i] = tabCentre(ImGui::GetCurrentContext()->CurrentTabBar, i);
        gevrLauncherBeginPage("settings-page", gevrLauncherFooterHeight());
        ImGui::TextUnformatted("Settings content");
        ImGui::EndChild();
    });
    for (int i = 0; i < GEVR_LTAB_COUNT; ++i)
        tabButtons[i] = tabCentre(ImGui::GetCurrentContext()->TabBars.GetByKey(ImGui::GetID("launcher-tabs")), i);
    ImGui::Button("START", ImVec2(-1, ImGui::GetFrameHeight() * 1.6f));
    check(ImGui::GetItemRectMax().y <= ImGui::GetIO().DisplaySize.y - 4,
          "tabbed launcher keeps START visible");
    ImGui::End();
    ImGui::Render();
}
static bool settingsClick(ImVec2 p) {
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(p.x, p.y); settingsFrame();
    io.AddMouseButtonEvent(0, true); settingsFrame();
    io.AddMouseButtonEvent(0, false); settingsFrame();
    const bool clicked = debugClicked;
    settingsFrame();
    return clicked;
}
static void settingsChecks() {
    reset();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 960);
    ImGui::GetIO().FontGlobalScale = 2.2f;
    ImGui::GetStyle().ScaleAllSizes(2.2f);
    settingsFrame(); settingsFrame();
    check(romOpen, "missing ROM expands the section");
    check(!settingsClick(debugButton), "locked debug button ignores clicks");
    session.updateDebugCombo(false, false);
    session.updateDebugCombo(true, false);
    session.updateDebugCombo(false, true);
    check(!session.debugUnlocked, "separate stick clicks cannot unlock debug");
    session.updateDebugCombo(true, true);
    session.updateDebugCombo(false, false);
    check(session.debugUnlocked && settingsClick(debugButton), "both stick clicks unlock for this session");
    romReady = true; settingsFrame();
    check(!romOpen, "successful ROM validation collapses the section");
    settingsClick(romHeader); check(romOpen, "valid ROM can be manually expanded");
    session.romSelectionFinished(); settingsFrame();
    check(!romOpen, "selecting another valid ROM collapses an expanded section");
    settingsClick(romHeader); settingsClick(romHeader);
    check(!romOpen, "valid ROM can be manually collapsed");
    romError = true; settingsFrame(); check(romOpen, "selection error reopens a valid ROM section");
    romError = false; settingsFrame(); check(!romOpen, "cleared ROM error collapses the section");
    for (int tab : {2, 3, 4, 5, 6, 1, 0}) {
        settingsClick(tabButtons[tab]);
        check(activeTab == tab && tabs.top == tab, "each top tab can be selected with the pointer");
    }
    settingsClick(tabButtons[GEVR_LTAB_CONTROLS]);
    check(activeSub == GEVR_LSUB_HANDS, "Controls opens on Hands");
    settingsClick(subButtons[GEVR_LSUB_THROWING]);
    check(activeTab == GEVR_LTAB_CONTROLS && activeSub == GEVR_LSUB_THROWING, "a sub-tab can be selected with the pointer");
    settingsClick(tabButtons[GEVR_LTAB_RULES]);
    check(activeSub == GEVR_LSUB_GAME_RULES, "Rules keeps its own sub-tab");
    settingsClick(tabButtons[GEVR_LTAB_CONTROLS]);
    check(activeSub == GEVR_LSUB_THROWING, "Controls remembers its sub-tab");
    tabs.open(GEVR_LTAB_RULES, GEVR_LSUB_CHEATS);
    for (int i = 0; i < 4; i++) settingsFrame();   // a tab, then its sub-tab: a frame each
    check(activeTab == GEVR_LTAB_RULES && activeSub == GEVR_LSUB_CHEATS, "the launcher can bring up a tab and sub-tab (a friend's invite)");
    session = GevrLauncherSession{};
    session.updateDebugCombo(true, true);
    check(!session.debugUnlocked, "returning to launcher relocks and requires a fresh combo");
    settingsFrame(); check(!settingsClick(debugButton), "new session debug button is disabled");
    session.updateDebugCombo(false, false); session.updateDebugCombo(true, true);
    check(session.debugUnlocked, "fresh combo unlocks the next launcher session");
}
int main() {
    ImGui::CreateContext();
    for (int which = 0; which < 3; which++) {
        reset();
        openCombo();
        auto row = visible[which == 0 ? 0 : which == 1 ? visible.size() / 2 : visible.size() - 1];
        click(row.second);
        check(selected == row.first, "top/middle/bottom laser choice selects directly");
    }
    reset();
    openCombo();
    auto row = visible[0];
    disabledRow = row.first;
    click(row.second);
    check(selected == 20, "disabled row cannot be selected");
    check(!visible.empty(), "disabled click retains popup");
    // Hovering another visible row cannot keep recentering the selected row.
    auto other = visible.back();
    float y = visible.front().second.y;
    ImGui::GetIO().AddMousePosEvent(other.second.x, other.second.y);
    for (int i = 0; i < 12; i++)
        frame();
    check(std::fabs(visible.front().second.y - y) < .1f, "hover does not scroll the popup");
    reset();
    openCombo();
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(-1000, -1000);
    frame();
    // Stick navigation selects the next row and Enter/A confirms it.
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, true);
    frame();
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, false);
    frame();
    frame();
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true);
    frame();
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false);
    frame();
    frame();
    check(selected == 21, "stick navigation and confirmation");
    GevrPointerOwner owner;
    check(owner.update(true, .5f, .5f, false, false), "moving laser takes ownership");
    check(!owner.update(true, .5f, .5f, false, true), "stick takes ownership");
    bool pointer = owner.update(true, .5f, .5f, true, false);
    check(pointer && !gevrGamepadActivate(pointer, false, true), "same-frame trigger is mouse only");
    check(!owner.update(false, 0, 0, true, false) && gevrGamepadActivate(false, false, true),
          "off-panel trigger can activate navigation");
    // Fixed footer under four maximum-length names and many long body messages.
    reset();
    ImGui::GetIO().FontGlobalScale=2.2f;
    ImGui::GetStyle().ScaleAllSizes(2.2f);
    for (int height : {360, 600, 960}) {
        auto &footerIo = ImGui::GetIO();
        footerIo.DisplaySize = ImVec2(1280, float(height));
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(footerIo.DisplaySize);
        ImGui::Begin("Footer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Dummy(ImVec2(0,ImGui::GetTextLineHeight()*3));
        ImGui::TextUnformatted("ONLINE MULTIPLAYER");
        gevrLauncherBeginBody("body");
        for (int i = 0; i < 4; i++)
            ImGui::TextUnformatted("abcdefghijklmnopqrstuvwx Character Team Ready 99999ms");
        for (int i = 0; i < 30; i++)
            ImGui::TextWrapped("Very long connection message with more details than the available launcher width "
                               "allows in one row. Repeat this to exceed the body's height.");
        ImGui::EndChild();
        ImGui::Separator();
        gevrLauncherStatus("Very long status with\nseveral\nlines that must occupy one fixed footer row even when a "
                           "connection error includes\nnewlines.");
        ImGui::Button("Launch", ImVec2(0, ImGui::GetFrameHeight() * 1.5f));
        check(ImGui::GetItemRectMax().y <= height - 4, "footer button remains on screen");
        ImGui::TextUnformatted("Point and pull trigger to select");
        check(ImGui::GetItemRectMax().y <= height - 4, "outer help line remains on screen");
        ImGui::End();
        ImGui::Render();
    }
    settingsChecks();
    ImGui::DestroyContext();
    std::puts("PASS: launcher UI (combo clicks/navigation, pointer routing, footer bounds, settings tabs, "
              "ROM collapse/error recovery, the tab rows and sub-tabs, debug combo and session reset)");
}
