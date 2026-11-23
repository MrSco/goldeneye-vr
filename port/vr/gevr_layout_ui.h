// Shared layout helpers for the launcher and in-game pause/panel screens
// (docs/launcher-unify.md, Phase 1 of launcher-controls-plan.md).
//
// Keeps rows, sliders, dropdowns and checkbox grids inside the active pane's
// available width with no truncated labels/values and no page scrolling across
// PC flat, PC VR, Quest/mobile VR and the in-game pause overlay.
#ifndef GEVR_LAYOUT_UI_H
#define GEVR_LAYOUT_UI_H

#include "imgui/imgui.h"
#include "gevr_settings.h"
#include "gevr_tooltip.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef GEVR_SETTINGS_ONE_COLUMN
#define GEVR_SETTINGS_ONE_COLUMN 7   // more rows than this take two columns when width permits
#endif

struct GevrColumnPlan {
    int columns = 1;
    int perColumn = 0;
    float colWidth = 0.0f;
    float gap = 0.0f;
    float labelWidth = 0.0f;
    float controlWidth = 0.0f;
    float colLabelWidth[2] = {0.0f, 0.0f};
    float colControlWidth[2] = {0.0f, 0.0f};
    float rowPadY = 0.0f;
};

// Choose up to maxCols columns so that each column is at least minColWidth wide.
// Drops to fewer columns (down to 1) when the pane is narrower.
inline int gevrSettingColumns(float availWidth, int rowCount, float minColWidth, int maxCols = 2, float gap = -1.0f) {
    if (rowCount <= 0 || maxCols <= 1) return 1;
    const float colGap = gap >= 0.0f ? gap : ImGui::GetStyle().ItemSpacing.x * 1.5f;
    int cols = std::min(maxCols, rowCount);
    while (cols > 1) {
        const float w = (availWidth - colGap * (cols - 1)) / (float)cols;
        if (w >= minColWidth) break;
        cols--;
    }
    return cols;
}

// Minimum pixel width needed for one settings row's control + displayed value + Reset/reason.
inline float gevrMeasureControlNeed(int kind, const char *value, const char *reason, bool hasDefault) {
    const ImGuiStyle &style = ImGui::GetStyle();
    const float frameH = ImGui::GetFrameHeight();
    float need = 0.0f;
    if (kind == GEVR_SET_TOGGLE) {
        need = frameH;
        if (value && value[0] && strcmp(value, "On") != 0 && strcmp(value, "Off") != 0)
            need += style.ItemSpacing.x + ImGui::CalcTextSize(value).x;
        if (reason && reason[0])
            need += style.ItemSpacing.x + ImGui::CalcTextSize(reason).x;
    } else if (kind == GEVR_SET_CHOICE) {
        need = ImGui::CalcTextSize(value ? value : "").x + style.FramePadding.x * 2.0f + frameH;
    } else {
        const float textW = ImGui::CalcTextSize(value ? value : "").x + style.FramePadding.x * 2.0f;
        const float minSlider = std::max(textW, ImGui::GetFontSize() * 4.5f);
        need = minSlider;
        if (hasDefault)
            need += ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
    }
    return need;
}

