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
#include "gevr_layout_ui.h"
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
    const char *(*warning)(int id) = nullptr;   // optional: a performance cost of the value set now
    void (*drawKbm)() = nullptr;      // optional flat Keyboard & mouse page in gevrSettingsTabs
    void (*drawGamepad)() = nullptr;  // optional flat Gamepad page in gevrSettingsTabs
    // optional: sub-tabs a section adds beside its own rows (mods' pages under Mods/Fun,
    // gevr_modui_ui.h), and drawing them as tab items in the section's sub-tab row
    int (*extraTabs)(int section) = nullptr;
    void (*drawExtraTabs)(int section) = nullptr;
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
    a.warning = gevrSettingWarning;
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

// A warned value's colour (gevrSettingWarning: it can cost frames)
#define GEVR_SETTINGS_WARN_COLOR ImVec4(1.0f, 0.62f, 0.2f, 1.0f)

inline void gevrSettingsTip(const GevrSettingInfo &info, const char *reason, const char *warning = nullptr) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
    if (warning && !reason) {
        if (info.tooltip) gevrTooltip("%s\nWarning: %s.", info.tooltip, warning);
        else gevrTooltip("Warning: %s.", warning);
        return;
    }
    if (reason && info.tooltip) gevrTooltip("%s\n%s.", info.tooltip, reason);
    else if (reason) gevrTooltip("%s.", reason);
    else if (info.tooltip) gevrTooltip("%s", info.tooltip);
}

// One row's control, in the width given (the label is drawn by the caller).
inline void gevrSettingControl(const GevrSettingsApi &a, int id, float width) {
    GevrSettingInfo info{};
    if (!a.info(id, &info)) return;
    const char *reason = a.blocked ? a.blocked(id) : nullptr;
    const char *warning = !reason && a.warning ? a.warning(id) : nullptr;
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
        // a warned value (it can cost frames) reads in the warning colour
        if (warning) ImGui::PushStyleColor(ImGuiCol_Text, GEVR_SETTINGS_WARN_COLOR);
        const bool open = ImGui::BeginCombo("##v", value);
        if (warning) ImGui::PopStyleColor();
        if (open) {
            for (int i = 0; i < positions; i++) {
                char name[64];
                a.text(id, i, name, sizeof(name));
                ImGui::PushID(i);
                if (ImGui::Selectable(name, i == index)) a.set(id, i);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        gevrSettingsTip(info, reason, warning);
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

// Measure the column/label/control plan for a section's rows in the given box.
inline GevrColumnPlan gevrSettingsPlan(const GevrSettingsApi &a, const int *ids, int count, float availWidth, float availHeight) {
    return gevrPlanSettingsLayout(count, availWidth, availHeight, [&](int i, float &labelW, float &ctrlW) {
        GevrSettingInfo info{};
        if (!a.info(ids[i], &info)) return;
        labelW = ImGui::CalcTextSize(info.label ? info.label : "").x;
        char value[64] = "";
        a.text(ids[i], -1, value, sizeof(value));
        const char *reason = a.blocked ? a.blocked(ids[i]) : nullptr;
        const bool hasDef = a.defaultIndex && a.defaultIndex(ids[i]) >= 0;
        ctrlW = gevrMeasureControlNeed(info.kind, value, reason, hasDef);
    });
}

// A section's rows shown now in `where`: label and control, one column or two
// when each column is wide enough for every label and value without truncation.
inline void gevrSettingsSection(const GevrSettingsApi &a, int section, unsigned where) {
    int ids[48];
    const int n = a.sectionRows ? a.sectionRows(section, where, ids, 48) : 0;
    const int count = n > 48 ? 48 : n;
    if (count <= 0) return;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const GevrColumnPlan plan = gevrSettingsPlan(a, ids, count, avail.x, avail.y);
    const ImGuiStyle &style = ImGui::GetStyle();
    if (plan.rowPadY != 0.0f)
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, style.CellPadding.y + plan.rowPadY));
    if (ImGui::BeginTable("##settings", plan.columns * 2, ImGuiTableFlags_SizingFixedFit)) {
        for (int c = 0; c < plan.columns; c++) {
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, plan.colLabelWidth[c]);
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed,
                                    plan.colControlWidth[c] + (c + 1 < plan.columns ? plan.gap : 0.0f));
        }
        for (int r = 0; r < plan.perColumn; r++) {
            ImGui::TableNextRow();
            for (int c = 0; c < plan.columns; c++) {
                const int k = c * plan.perColumn + r;
                ImGui::TableNextColumn();
                if (k >= count) { ImGui::TableNextColumn(); continue; }
                GevrSettingInfo info{};
                a.info(ids[k], &info);
                ImGui::AlignTextToFramePadding();
                const char *why = a.blocked ? a.blocked(ids[k]) : nullptr;
                const char *warn = !why && a.warning ? a.warning(ids[k]) : nullptr;
                if (warn) ImGui::PushStyleColor(ImGuiCol_Text, GEVR_SETTINGS_WARN_COLOR);
                ImGui::TextUnformatted(info.label);
                if (warn) ImGui::PopStyleColor();
                gevrSettingsTip(info, why, warn);
                ImGui::TableNextColumn();
                gevrSettingControl(a, ids[k], plan.colControlWidth[c]);
            }
        }
        ImGui::EndTable();
    }
    if (plan.rowPadY != 0.0f) ImGui::PopStyleVar();
}

