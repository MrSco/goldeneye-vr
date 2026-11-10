// The in-game panel (hold Menu 1.5 s in solo play; port/src/input.c,
// vr_pause_menu.cpp). Online the same hold opens the match screen on its
// Settings tab (gevr_pause_widgets.h), whose sections are drawn the same way
// (gevrSettingsTabs). The panel:
// what the old "back to the launcher?" prompt offered and the VR settings
// (the settings table, port/src/gevr_settings.c, drawn by gevr_settings_ui.h
// as the launcher's controls), in solo play and online alike. It shares the online pause's header (gevrPauseHeader: health and
// armour, radar online) and surface, and the Friends side panel to its right
// (gevr_friends_ui.h), drawn by vr_pause_menu.cpp like the launcher's.
// No engine pointers, XR or GL state: the layout test draws it with fakes.
#ifndef GEVR_PANEL_UI_H
#define GEVR_PANEL_UI_H
#include "gevr_pause_widgets.h"
#include "gevr_settings_ui.h"

struct GevrPanelView {
    char session[128] = "";      // the mission or match
    char status[128] = "";       // under it: the difficulty, or the match's state
    char note[128] = "";         // what leaving costs ("... does not save this mission.")
    bool online = false;         // leaving says goodbye to the match
    int texpack = -1;            // 1 HD textures on, 0 off, -1 no pack
    bool touchLayout = false;    // a touch screen: "Edit touch layout" (flat_touch.cpp)
    GevrPauseGaugeVertex gauges[92]{};
    GevrPauseRadarView radar{};
    GevrSettingsApi settings;    // the VR settings: the table, or a test's rows
};
struct GevrPanelUi {
    bool focusLauncher = true;   // hold Menu then A still goes back to the launcher
    int openSection = -1;        // >= 0: bring that tab up on the next frame (the layout test's)
};
enum { GEVR_PANEL_NONE, GEVR_PANEL_RESUME, GEVR_PANEL_LAUNCHER, GEVR_PANEL_TEXPACK, GEVR_PANEL_RESET_SOUND,
       GEVR_PANEL_TOUCH_LAYOUT };

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
#ifdef GEVR_FLAT
    ImGui::TextColored(gold, "SETTINGS");
#else
    ImGui::TextColored(gold, "VR SETTINGS");
#endif
    gevrSettingsTabs(v.settings, ui.openSection);
    ImGui::EndChild();
    if (v.note[0]) {
        ImGui::SetCursorPos(ImVec2(18, 822));
        ImGui::TextDisabled("%s", v.note);
    }

    if (v.touchLayout) {   // over the game, which stays paused: Done comes back here
        ImGui::SetCursorPos(ImVec2(948, 812));
        if (ImGui::Button("Edit touch layout", ImVec2(314, 44))) action = GEVR_PANEL_TOUCH_LAYOUT;
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
#ifdef GEVR_IOS
    // an iOS app cannot start itself again: going back to the launcher closes it
    if (ImGui::Button(v.online ? "Leave and close" : "Close the game", ImVec2(314, 52))) action = GEVR_PANEL_LAUNCHER;
#else
    if (ImGui::Button(v.online ? "Leave to launcher" : "Back to launcher", ImVec2(314, 52))) action = GEVR_PANEL_LAUNCHER;
#endif
    ImGui::PopStyleColor();
    if (ui.focusLauncher) { ImGui::SetItemDefaultFocus(); ImGui::SetKeyboardFocusHere(-1); ui.focusLauncher = false; }
    ImGui::End();
    return action;
}
#endif
