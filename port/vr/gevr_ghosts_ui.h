// The launcher's Ghosts tab (docs/speedrun.md): a mission's ghost board, the
// run this headset has on file for it, and which run the mission races. Dab's
// Mod's Ghost Trials menu (port/src/ghostmenu.c: Ghost Share's leaderboards,
// My Ghosts' upload, the chooser) as one launcher page. vr_ghosts.cpp fills the
// view and acts on what comes back; this draws, with no file, network or game
// state, so a layout test can draw it at its fullest.
#ifndef GEVR_GHOSTS_UI_H
#define GEVR_GHOSTS_UI_H
#include "imgui/imgui.h"
#include "gevr_tooltip.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

enum { GEVR_GHOSTS_IDLE, GEVR_GHOSTS_LOADING, GEVR_GHOSTS_LOADED, GEVR_GHOSTS_FAILED };
const int kGevrGhostRowsPerPage = 8;

struct GevrGhostRow {
    long long id = 0;
    int time60 = 0, flags = 0;
    std::string code, name;
    bool mine = false;     // this headset's friend code
    bool racing = false;   // the run this mission races
};

struct GevrGhostsView {
    const char *note = nullptr;               // why there is no board here (a hack's campaign), or null
    const char *const *missions = nullptr;    // the twenty missions' names
    int missionCount = 0;
    int mission = 0, difficulty = 0, plays = 0;   // plays: 0 all, 1 VR, 2 flat
    // this headset's run of the mission
    bool haveBest = false;
    int bestTime60 = 0, bestFlags = 0, bestRank = 0;   // rank 0: not on the board shown
    bool bestUnranked = false;                         // force pull, the ally or a changed speed
    // what the mission races
    std::string racing;                        // "Your best run", "Natty (GE-7K3QX2), 1:58.20", ...
    bool racingOther = false;                  // a downloaded run (Race my best puts it back)
    bool raceOn = false;                       // the Ghost race setting
    // the board
    int state = GEVR_GHOSTS_IDLE;
    std::vector<GevrGhostRow> rows;
    int page = 0;
    bool busy = false;                         // an upload or a download on its way
    std::string message;                       // the last result
    bool messageBad = false;
};

struct GevrGhostsActions {
    bool selection = false;    // mission, difficulty or plays changed
    bool refresh = false;
    bool upload = false;       // this mission's run
    bool uploadAll = false;    // every mission's
    bool raceMine = false;
    bool raceOn = false;       // turn the Ghost race setting on
    long long race = -1;       // a board row's run to download and race
};

// 1:58.20, 12:03.05, 1:02:03.05
inline std::string gevrGhostTimeText(int time60) {
    char text[24];
    const int h = time60 / 216000, m = (time60 / 3600) % 60, s = (time60 / 60) % 60, c = ((time60 % 60) * 100) / 60;
    if (h > 0) snprintf(text, sizeof(text), "%d:%02d:%02d.%02d", h, m, s, c);
    else snprintf(text, sizeof(text), "%d:%02d.%02d", m, s, c);
    return text;
}

// what a run was set with beyond VR or flat, for a row's tooltip (gevr_ghost.h GEVR_GHF_*)
inline std::string gevrGhostFlagsText(int flags) {
    std::string out = flags & 0x01 ? "VR" : "Flat screen";
    if (flags & 0x20) out += ", third person";
    out += flags & 0x02 ? ", Bean's art" : flags & 0x04 ? ", GoldenEye X guns" : ", N64 art";
    return out;
}

// text cut to a width with "...", at a whole UTF-8 character (gevrLauncherStatus's rule)
inline std::string gevrGhostFit(const std::string &text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    std::string line = text;
    while (!line.empty() && ImGui::CalcTextSize((line + "...").c_str()).x > width) {
        size_t end = line.size() - 1;
        while (end > 0 && (static_cast<unsigned char>(line[end]) & 0xc0) == 0x80) --end;
        line.resize(end);
    }
    return line + "...";
}

inline float gevrGhostButtonWidth(const char *label) {
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
}

