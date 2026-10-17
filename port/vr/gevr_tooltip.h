#ifndef GEVR_TOOLTIP_H
#define GEVR_TOOLTIP_H
#include "imgui/imgui.h"
#include <cstdarg>
#include <cstdio>
/* ImGui::SetTooltip never wraps: at the launcher's and pause menu's large font
 * a sentence runs past the right edge of the 1280-wide screen. This one wraps
 * at most of the screen's width, so ImGui can keep the whole box on screen. */
inline float gevrTooltipWrapWidth() {
    const float display = ImGui::GetIO().DisplaySize.x;
    return display > 0 ? display * 0.6f : ImGui::GetFontSize() * 30.0f;
}
inline void gevrTooltip(const char *fmt, ...) IM_FMTARGS(1);
inline void gevrTooltip(const char *fmt, ...) {
    char text[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (!ImGui::BeginTooltip())
        return;
    ImGui::PushTextWrapPos(gevrTooltipWrapWidth());
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
#endif
