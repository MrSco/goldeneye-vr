// PC flat keybinds, mouse and gamepad controls UI (Phase 2 of launcher-controls-plan.md).
//
// Shared by the launcher's Controls tab (Keyboard & mouse / Gamepad sub-tabs) and
// the in-game pause/panel screens. Uses a plain C/C++ callback table (GevrKeybindApi)
// so native tests can verify bind capture, duplicate detection, save/load round-trips
// and multi-size layout without SDL or OpenGL.
#ifndef GEVR_KEYBINDS_UI_H
#define GEVR_KEYBINDS_UI_H

#include "imgui/imgui.h"
#include "gevr_layout_ui.h"
#include "gevr_tooltip.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// Action IDs mirror port/include/input.h's enum contkey so this header stays
// standalone for native tests that don't pull in ultra64/SDL headers.
enum {
    GEVR_CK_C_R = 0,
    GEVR_CK_C_L = 1,
    GEVR_CK_C_D = 2,
    GEVR_CK_C_U = 3,
    GEVR_CK_RTRIG = 4,
    GEVR_CK_LTRIG = 5,
    GEVR_CK_DPAD_R = 8,
    GEVR_CK_DPAD_L = 9,
    GEVR_CK_DPAD_D = 10,
    GEVR_CK_DPAD_U = 11,
    GEVR_CK_START = 12,
    GEVR_CK_ZTRIG = 13,
    GEVR_CK_B = 14,
    GEVR_CK_A = 15,
    GEVR_CK_STICK_XNEG = 16,
    GEVR_CK_STICK_XPOS = 17,
    GEVR_CK_STICK_YNEG = 18,
    GEVR_CK_STICK_YPOS = 19,
    GEVR_CK_ACCEPT = 20,
    GEVR_CK_CANCEL = 21,
    GEVR_CK_PREV_WEAPON = 22,
    GEVR_CK_RESTART = 32       // past the pad's buttons (input.h CK_RESTART, #280)
};

enum {
    GEVR_VK_ESCAPE = 41,
    GEVR_VK_BACKSPACE = 42,
    GEVR_VK_DELETE = 76,
    GEVR_VK_MOUSE_BEGIN = 512,
    GEVR_VK_MOUSE_WHEEL_UP = 517,
    GEVR_VK_MOUSE_WHEEL_DN = 518,
    GEVR_VK_JOY_BEGIN = 519,
    GEVR_VK_VR_BEGIN = 519 + 4 * 32
};

struct GevrKeybindApi {
    unsigned (*getBind)(unsigned ck, bool isGamepad, int slot) = nullptr;
    void (*setBind)(unsigned ck, bool isGamepad, int slot, unsigned vk) = nullptr;
    void (*resetBinds)(bool isGamepad) = nullptr;
    int (*countBinds)(unsigned vk, bool isGamepad, const unsigned *actions, int actionCount) = nullptr;
    const char *(*keyDisplayName)(int vk) = nullptr;

    // Capture-next-key hooks
    int (*getLastKey)() = nullptr;
    void (*clearLastKey)() = nullptr;
    void (*setCapture)(bool active) = nullptr;

    // Mouse settings
    void (*getMouseSpeed)(float *x, float *y) = nullptr;
    void (*setMouseSpeed)(float x, float y) = nullptr;
    int (*getRawInput)() = nullptr;
    void (*setRawInput)(int enable) = nullptr;
    int (*getMouseLockMode)() = nullptr;
    void (*setMouseLockMode)(int mode) = nullptr;

    // Gamepad stick & feedback settings
    int (*getSwapSticks)() = nullptr;      // 1 = default (L=move, R=look), 0 = swapped (L=look, R=move)
    void (*setSwapSticks)(int val) = nullptr;
    float (*getMoveDeadzone)() = nullptr;  // 0..0.5
    void (*setMoveDeadzone)(float dz) = nullptr;
    float (*getMoveScale)() = nullptr;     // 0.5..2.0
    void (*setMoveScale)(float s) = nullptr;
    float (*getLookSpeed)() = nullptr;     // 0.5..2.0
    float (*getLookDeadzone)() = nullptr;  // 0..0.3
    int (*getLookInverted)() = nullptr;
    void (*setLook)(float speed, float deadzone, int inverted) = nullptr;
    float (*getPadVibration)() = nullptr;  // 0..1
    void (*setPadVibration)(float v) = nullptr;
    int (*getTriggerFeedback)() = nullptr; // 0..100
    void (*setTriggerFeedback)(int pct) = nullptr;

    void (*save)() = nullptr;
};

