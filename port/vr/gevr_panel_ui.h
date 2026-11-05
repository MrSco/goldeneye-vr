// The in-game panel (hold Menu 1.5 s; port/src/input.c, vr_pause_menu.cpp):
// what the old "back to the launcher?" prompt offered and the VR settings
// that were on the watch (src/game/options.c's rows), in solo play and online
// alike. It shares the online pause's header (gevrPauseHeader: health and
// armour, radar online) and surface, and the Friends side panel to its right
// (gevr_friends_ui.h), drawn by vr_pause_menu.cpp like the launcher's.
// No engine pointers, XR or GL state: the layout test draws it with fakes.
#ifndef GEVR_PANEL_UI_H
#define GEVR_PANEL_UI_H
#include "gevr_pause_widgets.h"

struct GevrPanelView {
    char session[128] = "";      // the mission or match
    char status[128] = "";       // under it: the difficulty, or the match's state
    char note[128] = "";         // what leaving costs ("... does not save this mission.")
    bool online = false;         // leaving says goodbye to the match
    int texpack = -1;            // 1 HD textures on, 0 off, -1 no pack
    GevrPauseGaugeVertex gauges[92]{};
    GevrPauseRadarView radar{};
    // the VR settings (options.c gevrVrSectionCount and its neighbours)
    int (*sectionCount)(void) = nullptr;
    const char *(*sectionName)(int) = nullptr;
    int (*rowCount)(int) = nullptr;
    int (*rowRead)(int, int, const char **, char *, int) = nullptr;
    void (*rowStep)(int, int, int) = nullptr;
};
struct GevrPanelUi {
    bool focusLauncher = true;   // hold Menu then A still goes back to the launcher
};
enum { GEVR_PANEL_NONE, GEVR_PANEL_RESUME, GEVR_PANEL_LAUNCHER, GEVR_PANEL_TEXPACK, GEVR_PANEL_RESET_SOUND };

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
    gevrPauseHeader(icon, v.radar, v.gauges, v.session, v.status);

    // The VR settings, a tab per section, across the whole surface.
    ImGui::SetCursorPos(ImVec2(18, 112));
    ImGui::BeginChild("##panel-settings", ImVec2(1244, 700));
    ImGui::TextColored(gold, "VR SETTINGS");
    const int sections = v.sectionCount ? v.sectionCount() : 0;
    if (ImGui::BeginTabBar("##panel-sections", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        for (int s = 0; s < sections; s++) {
            if (!ImGui::BeginTabItem(v.sectionName(s))) continue;
            ImGui::BeginChild("##panel-rows", ImVec2(0, 0));
            if (ImGui::BeginTable("##panel-rowtable", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, 460);
                const int rows = v.rowCount ? v.rowCount(s) : 0;
                for (int r = 0; r < rows; r++) gevrPanelRow(v, s, r, 460 - 2 * (ImGui::GetFrameHeight() + 4));
                ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    if (v.note[0]) {
        ImGui::SetCursorPos(ImVec2(18, 822));
        ImGui::TextDisabled("%s", v.note);
    }

    ImGui::SetCursorPos(ImVec2(18, 864));
    ImGui::Separator();
    ImGui::SetCursorPos(ImVec2(18, 886));
    if (ImGui::Button("Resume", ImVec2(244, 52))) action = GEVR_PANEL_RESUME;
    if (v.texpack >= 0) {
        ImGui::SetCursorPos(ImVec2(282, 886));
        if (ImGui::Button(v.texpack ? "HD textures off" : "HD textures on", ImVec2(300, 52))) action = GEVR_PANEL_TEXPACK;
        if (ImGui::IsItemHovered()) gevrTooltip("Switches the texture pack for this session.");
    }
    // #229: sound gone bit-crushed with an echo comes back with a new output stream
    ImGui::SetCursorPos(ImVec2(600, 886));
    if (ImGui::Button("Reset sound", ImVec2(330, 52))) action = GEVR_PANEL_RESET_SOUND;
    if (ImGui::IsItemHovered()) gevrTooltip("If the sound turns crunchy or echoes, this restarts it.");
    ImGui::SetCursorPos(ImVec2(948, 886));
    ImGui::PushStyleColor(ImGuiCol_Text, bad);
    if (ImGui::Button(v.online ? "Leave to launcher" : "Back to launcher", ImVec2(314, 52))) action = GEVR_PANEL_LAUNCHER;
    ImGui::PopStyleColor();
    if (ui.focusLauncher) { ImGui::SetItemDefaultFocus(); ImGui::SetKeyboardFocusHere(-1); ui.focusLauncher = false; }
    ImGui::End();
    return action;
}
#endif