inline GevrGhostsActions gevrGhostsPageDraw(GevrGhostsView &v, const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad) {
    GevrGhostsActions act;
    const float font = ImGui::GetFontSize();
    if (v.note) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", v.note);
        ImGui::PopStyleColor();
        return act;
    }
    const int pages = std::max(1, ((int)v.rows.size() + kGevrGhostRowsPerPage - 1) / kGevrGhostRowsPerPage);
    if (v.page >= pages) v.page = pages - 1;
    if (v.page < 0) v.page = 0;

    // the board's mission, difficulty and who it shows; its pages
    static const char *const difficulties[] = { "Agent", "Secret Agent", "00 Agent" };
    static const char *const plays[] = { "All runs", "VR runs", "Flat runs" };
    ImGui::SetNextItemWidth(font * 8.0f);
    if (ImGui::BeginCombo("##ghost-mission", v.mission >= 0 && v.mission < v.missionCount ? v.missions[v.mission] : "",
                          ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < v.missionCount; i++) {
            if (ImGui::Selectable(v.missions[i], i == v.mission) && i != v.mission) { v.mission = i; act.selection = true; }
            if (i == v.mission) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(font * 8.0f);
    if (ImGui::BeginCombo("##ghost-difficulty", difficulties[v.difficulty])) {
        for (int i = 0; i < 3; i++)
            if (ImGui::Selectable(difficulties[i], i == v.difficulty) && i != v.difficulty) { v.difficulty = i; act.selection = true; }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(font * 6.5f);
    if (ImGui::BeginCombo("##ghost-plays", plays[v.plays])) {
        for (int i = 0; i < 3; i++)
            if (ImGui::Selectable(plays[i], i == v.plays) && i != v.plays) { v.plays = i; act.selection = true; }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(v.state == GEVR_GHOSTS_LOADING);
    if (ImGui::Button("Refresh")) act.refresh = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(v.page <= 0);
    if (ImGui::ArrowButton("##ghost-prev", ImGuiDir_Left)) v.page--;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (v.rows.empty()) ImGui::TextDisabled("Top 100");
    else ImGui::Text("%d-%d of %d", v.page * kGevrGhostRowsPerPage + 1,
                     std::min((int)v.rows.size(), (v.page + 1) * kGevrGhostRowsPerPage), (int)v.rows.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(v.page >= pages - 1);
    if (ImGui::ArrowButton("##ghost-next", ImGuiDir_Right)) v.page++;
    ImGui::EndDisabled();

    // this headset's run of the mission, and every mission's to the boards
    ImGui::AlignTextToFramePadding();
    if (v.haveBest) {
        ImGui::Text("Your best: %s%s", gevrGhostTimeText(v.bestTime60).c_str(), v.bestFlags & 0x01 ? " (VR)" : " (flat)");
        if (v.bestRank > 0) {
            ImGui::SameLine();
            ImGui::TextColored(gold, "#%d", v.bestRank);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(v.busy || v.bestUnranked);
        if (ImGui::Button("Upload")) act.upload = true;
        ImGui::EndDisabled();
        if (v.bestUnranked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            gevrTooltip("Set with force pull, the AI ally or a changed movement speed: the boards rank the original rules.");
    } else {
        ImGui::TextDisabled("No run of yours yet.");
        if (ImGui::IsItemHovered()) gevrTooltip("Finish the mission with Ghost race or the speedrun timer on.");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(v.busy);
    if (ImGui::Button("Upload all my best runs")) act.uploadAll = true;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) gevrTooltip("Your best run of every mission and difficulty, to the boards, under your Friends name.");

    // what the mission races: the setting first, so it is never cut off
    ImGui::AlignTextToFramePadding();
    if (!v.raceOn) {
        ImGui::TextColored(bad, "Ghost race is off.");
        ImGui::SameLine();
        if (ImGui::Button("Turn on")) act.raceOn = true;
        ImGui::SameLine();
    }
    const float tail = v.racingOther ? gevrGhostButtonWidth("Race my best") : 0.0f;
    ImGui::TextUnformatted(gevrGhostFit("Racing: " + v.racing, ImGui::GetContentRegionAvail().x - tail).c_str());
    if (ImGui::IsItemHovered()) gevrTooltip("Racing: %s", v.racing.c_str());
    if (v.racingOther) {
        ImGui::SameLine();
        if (ImGui::Button("Race my best")) act.raceMine = true;
    }

    // the board, a page at a time: a page never scrolls (docs/launcher-unify.md)
    if (ImGui::BeginTable("##ghost-board", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, font * 2.2f);
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, font * 5.5f);
        ImGui::TableSetupColumn("Plays", ImGuiTableColumnFlags_WidthFixed, font * 3.2f);
        ImGui::TableSetupColumn("##race", ImGuiTableColumnFlags_WidthFixed, font * 4.2f);
        ImGui::TableHeadersRow();
        const int first = v.page * kGevrGhostRowsPerPage;
        for (int i = first; i < (int)v.rows.size() && i < first + kGevrGhostRowsPerPage; i++) {
            const GevrGhostRow &r = v.rows[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", i + 1);
            ImGui::TableNextColumn();
            const std::string name = gevrGhostFit(r.name, ImGui::GetContentRegionAvail().x);
            if (r.mine) ImGui::TextColored(gold, "%s", name.c_str());
            else ImGui::TextUnformatted(name.c_str());
            if (ImGui::IsItemHovered()) gevrTooltip("%s, GE-%s", r.name.c_str(), r.code.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(gevrGhostTimeText(r.time60).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.flags & 0x01 ? "VR" : "Flat");
            if (ImGui::IsItemHovered()) gevrTooltip("%s", gevrGhostFlagsText(r.flags).c_str());
            ImGui::TableNextColumn();
            if (r.racing) {
                ImGui::TextColored(good, "Racing");
            } else {
                ImGui::BeginDisabled(v.busy || (r.mine && !v.haveBest));
                if (ImGui::SmallButton("Race")) act.race = r.id;
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // one line: what the board is doing, else the last result
    const char *status = v.state == GEVR_GHOSTS_LOADING ? "Loading the board..."
        : v.state == GEVR_GHOSTS_LOADED && v.rows.empty() ? "No runs on this board yet." : nullptr;
    if (status) {
        ImGui::TextDisabled("%s", status);
    } else if (v.state == GEVR_GHOSTS_FAILED || !v.message.empty()) {
        const std::string text = v.message.empty() ? std::string("The board did not load.") : v.message;
        ImGui::PushStyleColor(ImGuiCol_Text, v.messageBad || v.state == GEVR_GHOSTS_FAILED ? bad : good);
        ImGui::TextUnformatted(gevrGhostFit(text, ImGui::GetContentRegionAvail().x).c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) gevrTooltip("%s", text.c_str());
    }
    return act;
}
#endif
