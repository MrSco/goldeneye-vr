// The in-game screens' settings at their fullest, against the real settings
// table (port/src/gevr_settings.c, stubbed by settings_table_stubs.h): the
// solo hold-Menu panel (gevr_panel_ui.h) and the online match screen's
// Settings tab (gevr_pause_widgets.h), flat with third person and stereo,
// solo, as a co-op host and a deathmatch joiner. Every section fits its box
// with no scrolling, every label its column, every control with its value or
// reason beside it, and the footer stays on the canvas.
#include "gevr_panel_ui.h"
#include "gevr_pause_widgets.h"
#include "gevr_modui_ui.h"
#include "imgui/imgui_internal.h"
#include "settings_table_stubs.h"
#include "settings_fit.h"

// Mods' pages (mod.ui, gevr_modui_ui.h) at their fullest: every mod's four,
// each with the longest title, text, labels, choices and reasons
// gevr_modapi.c takes, eight rows and four buttons.
static GevrModUiPage s_modPages[GEVR_MODAPI_UI_PAGES];
static int s_modAsked = -1, s_modClicked = -1, s_modSetPage = -1, s_modSetRow = -1, s_modSetValue = 0;
static void fillText(char *out, int n, char c) { for (int i = 0; i < n; i++) out[i] = c; out[n] = '\0'; }
static void makeModPages() {
    for (int p = 0; p < GEVR_MODAPI_UI_PAGES; p++) {
        GevrModUiPage &g = s_modPages[p];
        g = GevrModUiPage{};
        snprintf(g.mod, sizeof(g.mod), "tactical");
        snprintf(g.id, sizeof(g.id), "page%d", p);
        fillText(g.title, GEVR_MODAPI_UI_TITLE, (char)('A' + p));
        fillText(g.text, GEVR_MODAPI_UI_TEXT, 'T');
        for (int i = 9; i < GEVR_MODAPI_UI_TEXT; i += 10) g.text[i] = ' ';   // words, as text wraps
        g.rows = GEVR_MODAPI_UI_ROWS;
        for (int r = 0; r < g.rows; r++) {
            GevrModUiRow &w = g.row[r];
            snprintf(w.id, sizeof(w.id), "row%d", r);
            fillText(w.label, GEVR_MODAPI_UI_LABEL, 'L');
            fillText(w.tip, GEVR_MODAPI_UI_TEXT, 'P');
            w.kind = r % 3 == 0 ? GEVR_MODUI_TOGGLE : r % 3 == 1 ? GEVR_MODUI_CHOICE : GEVR_MODUI_SLIDER;
            if (w.kind == GEVR_MODUI_TOGGLE) {
                fillText(w.blocked, GEVR_MODAPI_UI_REASON, 'R');   // a toggle's reason sits beside it: its widest
                w.value = 1;
            } else if (w.kind == GEVR_MODUI_CHOICE) {
                w.count = GEVR_MODAPI_UI_CHOICES;
                for (int c = 0; c < w.count; c++) fillText(w.choices[c], GEVR_MODAPI_UI_LABEL, 'C');
                w.value = 3;
            } else {
                w.min = -GEVR_MODAPI_UI_RANGE; w.max = w.min + GEVR_MODAPI_UI_STEPS; w.value = w.min;   // "-100000"
            }
        }
        g.buttons = GEVR_MODAPI_UI_BUTTONS;
        for (int b = 0; b < g.buttons; b++) {
            snprintf(g.button[b].id, sizeof(g.button[b].id), "button%d", b);
            fillText(g.button[b].label, GEVR_MODAPI_UI_BUTTON, 'B');
            if (b == 3) fillText(g.button[b].blocked, GEVR_MODAPI_UI_REASON, 'R');
        }
    }
}
static GevrModUiApi fakeModUi() {
    GevrModUiApi m;
    m.count = []() { return GEVR_MODAPI_UI_PAGES; };
    m.page = [](int i) -> const GevrModUiPage * { return i >= 0 && i < GEVR_MODAPI_UI_PAGES ? &s_modPages[i] : nullptr; };
    m.set = [](int page, int row, int value) { s_modSetPage = page; s_modSetRow = row; s_modSetValue = value; };
    m.click = [](int page, int button) { s_modClicked = page * 10 + button; };
    m.asked = []() { return s_modAsked; };
    m.opened = []() { s_modAsked = -1; };
    return m;
}
// A child drawn this frame by its own name: the last part of its path ("parent/##name_ID"),
// so ##panel-rows isn't taken for the ##modui-rows inside it
static ImGuiWindow *activeWindow(const char *name) {
    ImGuiWindow *found = nullptr;
    for (ImGuiWindow *x : ImGui::GetCurrentContext()->Windows) {
        const char *leaf = strrchr(x->Name, '/');
        leaf = leaf ? leaf + 1 : x->Name;
        if (!strncmp(leaf, name, strlen(name)) && x->Active && x->LastFrameActive == ImGui::GetFrameCount()) found = x;
    }
    return found;
}
// Each page brought up as mod.ui.open brings it up (vr_pause_menu.cpp: the Mods/Fun tab
// while asked, the page's sub-tab by the drawing), and checked to fit.
static int modScreens;
static void sweepMods(bool online, const char *state) {
    GevrPanelView panel; GevrPanelUi panelUi;
    GevrPauseView pause; GevrPauseUi pauseUi;
    panel.settings = pause.settings = gevrSettingsLive();
    panel.settings.extraTabs = pause.settings.extraTabs = gevrModUiTabCount;
    panel.settings.drawExtraTabs = pause.settings.drawExtraTabs = gevrModUiTabs;
    gevrModUi() = fakeModUi();
    for (int p = 0; p < GEVR_MODAPI_UI_PAGES; p++) {
        s_modAsked = p;
        for (int pass = 0; pass < 4; pass++) {
            if (s_modAsked >= 0) {
                panelUi.openSection = GEVR_SEC_MODS;
                pauseUi.tab = GEVR_PAUSE_SETTINGS; pauseUi.openSection = GEVR_SEC_MODS;
            }
            ImGui::NewFrame();
            if (online) gevrDrawPauseWindow(pause, pauseUi);
            else gevrDrawPanelWindow(panel, panelUi);
            ImGui::Render();
        }
        char what[220];
        std::snprintf(what, sizeof(what), "%s: page %d's tab is up and the ask done", state, p);
        settingsFitFail(s_modAsked == -1, what);
        ImGuiWindow *box = activeWindow("##panel-rows"), *rows = activeWindow("##modui-rows");
        std::snprintf(what, sizeof(what), "%s: page %d is drawn", state, p);
        settingsFitFail(box && rows, what);
        std::snprintf(what, sizeof(what), "%s: page %d fits without scrolling (content %.0f, box %.0f; rows %.0f of %.0f)",
                      state, p, box->ContentSize.y, box->Size.y, rows->ContentSize.y, rows->Size.y);
        settingsFitFail(box->ContentSize.y <= box->Size.y && rows->ContentSize.y <= rows->Size.y, what);
        std::snprintf(what, sizeof(what), "%s: page %d and its four buttons fit the width (%.0f of %.0f)", state, p,
                      box->ContentSize.x + box->WindowPadding.x * 2, box->Size.x);
        settingsFitFail(box->ContentSize.x + box->WindowPadding.x * 2 <= box->Size.x + 1, what);
        std::snprintf(what, sizeof(what), "%s, page %d", state, p);
        settingsFitControls(gevrModUiRowsApi(p), 0, GEVR_SET_INGAME, rows->Size.x - rows->WindowPadding.x * 2, what);
        for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
            if (w->WasActive && !(w->Flags & ImGuiWindowFlags_Tooltip) && w->ScrollMax.y > 0.5f) {
                std::snprintf(what, sizeof(what), "%s: page %d: %s scrolls by %.1f", state, p, w->Name, w->ScrollMax.y);
                settingsFitFail(false, what);
            }
        ImGuiContext &g = *ImGui::GetCurrentContext();
        for (int t = 0; t < g.TabBars.GetMapSize(); t++)
            if (ImGuiTabBar *bar = g.TabBars.TryGetMapData(t)) {
                std::snprintf(what, sizeof(what), "%s: the tab rows with Options and four pages fit without shrinking", state);
                settingsFitFail(bar->WidthAllTabsIdeal <= bar->BarRect.GetWidth() + 1, what);
            }
        modScreens++;
    }
    // a button pressed with the pointer: the page's click, a blocked one's none
    ImGuiIO &io = ImGui::GetIO();
    for (int b : {1, 3}) {
        ImGuiWindow *rows = activeWindow("##modui-rows");
        const ImGuiStyle &style = ImGui::GetStyle();
        const float x = rows->Pos.x + b * (GEVR_MODUI_BUTTON_W + style.ItemSpacing.x) + GEVR_MODUI_BUTTON_W / 2;
        const float y = rows->Pos.y + rows->Size.y + style.ItemSpacing.y * 2 + GEVR_MODUI_BUTTON_H / 2;
        s_modClicked = -1;
        for (int step = 0; step < 4; step++) {
            if (step == 0) io.AddMousePosEvent(x, y);
            if (step == 1) io.AddMouseButtonEvent(0, true);
            if (step == 2) io.AddMouseButtonEvent(0, false);
            ImGui::NewFrame();
            if (online) gevrDrawPauseWindow(pause, pauseUi);
            else gevrDrawPanelWindow(panel, panelUi);
            ImGui::Render();
        }
        char what[160];
        std::snprintf(what, sizeof(what), "%s: button %d of the last page %s", state, b, b == 3 ? "is blocked" : "clicks");
        settingsFitFail(s_modClicked == (b == 3 ? -1 : (GEVR_MODAPI_UI_PAGES - 1) * 10 + b), what);
    }
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

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
    s_hmdClass = GEVR_HMD_QUEST3;   // the Quest 3's Auto values, the widest ("Auto (16x)")
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
    // Flat PC: Controls has Keyboard & mouse / Gamepad / Look sub-tabs so top tab row stays compact
    // without shrinking, and Back to launcher opens a confirmation modal instead of exiting immediately.
    {
        GevrPanelView panel; GevrPanelUi panelUi;
        panel.settings = gevrSettingsLive();
        panel.settings.drawKbm = []() { ImGui::TextUnformatted("KBM"); };
        panel.settings.drawGamepad = []() { ImGui::TextUnformatted("PAD"); };
        snprintf(panel.session, sizeof(panel.session), "Dam");
        snprintf(panel.note, sizeof(panel.note), "Going back to the launcher does not save this mission.");
        VrPlayMode = 0; s_net = 0;
        for (int pass = 0; pass < 3; pass++) {
            if (pass == 0) panelUi.openSection = GEVR_SEC_CONTROLS;
            ImGui::NewFrame();
            gevrDrawPanelWindow(panel, panelUi);
            ImGui::Render();
        }
        ImGuiContext &g = *ImGui::GetCurrentContext();
        for (int t = 0; t < g.TabBars.GetMapSize(); t++)
            if (ImGuiTabBar *bar = g.TabBars.TryGetMapData(t))
                settingsFitFail(bar->WidthAllTabsIdeal <= bar->BarRect.GetWidth() + 1,
                                "flat in-game tab bar and Controls sub-tab bar fit without shrinking");
        // Clicking Back to launcher (948..1262, 886..938) opens Return to launcher? confirmation modal
        int panelAction = GEVR_PANEL_NONE;
        auto runFrame = [&]() {
            ImGui::NewFrame();
            const int a = gevrDrawPanelWindow(panel, panelUi);
            if (a != GEVR_PANEL_NONE) panelAction = a;
            ImGui::Render();
        };
        auto clickPanel = [&](float x, float y) {
            panelAction = GEVR_PANEL_NONE;
            io.AddMousePosEvent(x, y); runFrame();
            io.AddMouseButtonEvent(0, true); runFrame();
            io.AddMouseButtonEvent(0, false); runFrame();
            runFrame();
        };
        clickPanel(1100.0f, 912.0f);
        settingsFitFail(panelAction == GEVR_PANEL_NONE, "Back to launcher requires confirmation");
        ImGuiWindow *modal = ImGui::FindWindowByName("Return to launcher?");
        settingsFitFail(modal && modal->Active, "Return to launcher? confirmation modal is visible");
        // Cancel (left button) keeps the game open
        clickPanel(modal->Pos.x + 100.0f, modal->Pos.y + modal->Size.y - 36.0f);
        settingsFitFail(panelAction == GEVR_PANEL_NONE, "Cancel closes modal without returning to launcher");
        // Open again and confirm (second button beside Cancel)
        clickPanel(1100.0f, 912.0f);
        modal = ImGui::FindWindowByName("Return to launcher?");
        settingsFitFail(modal && modal->Active, "Return to launcher? reopens");
        clickPanel(modal->Pos.x + 320.0f, modal->Pos.y + modal->Size.y - 36.0f);
        settingsFitFail(panelAction == GEVR_PANEL_LAUNCHER, "Confirm returns GEVR_PANEL_LAUNCHER");
    }
    // Mods' pages under Mods/Fun: solo (flat, stereo) and online, at their fullest
    makeModPages();
    VrPlayMode = 0; s_net = 0;
    sweepMods(false, "mods' pages, solo, flat");
    VrPlayMode = 1;
    sweepMods(false, "mods' pages, solo, stereo");
    VrPlayMode = 0; s_net = 1; s_host = 1; s_coop = 1;
    sweepMods(true, "mods' pages, online, co-op host");
    s_net = 0; s_host = 0; s_coop = 0;
    settingsFitFail(modScreens == 3 * GEVR_MODAPI_UI_PAGES, "every page on both screens");
    // the rows as the settings renderer reads them: a slider by its steps from min, a choice by its names
    {
        gevrModUi() = fakeModUi();
        const GevrSettingsApi rows = gevrModUiRowsApi(1);
        GevrSettingInfo info{};
        char text[64];
        settingsFitFail(rows.sectionRows(0, GEVR_SET_INGAME, nullptr, 0) == GEVR_MODAPI_UI_ROWS, "a page's rows");
        settingsFitFail(rows.info(0, &info) && info.kind == GEVR_SET_TOGGLE && rows.positions(0) == 2 && rows.index(0) == 1
                        && rows.blocked(0) != nullptr, "a toggle: on, blocked with its reason");
        settingsFitFail(rows.info(1, &info) && info.kind == GEVR_SET_CHOICE && rows.positions(1) == GEVR_MODAPI_UI_CHOICES
                        && rows.index(1) == 3 && rows.blocked(1) == nullptr, "a choice: its count, its index");
        settingsFitFail(rows.info(2, &info) && info.kind == GEVR_SET_RANGE && rows.positions(2) == GEVR_MODAPI_UI_STEPS + 1
                        && rows.index(2) == 0 && rows.defaultIndex(2) < 0, "a slider: steps from min, no Reset");
        rows.text(2, 7, text, sizeof(text));
        settingsFitFail(!strcmp(text, "-99993"), "a slider's step shows its value");
        rows.set(2, 7);
        settingsFitFail(s_modSetPage == 1 && s_modSetRow == 2 && s_modSetValue == -GEVR_MODAPI_UI_RANGE + 7,
                        "a slider set by its step: the value goes to the page");
        rows.set(1, 5);
        settingsFitFail(s_modSetRow == 1 && s_modSetValue == 5, "a choice set by its index");
        settingsFitFail(rows.info(GEVR_MODAPI_UI_ROWS, &info) == 0, "past the rows: none");
        // no pages: Mods/Fun as before, no sub-tabs
        GevrModUiApi none;
        gevrModUi() = none;
        settingsFitFail(gevrModUiTabCount(GEVR_SEC_MODS) == 0, "no pages: no extra tabs");
    }
    ImGui::DestroyContext();
    std::printf("PASS: in-game settings: the solo panel and the online Settings tab, %d sections (flat, stereo, co-op host, "
                "joiner), and the online Rules, Player and Audio tabs at their fullest (host, joiner): rows, labels, "
                "controls and reasons fit without scrolling at Quest font size; mods' pages at their fullest (four, "
                "eight rows, four buttons) under Mods/Fun on both screens, brought up as mod.ui.open asks, clicked\n", screens);
}
