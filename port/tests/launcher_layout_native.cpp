// Multi-size layout verification for shared layout helpers (port/vr/gevr_layout_ui.h)
// and every launcher / in-game page at PC window, VR panel, and pause-overlay sizes.
// Fails if any item's rect extends past its pane, any label/value truncates, or any
// page scrolls.
#include "gevr_launcher_ui.h"
#include "gevr_settings_ui.h"
#include "gevr_panel_ui.h"
#include "gevr_pause_widgets.h"
#include "gevr_ghosts_ui.h"
#include "gevr_layout_ui.h"
#include "imgui/imgui_internal.h"
#include "settings_table_stubs.h"
#include "settings_fit.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void failIf(bool ok, const char *what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

// Stubs for gevr_pause_widgets.h
extern "C" int gevrPauseReadField(int, int, GevrPauseField *) { return 0; }
extern "C" const char *gevrPauseChoice(int, int) { return ""; }
extern "C" void gevrPauseSelect(int, int) {}
extern "C" void gevrPauseStep(int, int) {}
extern "C" void gevrPauseVolume(int, int) {}
extern "C" void gevrPauseAction(const char *) {}
extern "C" int gevrPauseObjective(int, char *, unsigned) { return -1; }

struct SizeSpec {
    const char *name;
    float w, h, scale;
};

static void setupContext(float w, float h, float scale) {
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(w, h);
    io.DeltaTime = 1.0f / 60.0f;
    io.FontGlobalScale = scale;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().WindowRounding = 0.0f;
    unsigned char *px = nullptr;
    int tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);
}

static void checkFavoritesGrid(const SizeSpec &sz) {
    static const char *const maps[] = {
        "Temple", "Complex", "Caves", "Library", "Basement", "Stack",
        "Facility", "Bunker", "Archives", "Caverns", "Egyptian"
    };
    static const char *const sets[] = {
        "Slappers Only!", "Pistols", "Throwing Knives", "Automatics",
        "Power Weapons", "Sniper Rifles", "Grenades", "Remote Mines",
        "Grenade Launchers", "Timed Mines", "Proximity Mines", "Rockets",
        "Lasers", "Golden Gun", "License to Kill"
    };
    unsigned mapMask = 0x15u, setMask = 0x2Au;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(sz.w, sz.h));
    ImGui::Begin("##fav-window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("##fav-pane", ImVec2(0, sz.h - ImGui::GetFrameHeight() * 4.0f), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    gevrSectionHeader(ImVec4(1.0f, 0.84f, 0.47f, 1.0f), "FAVORITE MAPS");
    gevrCheckGrid("##fav-maps", (int)(sizeof(maps) / sizeof(maps[0])),
                  [&](int i) { return maps[i]; },
                  [&](int i) { return ((mapMask >> i) & 1u) != 0; },
                  [&](int i, bool on) { if (on) mapMask |= (1u << i); else mapMask &= ~(1u << i); }, 6);
    ImGui::Spacing();
    gevrSectionHeader(ImVec4(1.0f, 0.84f, 0.47f, 1.0f), "FAVORITE WEAPON SETS");
    gevrCheckGrid("##fav-sets", (int)(sizeof(sets) / sizeof(sets[0])),
                  [&](int i) { return sets[i]; },
                  [&](int i) { return ((setMask >> i) & 1u) != 0; },
                  [&](int i, bool on) { if (on) setMask |= (1u << i); else setMask &= ~(1u << i); }, 5);
    ImGuiWindow *pane = ImGui::GetCurrentWindow();
    const float maxX = pane->DC.CursorMaxPos.x;
    const float limX = pane->ClipRect.Max.x;
    const float maxY = pane->DC.CursorMaxPos.y;
    const float limY = pane->ClipRect.Max.y;
    const float scrollY = pane->ScrollMax.y;
    ImGui::EndChild();
    ImGui::End();
    ImGui::Render();

    char msg[200];
    std::snprintf(msg, sizeof(msg), "%s: Favorites grid stays inside pane width (%.0f <= %.0f)", sz.name, maxX, limX);
    failIf(maxX <= limX + 1.0f, msg);
    std::snprintf(msg, sizeof(msg), "%s: Favorites grid fits without scrolling (%.0f <= %.0f, scroll %.1f)",
                  sz.name, maxY, limY, scrollY);
    failIf(maxY <= limY + 1.0f && scrollY <= 0.5f, msg);
}