struct GevrKeybindUiState {
    bool capturing = false;
    bool captureGamepad = false;
    unsigned captureAction = 0;
    int captureSlot = 0;
    bool separateXY = false;
    bool separateInit = false;
    char status[96] = "";
};

// Campaign-aware game action labels (GE, Goldfinger 64, TND, Mirror, Mario).
inline const char *gevrActionLabel(int campaign, unsigned ck, const char **tooltipOut = nullptr) {
    // 0 = GoldenEye, 1 = Goldfinger 64, 2 = Tomorrow Never Dies, 3 = Mirror Mode, 4 = Mario Characters
    const bool mirror = (campaign == 3);
    const char *tip = nullptr;
    const char *label = "Action";
    switch (ck) {
        case GEVR_CK_ZTRIG:       label = "Fire";         tip = "Fire current weapon or throw."; break;
        case GEVR_CK_RTRIG:       label = "Aim";          tip = "Hold for precision aim / zoom."; break;
        case GEVR_CK_B:           label = "Use / Reload"; tip = "Open doors, activate switches, or reload."; break;
        case GEVR_CK_A:           label = "Next weapon";  tip = "Cycle forward through weapons."; break;
        case GEVR_CK_PREV_WEAPON: label = "Prev weapon";  tip = "Cycle backward through weapons (default: mouse wheel up)."; break;
        case GEVR_CK_LTRIG:       label = "Alt aim";      tip = "Secondary aim trigger (N64 L button)."; break;
        case GEVR_CK_START:       label = "Pause";        tip = "Open Bond's watch / pause menu."; break;
        case GEVR_CK_RESTART:     label = "Restart";      tip = "Restart the mission from its intro, as a fresh load: the speedrunner's reset (default: Backspace). Solo, offline, with the Speedrun timer, Ghost race or LiveSplit on."; break;
        case GEVR_CK_DPAD_D:      label = "Crouch / Dn";  tip = "Crouch while aiming / D-pad down."; break;
        case GEVR_CK_C_U:         label = "Forward";      tip = "Walk forward (C-Up)."; break;
        case GEVR_CK_C_D:         label = "Back";         tip = "Walk backward (C-Down)."; break;
        case GEVR_CK_C_L:
            label = mirror ? "Strafe right" : "Strafe left";
            tip = mirror ? "Mirror Mode: strafe right." : "Strafe left (C-Left).";
            break;
        case GEVR_CK_C_R:
            label = mirror ? "Strafe left" : "Strafe right";
            tip = mirror ? "Mirror Mode: strafe left." : "Strafe right (C-Right).";
            break;
        case GEVR_CK_STICK_YPOS:  label = "Look up";      tip = "Look up (keyboard / digital stick)."; break;
        case GEVR_CK_STICK_YNEG:  label = "Look down";    tip = "Look down (keyboard / digital stick)."; break;
        case GEVR_CK_STICK_XNEG:
            label = mirror ? "Turn right" : "Turn left";
            tip = mirror ? "Mirror Mode: turn right." : "Turn left (keyboard / digital stick).";
            break;
        case GEVR_CK_STICK_XPOS:
            label = mirror ? "Turn left" : "Turn right";
            tip = mirror ? "Mirror Mode: turn left." : "Turn right (keyboard / digital stick).";
            break;
        case GEVR_CK_ACCEPT:      label = "Menu accept";  tip = "Confirm menu selections."; break;
        case GEVR_CK_CANCEL:      label = "Menu back";    tip = "Go back in menus."; break;
        default: break;
    }
    if (campaign == 1 && ck == GEVR_CK_B) tip = "Goldfinger 64: open doors, use gadgets, or reload.";
    if (campaign == 2 && ck == GEVR_CK_B) tip = "Tomorrow Never Dies: open doors, use items, or reload.";
    if (campaign == 4 && ck == GEVR_CK_B) tip = "Mario Characters: open doors, activate, or reload.";
    if (tooltipOut) *tooltipOut = tip;
    return label;
}

inline const unsigned *gevrKbmActions(int &count) {
    static const unsigned kActions[] = {
        GEVR_CK_ZTRIG, GEVR_CK_RTRIG, GEVR_CK_B, GEVR_CK_A,
        GEVR_CK_PREV_WEAPON, GEVR_CK_LTRIG, GEVR_CK_START, GEVR_CK_DPAD_D,
        GEVR_CK_C_U, GEVR_CK_C_D, GEVR_CK_C_L, GEVR_CK_C_R,
        GEVR_CK_STICK_YPOS, GEVR_CK_STICK_YNEG, GEVR_CK_STICK_XNEG, GEVR_CK_STICK_XPOS
    };
    count = (int)(sizeof(kActions) / sizeof(kActions[0]));
    return kActions;
}