// The in-game sections as a tab row, each tab's rows under it in a box that
// never scrolls: the solo panel's whole body, the online screen's Settings tab
// (gevr_panel_ui.h, gevr_pause_widgets.h). openSection >= 0 brings that tab up
// once (the layout tests).
inline void gevrSettingsTabs(const GevrSettingsApi &a, int &openSection) {
    const int sections = a.sectionCount ? a.sectionCount() : 0;
    if (ImGui::BeginTabBar("##panel-sections", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        for (int s = 0; s < sections; s++) {
            const int rowCount = a.sectionRows ? a.sectionRows(s, GEVR_SET_INGAME, nullptr, 0) : 0;
            const bool hasControlSubs = (s == GEVR_SEC_CONTROLS) && (a.drawKbm || a.drawGamepad);
            const int extra = a.extraTabs && a.drawExtraTabs ? a.extraTabs(s) : 0;
            // a section with nothing to set here (a flat game's Gestures) has no tab
            if (rowCount == 0 && !hasControlSubs && extra == 0) continue;
            const ImGuiTabItemFlags open = s == openSection ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(a.sectionName(s), nullptr, open)) {
                if (hasControlSubs) {
                    if (ImGui::BeginTabBar("##panel-controls-subs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
                        if (a.drawKbm && ImGui::BeginTabItem("Keyboard & mouse")) {
                            ImGui::BeginChild("##panel-rows", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                            a.drawKbm();
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        if (a.drawGamepad && ImGui::BeginTabItem("Gamepad")) {
                            ImGui::BeginChild("##panel-rows", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                            a.drawGamepad();
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        if (rowCount > 0 && ImGui::BeginTabItem("Look")) {
                            ImGui::BeginChild("##panel-rows", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                            gevrSettingsSection(a, s, GEVR_SET_INGAME);
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                } else if (extra > 0) {
                    // the section's own rows under Options, then its extra sub-tabs
                    if (ImGui::BeginTabBar("##panel-section-subs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
                        if (rowCount > 0 && ImGui::BeginTabItem("Options")) {
                            ImGui::BeginChild("##panel-rows", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                            gevrSettingsSection(a, s, GEVR_SET_INGAME);
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        a.drawExtraTabs(s);
                        ImGui::EndTabBar();
                    }
                } else {
                    ImGui::BeginChild("##panel-rows", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                    gevrSettingsSection(a, s, GEVR_SET_INGAME);
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    openSection = -1;
}
#endif
