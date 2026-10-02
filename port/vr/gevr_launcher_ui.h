#ifndef GEVR_LAUNCHER_UI_H
#define GEVR_LAUNCHER_UI_H
#include "imgui/imgui.h"
#include <cmath>
#include <string>
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
inline void gevrLauncherBeginBody(const char *id) {
    ImGui::BeginChild(id, ImVec2(0, std::fmax(1.0f, ImGui::GetContentRegionAvail().y - gevrLauncherFooterHeight())), false);
}
#endif