// Every keyboard and mouse bind, for duplicates: the table's and Restart, which the
// page draws on the mouse settings' second row (a 17th table row scrolls the pause page).
inline const unsigned *gevrKbmBindActions(int &count) {
    static const unsigned kActions[] = {
        GEVR_CK_ZTRIG, GEVR_CK_RTRIG, GEVR_CK_B, GEVR_CK_A,
        GEVR_CK_PREV_WEAPON, GEVR_CK_LTRIG, GEVR_CK_START, GEVR_CK_DPAD_D,
        GEVR_CK_C_U, GEVR_CK_C_D, GEVR_CK_C_L, GEVR_CK_C_R,
        GEVR_CK_STICK_YPOS, GEVR_CK_STICK_YNEG, GEVR_CK_STICK_XNEG, GEVR_CK_STICK_XPOS,
        GEVR_CK_RESTART
    };
    count = (int)(sizeof(kActions) / sizeof(kActions[0]));
    return kActions;
}

inline const unsigned *gevrGamepadActions(int &count) {
    static const unsigned kActions[] = {
        GEVR_CK_ZTRIG, GEVR_CK_RTRIG, GEVR_CK_B, GEVR_CK_A,
        GEVR_CK_PREV_WEAPON, GEVR_CK_LTRIG,
        GEVR_CK_START, GEVR_CK_C_U, GEVR_CK_C_D, GEVR_CK_C_L,
        GEVR_CK_C_R, GEVR_CK_ACCEPT
    };
    count = (int)(sizeof(kActions) / sizeof(kActions[0]));
    return kActions;
}

// Every gamepad bind, for duplicates: the table's and Restart (on the settings' first row).
inline const unsigned *gevrGamepadBindActions(int &count) {
    static const unsigned kActions[] = {
        GEVR_CK_ZTRIG, GEVR_CK_RTRIG, GEVR_CK_B, GEVR_CK_A,
        GEVR_CK_PREV_WEAPON, GEVR_CK_LTRIG,
        GEVR_CK_START, GEVR_CK_C_U, GEVR_CK_C_D, GEVR_CK_C_L,
        GEVR_CK_C_R, GEVR_CK_ACCEPT, GEVR_CK_RESTART
    };
    count = (int)(sizeof(kActions) / sizeof(kActions[0]));
    return kActions;
}

inline void gevrKeybindCancelCapture(const GevrKeybindApi &api, GevrKeybindUiState &ui) {
    if (ui.capturing) {
        ui.capturing = false;
        if (api.setCapture) api.setCapture(false);
        if (api.clearLastKey) api.clearLastKey();
    }
}

// Poll for a captured key/button/wheel while capture mode is active.
// Returns true if capture finished (bound, cleared, or cancelled).
inline bool gevrKeybindPollCapture(const GevrKeybindApi &api, GevrKeybindUiState &ui) {
    if (!ui.capturing || !api.getLastKey) return false;
    const int vk = api.getLastKey();
    if (vk <= 0) return false;
    if (api.clearLastKey) api.clearLastKey();

    if (vk == GEVR_VK_ESCAPE) {
        gevrKeybindCancelCapture(api, ui);
        snprintf(ui.status, sizeof(ui.status), "Cancelled.");
        return true;
    }
    if (vk == GEVR_VK_DELETE || vk == GEVR_VK_BACKSPACE) {
        if (api.setBind) api.setBind(ui.captureAction, ui.captureGamepad, ui.captureSlot, 0);
        if (api.save) api.save();
        gevrKeybindCancelCapture(api, ui);
        snprintf(ui.status, sizeof(ui.status), "Bind cleared.");
        return true;
    }
    const bool isPadKey = (vk >= GEVR_VK_JOY_BEGIN && vk < GEVR_VK_VR_BEGIN);
    const bool isKbmKey = (vk > 0 && vk < GEVR_VK_JOY_BEGIN);
    if ((ui.captureGamepad && isPadKey) || (!ui.captureGamepad && isKbmKey)) {
        if (api.setBind) api.setBind(ui.captureAction, ui.captureGamepad, ui.captureSlot, (unsigned)vk);
        if (api.save) api.save();
        const char *kname = api.keyDisplayName ? api.keyDisplayName(vk) : "";
        gevrKeybindCancelCapture(api, ui);
        snprintf(ui.status, sizeof(ui.status), "Bound to %s.", kname);
        return true;
    }
    snprintf(ui.status, sizeof(ui.status), ui.captureGamepad
             ? "Press a gamepad button or trigger (Esc cancels)."
             : "Press a key, mouse button or wheel (Esc cancels).");
    return false;
}