// Compute a column, label, control and vertical-padding plan for a section of rows
// from measured label and value text widths so nothing truncates or overflows.
template <class RowMeasure>
inline GevrColumnPlan gevrPlanSettingsLayout(int count, float availWidth, float availHeight, RowMeasure measureRow,
                                             int oneColLimit = GEVR_SETTINGS_ONE_COLUMN) {
    GevrColumnPlan plan;
    if (count <= 0) return plan;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float cellPadX = style.CellPadding.x * 2.0f;
    const float gap = style.ItemSpacing.x;

    float maxLabelAll = 0.0f;
    float maxCtrlAll = 0.0f;
    for (int i = 0; i < count; i++) {
        float lw = 0.0f, cw = 0.0f;
        measureRow(i, lw, cw);
        if (lw > maxLabelAll) maxLabelAll = lw;
        if (cw > maxCtrlAll) maxCtrlAll = cw;
    }

    // Measure per-column label & control requirements for 2 columns so a wide
    // label or value in one column does not penalize the other column.
    int chosenCols = 1;
    float minLbl[2] = {maxLabelAll + cellPadX, 0.0f};
    float minCtl[2] = {maxCtrlAll, 0.0f};
    const int wantCols = count > oneColLimit ? 2 : 1;
    if (wantCols > 1 && count > 1) {
        const int half = (count + 1) / 2;
        float ml[2] = {0.0f, 0.0f}, mc[2] = {0.0f, 0.0f};
        for (int i = 0; i < count; i++) {
            float lw = 0.0f, cw = 0.0f;
            measureRow(i, lw, cw);
            const int c = i < half ? 0 : 1;
            if (lw > ml[c]) ml[c] = lw;
            if (cw > mc[c]) mc[c] = cw;
        }
        const float colNeed0 = ml[0] + cellPadX + mc[0];
        const float colNeed1 = ml[1] + cellPadX + mc[1];
        // ImGui places CellPadding.x * 2 between each of the 4 table columns (3 gaps)
        // plus our optional center gap.
        const float innerAvail2 = availWidth - cellPadX * 3.0f - gap;
        if (colNeed0 + colNeed1 <= innerAvail2 + 1.0f &&
            gevrSettingColumns(availWidth - cellPadX * 3.0f, count, std::min(colNeed0, colNeed1), 2, gap) == 2) {
            chosenCols = 2;
            minLbl[0] = ml[0] + cellPadX;
            minLbl[1] = ml[1] + cellPadX;
            minCtl[0] = mc[0];
            minCtl[1] = mc[1];
        }
    }

    plan.columns = chosenCols;
    plan.perColumn = (count + plan.columns - 1) / plan.columns;
    plan.gap = plan.columns > 1 ? gap : 0.0f;

    if (plan.columns == 1) {
        const float innerAvail = std::max(80.0f, availWidth - cellPadX);
        plan.colWidth = innerAvail;
        float labelW = innerAvail * 0.36f;
        if (labelW < minLbl[0]) labelW = minLbl[0];
        const float maxLabelAllow = std::max(minLbl[0], innerAvail - minCtl[0]);
        if (labelW > maxLabelAllow) labelW = maxLabelAllow;
        float ctrlAvail = std::max(40.0f, innerAvail - labelW);
        // Single-column wide panes: avoid stretching a slider awkwardly across 750+ px,
        // while keeping plenty of room for the value text and Reset button.
        if (innerAvail > 720.0f) {
            const float comfortable = std::max(minCtl[0] + ImGui::GetFontSize() * 3.0f, innerAvail * 0.48f);
            if (ctrlAvail > comfortable) ctrlAvail = comfortable;
        }
        plan.colLabelWidth[0] = plan.labelWidth = labelW;
        plan.colControlWidth[0] = plan.controlWidth = ctrlAvail;
    } else {
        const float innerAvail = std::max(160.0f, availWidth - cellPadX * 3.0f - plan.gap);
        const float need0 = minLbl[0] + minCtl[0];
        const float need1 = minLbl[1] + minCtl[1];
        const float half = innerAvail * 0.5f;
        float w0 = half, w1 = half;
        if (need0 > half) {
            w0 = std::min(innerAvail - need1, need0);
            w1 = innerAvail - w0;
        } else if (need1 > half) {
            w1 = std::min(innerAvail - need0, need1);
            w0 = innerAvail - w1;
        }
        const float colW[2] = {w0, w1};
        for (int c = 0; c < 2; c++) {
            float lw = colW[c] * 0.42f;
            if (lw < minLbl[c]) lw = minLbl[c];
            const float maxAllow = std::max(minLbl[c], colW[c] - minCtl[c]);
            if (lw > maxAllow) lw = maxAllow;
            plan.colLabelWidth[c] = lw;
            plan.colControlWidth[c] = std::max(40.0f, colW[c] - lw);
        }
        plan.colWidth = half;
        plan.labelWidth = std::min(plan.colLabelWidth[0], plan.colLabelWidth[1]);
        plan.controlWidth = std::min(plan.colControlWidth[0], plan.colControlWidth[1]);
    }

    // Use vertical space gracefully on sparse pages, and trim excess cell padding
    // slightly on dense 2-column pages so nothing ever triggers a scrollbar.
    if (availHeight > 0.0f && plan.perColumn > 0) {
        const float baseRowH = ImGui::GetFrameHeight() + style.CellPadding.y * 2.0f + style.ItemSpacing.y;
        const float spare = availHeight - baseRowH * (float)plan.perColumn;
        if (plan.perColumn <= 6 && spare > baseRowH * 2.0f) {
            const float extraPerRow = std::min(ImGui::GetFontSize() * 0.25f, (spare * 0.20f) / (float)plan.perColumn);
            plan.rowPadY = std::floor(extraPerRow * 0.5f);
        } else if (plan.perColumn >= 8 && style.CellPadding.y > 2.5f) {
            plan.rowPadY = -std::min(1.5f, style.CellPadding.y - 2.0f);
        }
    }
    return plan;
}