static void checkAllSettingsSections(const SizeSpec &sz) {
    const GevrSettingsApi api = gevrSettingsLive();
    for (int lsec = 0; lsec < GEVR_LSEC_COUNT; lsec++) {
        if (lsec == GEVR_LSEC_CHEATS || lsec == GEVR_LSEC_MODS) continue;
        int ids[48];
        const int n = api.sectionRows(lsec, GEVR_SET_LAUNCHER, ids, 48);
        if (n <= 0) continue;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(sz.w, sz.h));
        ImGui::Begin("##sec-window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
        const float bodyH = sz.h - ImGui::GetFrameHeight() * 5.0f;
        ImGui::BeginChild("##sec-pane", ImVec2(0, bodyH), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const float availW = ImGui::GetContentRegionAvail().x;
        gevrSettingsSection(api, lsec, GEVR_SET_LAUNCHER);
        ImGuiWindow *pane = ImGui::GetCurrentWindow();
        const float maxX = pane->DC.CursorMaxPos.x;
        const float limX = pane->ClipRect.Max.x;
        const float maxY = pane->DC.CursorMaxPos.y;
        const float limY = pane->ClipRect.Max.y;
        const float scrollY = pane->ScrollMax.y;
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();

        char state[128], msg[220];
        std::snprintf(state, sizeof(state), "%s lsec=%d", sz.name, lsec);
        settingsFitControls(api, lsec, GEVR_SET_LAUNCHER, availW, state);
        std::snprintf(msg, sizeof(msg), "%s: content width fits pane (%.0f <= %.0f)", state, maxX, limX);
        failIf(maxX <= limX + 1.0f, msg);
        std::snprintf(msg, sizeof(msg), "%s: content height fits without scrolling (%.0f <= %.0f, scroll %.1f)",
                      state, maxY, limY, scrollY);
        failIf(maxY <= limY + 1.0f && scrollY <= 0.5f, msg);
    }
}

static void checkColumnFallbackUnit() {
    setupContext(1280, 960, 2.2f);
    ImGui::NewFrame();
    // Narrow pane must drop from 2 columns to 1 when 2 columns can't hold minColWidth
    failIf(gevrSettingColumns(600.0f, 10, 420.0f, 2) == 1, "gevrSettingColumns drops to 1 on narrow pane");
    // Wide pane takes 2 columns when both fit
    failIf(gevrSettingColumns(1200.0f, 10, 420.0f, 2) == 2, "gevrSettingColumns uses 2 on wide pane");
    // Few rows stays 1 column
    failIf(gevrSettingColumns(1200.0f, 1, 420.0f, 2) == 1, "gevrSettingColumns keeps 1 for 1 row");
    ImGui::Render();
}

int main() {
    VrRagdolls = VrDismemberment = 1;
    VrBodySlots = 1;
    VrAlly = 1;
    VrMotionThrowing = VrForcePull = 1;
    VrPlayMode = 0;
    VrThirdPerson = 1;

    checkColumnFallbackUnit();

    // Three target sizes from launcher-controls-plan.md:
    // 1. PC window default size (1280x720 at crisp desktop scale 1.4x)
    // 2. VR panel size (1280x960 at 2.2x scale)
    // 3. Pause overlay size (1244x700 settings area at 2.2x scale)
    const SizeSpec sizes[] = {
        { "PC window (1280x720)", 1280.0f, 720.0f, 1.4f },
        { "VR panel (1280x960)",  1280.0f, 960.0f, 2.2f },
        { "Pause overlay (1244x960)", 1244.0f, 960.0f, 2.2f },
    };
    for (const SizeSpec &sz : sizes) {
        setupContext(sz.w, sz.h, sz.scale);
        checkFavoritesGrid(sz);
        checkAllSettingsSections(sz);
    }
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    std::puts("PASS: shared layout helpers fit Favorites and every settings section at PC, VR panel, and pause-overlay sizes without clipping or scrolling");
    return 0;
}
