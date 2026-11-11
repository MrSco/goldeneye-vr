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

// mpmenu.c's fields at their fullest, with the longest names they can show
// (net_match.c): a custom weapon set, fixed bots, teams, loadouts
struct FakeField { const char *label; int kind, count; const char *value; };
static const FakeField s_rules[] = {
    {"HOST EQUALIZATION", GEVR_PAUSE_TOGGLE, 0, "ON"}, {"HOST DELAY CAP", GEVR_PAUSE_CHOICE, 5, "80 ms"},
    {"FRIENDLY FIRE", GEVR_PAUSE_TOGGLE, 0, "ON"}, {"BOTS", GEVR_PAUSE_CHOICE, 3, "Fill empty slots"},
    {"BOT COUNT", GEVR_PAUSE_CHOICE, 7, "7"}, {"BOT DIFFICULTY", GEVR_PAUSE_CHOICE, 5, "Perfect"},
    {"CUSTOM 1", GEVR_PAUSE_CHOICE, 25, "AR33 Assault Rifle"}, {"CUSTOM 2", GEVR_PAUSE_CHOICE, 25, "AR33 Assault Rifle"},
    {"CUSTOM 3", GEVR_PAUSE_CHOICE, 25, "AR33 Assault Rifle"}, {"CUSTOM 4", GEVR_PAUSE_CHOICE, 25, "AR33 Assault Rifle"},
    {"LENGTH", GEVR_PAUSE_CHOICE, 8, "Last one standing"}, {"HEALTH", GEVR_PAUSE_CHOICE, 11, "-4 (Veteran)"},
    {"DUAL WIELD", GEVR_PAUSE_CHOICE, 3, "Any two guns"}, {"LOADOUTS", GEVR_PAUSE_TOGGLE, 0, "ON"},
    /* mpmenu.c's own fun switches: the table's are drawn instead */
    {"DK MODE", GEVR_PAUSE_TOGGLE, 0, "ON"}, {"GUN SIZE", GEVR_PAUSE_CHOICE, 3, "Normal"},
    {"MOVEMENT SPEED", GEVR_PAUSE_CHOICE, 7, "100% (default)"},
};
static const FakeField s_player[] = {
    {"NEXT ROUND READY", GEVR_PAUSE_TOGGLE, 0, "READY"}, {"YOUR TEAM", GEVR_PAUSE_CHOICE, 3, "Unassigned"},
    {"CHARACTER", GEVR_PAUSE_CHOICE, 64, "Siberian Special Forces"}, {"LOADOUT 1", GEVR_PAUSE_CHOICE, 25, "AR33 Assault Rifle"},
    {"LOADOUT 2", GEVR_PAUSE_CHOICE, 25, "AR33 Assault Rifle"}, {"LOADOUT 3", GEVR_PAUSE_CHOICE, 25, "AR33 Assault Rifle"},
};
static int s_fieldsEditable = 1;
extern "C" int gevrPauseReadField(int tab, int index, GevrPauseField *f) {
    const FakeField *rows = tab == GEVR_PAUSE_RULES ? s_rules : tab == GEVR_PAUSE_PLAYER ? s_player : nullptr;
    const int n = tab == GEVR_PAUSE_RULES ? (int)(sizeof(s_rules) / sizeof(s_rules[0]))
                : tab == GEVR_PAUSE_PLAYER ? (int)(sizeof(s_player) / sizeof(s_player[0])) : 0;
    if (!rows || index >= n) return 0;
    *f = GevrPauseField{};
    /* mpmenu.c's ids: lobby rows from 0, pause rows from 100, fun rows from 200 */
    f->id = tab * 30 + index + (!strcmp(rows[index].label, "DK MODE") || !strcmp(rows[index].label, "GUN SIZE")
                                    || !strcmp(rows[index].label, "MOVEMENT SPEED") ? 200 : 0);
    f->kind = rows[index].kind; f->count = rows[index].count; f->editable = s_fieldsEditable;
    snprintf(f->label, sizeof(f->label), "%s", rows[index].label);
    snprintf(f->value, sizeof(f->value), "%s", rows[index].value);
    return 1;
}
static const char *fieldValue(int id) {
    for (int tab : {GEVR_PAUSE_RULES, GEVR_PAUSE_PLAYER}) {
        GevrPauseField f;
        for (int i = 0; gevrPauseReadField(tab, i, &f); i++) if (f.id == id) {
            static char value[128]; snprintf(value, sizeof(value), "%s", f.value); return value;
        }
    }
    return "";
}
extern "C" const char *gevrPauseChoice(int id, int) { return fieldValue(id); }
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
    settingsFitFail(screens == 4 * GEVR_SEC_COUNT, "every section of both in-game screens, in four states");
    // the online tabs: Rules (mpmenu.c's rules and the table's fun switches), Player, Audio,
    // as the host (all theirs) and as a joiner (the host's greyed with their reason)
    int online = 0;
    for (int host = 1; host >= 0; host--) {
        s_net = 1; s_host = host; s_fieldsEditable = host;
        const char *who = host ? "online host" : "online joiner";
        for (int tab : {GEVR_PAUSE_RULES, GEVR_PAUSE_PLAYER, GEVR_PAUSE_AUDIO}) {
            GevrPauseView pause; GevrPauseUi pauseUi;
            pause.settings = gevrSettingsLive();
            pauseUi.tab = tab;
            for (int pass = 0; pass < 3; pass++) { ImGui::NewFrame(); gevrDrawPauseWindow(pause, pauseUi); ImGui::Render(); }
            ImGuiWindow *box = nullptr;
            for (ImGuiWindow *x : ImGui::GetCurrentContext()->Windows)
                if (strstr(x->Name, "##pause-rows") && x->Active && x->LastFrameActive == ImGui::GetFrameCount()) box = x;
            char what[200], state[64];
            std::snprintf(state, sizeof(state), "%s, %s", who, gevrPauseRowsName(tab));
            std::snprintf(what, sizeof(what), "%s: the rows box is drawn", state);
            settingsFitFail(box != nullptr, what);
            std::snprintf(what, sizeof(what), "%s fits without scrolling (content %.0f, box %.0f)", state, box->ContentSize.y, box->Size.y);
            settingsFitFail(box->ContentSize.y <= box->Size.y && box->ScrollMax.y <= 0.5f, what);
            std::snprintf(what, sizeof(what), "%s fits the width", state);
            settingsFitFail(box->ContentSize.x + box->WindowPadding.x * 2 <= box->Size.x + 1, what);
            const GevrSettingsApi rows = gevrPauseRowsApi(gevrSettingsLive());
            settingsFitControls(rows, tab, GEVR_SET_INGAME, box->Size.x - box->WindowPadding.x * 2, state);
            int ids[48];
            const int n = rows.sectionRows(tab, GEVR_SET_INGAME, ids, 48);
            std::snprintf(what, sizeof(what), "%s: its rows (%d)", state, n);
            /* Rules: mpmenu.c's 17 less its two fun copies, and the table's six fun rows; Audio: the table's five */
            settingsFitFail(n == (tab == GEVR_PAUSE_RULES ? 15 + 6 : tab == GEVR_PAUSE_PLAYER ? 6 : 5), what);
            online++;
        }
    }
    settingsFitFail(online == 6, "the online Rules, Player and Audio tabs, as host and joiner");
    ImGui::DestroyContext();
    std::printf("PASS: in-game settings: the solo panel and the online Settings tab, %d sections (flat, stereo, co-op host, "
                "joiner), and the online Rules, Player and Audio tabs at their fullest (host, joiner): rows, labels, "
                "controls and reasons fit without scrolling at Quest font size\n", screens);
}
