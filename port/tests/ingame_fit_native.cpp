// The in-game screens' settings at their fullest, against the real settings
// table (port/src/gevr_settings.c, stubbed by settings_table_stubs.h): the
// solo hold-Menu panel (gevr_panel_ui.h) and the online match screen's
// Settings tab (gevr_pause_widgets.h), flat with third person and stereo,
// solo, as a co-op host and a deathmatch joiner. Every section fits its box
// with no scrolling, every label its column, every control with its value or
// reason beside it, and the footer stays on the canvas.
#include "gevr_panel_ui.h"
#include "gevr_pause_widgets.h"
#include "imgui/imgui_internal.h"
#include "settings_table_stubs.h"
#include "settings_fit.h"

// the online screen's match tabs, not drawn here (test_pause_ui)
extern "C" int gevrPauseReadField(int, int, GevrPauseField *) { return 0; }
extern "C" const char *gevrPauseChoice(int, int) { return ""; }
extern "C" void gevrPauseSelect(int, int) {}
extern "C" void gevrPauseStep(int, int) {}
extern "C" void gevrPauseVolume(int, int) {}
extern "C" void gevrPauseAction(const char *) {}
extern "C" int gevrPauseObjective(int, char *, unsigned) { return -1; }

static ImGuiWindow *activeRows() {
    ImGuiWindow *rows = nullptr;
    for (ImGuiWindow *x : ImGui::GetCurrentContext()->Windows)
        if (strstr(x->Name, "##panel-rows") && x->Active && x->LastFrameActive == ImGui::GetFrameCount()) rows = x;
    return rows;
}

static int screens;
static void sweep(bool online, const char *state) {
    const GevrSettingsApi api = gevrSettingsLive();
    GevrPanelView panel; GevrPanelUi panelUi;
    GevrPauseView pause; GevrPauseUi pauseUi;
    panel.settings = pause.settings = api;
    panel.texpack = pause.texpack = 1;
    snprintf(panel.session, sizeof(panel.session), "Surface II");
    snprintf(panel.note, sizeof(panel.note), "Going back to the launcher does not save this mission.");
    pauseUi.tab = GEVR_PAUSE_SETTINGS;
    for (int s = 0; s < api.sectionCount(); s++) {
        float footer = 0;
        for (int pass = 0; pass < 3; pass++) {
            if (pass == 0) { panelUi.openSection = s; pauseUi.openSection = s; }
            ImGui::NewFrame();
            if (online) gevrDrawPauseWindow(pause, pauseUi);
            else gevrDrawPanelWindow(panel, panelUi);
            footer = ImGui::GetCurrentContext()->Windows.back()->DC.CursorMaxPos.y;
            ImGui::Render();
        }
        ImGuiWindow *rows = activeRows();
        char what[200];
        std::snprintf(what, sizeof(what), "%s: %s is drawn", state, api.sectionName(s));
        settingsFitFail(rows != nullptr, what);
        std::snprintf(what, sizeof(what), "%s: %s fits without scrolling (content %.0f, box %.0f)", state, api.sectionName(s),
                      rows->ContentSize.y, rows->Size.y);
        settingsFitFail(rows->ContentSize.y <= rows->Size.y && rows->ScrollMax.y <= 0.5f, what);
        std::snprintf(what, sizeof(what), "%s: %s fits the width", state, api.sectionName(s));
        settingsFitFail(rows->ContentSize.x + rows->WindowPadding.x * 2 <= rows->Size.x + 1, what);
        settingsFitControls(api, s, GEVR_SET_INGAME, rows->Size.x - rows->WindowPadding.x * 2, state);
        for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
            if (w->WasActive && !(w->Flags & ImGuiWindowFlags_Tooltip) && w->ScrollMax.y > 0.5f) {
                std::snprintf(what, sizeof(what), "%s: %s: %s scrolls by %.1f", state, api.sectionName(s), w->Name, w->ScrollMax.y);
                settingsFitFail(false, what);
            }
        (void)footer;
        screens++;
    }
    // the tab rows fit without shrinking
    ImGuiContext &g = *ImGui::GetCurrentContext();
    for (int t = 0; t < g.TabBars.GetMapSize(); t++)
        if (ImGuiTabBar *bar = g.TabBars.TryGetMapData(t))
            settingsFitFail(bar->WidthAllTabsIdeal <= bar->BarRect.GetWidth() + 1, "the sections' tab row fits without shrinking");
}

int main() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = ImVec2(1280, 960); io.DeltaTime = 1.f / 72; io.FontGlobalScale = 2.2f;
    ImGui::StyleColorsDark(); ImGui::GetStyle().ScaleAllSizes(2.2f); ImGui::GetStyle().WindowRounding = 0;
    unsigned char *px; int w, h; io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    // every row there is: ragdolls with dismemberment, body slots, motion throw, force pull, GE-X
    VrRagdolls = VrDismemberment = 1; VrBodySlots = 1; VrMotionThrowing = VrForcePull = 1; VrGexGuns = 1;
    VrRefreshRate = 120; VrSpaceWarp = -1;
    VrPlayMode = 0; VrThirdPerson = 1;
    sweep(false, "solo, flat, third person");
    VrPlayMode = 1;
    sweep(false, "solo, stereo");
    VrPlayMode = 0; s_net = 1; s_host = 1; s_coop = 1; s_netFast = 1;
    sweep(true, "online, co-op host");
    s_host = 0; s_coop = 0; s_netSpeed = 4;
    sweep(true, "online, deathmatch joiner");
    settingsFitFail(screens == 4 * 7, "every section of both in-game screens, in four states");
    ImGui::DestroyContext();
    std::printf("PASS: in-game settings: the solo panel and the online Settings tab, %d sections (flat, stereo, co-op host, "
                "joiner), rows, labels, controls and reasons fit without scrolling at Quest font size\n", screens);
}
