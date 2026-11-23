// Mods' pages on the in-game screen (mod.ui; docs/mod-system.md, the UI):
// a sub-tab each under the Mods/Fun tab, beside the section's own rows, on
// the solo panel and the online screen's Settings tab alike (gevrSettingsTabs
// draws them through GevrSettingsApi's extra tabs). A page is its text, its
// rows drawn as the settings' rows are (gevrSettingsSection, through a
// GevrSettingsApi over the page), and its buttons in a row under them.
//
// The pages come from gevr_modapi.c (gevrModUiLive); a click or a change
// goes back there and reaches the mod's script at its next frame, never
// from here. No engine pointers: the fit test hands in pages of its own.
#ifndef GEVR_MODUI_UI_H
#define GEVR_MODUI_UI_H
#include "gevr_settings_ui.h"
#include "gevr_modapi.h"

struct GevrModUiApi {
    int (*count)(void) = nullptr;                    // pages shown now
    const GevrModUiPage *(*page)(int) = nullptr;
    void (*set)(int page, int row, int value) = nullptr;
    void (*click)(int page, int button) = nullptr;
    int (*asked)(void) = nullptr;                    // the page mod.ui.open asked for, -1 none
    void (*opened)(void) = nullptr;                  // and its tab is up
};

inline GevrModUiApi gevrModUiLive() {
    GevrModUiApi m;
    m.count = gevrModUiCount;
    m.page = gevrModUiPage;
    m.set = gevrModUiSet;
    m.click = gevrModUiClick;
    m.asked = gevrModUiAsked;
    m.opened = gevrModUiOpened;
    return m;
}

// The pages drawn: the live ones (vr_pause_menu.cpp), or a test's.
inline GevrModUiApi &gevrModUi() {
    static GevrModUiApi api;
    return api;
}

// One page's rows as the settings renderer reads rows: ids are the rows'
// indices, the page the one drawn now.
inline int &gevrModUiRowsPage() {
    static int page = -1;
    return page;
}
inline const GevrModUiRow *gevrModUiRowOf(int id) {
    const GevrModUiApi &m = gevrModUi();
    const GevrModUiPage *p = m.page ? m.page(gevrModUiRowsPage()) : nullptr;
    return p && id >= 0 && id < p->rows ? &p->row[id] : nullptr;
}
inline int gevrModUiRowsCount() { return 1; }
inline const char *gevrModUiRowsName(int) { return ""; }
inline int gevrModUiRowsList(int, unsigned, int *ids, int max) {
    const GevrModUiApi &m = gevrModUi();
    const GevrModUiPage *p = m.page ? m.page(gevrModUiRowsPage()) : nullptr;
    const int n = p ? p->rows : 0;
    for (int i = 0; ids && i < n && i < max; i++) ids[i] = i;
    return n;
}
inline int gevrModUiRowsInfo(int id, GevrSettingInfo *out) {
    const GevrModUiRow *r = gevrModUiRowOf(id);
    if (!r) return 0;
    *out = GevrSettingInfo{};
    out->label = r->label;
    out->tooltip = r->tip[0] ? r->tip : nullptr;
    out->kind = r->kind == GEVR_MODUI_TOGGLE ? GEVR_SET_TOGGLE : r->kind == GEVR_MODUI_CHOICE ? GEVR_SET_CHOICE : GEVR_SET_RANGE;
    out->section = GEVR_SEC_MODS;
    out->where = GEVR_SET_INGAME;
    return 1;
}
inline int gevrModUiRowsPositions(int id) {
    const GevrModUiRow *r = gevrModUiRowOf(id);
    return !r ? 0 : r->kind == GEVR_MODUI_TOGGLE ? 2 : r->kind == GEVR_MODUI_CHOICE ? r->count : r->max - r->min + 1;
}
inline int gevrModUiRowsIndex(int id) {
    const GevrModUiRow *r = gevrModUiRowOf(id);
    return !r ? 0 : r->kind == GEVR_MODUI_SLIDER ? r->value - r->min : r->value;
}
inline int gevrModUiRowsDefault(int) { return -1; }   // no Reset: the mod keeps its own defaults
inline void gevrModUiRowsSet(int id, int index) {
    const GevrModUiRow *r = gevrModUiRowOf(id);
    const GevrModUiApi &m = gevrModUi();
    if (r && m.set) m.set(gevrModUiRowsPage(), id, r->kind == GEVR_MODUI_SLIDER ? r->min + index : index);
}
inline void gevrModUiRowsText(int id, int index, char *buf, int size) {
    const GevrModUiRow *r = gevrModUiRowOf(id);
    if (!r) { snprintf(buf, size, "%s", ""); return; }
    const int i = index < 0 ? gevrModUiRowsIndex(id) : index;
    if (r->kind == GEVR_MODUI_TOGGLE) snprintf(buf, size, "%s", i ? "On" : "Off");
    else if (r->kind == GEVR_MODUI_CHOICE) snprintf(buf, size, "%s", i >= 0 && i < r->count ? r->choices[i] : "");
    else snprintf(buf, size, "%d", r->min + i);
}
inline const char *gevrModUiRowsBlocked(int id) {
    const GevrModUiRow *r = gevrModUiRowOf(id);
    return r && r->blocked[0] ? r->blocked : nullptr;
}
inline GevrSettingsApi gevrModUiRowsApi(int page) {
    gevrModUiRowsPage() = page;
    GevrSettingsApi a;
    a.sectionCount = gevrModUiRowsCount;
    a.sectionName = gevrModUiRowsName;
    a.sectionRows = gevrModUiRowsList;
    a.info = gevrModUiRowsInfo;
    a.positions = gevrModUiRowsPositions;
    a.index = gevrModUiRowsIndex;
    a.defaultIndex = gevrModUiRowsDefault;
    a.set = gevrModUiRowsSet;
    a.text = gevrModUiRowsText;
    a.blocked = gevrModUiRowsBlocked;
    return a;
}

