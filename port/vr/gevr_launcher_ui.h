#ifndef GEVR_LAUNCHER_UI_H
#define GEVR_LAUNCHER_UI_H
#include "imgui/imgui.h"
#include "gevr_tooltip.h"
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
/*
 * The launcher's tabs (docs/launcher-unify.md): a top row, and under it a
 * second row where a tab has sub-tabs. No Done buttons and no pages behind
 * buttons: every page is a tab or a sub-tab, and none scrolls.
 */
enum { GEVR_LTAB_PLAY, GEVR_LTAB_MULTIPLAYER, GEVR_LTAB_CONTROLS, GEVR_LTAB_COMFORT, GEVR_LTAB_SCREEN,
       GEVR_LTAB_RULES, GEVR_LTAB_MODS, GEVR_LTAB_COUNT };
enum { GEVR_LSUB_HOST, GEVR_LSUB_JOIN, GEVR_LSUB_PLAYER, GEVR_LSUB_AUDIO, GEVR_LSUB_FAVORITES };  // Multiplayer's
enum { GEVR_LSUB_HANDS, GEVR_LSUB_GESTURES, GEVR_LSUB_THROWING, GEVR_LSUB_HAPTICS };   // Controls'
enum { GEVR_LSUB_GAME_RULES, GEVR_LSUB_ALLY, GEVR_LSUB_CHEATS };                       // Rules'
struct GevrLauncherTabs {
    int top = GEVR_LTAB_PLAY;          // the tab drawn last frame
    int sub[GEVR_LTAB_COUNT] = {};     // each tab's sub-tab drawn last
    int openTop = -1, openSub = -1;    // brought up over the next frames
    int openFrames = 0;
    // ImGui selects a tab a frame after asking, and only then draws its
    // sub-tab row: the ask stands until both are up, a few frames at most
    void open(int tab, int subTab = -1) { openTop = tab; openSub = subTab; openFrames = 4; }
};
inline const char *gevrLauncherTabName(int tab) {
    static const char *const names[GEVR_LTAB_COUNT] = { "Play", "Multiplayer", "Controls", "Comfort", "Screen", "Rules", "Mods" };
    return tab >= 0 && tab < GEVR_LTAB_COUNT ? names[tab] : "";
}
inline int gevrLauncherSubCount(int tab) {
#ifdef GEVR_FLAT
    if (tab == GEVR_LTAB_CONTROLS) return 0;   // a flat game: the sub-tabs are tracked controllers'
#endif
    return tab == GEVR_LTAB_MULTIPLAYER ? 5 : tab == GEVR_LTAB_CONTROLS ? 4 : tab == GEVR_LTAB_RULES ? 3 : 0;
}
inline const char *gevrLauncherSubName(int tab, int sub) {
    static const char *const controls[] = { "Hands", "Gestures", "Throwing", "Haptics" };
    static const char *const rules[] = { "Game rules", "AI ally", "Cheats" };
    static const char *const multiplayer[] = { "Host", "Join", "Player", "Audio", "Favorites" };
    if (sub < 0 || sub >= gevrLauncherSubCount(tab)) return "";
    return tab == GEVR_LTAB_MULTIPLAYER ? multiplayer[sub] : tab == GEVR_LTAB_CONTROLS ? controls[sub] : rules[sub];
}
// The two tab rows; page(tab, sub) draws the open one (sub -1 where it has none).
template <class Page>
inline void gevrLauncherTabRows(GevrLauncherTabs &tabs, Page page) {
    if (ImGui::BeginTabBar("launcher-tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        for (int t = 0; t < GEVR_LTAB_COUNT; t++) {
            const ImGuiTabItemFlags open = t == tabs.openTop ? ImGuiTabItemFlags_SetSelected : 0;
            if (!ImGui::BeginTabItem(gevrLauncherTabName(t), nullptr, open)) continue;
            tabs.top = t;
            const int subs = gevrLauncherSubCount(t);
            if (subs == 0) {
                page(t, -1);
            } else {
                ImGui::PushID(t);   // each tab's sub-tabs keep their own choice
                if (ImGui::BeginTabBar("launcher-subtabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
                    for (int s = 0; s < subs; s++) {
                        const ImGuiTabItemFlags pick = t == tabs.openTop && s == tabs.openSub ? ImGuiTabItemFlags_SetSelected : 0;
                        if (!ImGui::BeginTabItem(gevrLauncherSubName(t, s), nullptr, pick)) continue;
                        tabs.sub[t] = s;
                        page(t, s);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    const bool done = tabs.top == tabs.openTop && (tabs.openSub < 0 || tabs.sub[tabs.top] == tabs.openSub);
    if (done || --tabs.openFrames <= 0) tabs.openTop = tabs.openSub = -1;
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
        gevrTooltip("%s", message);
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
// A tab's page: the height above a footer of footerH, and no scrolling (the fit
// tests draw every page at its fullest).
inline void gevrLauncherBeginPage(const char *id, float footerH) {
    ImGui::BeginChild(id, ImVec2(0, std::fmax(1.0f, ImGui::GetContentRegionAvail().y - footerH)), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}
#endif
