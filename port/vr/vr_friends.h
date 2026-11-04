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
/* A new invite's line for the bottom of the view in a mission, for a few seconds; NULL otherwise. */
const char *gevrFriendsNotice(void);
/* The menu-hold prompt's extra line while an invite waits ("Y: JOIN NATTY'S GAME"), or NULL. */
const char *gevrFriendsPromptLine(void);
/* Y on that prompt: saves the invite's lobby for the restarted launcher. 1 when there was one. */
int gevrFriendsAcceptInvite(void);

#ifdef __cplusplus
}

#include <cstdint>
#include <string>
#include <vector>

struct GevrFriend {
    std::string id, name, code, state, lobby, detail;
    int version = 0, players = 0, maxPlayers = 0, lobbyVersion = 0;
};
struct GevrFriendPerson {
    std::string id, name, code;
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
/* A lobby to join from the friends page or an invite; the Multiplayer page takes it. */
void gevrFriendsJoin(const std::string &lobbyCode);
std::string gevrFriendsTakeJoin(void);
bool gevrFriendsJoinPending(void);
void gevrFriendsAnswerInvite(const GevrFriendInvite &invite, bool join);
/* The watch's Join (an online match): as gevrFriendsAcceptInvite, for one invite. */
bool gevrFriendsAcceptInviteId(const std::string &id);
/* The launcher's start: an invite accepted in a mission (gevrFriendsAcceptInvite). */
void gevrFriendsLoadPendingJoin(void);
/* "GE-7K3QX2" */
std::string gevrFriendCodeText(const std::string &code);
#endif

#endif