// One bind: its button (click to capture the next key) and its clear (X) button. The
// table's cells, and the keyboard page's Restart. Duplicates count across dupActions.
inline void gevrDrawBindSlot(const GevrKeybindApi &api, GevrKeybindUiState &ui, unsigned ck,
                             const char *label, bool isGamepad, int slot,
                             const unsigned *dupActions, int dupCount,
                             float btnW, float clearW, bool &anyDuplicate) {
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec4 gold(1.0f, 0.84f, 0.47f, 1.0f);
    const ImVec4 dupBg(0.55f, 0.28f, 0.08f, 1.0f);
    const ImVec4 dupHover(0.68f, 0.36f, 0.10f, 1.0f);
    const ImVec4 capBg(0.45f, 0.36f, 0.08f, 1.0f);
    ImGui::PushID((int)(ck * 4 + slot + (isGamepad ? 1000 : 0)));
    const unsigned vk = api.getBind ? api.getBind(ck, isGamepad, slot) : 0;
    const bool isCap = ui.capturing && ui.captureGamepad == isGamepad &&
                       ui.captureAction == ck && ui.captureSlot == slot;
    const bool isDup = !isCap && vk != 0 && api.countBinds &&
                       api.countBinds(vk, isGamepad, dupActions, dupCount) >= 2;
    if (isDup) anyDuplicate = true;

    const char *btnText = isCap ? "Press..." : (api.keyDisplayName ? api.keyDisplayName((int)vk) : "—");
    int pushedCols = 0;
    if (isCap) {
        ImGui::PushStyleColor(ImGuiCol_Button, capBg);
        ImGui::PushStyleColor(ImGuiCol_Text, gold);
        pushedCols = 2;
    } else if (isDup) {
        ImGui::PushStyleColor(ImGuiCol_Button, dupBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, dupHover);
        pushedCols = 2;
    }

    if (ImGui::Button(btnText, ImVec2(btnW, 0))) {
        ui.capturing = true;
        ui.captureGamepad = isGamepad;
        ui.captureAction = ck;
        ui.captureSlot = slot;
        if (api.clearLastKey) api.clearLastKey();
        if (api.setCapture) api.setCapture(true);
        snprintf(ui.status, sizeof(ui.status), isGamepad
                 ? "Press a gamepad button for %s (Esc cancels, Del clears)..."
                 : "Press a key, mouse button or wheel for %s (Esc cancels, Del clears)...", label);
    }
    if (pushedCols) ImGui::PopStyleColor(pushedCols);
    if (ImGui::IsItemHovered()) {
        if (isDup) gevrTooltip("Duplicate bind: %s is assigned to another action.", btnText);
        else gevrTooltip("Click or press Enter/A to bind %s (%s).", label, slot == 0 ? "Primary" : "Secondary");
    }

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::BeginDisabled(vk == 0 || ui.capturing);
    if (ImGui::Button("X", ImVec2(clearW, 0))) {
        if (api.setBind) api.setBind(ck, isGamepad, slot, 0);
        if (api.save) api.save();
        snprintf(ui.status, sizeof(ui.status), "Cleared %s.", label);
    }
    ImGui::EndDisabled();
    if (vk != 0 && ImGui::IsItemHovered()) gevrTooltip("Clear this bind.");
    ImGui::PopID();
}

// Restart (#280) on a settings row's free width, after what the row already holds: its
// primary bind (the table has no room for a further row without scrolling the pause page).
inline void gevrDrawRestartBind(const GevrKeybindApi &api, GevrKeybindUiState &ui, int campaign, bool isGamepad) {
    const ImGuiStyle &style = ImGui::GetStyle();
    int nAll = 0;
    const unsigned *all = isGamepad ? gevrGamepadBindActions(nAll) : gevrKbmBindActions(nAll);
    const char *tip = nullptr;
    const char *label = gevrActionLabel(campaign, GEVR_CK_RESTART, &tip);
    const float clearW = std::max(26.0f, ImGui::CalcTextSize("X").x + style.FramePadding.x * 1.4f);
    const float btnW = std::max(100.0f, ImGui::CalcTextSize("Backspace").x + style.FramePadding.x * 4.0f);
    bool dup = false;
    ImGui::SameLine(0.0f, style.ItemSpacing.x * 4.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tip && ImGui::IsItemHovered()) gevrTooltip("%s", tip);
    ImGui::SameLine();
    gevrDrawBindSlot(api, ui, GEVR_CK_RESTART, label, isGamepad, 0, all, nAll, btnW, clearW, dup);
}

