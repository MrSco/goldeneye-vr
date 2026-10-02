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
    ImGui::DestroyContext();
    std::puts("PASS: 8 launcher UI checks (direct clicks, disabled rows, hover stability, stick navigation, pointer "
              "routing, footer bounds)");
}
