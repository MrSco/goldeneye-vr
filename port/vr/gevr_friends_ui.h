// The Friends sidebar (docs/friends-and-invites.md): one widget for the
// launcher's right-hand panel and the in-game panel (vr_pause_menu.cpp), so
// both look and behave the same. It reads vr_friends.cpp's model and acts
// through its calls; no engine, XR or GL state, so a layout test can draw it.
#ifndef GEVR_FRIENDS_UI_H
#define GEVR_FRIENDS_UI_H
#include "imgui/imgui.h"
#include "gevr_tooltip.h"
#include "vr_friends.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" void vrSettingsSave(void);
extern char VrPlayerName[16];
extern int VrFriendsShowDetail, VrFriendsAppearOffline, VrFriendsRestoreNoted, VrFriendsSidebar;

// Every ImGui surface (the launcher, the hold-Menu panel, the online pause) is
// 1280 x 960 with the Friends side panel to its right, open or folded to a
// strip (VrFriendsSidebar, one setting for all three). The canvas grows by the
// panel's width and the screen quad with it (vr_screen_set_width_scale), so the
// surface itself never gets narrower.
const int kGevrSurfaceW = 1280, kGevrSurfaceH = 960, kGevrSidebarW = 500, kGevrSidebarFoldedW = 76;
inline int gevrSurfaceCanvasW() { return kGevrSurfaceW + (VrFriendsSidebar ? kGevrSidebarW : kGevrSidebarFoldedW); }

struct GevrFriendsUiState {
    char addCode[16] = "";
    char restoreFriend[16] = "", restoreCode[24] = "";
    bool showRestore = false;
    std::string confirmId, confirmName;
    int confirmKind = 0;   // 1 remove, 2 block
};

// What the place it is drawn in allows.
struct GevrFriendsUiContext {
    bool canJoin = false;          // joining a lobby is possible from here
    const char *joinHint = "";     // why not, when it is not
    bool canInvite = false;        // there is (or can be) a lobby to invite into
    const char *inviteHint = "";
    const char *lobby = "";        // this headset's lobby code, or ""
    int netVersion = 0;            // GEVR_NET_VERSION
    const char *message = "";      // the last notice or error, while it is fresh
};

inline const char *gevrFriendStateText(const GevrFriend &f) {
    if (f.state == "lobby") return "In a lobby";
    if (f.state == "match") return "In an online match";
    if (f.state == "solo") return "Playing solo";
    if (f.state == "launcher") return "In the launcher";
    return "Offline";
}

// 16 characters as written down: ABCD-EFGH-JKLM-NPQR
inline std::string gevrRestoreCodeText(const std::string &code) {
    std::string out;
    for (size_t i = 0; i < code.size(); i++) {
        if (i && i % 4 == 0) out += '-';
        out += code[i];
    }
    return out;
}

inline int gevrFriendCodeFilter(ImGuiInputTextCallbackData *data) {
    ImWchar c = data->EventChar;
    if (c >= 'a' && c <= 'z') c = (ImWchar)(c - 'a' + 'A');
    data->EventChar = c;
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ? 0 : 1;
}

