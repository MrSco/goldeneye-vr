// The settings table's rows as ImGui controls (docs/launcher-unify.md): the
// in-game screen's now, the launcher's from phase 2. A toggle is a checkbox,
// a choice a dropdown, a range a slider (with Reset where the row has a
// default). A row that can't change now (gevrSettingBlocked) stays, greyed,
// with its reason. A section's rows take one column, or two past seven, so a
// page never scrolls.
//
// The table is reached through GevrSettingsApi, so the layout tests can hand
// in rows of their own (port/tests/friends_ui_native.cpp).
#ifndef GEVR_SETTINGS_UI_H
#define GEVR_SETTINGS_UI_H
#include "imgui/imgui.h"
#include "gevr_settings.h"
#include "gevr_tooltip.h"
#include <stdio.h>
#include <string.h>

struct GevrSettingsApi {
    int (*sectionCount)(void) = nullptr;
    const char *(*sectionName)(int) = nullptr;
    int (*sectionRows)(int section, unsigned where, int *ids, int max) = nullptr;
    int (*info)(int id, GevrSettingInfo *out) = nullptr;
    int (*positions)(int id) = nullptr;
    int (*index)(int id) = nullptr;
    int (*defaultIndex)(int id) = nullptr;
    void (*set)(int id, int index) = nullptr;
    void (*text)(int id, int index, char *buf, int size) = nullptr;
    const char *(*blocked)(int id) = nullptr;
};

// The real table (port/src/gevr_settings.c).
inline GevrSettingsApi gevrSettingsLive() {
    GevrSettingsApi a;
    a.sectionCount = gevrSettingSectionCount;
    a.sectionName = gevrSettingSectionName;
    a.sectionRows = gevrSettingSectionRows;
    a.info = gevrSettingInfo;
    a.positions = gevrSettingPositions;
    a.index = gevrSettingIndex;
    a.defaultIndex = gevrSettingDefault;
    a.set = gevrSettingSet;
    a.text = gevrSettingText;
    a.blocked = gevrSettingBlocked;
    return a;
}

#define GEVR_SETTINGS_ONE_COLUMN 7   // more rows than this take two columns

// A slider's format shows the value's own text; ImGui reads '%' in it.
inline void gevrSettingsEscape(const char *in, char *out, int size) {
    int o = 0;
    for (int i = 0; in[i] && o + 2 < size; i++) {
        if (in[i] == '%') out[o++] = '%';
        out[o++] = in[i];
    }
    out[o] = '\0';
}

inline void gevrSettingsTip(const GevrSettingInfo &info, const char *reason) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
    if (reason && info.tooltip) gevrTooltip("%s\n%s.", info.tooltip, reason);
    else if (reason) gevrTooltip("%s.", reason);
    else if (info.tooltip) gevrTooltip("%s", info.tooltip);
}

// One row's control, in the width given (the label is drawn by the caller).
inline void gevrSettingControl(const GevrSettingsApi &a, int id, float width) {
    GevrSettingInfo info{};
    if (!a.info(id, &info)) return;
    const char *reason = a.blocked ? a.blocked(id) : nullptr;
    const int positions = a.positions(id);
    const int index = a.index(id);
    char value[64];
    a.text(id, -1, value, sizeof(value));
    ImGui::PushID(id);
    ImGui::BeginDisabled(reason != nullptr);
    if (info.kind == GEVR_SET_TOGGLE) {
        bool on = index != 0;
        if (ImGui::Checkbox("##v", &on)) a.set(id, on ? 1 : 0);
        gevrSettingsTip(info, reason);
        // Off/On says itself; a toggle with its own words (Per gun, Host: on) shows them
        if (strcmp(value, "On") && strcmp(value, "Off")) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(value);
        }
    } else if (info.kind == GEVR_SET_CHOICE) {
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##v", value)) {
            for (int i = 0; i < positions; i++) {
                char name[64];
                a.text(id, i, name, sizeof(name));
                ImGui::PushID(i);
                if (ImGui::Selectable(name, i == index)) a.set(id, i);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        gevrSettingsTip(info, reason);
    } else {
        const int def = a.defaultIndex ? a.defaultIndex(id) : -1;
        const ImGuiStyle &style = ImGui::GetStyle();
        const float reset = def >= 0 ? ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2 + style.ItemSpacing.x : 0;
        char format[96];
        gevrSettingsEscape(value, format, sizeof(format));
        int v = index;
        ImGui::SetNextItemWidth(width - reset);
        if (ImGui::SliderInt("##v", &v, 0, positions > 0 ? positions - 1 : 0, format, ImGuiSliderFlags_AlwaysClamp)
            && v != index) {
            a.set(id, v);
        }
        gevrSettingsTip(info, reason);
        if (def >= 0) {
            ImGui::SameLine();
            ImGui::BeginDisabled(index == def);
            if (ImGui::Button("Reset")) a.set(id, def);
            ImGui::EndDisabled();
        }
    }
    ImGui::EndDisabled();
    if (reason && info.kind != GEVR_SET_RANGE && info.kind != GEVR_SET_CHOICE) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", reason);
    }
    ImGui::PopID();
}

// A section's rows shown now in `where`: label and control, one column or two.
inline void gevrSettingsSection(const GevrSettingsApi &a, int section, unsigned where) {
    int ids[48];
    const int n = a.sectionRows ? a.sectionRows(section, where, ids, 48) : 0;
    const int count = n > 48 ? 48 : n;
    const int columns = count > GEVR_SETTINGS_ONE_COLUMN ? 2 : 1;
    const int perColumn = (count + columns - 1) / columns;
    if (!ImGui::BeginTable("##settings", columns * 2, ImGuiTableFlags_SizingFixedFit)) return;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float gap = columns > 1 ? ImGui::GetStyle().ItemSpacing.x * 4 : 0;
    const float half = (avail - gap) / columns;
    const float labelW = half * (columns > 1 ? 0.44f : 0.40f);
    for (int c = 0; c < columns; c++) {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, labelW);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, half - labelW);
    }
    for (int r = 0; r < perColumn; r++) {
        ImGui::TableNextRow();
        for (int c = 0; c < columns; c++) {
            const int k = c * perColumn + r;
            ImGui::TableNextColumn();
            if (k >= count) { ImGui::TableNextColumn(); continue; }
            GevrSettingInfo info{};
            a.info(ids[k], &info);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(info.label);
            gevrSettingsTip(info, a.blocked ? a.blocked(ids[k]) : nullptr);
            ImGui::TableNextColumn();
            gevrSettingControl(a, ids[k], half - labelW - (columns > 1 ? gap : ImGui::GetStyle().ItemSpacing.x));
        }
    }
    ImGui::EndTable();
}

// The in-game sections as a tab row, each tab's rows under it in a box that
// never scrolls: the solo panel's whole body, the online screen's Settings tab
// (gevr_panel_ui.h, gevr_pause_widgets.h). openSection >= 0 brings that tab up
// once (the layout tests).
inline void gevrSettingsTabs(const GevrSettingsApi &a, int &openSection) {
    const int sections = a.sectionCount ? a.sectionCount() : 0;
    if (ImGui::BeginTabBar("##panel-sections", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        for (int s = 0; s < sections; s++) {
            const ImGuiTabItemFlags open = s == openSection ? ImGuiTabItemFlags_SetSelected : 0;
            if (!ImGui::BeginTabItem(a.sectionName(s), nullptr, open)) continue;
            ImGui::BeginChild("##panel-rows", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
            gevrSettingsSection(a, s, GEVR_SET_INGAME);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    openSection = -1;
}
#endif
