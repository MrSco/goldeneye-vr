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
    const int columns = n > GEVR_SETTINGS_ONE_COLUMN ? 2 : 1;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float half = (pageWidth - (columns > 1 ? style.ItemSpacing.x * 4 : 0)) / columns;
    const float labelW = half * (columns > 1 ? 0.44f : 0.40f);
    const float controlW = half - labelW - style.CellPadding.x * 2;
    for (int i = 0; i < n; i++) {
        GevrSettingInfo info{};
        api.info(ids[i], &info);
        char what[220], value[64];
        std::snprintf(what, sizeof(what), "%s: the label \"%s\" fits its column", state, info.label);
        settingsFitFail(ImGui::CalcTextSize(info.label).x + style.CellPadding.x * 2 <= labelW, what);
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
                      value, reason ? ", " : "", reason ? reason : "", need, controlW);
        settingsFitFail(need <= controlW + 1, what);
    }
}
#endif