// Reusable 2-column-pair bind table (Action | Primary | Secondary | Action | Primary | Secondary).
// Fits all actions on one screen with zero scrolling, big tap targets, clear (X) buttons
// for touch/pad users, and duplicate-bind highlighting.
inline void gevrDrawBindTable(const char *id, const GevrKeybindApi &api, GevrKeybindUiState &ui,
                              int campaign, bool isGamepad, const unsigned *actions, int count,
                              const unsigned *dupActions = nullptr, int dupCount = 0) {
    if (!dupActions) {
        dupActions = actions;
        dupCount = count;
    }
    if (ui.capturing && ui.captureGamepad == isGamepad) {
        gevrKeybindPollCapture(api, ui);
    }

    const ImGuiStyle &style = ImGui::GetStyle();
    const float avail = std::max(320.0f, ImGui::GetContentRegionAvail().x);
    const int cols = avail >= 860.0f ? 2 : 1;
    const int rows = (count + cols - 1) / cols;
    const float centerGap = cols > 1 ? style.ItemSpacing.x : 0.0f;
    const float cellPadX = style.CellPadding.x * 2.0f;
    const float sideAvail = (avail - centerGap - cellPadX * (float)(cols * 3 - 1)) / (float)cols;

    float maxLabel = 0.0f;
    for (int i = 0; i < count; i++) {
        const float w = ImGui::CalcTextSize(gevrActionLabel(campaign, actions[i])).x;
        if (w > maxLabel) maxLabel = w;
    }
    const float labelW = std::min(sideAvail * 0.36f, maxLabel + 10.0f);
    const float slotColW = std::max(100.0f, (sideAvail - labelW) * 0.5f);
    const float clearW = std::max(26.0f, ImGui::CalcTextSize("X").x + style.FramePadding.x * 1.4f);
    const float btnW = std::max(60.0f, slotColW - clearW - style.ItemInnerSpacing.x);

    const ImVec4 gold(1.0f, 0.84f, 0.47f, 1.0f);

    bool anyDuplicate = false;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * 0.7f, 2.5f));
    if (ImGui::BeginTable(id, cols * 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        for (int c = 0; c < cols; c++) {
            ImGui::TableSetupColumn(c == 0 ? "Action" : "Action##2", ImGuiTableColumnFlags_WidthFixed, labelW);
            ImGui::TableSetupColumn(c == 0 ? "Primary" : "Primary##2", ImGuiTableColumnFlags_WidthFixed, slotColW);
            ImGui::TableSetupColumn(c == 0 ? "Secondary" : "Secondary##2", ImGuiTableColumnFlags_WidthFixed,
                                    slotColW + (c + 1 < cols ? centerGap : 0.0f));
        }
        ImGui::TableHeadersRow();

        for (int r = 0; r < rows; r++) {
            ImGui::TableNextRow();
            for (int c = 0; c < cols; c++) {
                const int idx = c * rows + r;
                if (idx >= count) {
                    ImGui::TableNextColumn();
                    ImGui::TableNextColumn();
                    ImGui::TableNextColumn();
                    continue;
                }
                const unsigned ck = actions[idx];
                const char *tip = nullptr;
                const char *label = gevrActionLabel(campaign, ck, &tip);

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                if (tip && ImGui::IsItemHovered()) gevrTooltip("%s", tip);

                for (int slot = 0; slot < 2; slot++) {
                    ImGui::TableNextColumn();
                    gevrDrawBindSlot(api, ui, ck, label, isGamepad, slot, dupActions, dupCount,
                                     btnW, clearW, anyDuplicate);
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);

    ImGui::Spacing();
    if (ImGui::Button(isGamepad ? "Reset pad defaults" : "Reset all to defaults")) {
        gevrKeybindCancelCapture(api, ui);
        if (api.resetBinds) api.resetBinds(isGamepad);
        if (api.save) api.save();
        snprintf(ui.status, sizeof(ui.status), "Defaults restored.");
    }
    ImGui::SameLine();
    if (ui.capturing) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(gold, "%s", ui.status);
    } else if (anyDuplicate) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f), "Highlighted keys are bound to multiple actions.");
    } else if (ui.status[0]) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", ui.status);
    }
}

