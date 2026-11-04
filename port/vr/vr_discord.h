/* Discord (docs/friends-and-invites.md, phases 3 and 4): Link Discord, Rich
 * Presence, Discord friends who also play, and Discord invites, through
 * Discord's Social SDK. Built only when the SDK's Android library is present
 * (GEVR_DISCORD: android/app/build.gradle looks for
 * build/discord/discord_partner_sdk.aar); otherwise every call here does
 * nothing and gevrDiscordState() is GEVR_DISCORD_UNAVAILABLE.
 *
 * The app is a confidential client: Client::Authorize's code (with PKCE) goes to
 * the lobby Worker (FriendsClient "discord-link"), which exchanges it with the
 * secret and answers with the tokens; this side only ever holds a user token. */
#ifndef VR_DISCORD_H
#define VR_DISCORD_H
#include <cstdint>
#include <string>
#include <vector>

enum {
    GEVR_DISCORD_UNAVAILABLE = -1,   // no SDK in this build
    GEVR_DISCORD_UNLINKED = 0,
    GEVR_DISCORD_SIGNING_IN = 1,     // the Discord sign-in page is open
    GEVR_DISCORD_CONNECTING = 2,     // linked; connecting with the token
    GEVR_DISCORD_CONNECTED = 3,
};

// A Discord friend (the SDK's relationships), for "Invite from Discord".
struct GevrDiscordFriend {
    uint64_t id = 0;
    std::string name;
    bool online = false;
    bool invited = false;   // invited into the current lobby
};
// A Discord invite received (Client::SetActivityInviteCreatedCallback).
struct GevrDiscordInvite {
    int key = 0;
    std::string name;
};

int gevrDiscordState(void);
const char *gevrDiscordName(void);   // the linked account's name, or ""
void gevrDiscordLink(void);          // opens Discord's sign-in
void gevrDiscordUnlink(void);
/* Each launcher frame and game tick (vr_friends.cpp): the SDK's callbacks. */
void gevrDiscordTick(void);
/* From FriendsClient (vr_friends.cpp): linked or not, and a fresh user token. */
void gevrDiscordOnLinked(bool linked, const std::string &name);
void gevrDiscordOnToken(const std::string &accessToken);
/* What friends see, shown in Discord too (cleared while appearing offline). With
 * a lobby code the status carries it as the join secret, which Discord hands only
 * to an invited friend or, for a public or friends-only lobby, one who joins. */
void gevrDiscordPresence(const std::string &state, const std::string &detail, int players, int maxPlayers,
                         const std::string &lobbyCode, bool joinable, bool offline);
/* Phase 4: Discord friends to invite (online first), and invites received. */
const std::vector<GevrDiscordFriend> &gevrDiscordFriends(void);
const std::vector<GevrDiscordInvite> &gevrDiscordInvites(void);
/* Invite a Discord friend into this headset's lobby; with none, once one is up
 * (the launcher hosts a private one for it, as the Friends panel's Invite does). */
void gevrDiscordInvite(uint64_t userId);
void gevrDiscordAnswer(int key, bool join);
#endif
