// Fit checks for the settings table's rows as gevr_settings_ui.h draws them
// (gevrSettingsSection): each label fits its column, and each control with its
// value or reason ("Host's choice") fits beside it. For the launcher's and the
// in-game screens' fit tests (launcher_frame_native.cpp, ingame_fit_native.cpp).
#ifndef GEVR_SETTINGS_FIT_H
#define GEVR_SETTINGS_FIT_H
#include "gevr_settings_ui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

inline void settingsFitFail(bool ok, const char *what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}

// pageWidth: the width gevrSettingsSection's table is given (the content region).
inline void settingsFitControls(const GevrSettingsApi &api, int section, unsigned where, float pageWidth, const char *state) {
    int ids[48];
    const int n = api.sectionRows(section, where, ids, 48);
    if (n <= 0) return;
    const GevrColumnPlan plan = gevrSettingsPlan(api, ids, n, pageWidth, 0.0f);
    const ImGuiStyle &style = ImGui::GetStyle();
    const float cellPadX = style.CellPadding.x * 2.0f;
    float totalW = (plan.columns * 2 - 1) * cellPadX + (plan.columns - 1) * plan.gap;
    for (int c = 0; c < plan.columns; c++)
        totalW += plan.colLabelWidth[c] + plan.colControlWidth[c];
    char what[220];
    std::snprintf(what, sizeof(what), "%s: table width (%.0f) fits pane (%.0f)", state, totalW, pageWidth);
    settingsFitFail(totalW <= pageWidth + 1.0f, what);
    for (int i = 0; i < n; i++) {
        const int col = plan.columns > 1 && i >= plan.perColumn ? 1 : 0;
        const float lblW = plan.colLabelWidth[col];
        const float ctlW = plan.colControlWidth[col];
        GevrSettingInfo info{};
        api.info(ids[i], &info);
        char value[64];
        std::snprintf(what, sizeof(what), "%s: the label \"%s\" fits its column (%.0f of %.0f)", state, info.label,
                      ImGui::CalcTextSize(info.label).x + cellPadX, lblW);
        settingsFitFail(ImGui::CalcTextSize(info.label).x + cellPadX <= lblW + 1.0f, what);
        api.text(ids[i], -1, value, sizeof(value));
        const char *reason = api.blocked ? api.blocked(ids[i]) : nullptr;
        float need = 0;
        if (info.kind == GEVR_SET_TOGGLE) {
            need = ImGui::GetFrameHeight();
            if (strcmp(value, "On") && strcmp(value, "Off")) need += style.ItemSpacing.x + ImGui::CalcTextSize(value).x;
            if (reason) need += style.ItemSpacing.x + ImGui::CalcTextSize(reason).x;
        } else if (info.kind == GEVR_SET_CHOICE) {
            need = ImGui::CalcTextSize(value).x + style.FramePadding.x * 2 + ImGui::GetFrameHeight();
        } else {
            need = ImGui::CalcTextSize(value).x + style.FramePadding.x * 2;   // the slider's own text
            if (api.defaultIndex && api.defaultIndex(ids[i]) >= 0)
                need += ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
        }
        std::snprintf(what, sizeof(what), "%s: \"%s\" (%s%s%s) fits its control column (%.0f of %.0f)", state, info.label,
                      value, reason ? ", " : "", reason ? reason : "", need, ctlW);
        settingsFitFail(need <= ctlW + 1.0f, what);
    }
}
#endif