// Keyboard & mouse page: mouse settings (and Restart) + 16-action bind table (no scrolling).
inline void gevrDrawKbmPage(const GevrKeybindApi &api, GevrKeybindUiState &ui, int campaign) {
    float mx = 2.5f, my = 2.5f;
    if (api.getMouseSpeed) api.getMouseSpeed(&mx, &my);
    bool invertY = my < 0.0f;
    float absX = std::fabs(mx);
    float absY = std::fabs(my);
    if (absX < 0.2f) absX = 0.2f;
    if (absY < 0.2f) absY = 0.2f;
    if (!ui.separateInit) {
        ui.separateXY = std::fabs(absX - absY) > 0.05f;
        ui.separateInit = true;
    }

    bool mouseChanged = false;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float avail = std::max(320.0f, ImGui::GetContentRegionAvail().x);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 3.0f));
    if (ImGui::BeginTable("##kbm-mouse-hdr", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 0.38f);

        // Row 1: Mouse sensitivity + Separate X/Y
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (!ui.separateXY) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Mouse sens");
            ImGui::SameLine();
            const float resetW = ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
            ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - resetW));
            const char *fmt = std::fabs(absX - 2.5f) < 0.05f ? "%.1f (default)" : "%.1f";
            if (ImGui::SliderFloat("##msens", &absX, 0.2f, 15.0f, fmt)) {
                absY = absX;
                mouseChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset##msens")) {
                absX = absY = 2.5f;
                mouseChanged = true;
            }
        } else {
            const float colW = ImGui::GetContentRegionAvail().x;
            const float lblX = ImGui::CalcTextSize("Sens X").x + style.ItemSpacing.x;
            const float lblY = ImGui::CalcTextSize("Sens Y").x + style.ItemSpacing.x;
            const float sW = std::max(70.0f, (colW - lblX - lblY - style.ItemSpacing.x * 2.0f) * 0.5f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Sens X");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(sW);
            if (ImGui::SliderFloat("##msensx", &absX, 0.2f, 15.0f, "%.1f")) mouseChanged = true;
            ImGui::SameLine();
            ImGui::TextUnformatted("Sens Y");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(sW);
            if (ImGui::SliderFloat("##msensy", &absY, 0.2f, 15.0f, "%.1f")) mouseChanged = true;
        }
        ImGui::TableNextColumn();
        if (ImGui::Checkbox("Separate X/Y", &ui.separateXY)) {
            if (!ui.separateXY) {
                absY = absX;
                mouseChanged = true;
            }
        }
        if (ImGui::IsItemHovered()) gevrTooltip("Set horizontal and vertical mouse sensitivity separately.");
        ImGui::SameLine();
        if (ImGui::Checkbox("Invert Y", &invertY)) mouseChanged = true;
        if (ImGui::IsItemHovered()) gevrTooltip("Invert vertical mouse look.");

        // Row 2: Raw input + Mouse lock mode
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        bool raw = api.getRawInput ? (api.getRawInput() != 0) : true;
        if (ImGui::Checkbox("Raw input", &raw)) {
            if (api.setRawInput) api.setRawInput(raw ? 1 : 0);
            if (api.save) api.save();
        }
        if (ImGui::IsItemHovered()) gevrTooltip("Bypass OS pointer acceleration for 1:1 mouse movement.");
        gevrDrawRestartBind(api, ui, campaign, false);

        ImGui::TableNextColumn();
        int lockMode = api.getMouseLockMode ? api.getMouseLockMode() : 2;
        if (lockMode < 0 || lockMode > 2) lockMode = 2;
        static const char *const kLockNames[] = { "Off", "Always", "Auto" };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Mouse lock");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo("##mlock", kLockNames[lockMode])) {
            for (int i = 0; i < 3; i++) {
                if (ImGui::Selectable(kLockNames[i], lockMode == i)) {
                    lockMode = i;
                    if (api.setMouseLockMode) api.setMouseLockMode(lockMode);
                    if (api.save) api.save();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) gevrTooltip("Auto: click window to lock mouse, Esc releases. Always: lock in play. Off: never lock.");
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    if (mouseChanged) {
        if (api.setMouseSpeed) api.setMouseSpeed(absX, invertY ? -absY : absY);
        if (api.save) api.save();
    }

    (void)avail;
    ImGui::Separator();

    int n = 0, nAll = 0;
    const unsigned *actions = gevrKbmActions(n);
    const unsigned *all = gevrKbmBindActions(nAll);
    gevrDrawBindTable("##kbm-binds", api, ui, campaign, false, actions, n, all, nAll);
}

// Gamepad page: stick deadzones/scales, swap sticks, vibration, look & trigger feedback + pad bind table.
inline void gevrDrawGamepadPage(const GevrKeybindApi &api, GevrKeybindUiState &ui, int campaign) {
    const ImGuiStyle &style = ImGui::GetStyle();
    bool swapped = api.getSwapSticks ? (api.getSwapSticks() == 0) : false;   // default 1 = L move / R look
    float moveDz = api.getMoveDeadzone ? api.getMoveDeadzone() : 0.125f;
    float moveSc = api.getMoveScale ? api.getMoveScale() : 1.0f;
    float lookSpd = api.getLookSpeed ? api.getLookSpeed() : 1.0f;
    float lookDz = api.getLookDeadzone ? api.getLookDeadzone() : 0.125f;
    bool lookInv = api.getLookInverted ? (api.getLookInverted() != 0) : false;
    int vibPct = api.getPadVibration ? (int)std::floor(api.getPadVibration() * 100.0f + 0.5f) : 30;
    int trigPct = api.getTriggerFeedback ? api.getTriggerFeedback() : 100;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, 2.0f));
    if (ImGui::BeginTable("##pad-settings", 4, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 0.21f);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 0.29f);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 0.21f);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 0.29f);

        // Row 1: Swap sticks | Invert look
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Swap sticks");
        ImGui::TableNextColumn();
        if (ImGui::Checkbox("##swapsticks", &swapped)) {
            if (api.setSwapSticks) api.setSwapSticks(swapped ? 0 : 1);
            if (api.save) api.save();
        }
        if (ImGui::IsItemHovered()) gevrTooltip("Swap left (move) and right (look) sticks.");
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Invert look");
        ImGui::TableNextColumn();
        if (ImGui::Checkbox("##padinv", &lookInv)) {
            if (api.setLook) api.setLook(lookSpd, lookDz, lookInv ? 1 : 0);
            if (api.save) api.save();
        }
        if (ImGui::IsItemHovered()) gevrTooltip("Push the look stick up to look down.");
        gevrDrawRestartBind(api, ui, campaign, true);

        // Row 2: Move deadzone | Look deadzone
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Move deadzone");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        int moveDzPct = (int)std::floor(moveDz * 100.0f + 0.5f);
        if (ImGui::SliderInt("##movedz", &moveDzPct, 0, 50, "%d%%")) {
            if (api.setMoveDeadzone) api.setMoveDeadzone(moveDzPct / 100.0f);
            if (api.save) api.save();
        }
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Look deadzone");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        int lookDzPct = (int)std::floor(lookDz * 100.0f + 0.5f);
        if (ImGui::SliderInt("##lookdz", &lookDzPct, 0, 30, "%d%%")) {
            if (api.setLook) api.setLook(lookSpd, lookDzPct / 100.0f, lookInv ? 1 : 0);
            if (api.save) api.save();
        }

        // Row 3: Move scale | Look speed
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Move scale");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##movesc", &moveSc, 0.5f, 2.0f, "%.2fx")) {
            if (api.setMoveScale) api.setMoveScale(moveSc);
            if (api.save) api.save();
        }
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Look speed");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##lookspd", &lookSpd, 0.5f, 2.0f, "%.1fx")) {
            if (api.setLook) api.setLook(lookSpd, lookDz, lookInv ? 1 : 0);
            if (api.save) api.save();
        }

        // Row 4: Pad vibration | Trigger feedback
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Pad vibration");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderInt("##padvib", &vibPct, 0, 100, vibPct == 0 ? "Off" : "%d%%")) {
            if (api.setPadVibration) api.setPadVibration(vibPct / 100.0f);
            if (api.save) api.save();
        }
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Trigger feel");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderInt("##trigfb", &trigPct, 0, 100, trigPct == 0 ? "Off" : "%d%%")) {
            if (api.setTriggerFeedback) api.setTriggerFeedback(trigPct);
            if (api.save) api.save();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
    ImGui::Separator();

    int n = 0, nAll = 0;
    const unsigned *actions = gevrGamepadActions(n);
    const unsigned *all = gevrGamepadBindActions(nAll);
    gevrDrawBindTable("##pad-binds", api, ui, campaign, true, actions, n, all, nAll);
}