// A row of small buttons that wraps to the next line when the next would not fit.
struct GevrButtonRow {
    float right;
    bool first = true;
    GevrButtonRow() : right(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x) {}
    bool button(const char *label, bool enabled = true, const char *tip = nullptr) {
        const float w = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
        if (!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w <= right) ImGui::SameLine();
        first = false;
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::SmallButton(label);
        ImGui::EndDisabled();
        if (tip && tip[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) gevrTooltip("%s", tip);
        return clicked;
    }
};

inline void gevrFriendsInviteCard(const GevrFriendInvite &invite, const GevrFriendsUiContext &ctx, const ImVec4 &gold) {
    ImGui::PushID(invite.id.c_str());
    ImGui::TextColored(gold, "%s", invite.name.c_str());
    ImGui::SameLine();
    ImGui::TextWrapped("invites you to %s (%d/%d)", invite.lobbyName.c_str(), invite.players, invite.maxPlayers);
    const bool sameVersion = invite.lobbyVersion == ctx.netVersion;
    GevrButtonRow row;
    if (row.button("Join", ctx.canJoin && sameVersion, !sameVersion ? "Their game runs a different version of GoldenEye VR." : ctx.canJoin ? "Join their lobby." : ctx.joinHint))
        gevrFriendsAnswerInvite(invite, true);
    if (row.button("Ignore")) gevrFriendsAnswerInvite(invite, false);
    ImGui::PopID();
}

// The whole sidebar, filling the space it is given (a child window, scrolling).
inline void gevrFriendsSidebar(GevrFriendsUiState &ui, const GevrFriendsUiContext &ctx,
                               const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad) {
    const GevrFriendsModel &m = gevrFriends();
    ImGui::BeginChild("##friends-scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    if (m.status == "ready")
        ImGui::TextDisabled("You: %s  %s", VrPlayerName, gevrFriendCodeText(m.friendCode).c_str());
    if (m.status == "signed_out") {
        ImGui::TextColored(bad, "Signed out");
        ImGui::TextWrapped("%s was restored on another headset. Restore it below, or start again with a new code.",
                           gevrFriendCodeText(m.friendCode).c_str());
        if (ImGui::SmallButton("New friend code")) {
            gevrFriendsSend("reset");
            VrFriendsRestoreNoted = 0;
            vrSettingsSave();
        }
    } else if (m.status != "ready") {
        ImGui::TextDisabled("Getting your friend code...");
    }
    // The nudge (user, 2026-11-04): once there is a friend to lose, not at first launch.
    if (m.status == "ready" && !VrFriendsRestoreNoted && !m.friends.empty()) {
        ImGui::Separator();
        ImGui::TextColored(gold, "Write down your restore code");
        ImGui::TextWrapped("It brings this friend code and your friends back after a reinstall or on a new headset: %s",
                           gevrRestoreCodeText(m.restoreCode).c_str());
        if (ImGui::SmallButton("I wrote it down")) {
            VrFriendsRestoreNoted = 1;
            vrSettingsSave();
        }
    }
    if (ctx.message[0]) ImGui::TextWrapped("%s", ctx.message);
    ImGui::Separator();

    for (const GevrFriendInvite &invite : m.invites) gevrFriendsInviteCard(invite, ctx, gold);
    if (!m.invites.empty()) ImGui::Separator();

    if (!m.requests.empty()) {
        ImGui::TextColored(gold, "FRIEND REQUESTS");
        for (const GevrFriendPerson &p : m.requests) {
            ImGui::PushID(p.id.c_str());
            ImGui::TextUnformatted(p.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", gevrFriendCodeText(p.code).c_str());
            GevrButtonRow row;
            if (row.button("Accept")) gevrFriendsSend("answer|" + p.id + "|1");
            if (row.button("Decline")) gevrFriendsSend("answer|" + p.id + "|0");
            if (row.button("Block")) { ui.confirmId = p.id; ui.confirmName = p.name; ui.confirmKind = 2; }
            ImGui::PopID();
        }
        if (m.requests.size() > 1 && ImGui::SmallButton("Decline all")) gevrFriendsSend("decline-all");
        ImGui::Separator();
    }

    int online = 0;
    for (const GevrFriend &f : m.friends) online += f.state != "offline";
    ImGui::TextColored(gold, "FRIENDS");
    ImGui::SameLine();
    ImGui::TextDisabled("%d online", online);
    if (m.friends.empty())
        ImGui::TextWrapped(m.loaded ? "No friends yet. Add one below with their friend code." : "Loading...");
    bool offlineOpen = false, offlineHeader = false;
    for (const GevrFriend &f : m.friends) {
        if (f.state == "offline" && !offlineHeader) {
            offlineHeader = true;
            char title[48];
            snprintf(title, sizeof(title), "Offline (%d)###friends-offline", (int)m.friends.size() - online);
            offlineOpen = ImGui::CollapsingHeader(title);
        }
        if (f.state == "offline" && !offlineOpen) continue;
        ImGui::PushID(f.id.c_str());
        if (f.state == "offline") ImGui::TextDisabled("%s", f.name.c_str());
        else ImGui::TextColored(good, "%s", f.name.c_str());
        if (ImGui::IsItemHovered()) gevrTooltip("%s", gevrFriendCodeText(f.code).c_str());
        if (f.state != "offline") {
            if (f.detail.empty()) ImGui::TextDisabled("%s", gevrFriendStateText(f));
            else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s: %s", gevrFriendStateText(f), f.detail.c_str());
                ImGui::PopStyleColor();
            }
        }
        GevrButtonRow row;
        if (!f.lobby.empty() && f.lobby != ctx.lobby) {
            const bool sameVersion = f.lobbyVersion == ctx.netVersion;
            char join[32];
            snprintf(join, sizeof(join), "Join %d/%d", f.players, f.maxPlayers);
            if (row.button(join, ctx.canJoin && sameVersion,
                           !sameVersion ? "Their game runs a different version of GoldenEye VR." : ctx.canJoin ? "Join their lobby." : ctx.joinHint))
                gevrFriendsJoin(f.lobby);
        }
        if (f.state != "offline") {
            const bool invited = std::find(m.invited.begin(), m.invited.end(), f.id) != m.invited.end();
            const bool sameVersion = f.version == ctx.netVersion;
            const bool together = ctx.lobby[0] && f.lobby == ctx.lobby;
            if (row.button(invited ? "Invited" : "Invite", ctx.canInvite && sameVersion && !invited && !together,
                           !sameVersion ? "They run a different version of GoldenEye VR." : together ? "They are in your lobby." : ctx.inviteHint))
                gevrFriendsInvite(f.id);
        }
        if (row.button("...")) ImGui::OpenPopup("friend-menu");
        if (ImGui::BeginPopup("friend-menu")) {
            ImGui::TextDisabled("%s  %s", f.name.c_str(), gevrFriendCodeText(f.code).c_str());
            if (ImGui::Selectable("Remove friend")) { ui.confirmId = f.id; ui.confirmName = f.name; ui.confirmKind = 1; }
            if (ImGui::Selectable("Block")) { ui.confirmId = f.id; ui.confirmName = f.name; ui.confirmKind = 2; }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (!m.sent.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Waiting for:");
        for (const GevrFriendPerson &p : m.sent) {
            ImGui::PushID(p.id.c_str());
            ImGui::TextDisabled("%s", p.name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel")) gevrFriendsSend("remove|" + p.id);
            ImGui::PopID();
        }
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Add a friend")) {
        ImGui::TextWrapped("Your code is %s. Give it to a friend, or type theirs:", gevrFriendCodeText(m.friendCode).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.5f);
        ImGui::InputTextWithHint("##add-code", "GE-XXXXXX", ui.addCode, sizeof(ui.addCode),
                                 ImGuiInputTextFlags_CallbackCharFilter, gevrFriendCodeFilter);
        ImGui::SameLine();
        ImGui::BeginDisabled(m.status != "ready" || strlen(ui.addCode) < 6);
        if (ImGui::SmallButton("Send request")) {
            gevrFriendsSend(std::string("add|") + ui.addCode);
            ui.addCode[0] = '\0';
        }
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Privacy and restore code")) {
        bool detail = VrFriendsShowDetail != 0, hidden = VrFriendsAppearOffline != 0, changed = false;
        if (ImGui::Checkbox("Show what I'm playing", &detail)) { VrFriendsShowDetail = detail; changed = true; }
        if (ImGui::IsItemHovered()) gevrTooltip("Friends see the mission or map, the mode and the players.");
        if (ImGui::Checkbox("Appear offline", &hidden)) { VrFriendsAppearOffline = hidden; changed = true; }
        if (ImGui::IsItemHovered()) gevrTooltip("Friends see you as offline. You still get requests and invites.");
        if (changed) {
            vrSettingsSave();
            gevrFriendsSend(std::string("settings|") + (VrFriendsShowDetail ? "1" : "0") + "|" + (VrFriendsAppearOffline ? "1" : "0"));
        }
        ImGui::Text("Restore code: %s", ui.showRestore ? gevrRestoreCodeText(m.restoreCode).c_str() : "****-****-****-****");
        ImGui::SameLine();
        if (ImGui::SmallButton(ui.showRestore ? "Hide" : "Show")) ui.showRestore = !ui.showRestore;
        if (ImGui::IsItemHovered())
            gevrTooltip("With your friend code it brings your friends back after a reinstall or on a new headset. Keep it to yourself.");
        ImGui::TextDisabled("Restore a friend code:");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.5f);
        ImGui::InputTextWithHint("##restore-friend", "GE-XXXXXX", ui.restoreFriend, sizeof(ui.restoreFriend),
                                 ImGuiInputTextFlags_CallbackCharFilter, gevrFriendCodeFilter);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11);
        ImGui::InputTextWithHint("##restore-code", "restore code", ui.restoreCode, sizeof(ui.restoreCode),
                                 ImGuiInputTextFlags_CallbackCharFilter, gevrFriendCodeFilter);
        ImGui::SameLine();
        ImGui::BeginDisabled(strlen(ui.restoreFriend) < 6 || strlen(ui.restoreCode) < 16);
        if (ImGui::SmallButton("Restore")) {
            gevrFriendsSend(std::string("restore|") + ui.restoreFriend + "|" + ui.restoreCode);
            ui.restoreFriend[0] = ui.restoreCode[0] = '\0';
            VrFriendsRestoreNoted = 1;   // they had it written down
            vrSettingsSave();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            gevrTooltip("Replaces this headset's friend code with the restored one, and signs the other headset out.");
    }
    if (!m.blocked.empty()) {
        char title[48];
        snprintf(title, sizeof(title), "Blocked (%d)###friends-blocked", (int)m.blocked.size());
        if (ImGui::CollapsingHeader(title)) {
            for (const GevrFriendPerson &p : m.blocked) {
                ImGui::PushID(p.id.c_str());
                ImGui::TextUnformatted(p.name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Unblock")) gevrFriendsSend("unblock|" + p.id);
                ImGui::PopID();
            }
        }
    }
    if (ui.confirmKind && !ImGui::IsPopupOpen("Friend?")) ImGui::OpenPopup("Friend?");
    if (ImGui::BeginPopupModal("Friend?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ui.confirmKind == 2) ImGui::Text("Block %s? They can't send you requests or invites.", ui.confirmName.c_str());
        else ImGui::Text("Remove %s from your friends?", ui.confirmName.c_str());
        if (ImGui::Button("Cancel")) { ui.confirmKind = 0; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button(ui.confirmKind == 2 ? "Block" : "Remove")) {
            gevrFriendsSend((ui.confirmKind == 2 ? "block|" : "remove|") + ui.confirmId);
            ui.confirmKind = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();
}
// The side panel itself: its own window right of the surface. Folded, a strip
// with the word down it and what is waiting; open, the sidebar under a FRIENDS
// heading with >> to fold it.
inline void gevrFriendsSidePanel(GevrFriendsUiState &ui, const GevrFriendsUiContext &ctx,
                                 const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad) {
    const GevrFriendsModel &m = gevrFriends();
    const float width = (float)(gevrSurfaceCanvasW() - kGevrSurfaceW);
    ImGui::SetNextWindowPos(ImVec2((float)kGevrSurfaceW, 0));
    ImGui::SetNextWindowSize(ImVec2(width, (float)kGevrSurfaceH));
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##friends-sidebar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    int online = 0;
    for (const GevrFriend &f : m.friends) online += f.state != "offline";
    const int waiting = (int)(m.requests.size() + m.invites.size());
    if (!VrFriendsSidebar) {
        char label[64];
        snprintf(label, sizeof(label), "<\n\nF\nR\nI\nE\nN\nD\nS\n\n%d%s###friends-unfold", waiting ? waiting : online, waiting ? "\nNEW" : "");
        if (waiting) ImGui::PushStyleColor(ImGuiCol_Text, gold);
        if (ImGui::Button(label, ImVec2(-1, -1))) { VrFriendsSidebar = 1; vrSettingsSave(); }
        if (waiting) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) gevrTooltip("Friends: %d online, %d new", online, waiting);
        ImGui::End();
        return;
    }
    ImGui::TextColored(gold, "FRIENDS");
    ImGui::SameLine(width - ImGui::CalcTextSize(">>").x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::SmallButton(">>")) { VrFriendsSidebar = 0; vrSettingsSave(); }
    if (ImGui::IsItemHovered()) gevrTooltip("Fold the friends list to the side.");
    gevrFriendsSidebar(ui, ctx, gold, good, bad);
    ImGui::End();
}
#endif
