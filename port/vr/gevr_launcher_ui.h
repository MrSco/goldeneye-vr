#ifndef GEVR_LAUNCHER_UI_H
#define GEVR_LAUNCHER_UI_H
#include "imgui/imgui.h"
#include "gevr_tooltip.h"
#include <cmath>
#include <string>
#include <cstdio>
#include "gevr_layout_ui.h"
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
 * The launcher's tabs (docs/launcher-unify.md, Phase 3 of launcher-controls-plan.md):
 * a top row, and under it a second row where a tab has sub-tabs in the active context.
 * All tab and sub-tab visibility comes from the single table below (GEVR_CTX_*).
 * No Done buttons and no pages behind buttons: every page is a tab or sub-tab, and none scrolls.
 */
enum {
    GEVR_LTAB_PLAY,
    GEVR_LTAB_ONLINE,
    GEVR_LTAB_CONTROLS,
    GEVR_LTAB_COMFORT_DISPLAY,
    GEVR_LTAB_GAME,
    GEVR_LTAB_MODS,
    GEVR_LTAB_COUNT
};
// Play's sub-tabs
enum { GEVR_LSUB_PLAY_MAIN, GEVR_LSUB_PLAY_GHOSTS };
// Online's sub-tabs
enum { GEVR_LSUB_HOST, GEVR_LSUB_JOIN, GEVR_LSUB_PLAYER, GEVR_LSUB_AUDIO, GEVR_LSUB_FAVORITES };
// Controls' sub-tabs (visibility table picks flat vs VR entries)
enum {
    GEVR_LSUB_KBM,
    GEVR_LSUB_GAMEPAD,
    GEVR_LSUB_HANDS,
    GEVR_LSUB_GESTURES,
    GEVR_LSUB_THROWING,
    GEVR_LSUB_HAPTICS,
    GEVR_LSUB_LOOK
};
// Game's sub-tabs
enum { GEVR_LSUB_GAME_RULES, GEVR_LSUB_ALLY, GEVR_LSUB_CHEATS };
// Comfort & display's sub-tabs: the graphics rows on their own page (the one page ran out of room)
enum { GEVR_LSUB_COMFORT, GEVR_LSUB_GRAPHICS };

struct GevrTabSpec {
    int id;
    const char *name;
    unsigned contexts;
};
struct GevrSubSpec {
    int tab;
    int id;
    const char *name;
    unsigned contexts;
};

inline unsigned gevrLauncherBuildContext() {
#if defined(GEVR_TVOS)
    return GEVR_CTX_APPLE_TV;
#elif defined(GEVR_IOS)
    return GEVR_CTX_APPLE;
#elif defined(GEVR_MOBILE)
    return GEVR_CTX_MOBILE;
#elif defined(GEVR_PC_FLAT) || defined(GEVR_FLAT)
    return GEVR_CTX_PC_FLAT;
#elif defined(GEVR_PCVR)
    return GEVR_CTX_PC_VR;
#else
    return GEVR_CTX_QUEST;
#endif
}

inline unsigned &gevrLauncherContextOverrideRef() {
    static unsigned overrideCtx = 0;
    return overrideCtx;
}
inline unsigned gevrLauncherContext() {
    const unsigned o = gevrLauncherContextOverrideRef();
    return o ? o : gevrLauncherBuildContext();
}
inline void gevrLauncherSetContextOverride(unsigned ctx) {
    gevrLauncherContextOverrideRef() = ctx;
}

// One declarative visibility table for all launcher tabs and sub-tabs.
inline const GevrTabSpec *gevrLauncherTabTable(int &count) {
    static const GevrTabSpec kTabs[] = {
        { GEVR_LTAB_PLAY,            "Play",              GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_ONLINE,          "Online",            GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_CONTROLS,        "Controls",          GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_COMFORT_DISPLAY, "Comfort & display", GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_GAME,            "Game",              GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_MODS,            "Mods",              GEVR_CTX_ALL_LAUNCHER },
    };
    count = (int)(sizeof(kTabs) / sizeof(kTabs[0]));
    return kTabs;
}

inline const GevrSubSpec *gevrLauncherSubTable(int &count) {
    static const GevrSubSpec kSubs[] = {
        // Play
        { GEVR_LTAB_PLAY,     GEVR_LSUB_PLAY_MAIN,   "General",          GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_PLAY,     GEVR_LSUB_PLAY_GHOSTS, "Ghosts",           GEVR_CTX_ALL_LAUNCHER },
        // Online
        { GEVR_LTAB_ONLINE,   GEVR_LSUB_HOST,        "Host",             GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_ONLINE,   GEVR_LSUB_JOIN,        "Join",             GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_ONLINE,   GEVR_LSUB_PLAYER,      "Player",           GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_ONLINE,   GEVR_LSUB_AUDIO,       "Audio",            GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_ONLINE,   GEVR_LSUB_FAVORITES,   "Favorites",        GEVR_CTX_ALL_LAUNCHER },
        // Controls (flat vs VR driven solely by contexts)
        { GEVR_LTAB_CONTROLS, GEVR_LSUB_KBM,         "Keyboard & mouse", GEVR_CTX_PC_FLAT | GEVR_CTX_MOBILE | GEVR_CTX_APPLE },
        { GEVR_LTAB_CONTROLS, GEVR_LSUB_GAMEPAD,     "Gamepad",          GEVR_CTX_FLAT },
        { GEVR_LTAB_CONTROLS, GEVR_LSUB_HANDS,       "Hands",            GEVR_CTX_VR },
        { GEVR_LTAB_CONTROLS, GEVR_LSUB_GESTURES,    "Gestures",         GEVR_CTX_VR },
        { GEVR_LTAB_CONTROLS, GEVR_LSUB_THROWING,    "Throwing",         GEVR_CTX_VR },
        { GEVR_LTAB_CONTROLS, GEVR_LSUB_HAPTICS,     "Haptics",          GEVR_CTX_VR },
        { GEVR_LTAB_CONTROLS, GEVR_LSUB_LOOK,        "Look",             GEVR_CTX_ALL_LAUNCHER },
        // Comfort & display
        { GEVR_LTAB_COMFORT_DISPLAY, GEVR_LSUB_COMFORT,  "General",      GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_COMFORT_DISPLAY, GEVR_LSUB_GRAPHICS, "Graphics",     GEVR_CTX_ALL_LAUNCHER },
        // Game
        { GEVR_LTAB_GAME,     GEVR_LSUB_GAME_RULES,  "Game rules",       GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_GAME,     GEVR_LSUB_ALLY,        "AI ally",          GEVR_CTX_ALL_LAUNCHER },
        { GEVR_LTAB_GAME,     GEVR_LSUB_CHEATS,      "Cheats",           GEVR_CTX_ALL_LAUNCHER },
    };
    count = (int)(sizeof(kSubs) / sizeof(kSubs[0]));
    return kSubs;
}