// One labelled setting row inside a 2-column table (label column + control column).
// Keeps label aligned to frame padding, applies optional tooltip, and passes the
// exact available control width to drawControl(float width).
template <class ControlFn>
inline void gevrSettingRow(const char *label, float controlWidth, ControlFn drawControl, const char *tooltip = nullptr) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tooltip && tooltip[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        gevrTooltip("%s", tooltip);
    ImGui::TableNextColumn();
    drawControl(controlWidth);
}

// Section group heading with consistent vertical breathing room.
inline void gevrSectionHeader(const ImVec4 &color, const char *title, bool leadingSpace = false) {
    if (leadingSpace) ImGui::Spacing();
    ImGui::TextColored(color, "%s", title);
}

// Wrapping checkbox grid that measures ImGui::GetContentRegionAvail().x (the active
// pane only) and sizes uniform columns from the longest label so nothing overflows
// or truncates. Returns true if any checkbox changed.
template <class NameFn, class GetFn, class SetFn>
inline bool gevrCheckGrid(const char *id, int count, NameFn nameAt, GetFn getAt, SetFn setAt, int maxCols = 0) {
    if (count <= 0) return false;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float boxW = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x;
    float maxCellW = 0.0f;
    for (int i = 0; i < count; i++) {
        const char *label = nameAt(i);
        const float w = boxW + ImGui::CalcTextSize(label ? label : "").x + style.CellPadding.x * 2.0f + 4.0f;
        if (w > maxCellW) maxCellW = w;
    }
    int cols = maxCellW > 0.0f ? (int)std::floor(avail / maxCellW) : 1;
    if (cols < 1) cols = 1;
    if (cols > count) cols = count;
    if (maxCols > 0 && cols > maxCols) cols = maxCols;

    bool changed = false;
    if (ImGui::BeginTable(id, cols, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < count; i++) {
            if (i % cols == 0) ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool on = getAt(i);
            const char *label = nameAt(i);
            ImGui::PushID(i);
            if (ImGui::Checkbox(label ? label : "", &on)) {
                setAt(i, on);
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    return changed;
}

// Shared confirmation modal for destructive actions (Quit, Back to launcher,
// Leave session, End mission). Centers on the viewport and returns:
//   0 = still open / not open, -1 = cancelled, 1 = confirmed.
inline int gevrConfirmModal(const char *popupId, const char *message,
                            const char *confirmLabel, const char *cancelLabel = "Cancel",
                            bool danger = true) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    int result = 0;
    if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextUnformatted(message);
        ImGui::Spacing();
        const float btnW = std::max(180.0f, ImGui::GetFontSize() * 7.7f);
        const float btnH = std::max(36.0f, ImGui::GetFrameHeight() * 1.35f);
        if (ImGui::Button(cancelLabel, ImVec2(btnW, btnH))) {
            result = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (danger) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.5f, 0.4f, 1.0f));
        if (ImGui::Button(confirmLabel, ImVec2(btnW, btnH))) {
            result = 1;
            ImGui::CloseCurrentPopup();
        }
        if (danger) ImGui::PopStyleColor();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
            result = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return result;
}

#endif
