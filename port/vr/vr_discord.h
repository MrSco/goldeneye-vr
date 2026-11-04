/* Discord (docs/friends-and-invites.md, phase 3): Link Discord, Rich Presence
 * and Discord friends who also play, through Discord's Social SDK. Built only
 * when the SDK's Android library is present (GEVR_DISCORD: android/app/build.gradle
 * looks for build/discord/discord_partner_sdk.aar); otherwise every call here
 * does nothing and gevrDiscordState() is GEVR_DISCORD_UNAVAILABLE.
 *
 * The app is a confidential client: Client::Authorize's code (with PKCE) goes to
 * the lobby Worker (FriendsClient "discord-link"), which exchanges it with the
 * secret and answers with the tokens; this side only ever holds a user token. */
#ifndef VR_DISCORD_H
#define VR_DISCORD_H
#include <string>

enum {
    GEVR_DISCORD_UNAVAILABLE = -1,   // no SDK in this build
    GEVR_DISCORD_UNLINKED = 0,
    GEVR_DISCORD_SIGNING_IN = 1,     // the Discord sign-in page is open
    GEVR_DISCORD_CONNECTING = 2,     // linked; connecting with the token
    GEVR_DISCORD_CONNECTED = 3,
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
/* What friends see, shown in Discord too (cleared while appearing offline). */
void gevrDiscordPresence(const std::string &state, const std::string &detail, int players, int maxPlayers,
                         const std::string &lobby, bool offline);
#endif
