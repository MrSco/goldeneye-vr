// The in-game panel (hold Menu 1.5 s; port/src/input.c, vr_pause_menu.cpp):
// what the old "back to the launcher?" prompt offered, the VR settings that
// were on the watch (src/game/options.c's rows) and the Friends sidebar, in
// solo play and online alike, on the room screen like the online pause.
// No engine pointers, XR or GL state: the layout test draws it with fakes.
#ifndef GEVR_PANEL_UI_H
#define GEVR_PANEL_UI_H
#include "imgui/imgui.h"
#include "gevr_tooltip.h"
#include <cstdio>

struct GevrPanelView {
    char session[128] = "";      // the mission or match, top right
    char note[128] = "";         // what leaving costs ("This mission will not be saved.")
    bool online = false;         // leaving says goodbye to the match
    int texpack = -1;            // 1 HD textures on, 0 off, -1 no pack
    // the VR settings (options.c gevrVrSectionCount and its neighbours)
    int (*sectionCount)(void) = nullptr;
    const char *(*sectionName)(int) = nullptr;
    int (*rowCount)(int) = nullptr;
    int (*rowRead)(int, int, const char **, char *, int) = nullptr;
    void (*rowStep)(int, int, int) = nullptr;
    void (*drawFriends)(void) = nullptr;   // gevr_friends_ui.h's sidebar
};
struct GevrPanelUi {
    int section = 0;
    bool focusLauncher = true;   // hold Menu then A still goes back to the launcher
};
enum { GEVR_PANEL_NONE, GEVR_PANEL_RESUME, GEVR_PANEL_LAUNCHER, GEVR_PANEL_TEXPACK };

// One VR setting: its label, then < VALUE > (the value steps forward, wrapping).
inline void gevrPanelRow(const GevrPanelView &v, int section, int index, float valueWidth) {
    const char *label = "";
    char value[64] = "";
    if (!v.rowRead || !v.rowRead(section, index, &label, value, sizeof(value))) return;
    ImGui::PushID(index);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    const float arrow = ImGui::GetFrameHeight();
    if (ImGui::Button("<", ImVec2(arrow, 0))) v.rowStep(section, index, -1);
    ImGui::SameLine(0, 4);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.88f, .69f, .25f, 1));
    if (ImGui::Button(value, ImVec2(valueWidth, 0))) v.rowStep(section, index, 2);
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 4);
    if (ImGui::Button(">", ImVec2(arrow, 0))) v.rowStep(section, index, 1);
    ImGui::PopID();
}

inline int gevrDrawPanelWindow(const GevrPanelView &v, GevrPanelUi &ui, ImTextureID icon = 0) {
    int action = GEVR_PANEL_NONE;
    const ImVec4 gold(.88f, .69f, .25f, 1), bad(.95f, .5f, .4f, 1);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1280, 960));
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##gevr-panel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    if (icon) { ImGui::SetCursorPos(ImVec2(18, 18)); ImGui::Image(icon, ImVec2(64, 64)); }
    ImGui::SetCursorPos(ImVec2(100, 32));
    ImGui::TextColored(gold, "GOLDENEYE VR");
    ImGui::SetCursorPos(ImVec2(1262 - ImGui::CalcTextSize(v.session).x, 32));
    ImGui::TextDisabled("%s", v.session);
    ImGui::SetCursorPos(ImVec2(18, 98));
    ImGui::Separator();

    // Left: the VR settings, a tab per section. Right: Friends.
    const float split = 760, top = 112, bottom = 812;
    ImGui::SetCursorPos(ImVec2(18, top));
    ImGui::BeginChild("##panel-settings", ImVec2(split - 18, bottom - top));
    ImGui::TextColored(gold, "VR SETTINGS");
    const int sections = v.sectionCount ? v.sectionCount() : 0;
    if (ImGui::BeginTabBar("##panel-sections", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int s = 0; s < sections; s++) {
            if (!ImGui::BeginTabItem(v.sectionName(s))) continue;
            ui.section = s;
            ImGui::BeginChild("##panel-rows", ImVec2(0, 0));
            if (ImGui::BeginTable("##panel-rowtable", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, 380);
                const int rows = v.rowCount ? v.rowCount(s) : 0;
                for (int r = 0; r < rows; r++) gevrPanelRow(v, s, r, 380 - 2 * (ImGui::GetFrameHeight() + 4));
                ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::SetCursorPos(ImVec2(split + 12, top));
    ImGui::BeginChild("##panel-friends", ImVec2(1262 - split - 12, bottom - top));
    ImGui::TextColored(gold, "FRIENDS");
    if (v.drawFriends) v.drawFriends();
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(18, 864));
    ImGui::Separator();
    ImGui::SetCursorPos(ImVec2(18, 886));
    if (ImGui::Button("Resume", ImVec2(244, 52))) action = GEVR_PANEL_RESUME;
    if (v.texpack >= 0) {
        ImGui::SetCursorPos(ImVec2(282, 886));
        if (ImGui::Button(v.texpack ? "HD textures off" : "HD textures on", ImVec2(300, 52))) action = GEVR_PANEL_TEXPACK;
        if (ImGui::IsItemHovered()) gevrTooltip("Switches the texture pack for this session.");
    }
    ImGui::SetCursorPos(ImVec2(948, 886));
    ImGui::PushStyleColor(ImGuiCol_Text, bad);
    if (ImGui::Button(v.online ? "Leave to launcher" : "Back to launcher", ImVec2(314, 52))) action = GEVR_PANEL_LAUNCHER;
    ImGui::PopStyleColor();
    if (ui.focusLauncher) { ImGui::SetItemDefaultFocus(); ImGui::SetKeyboardFocusHere(-1); ui.focusLauncher = false; }
    if (v.note[0]) {
        ImGui::SetCursorPos(ImVec2(18, 822));
        ImGui::TextDisabled("%s", v.note);
    }
    ImGui::End();
    return action;
}
#endif
