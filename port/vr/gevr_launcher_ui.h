#ifndef GEVR_LAUNCHER_UI_H
#define GEVR_LAUNCHER_UI_H
#include "imgui/imgui.h"
#include <cmath>
#include <string>
#include <cstdio>
struct GevrPointerOwner {
    bool owns = false;
    float u = -1, v = -1;
    bool update(bool on, float nextU, float nextV, bool trigger, bool nav) {
        if (!on)
            return owns = false;
        if (std::fabs(nextU - u) + std::fabs(nextV - v) > .01f || (trigger && !nav)) {
            owns = true;
            u = nextU;
            v = nextV;
        }
        if (nav)
            owns = false;
        return owns;
    }
};
inline bool gevrGamepadActivate(bool pointing, bool select, bool trigger) { return select || (!pointing && trigger); }
// Owned by one invocation of the launcher: never saved or shared with gameplay.
struct GevrLauncherSession {
    bool debugUnlocked = false, sticksReleased = false;
    bool romSeen = false, romNeedsAttention = true;
    void romSelectionFinished() { romSeen = false; }
    void updateDebugCombo(bool left, bool right) {
        if (!left && !right) sticksReleased = true;
        if (sticksReleased && left && right) debugUnlocked = true;
    }
};
inline bool gevrLauncherDebugButton(const GevrLauncherSession &session) {
    ImGui::BeginDisabled(!session.debugUnlocked);
    const bool clicked = ImGui::SmallButton("Send debug log");
    ImGui::EndDisabled();
    return clicked;
}
inline bool gevrLauncherRomHeader(GevrLauncherSession &session, bool ready, bool error, const char *label) {
    const bool attention = !ready || error;
    if (!session.romSeen || attention != session.romNeedsAttention)
        ImGui::SetNextItemOpen(attention, ImGuiCond_Always);
    session.romSeen = true;
    session.romNeedsAttention = attention;
    return ImGui::CollapsingHeader(label);
}
template <class Play, class Controls, class Comfort, class Screen>
inline void gevrLauncherMainTabs(Play play, Controls controls, Comfort comfort, Screen screen) {
    if (ImGui::BeginTabBar("launcher-settings")) {
        if (ImGui::BeginTabItem("Play")) { play(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Controls")) { controls(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Comfort")) { comfort(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Screen")) { screen(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
}
// Draw only changes the preference when a radio is activated. Rates come from
// the current session, so a saved preference may temporarily be unavailable.
inline void gevrDisplayRateControls(int *preference, const int *rates, int count) {
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImGui::RadioButton("Auto##display-rate", preference, 0);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Use the headset's refresh setting, including external profiles.");
    bool available = *preference == 0;
    for (int i = 0; i < count; ++i) {
        char label[48];
        std::snprintf(label, sizeof(label), "%d Hz##display-rate", rates[i]);
        char visible[24];
        std::snprintf(visible, sizeof(visible), "%d Hz", rates[i]);
        const float width = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                            ImGui::CalcTextSize(visible).x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right)
            ImGui::SameLine();
        ImGui::RadioButton(label, preference, rates[i]);
        available = available || *preference == rates[i];
    }
    // Auto may have been clicked after starting with an unavailable preference.
    if (!available && *preference != 0)
        ImGui::TextWrapped("Saved: %d Hz (unavailable on this headset).", *preference);
}
/* Only the combo's opening frame requests initial focus/scroll. ImGui's
 * navigation brings stick-selected rows into view; mouse hover never scrolls. */
template <class Disabled>
inline bool gevrNamedCombo(const char *id, int count, const char *(*name)(int), int *selected, bool longList,
                           Disabled disabled) {
    if (*selected < 0 || *selected >= count)
        *selected = 0;
    bool changed = false;
    if (ImGui::BeginCombo(id, name(*selected),
                          longList ? ImGuiComboFlags_HeightLarge : ImGuiComboFlags_HeightLargest)) {
        for (int n = 0; n < count; n++) {
            bool chosen = *selected == n;
            ImGui::BeginDisabled(disabled(n));
            if (ImGui::Selectable(name(n), chosen)) {
                *selected = n;
                changed = true;
            }
            ImGui::EndDisabled();
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}
inline void gevrLauncherStatus(const char *message) {
    std::string line = message;
    for (char &ch : line)
        if (ch == '\n' || ch == '\r')
            ch = ' ';
    float width = ImGui::GetContentRegionAvail().x;
    if (ImGui::CalcTextSize(line.c_str()).x > width) {
        while (!line.empty() && ImGui::CalcTextSize((line + "...").c_str()).x > width) {
            // Remove a complete UTF-8 codepoint.
            size_t end = line.size() - 1;
            while (end > 0 && (static_cast<unsigned char>(line[end]) & 0xc0) == 0x80)
                --end;
            line.resize(end);
        }
        line += "...";
    }
    ImGui::TextUnformatted(line.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", message);
}
inline float gevrLauncherFooterHeight() {
    return ImGui::GetFrameHeight() * 1.5f + ImGui::GetTextLineHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y * 4;
}
inline void gevrLauncherBeginRomDetails() {
    // Fit the content, with scrolling only when it would crowd the tabs and START.
    const float maxHeight = std::fmax(1.0f, ImGui::GetContentRegionAvail().y -
        gevrLauncherFooterHeight() - ImGui::GetFrameHeight() * 3);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 1), ImVec2(ImGui::GetContentRegionAvail().x, maxHeight));
    ImGui::BeginChild("rom-details", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
}
inline void gevrLauncherBeginBody(const char *id) {
    ImGui::BeginChild(id, ImVec2(0, std::fmax(1.0f, ImGui::GetContentRegionAvail().y - gevrLauncherFooterHeight())), false);
}
#endif
