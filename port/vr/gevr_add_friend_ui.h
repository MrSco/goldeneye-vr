// Add friend beside another player's name: the launcher lobby's roster (vr_launcher.cpp
// gevrLobbyRoster) and the ghost boards (gevr_ghosts_ui.h). What the friends list says of
// a player's friend code, and the button or word for it. Draws only: the caller sends
// what was pressed (gevrFriendsSend "add|CODE", or "answer|ID|1" for their request).
#ifndef GEVR_ADD_FRIEND_UI_H
#define GEVR_ADD_FRIEND_UI_H
#include "imgui/imgui.h"
#include "gevr_tooltip.h"
#include "vr_friends.h"
#include <algorithm>
#include <string>
#include <vector>

enum { GEVR_RELATION_UNKNOWN,   // no friend code (an older build, or a player who can't be found)
       GEVR_RELATION_NONE,      // Add
       GEVR_RELATION_REQUEST,   // they asked: Accept
       GEVR_RELATION_SENT, GEVR_RELATION_FRIENDS, GEVR_RELATION_BLOCKED,
       GEVR_RELATION_SELF };    // this player: nothing

// Codes a request went to from here: "Sent" until the friends list says so.
inline std::vector<std::string> &gevrFriendsAsked() {
    static std::vector<std::string> asked;
    return asked;
}

// What the friends list says of a code; requestId gets their request's id.
inline int gevrFriendRelation(const GevrFriendsModel &m, const std::string &code, std::string *requestId = nullptr) {
    if (code.empty()) return GEVR_RELATION_UNKNOWN;
    if (code == m.friendCode) return GEVR_RELATION_SELF;
    for (const GevrFriend &f : m.friends)
        if (f.code == code) return GEVR_RELATION_FRIENDS;
    for (const GevrFriendPerson &p : m.blocked)
        if (p.code == code) return GEVR_RELATION_BLOCKED;
    for (const GevrFriendPerson &p : m.requests) {
        if (p.code != code) continue;
        if (requestId) *requestId = p.id;
        return GEVR_RELATION_REQUEST;
    }
    for (const GevrFriendPerson &p : m.sent)
        if (p.code == code) return GEVR_RELATION_SENT;
    const std::vector<std::string> &asked = gevrFriendsAsked();
    if (std::find(asked.begin(), asked.end(), code) != asked.end()) return GEVR_RELATION_SENT;
    return GEVR_RELATION_NONE;
}

// The cell: Add or Accept as a small button (off while friends signs in), else a word.
// True when pressed; Add remembers the code as asked.
inline bool gevrAddFriendCell(int relation, bool ready, const char *name, const std::string &code) {
    switch (relation) {
    case GEVR_RELATION_SELF: return false;
    case GEVR_RELATION_UNKNOWN:
        ImGui::TextDisabled("—");
        if (ImGui::IsItemHovered()) gevrTooltip("No friend code from this player: an older build, or they can't be found (Friends).");
        return false;
    case GEVR_RELATION_FRIENDS: ImGui::TextDisabled("Friends"); return false;
    case GEVR_RELATION_BLOCKED: ImGui::TextDisabled("Blocked"); return false;
    case GEVR_RELATION_SENT: ImGui::TextDisabled("Sent"); return false;
    default: break;
    }
    const bool request = relation == GEVR_RELATION_REQUEST;
    ImGui::BeginDisabled(!ready);
    const bool pressed = ImGui::SmallButton(request ? "Accept" : "Add");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        gevrTooltip(!ready ? "Friends are still signing in." : request ? "%s asked to be your friend: accept."
                                                                       : "Send %s a friend request.", name);
    if (pressed && !request) gevrFriendsAsked().push_back(code);
    return pressed;
}

// The same in a player's menu (the lobby roster): a button, or what stands in its place.
inline bool gevrAddFriendButton(int relation, bool ready, const char *name, const std::string &code) {
    switch (relation) {
    case GEVR_RELATION_SELF: return false;
    case GEVR_RELATION_UNKNOWN: ImGui::TextDisabled("No friend code: an older build, or they can't be found."); return false;
    case GEVR_RELATION_FRIENDS: ImGui::TextDisabled("Friends"); return false;
    case GEVR_RELATION_BLOCKED: ImGui::TextDisabled("Blocked"); return false;
    case GEVR_RELATION_SENT: ImGui::TextDisabled("Friend request sent"); return false;
    default: break;
    }
    const bool request = relation == GEVR_RELATION_REQUEST;
    ImGui::BeginDisabled(!ready);
    const bool pressed = ImGui::Button(request ? "Accept friend request" : "Add friend");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        gevrTooltip(!ready ? "Friends are still signing in." : request ? "%s asked to be your friend: accept."
                                                                       : "Send %s a friend request.", name);
    if (pressed && !request) gevrFriendsAsked().push_back(code);
    return pressed;
}
#endif
