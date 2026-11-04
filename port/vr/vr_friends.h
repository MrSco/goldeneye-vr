/* Friends, presence and invites (docs/friends-and-invites.md).
 *
 * android/.../FriendsClient.java talks to the lobby Worker; this side composes
 * the presence line from the launcher or the running game, keeps the friends
 * list it answers with, and carries invites into the launcher's join path. */
#ifndef VR_FRIENDS_H
#define VR_FRIENDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Every launcher frame (inGame 0) and every game tick (inGame 1, gevrLobbyGameTick). */
void gevrFriendsTick(int inGame);
/* vr_launcher.cpp: the hosted lobby's visibility ("public", "private", "friends"), or "" when not hosting. */
const char *gevrHostedLobbyVisibility(void);
/* A new invite's line for the bottom of the view in a mission, for a few seconds; NULL otherwise. */
const char *gevrFriendsNotice(void);

#ifdef __cplusplus
}

#include <cstdint>
#include <string>
#include <vector>

struct GevrFriend {
    std::string id, name, code, state, lobby, detail;
    int version = 0, players = 0, maxPlayers = 0, lobbyVersion = 0;
    bool discord = false;   // friends through Discord (both linked), not by code
};
struct GevrFriendPerson {
    std::string id, name, code;
};
struct GevrFriendSearchResult {
    std::string id, name, code, relation;   // relation: none, sent, request, friends
};
struct GevrFriendInvite {
    std::string id, from, name, code, lobby, lobbyName;
    int stage = 0, players = 0, maxPlayers = 0, lobbyVersion = 0;
    uint32_t expiresAtMs = 0;
};
struct GevrFriendsModel {
    // none (registering), ready, signed_out (restored on another headset)
    std::string status = "none", friendCode, restoreCode;
    bool showDetail = true, appearOffline = false;
    bool loaded = false;   // a presence answer has arrived since the start
    bool searchable = true;
    // name search: the last query, its results, and whether it is still running
    std::string searchQuery;
    std::vector<GevrFriendSearchResult> search;
    bool searching = false;
    std::vector<GevrFriend> friends;
    std::vector<GevrFriendPerson> requests, sent, blocked;
    std::vector<GevrFriendInvite> invites;
    std::vector<std::string> invited;   // friends invited into the current lobby
    std::string message;                // the last notice or error, for the launcher's status line
    uint32_t messageMs = 0;
};

const GevrFriendsModel &gevrFriends(void);
/* A FriendsClient command line ("add|CODE", "answer|id|1", ...). */
void gevrFriendsSend(const std::string &command);
/* Into the current lobby, or a private lobby hosted for it first (the launcher's
 * Multiplayer page takes gevrFriendsTakeHostRequest). */
void gevrFriendsInvite(const std::string &friendId);
bool gevrFriendsTakeHostRequest(void);
/* A lobby for an invite: true when there is one or the launcher will host one. */
bool gevrFriendsRequestLobby(void);
/* A notice for the panels' status line, and the bottom of the view in a mission. */
void gevrFriendsMessage(const std::string &text);
bool gevrFriendsHostRequested(void);
/* A lobby to join from the friends page or an invite; the Multiplayer page takes it. */
void gevrFriendsJoin(const std::string &lobbyCode);
std::string gevrFriendsTakeJoin(void);
bool gevrFriendsJoinPending(void);
void gevrFriendsAnswerInvite(const GevrFriendInvite &invite, bool join);
/* In a mission (the in-game panel): the lobby asked for (gevrFriendsJoin), kept in
 * data/gevr_pending_join.txt for the launcher the app restarts into. */
const std::string &gevrFriendsJoinCode(void);
bool gevrFriendsSavePendingJoin(const std::string &lobbyCode);
/* The launcher's start: a join saved in a mission. */
void gevrFriendsLoadPendingJoin(void);
/* The last notice or error while it is fresh (10 s), or "". */
const char *gevrFriendsFreshMessage(void);
/* The privacy settings (VrFriendsShowDetail, VrFriendsAppearOffline,
 * VrFriendsSearchable) to the lobby service. */
void gevrFriendsSendSettings(void);
/* A new player name from the Friends panel: tidied, saved, and sent with the next presence. */
void gevrFriendsSetName(const char *name);
/* "GE-7K3QX2" */
std::string gevrFriendCodeText(const std::string &code);
#endif

#endif
