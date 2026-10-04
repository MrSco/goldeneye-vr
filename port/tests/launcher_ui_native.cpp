#include "gevr_launcher_ui.h"
#include "gevr_watch_status.h"
#include "imgui/imgui_internal.h"
#include <cstdio>
#include <vector>
#include <cstdlib>
static ImVec4 gold(1,1,0,1);
int VrLeftHandedMode,VrSwapJoysticks,VrAimNoLean,VrAimSteady,VrGunFitArmed,VrMotionThrowing;
int VrWatchFaceStatus=GEVR_WATCH_FACE_ON,VrWatchGesturePause=1;
bool throwingPage,hapticsPage,gesturesPage;
static ImRect watchControlsRect;
/* INSERT_CONTROLS */
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
static ImVec2 romHeader, debugButton, tabButtons[4];
static int activeTab = -1, displayRate = 87;
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
    gevrLauncherBeginBody("settings-body");
    const auto content = [](int tab) {
        activeTab = tab;
        if (tab == 1) {
            gevrTestControls();
            const auto *window=ImGui::GetCurrentWindow();
            check(watchControlsRect.Min.x > window->Pos.x+window->Size.x*.45f,
                  "watch options use the right controls column");
            check(window->DC.CursorMaxPos.y <= window->ClipRect.Max.y,
                  "both production controls columns fit without scrolling");
            check(watchControlsRect.Max.x <= window->ClipRect.Max.x,
                  "watch description wraps within its column");
        } else if (tab == 3) {
            const int rates[] = {72, 80, 90, 120};
            gevrDisplayRateControls(&displayRate, rates, 4);
            check(ImGui::GetItemRectMax().x <= ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                  "refresh options and unavailable saved rate fit the panel");
        } else ImGui::TextUnformatted("Settings content");
    };
    gevrLauncherMainTabs([&] { content(0); }, [&] { content(1); }, [&] { content(2); },
                         [&] { content(3); });
    ImGuiTabBar *bar = ImGui::GetCurrentContext()->TabBars.GetByKey(ImGui::GetID("launcher-settings"));
    if (bar) for (int i = 0; i < bar->Tabs.Size; ++i)
        tabButtons[i] = ImVec2(bar->BarRect.Min.x + bar->Tabs[i].Offset + bar->Tabs[i].Width * .5f,
                               (bar->BarRect.Min.y + bar->BarRect.Max.y) * .5f);
    ImGui::EndChild();
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
    for (int tab : {1, 2, 3, 0}) {
        settingsClick(tabButtons[tab]);
        check(activeTab == tab, "each settings tab can be selected with the pointer");
    }
    check(displayRate == 87, "drawing rates preserves an unavailable saved preference");
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
              "ROM collapse/error recovery, saved rates, debug combo and session reset)");
}