#define GEVR_MODUI_BUTTON_W 280.0f
#define GEVR_MODUI_BUTTON_H 52.0f

// One page: its text, its rows, its buttons.
inline void gevrModUiPageDraw(int i) {
    const GevrModUiApi &m = gevrModUi();
    const GevrModUiPage *p = m.page ? m.page(i) : nullptr;
    if (!p) return;
    if (p->text[0]) ImGui::TextWrapped("%s", p->text);
    if (p->rows > 0) {
        // the rows' box leaves the buttons their row under it
        const ImGuiStyle &style = ImGui::GetStyle();
        const float buttons = p->buttons > 0 ? GEVR_MODUI_BUTTON_H + style.ItemSpacing.y * 2 : 0.0f;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##modui-rows", ImVec2(avail.x, avail.y - buttons), false, ImGuiWindowFlags_NoScrollbar);
        gevrSettingsSection(gevrModUiRowsApi(i), 0, GEVR_SET_INGAME);
        ImGui::EndChild();
    }
    for (int b = 0; b < p->buttons; b++) {
        if (b) ImGui::SameLine();
        else if (p->rows > 0) ImGui::Spacing();
        ImGui::PushID(b);
        const char *why = p->button[b].blocked[0] ? p->button[b].blocked : nullptr;
        ImGui::BeginDisabled(why != nullptr);
        if (ImGui::Button(p->button[b].label, ImVec2(GEVR_MODUI_BUTTON_W, GEVR_MODUI_BUTTON_H)) && m.click) m.click(i, b);
        ImGui::EndDisabled();
        if (why && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) gevrTooltip("%s.", why);
        ImGui::PopID();
    }
}

// GevrSettingsApi's extra tabs: the pages under Mods/Fun.
inline int gevrModUiTabCount(int section) {
    const GevrModUiApi &m = gevrModUi();
    return section == GEVR_SEC_MODS && m.count ? m.count() : 0;
}
inline void gevrModUiTabs(int section) {
    if (section != GEVR_SEC_MODS) return;
    const GevrModUiApi &m = gevrModUi();
    const int asked = m.asked ? m.asked() : -1;
    const int n = m.count ? m.count() : 0;
    for (int i = 0; i < n; i++) {
        const GevrModUiPage *p = m.page(i);
        if (!p) continue;
        char name[GEVR_MODAPI_UI_TITLE + GEVR_MODAPI_UI_ID + 80];
        snprintf(name, sizeof(name), "%s##modui.%s.%s", p->title, p->mod, p->id);   // two mods' pages may share a title
        if (!ImGui::BeginTabItem(name, nullptr, i == asked ? ImGuiTabItemFlags_SetSelected : 0)) continue;
        if (i == asked && m.opened) m.opened();   // its tab is up: the ask is done
        ImGui::BeginChild("##panel-rows", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
        gevrModUiPageDraw(i);
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
}

// The live screen's settings with the mods' pages in them (vr_pause_menu.cpp).
inline void gevrModUiAttach(GevrSettingsApi &settings) {
    gevrModUi() = gevrModUiLive();
    settings.extraTabs = gevrModUiTabCount;
    settings.drawExtraTabs = gevrModUiTabs;
}
#endif