struct GevrLauncherTabs {
    int top = GEVR_LTAB_PLAY;          // the tab drawn last frame
    int sub[GEVR_LTAB_COUNT] = {};     // each tab's sub-tab ID drawn last
    int openTop = -1, openSub = -1;    // brought up over the next frames
    int openFrames = 0;
    // ImGui selects a tab a frame after asking, and only then draws its
    // sub-tab row: the ask stands until both are up, a few frames at most
    void open(int tab, int subTab = -1) { openTop = tab; openSub = subTab; openFrames = 4; }
};

inline bool gevrLauncherTabVisible(int tab, unsigned ctx = 0) {
    if (!ctx) ctx = gevrLauncherContext();
    int n = 0;
    const GevrTabSpec *tabs = gevrLauncherTabTable(n);
    for (int i = 0; i < n; i++)
        if (tabs[i].id == tab) return (tabs[i].contexts & ctx) != 0;
    return false;
}

inline const char *gevrLauncherTabName(int tab) {
    int n = 0;
    const GevrTabSpec *tabs = gevrLauncherTabTable(n);
    for (int i = 0; i < n; i++)
        if (tabs[i].id == tab) return tabs[i].name;
    return "";
}

inline int gevrLauncherSubCount(int tab, unsigned ctx = 0) {
    if (!ctx) ctx = gevrLauncherContext();
    int n = 0, out = 0;
    const GevrSubSpec *subs = gevrLauncherSubTable(n);
    for (int i = 0; i < n; i++)
        if (subs[i].tab == tab && (subs[i].contexts & ctx) != 0) out++;
    return out;
}

inline int gevrLauncherSubId(int tab, int visibleIndex, unsigned ctx = 0) {
    if (!ctx) ctx = gevrLauncherContext();
    int n = 0, seen = 0;
    const GevrSubSpec *subs = gevrLauncherSubTable(n);
    for (int i = 0; i < n; i++) {
        if (subs[i].tab == tab && (subs[i].contexts & ctx) != 0) {
            if (seen == visibleIndex) return subs[i].id;
            seen++;
        }
    }
    return -1;
}

inline const char *gevrLauncherSubNameById(int tab, int subId) {
    int n = 0;
    const GevrSubSpec *subs = gevrLauncherSubTable(n);
    for (int i = 0; i < n; i++)
        if (subs[i].tab == tab && subs[i].id == subId) return subs[i].name;
    return "";
}

inline const char *gevrLauncherSubName(int tab, int visibleIndex, unsigned ctx = 0) {
    const int id = gevrLauncherSubId(tab, visibleIndex, ctx);
    return id >= 0 ? gevrLauncherSubNameById(tab, id) : "";
}

// The two tab rows; page(tabId, subId) draws the open one (subId -1 where it has none).
template <class Page>
inline void gevrLauncherTabRows(GevrLauncherTabs &tabs, Page page, unsigned ctx = 0) {
    if (!ctx) ctx = gevrLauncherContext();
    int tn = 0;
    const GevrTabSpec *tabSpecs = gevrLauncherTabTable(tn);
    if (ImGui::BeginTabBar("launcher-tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        for (int ti = 0; ti < tn; ti++) {
            const GevrTabSpec &ts = tabSpecs[ti];
            if ((ts.contexts & ctx) == 0) continue;
            const int t = ts.id;
            const ImGuiTabItemFlags open = t == tabs.openTop ? ImGuiTabItemFlags_SetSelected : 0;
            if (!ImGui::BeginTabItem(ts.name, nullptr, open)) continue;
            tabs.top = t;
            const int subs = gevrLauncherSubCount(t, ctx);
            if (subs == 0) {
                tabs.sub[t] = -1;
                page(t, -1);
            } else {
                ImGui::PushID(t);   // each tab's sub-tabs keep their own choice
                if (ImGui::BeginTabBar("launcher-subtabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
                    int sn = 0;
                    const GevrSubSpec *subSpecs = gevrLauncherSubTable(sn);
                    for (int si = 0; si < sn; si++) {
                        const GevrSubSpec &ss = subSpecs[si];
                        if (ss.tab != t || (ss.contexts & ctx) == 0) continue;
                        const ImGuiTabItemFlags pick = (t == tabs.openTop && ss.id == tabs.openSub) ? ImGuiTabItemFlags_SetSelected : 0;
                        if (!ImGui::BeginTabItem(ss.name, nullptr, pick)) continue;
                        tabs.sub[t] = ss.id;
                        page(t, ss.id);
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