#ifndef GEVR_KEYBINDS_NO_LIVE
#ifdef GEVR_FLAT
extern "C" {
unsigned inputGetClassBind(int cidx, unsigned ck, int isGamepad, int slot);
void inputSetClassBind(int cidx, unsigned ck, int isGamepad, int slot, unsigned vk);
void inputResetClassBinds(int cidx, int isGamepad);
int inputCountKeyBinds(int cidx, unsigned vk, int isGamepad, const unsigned *actions, int actionCount);
const char *inputGetKeyDisplayName(int vk);
int inputGetLastKey(void);
void inputClearLastKey(void);
void inputSetBindingCapture(int active);
void inputMouseGetSpeed(float *x, float *y);
void inputMouseSetSpeed(float x, float y);
int inputGetRawInput(void);
void inputSetRawInput(int enable);
int inputGetMouseLockMode(void);
void inputSetMouseLockMode(int lockmode);
int inputControllerGetSticksSwapped(int cidx);
void inputControllerSetSticksSwapped(int cidx, int swapped);
float inputControllerGetAxisDeadzone(int cidx, int stick, int axis);
void inputControllerSetAxisDeadzone(int cidx, int stick, int axis, float value);
float inputControllerGetAxisScale(int cidx, int stick, int axis);
void inputControllerSetAxisScale(int cidx, int stick, int axis, float value);
float inputPadLookSpeed(void);
float inputPadLookDeadzone(void);
int inputPadLookInverted(void);
void inputPadLookSet(float speed, float deadzone, int inverted);
float inputRumbleGetStrength(int cidx);
void inputRumbleSetStrengthAll(float val);
void inputRumbleTest(int cidx);
void inputSaveConfigNow(void);
void vrSettingsSave(void);
extern int VrTriggerFeedback;
}

inline GevrKeybindApi gevrKeybindLive() {
    GevrKeybindApi a;
    a.getBind = [](unsigned ck, bool pad, int slot) { return inputGetClassBind(0, ck, pad ? 1 : 0, slot); };
    a.setBind = [](unsigned ck, bool pad, int slot, unsigned vk) { inputSetClassBind(0, ck, pad ? 1 : 0, slot, vk); };
    a.resetBinds = [](bool pad) { inputResetClassBinds(0, pad ? 1 : 0); };
    a.countBinds = [](unsigned vk, bool pad, const unsigned *acts, int n) { return inputCountKeyBinds(0, vk, pad ? 1 : 0, acts, n); };
    a.keyDisplayName = [](int vk) { return inputGetKeyDisplayName(vk); };
    a.getLastKey = []() { return inputGetLastKey(); };
    a.clearLastKey = []() { inputClearLastKey(); };
    a.setCapture = [](bool on) { inputSetBindingCapture(on ? 1 : 0); };
    a.getMouseSpeed = [](float *x, float *y) { inputMouseGetSpeed(x, y); };
    a.setMouseSpeed = [](float x, float y) { inputMouseSetSpeed(x, y); };
    a.getRawInput = []() { return inputGetRawInput(); };
    a.setRawInput = [](int on) { inputSetRawInput(on); };
    a.getMouseLockMode = []() { return inputGetMouseLockMode(); };
    a.setMouseLockMode = [](int m) { inputSetMouseLockMode(m); };
    a.getSwapSticks = []() { return inputControllerGetSticksSwapped(0); };
    a.setSwapSticks = [](int v) { inputControllerSetSticksSwapped(0, v); };
    a.getMoveDeadzone = []() { return inputControllerGetAxisDeadzone(0, 0, 0); };
    a.setMoveDeadzone = [](float dz) {
        inputControllerSetAxisDeadzone(0, 0, 0, dz);
        inputControllerSetAxisDeadzone(0, 0, 1, dz);
    };
    a.getMoveScale = []() { return inputControllerGetAxisScale(0, 0, 0); };
    a.setMoveScale = [](float s) {
        inputControllerSetAxisScale(0, 0, 0, s);
        inputControllerSetAxisScale(0, 0, 1, s);
    };
    a.getLookSpeed = []() { return inputPadLookSpeed(); };
    a.getLookDeadzone = []() { return inputPadLookDeadzone(); };
    a.getLookInverted = []() { return inputPadLookInverted(); };
    a.setLook = [](float spd, float dz, int inv) { inputPadLookSet(spd, dz, inv); };
    a.getPadVibration = []() { return inputRumbleGetStrength(0); };
    a.setPadVibration = [](float v) { inputRumbleSetStrengthAll(v); inputRumbleTest(0); };
    a.getTriggerFeedback = []() { return VrTriggerFeedback; };
    a.setTriggerFeedback = [](int pct) { VrTriggerFeedback = pct; };
    a.save = []() { inputSaveConfigNow(); vrSettingsSave(); };
    return a;
}
#endif
#endif

#endif
