/* Discord's Social SDK for Link Discord, Rich Presence, Discord friends and
 * Discord invites; see vr_discord.h. Without the SDK (no GEVR_DISCORD) only
 * the stubs at the end are built. */
#include "vr_discord.h"
#include "vr_friends.h"
#include "vr_log.h"

#if defined(ANDROID) && defined(GEVR_DISCORD)
#define DISCORDPP_IMPLEMENTATION
#include "discordpp.h"
#include <SDL.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>

namespace {
const uint64_t kApplicationId = 1567606949862711468ULL;   // the GoldenEye VR Discord application
const char *kSecretPrefix = "gevr1:";                     // a join secret is this and a lobby code
std::shared_ptr<discordpp::Client> s_client;
std::optional<discordpp::AuthorizationCodeVerifier> s_verifier;
int s_state = GEVR_DISCORD_UNLINKED;
bool s_linked = false;              // FriendsClient holds a refresh token
bool s_refreshAsked = false;
bool s_haveToken = false;           // a token arrived this run (a failed refresh is asked again)
uint32_t s_refreshAskedMs = 0;
bool s_friendsDirty = true;
uint32_t s_friendsSentMs = 0;
std::string s_name;
std::string s_presenceKey;          // the presence last sent, so an unchanged one is not sent again
std::string s_secretLobby;          // the lobby whose join secret Discord has (invites can go out)
std::vector<GevrDiscordFriend> s_friends;
std::vector<uint64_t> s_inviteQueue;   // friends to invite once a lobby's secret is out
struct Received { int key; std::string name; discordpp::ActivityInvite invite; };
std::vector<Received> s_received;
std::vector<GevrDiscordInvite> s_receivedView;
int s_nextKey = 1;

std::string userName(uint64_t id) {
    if (auto user = s_client->GetUser(id)) return user->DisplayName();
    return "A Discord friend";
}

void refreshReceivedView() {
    s_receivedView.clear();
    for (const Received &r : s_received) s_receivedView.push_back({r.key, r.name});
}

// The lobby code inside a join secret, or "".
std::string codeOf(const std::string &secret) {
    const size_t n = strlen(kSecretPrefix);
    if (secret.compare(0, n, kSecretPrefix) != 0 || secret.size() != n + 8) return "";
    const std::string code = secret.substr(n);
    for (char c : code)
        if (!((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '9'))) return "";
    return code;
}

// Accepted in Discord or here: the launcher joins it (in a mission, after the panel asks).
void joinFromSecret(const std::string &secret, const std::string &who) {
    const std::string code = codeOf(secret);
    if (code.empty()) {
        gevrFriendsMessage("That Discord invite is not for a GoldenEye VR lobby");
        return;
    }
    vr_log("discord: joining a lobby from Discord");
    gevrFriendsJoin(code);
    gevrFriendsMessage("Joining " + who + "'s game from Discord. In a mission, hold Menu to confirm.");
}

void sendInvite(uint64_t id) {
    s_client->SendActivityInvite(id, "", [id](discordpp::ClientResult result) {
        if (result.Successful()) {
            for (GevrDiscordFriend &f : s_friends)
                if (f.id == id) f.invited = true;
            gevrFriendsMessage("Invited " + userName(id) + " on Discord");
        } else {
            vr_log("discord: invite refused: %s", result.Error().c_str());
            gevrFriendsMessage("Discord did not send that invite: " + result.Error());
        }
    });
}

void ensureClient() {
    if (s_client) return;
    s_client = std::make_shared<discordpp::Client>();
    s_client->SetApplicationId(kApplicationId);
    s_client->SetStatusChangedCallback([](discordpp::Client::Status status, discordpp::Client::Error error, int32_t detail) {
        vr_log("discord: status %d (error %d/%d)", (int)status, (int)error, (int)detail);
        if (status == discordpp::Client::Status::Ready) {
            s_state = GEVR_DISCORD_CONNECTED;
            s_friendsDirty = true;
            s_presenceKey.clear();   // send it again on this connection
            if (auto me = s_client->GetCurrentUserV2()) s_name = me->DisplayName();
        } else if (status == discordpp::Client::Status::Disconnected && s_linked) {
            s_state = GEVR_DISCORD_CONNECTING;
        }
    });
    s_client->SetRelationshipCreatedCallback([](uint64_t, bool) { s_friendsDirty = true; });
    s_client->SetRelationshipDeletedCallback([](uint64_t, bool) { s_friendsDirty = true; });
    // Phase 4: an invite from a Discord friend, and a Join from Discord's own app
    s_client->SetActivityInviteCreatedCallback([](discordpp::ActivityInvite invite) {
        const std::string name = userName(invite.SenderId());
        auto same = [&](const Received &r) { return r.invite.SenderId() == invite.SenderId(); };
        s_received.erase(std::remove_if(s_received.begin(), s_received.end(), same), s_received.end());
        s_received.push_back({s_nextKey++, name, invite});
        if (s_received.size() > 5) s_received.erase(s_received.begin());
        refreshReceivedView();
        vr_log("discord: an invite arrived");
        gevrFriendsMessage(name + " invites you on Discord");
    });
    s_client->SetActivityJoinCallback([](std::string secret) { joinFromSecret(secret, "a friend"); });
}

// Discord friends: to the lobby service (the linked ones among them become
// friends there, when both sides report each other) and to "Invite from Discord".
void sendFriends() {
    std::string ids;
    std::vector<GevrDiscordFriend> list;
    for (const discordpp::RelationshipHandle &r : s_client->GetRelationships()) {
        if (r.DiscordRelationshipType() != discordpp::RelationshipType::Friend) continue;
        if (!ids.empty()) ids += ',';
        ids += std::to_string(r.Id());
        GevrDiscordFriend f;
        f.id = r.Id();
        if (auto user = r.User()) {
            f.name = user->DisplayName();
            f.online = user->Status() != discordpp::StatusType::Offline && user->Status() != discordpp::StatusType::Invisible;
        }
        for (const GevrDiscordFriend &old : s_friends)
            if (old.id == f.id) f.invited = old.invited;
        list.push_back(f);
    }
    std::stable_sort(list.begin(), list.end(), [](const GevrDiscordFriend &a, const GevrDiscordFriend &b) {
        return a.online != b.online ? a.online : a.name < b.name;
    });
    s_friends.swap(list);
    gevrFriendsSend("discord-friends|" + ids);
    s_friendsDirty = false;
    s_friendsSentMs = SDL_GetTicks();
}
}  // namespace

int gevrDiscordState(void) { return s_state; }
const char *gevrDiscordName(void) { return s_name.c_str(); }
const std::vector<GevrDiscordFriend> &gevrDiscordFriends(void) { return s_friends; }
const std::vector<GevrDiscordInvite> &gevrDiscordInvites(void) { return s_receivedView; }

void gevrDiscordLink(void) {
    ensureClient();
    s_verifier = s_client->CreateAuthorizationCodeVerifier();
    discordpp::AuthorizationArgs args;
    args.SetClientId(kApplicationId);
    args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());
    args.SetCodeChallenge(s_verifier->Challenge());
    s_state = GEVR_DISCORD_SIGNING_IN;
    vr_log("discord: sign-in opened");
    s_client->Authorize(args, [](discordpp::ClientResult result, std::string code, std::string redirectUri) {
        if (!result.Successful() || !s_verifier) {
            vr_log("discord: sign-in failed: %s", result.Error().c_str());
            s_state = s_linked ? GEVR_DISCORD_CONNECTING : GEVR_DISCORD_UNLINKED;
            // seen 2026-11-04: an old Discord app on the Quest said "Invalid scope: openid"
            gevrFriendsMessage("Discord sign-in did not finish. If Discord said \"Invalid scope\", update the Discord app "
                               "from the Meta Quest store and try again.");
            return;
        }
        // the Worker exchanges it (the secret stays there) and FriendsClient
        // answers with DISCORD and DISCORD_TOKEN (gevrDiscordOnLinked / OnToken)
        gevrFriendsSend("discord-link|" + code + "|" + s_verifier->Verifier() + "|" + redirectUri);
        s_verifier.reset();
    });
}

void gevrDiscordUnlink(void) {
    gevrFriendsSend("discord-unlink");
    if (s_client) {
        s_client->ClearRichPresence();
        s_client->Disconnect();
    }
    s_linked = false;
    s_state = GEVR_DISCORD_UNLINKED;
    s_name.clear();
    s_friends.clear();
    s_received.clear();
    refreshReceivedView();
}

void gevrDiscordTick(void) {
    if (!s_client && !s_linked) return;   // nothing to run until a link or a sign-in
    ensureClient();
    discordpp::RunCallbacks();
    // a saved link: a fresh token for this run; asked again each minute until one
    // comes (offline at start, or the Worker or Discord briefly down)
    if (s_linked && !s_haveToken && (!s_refreshAsked || SDL_GetTicks() - s_refreshAskedMs > 60u * 1000u)) {
        s_refreshAsked = true;
        s_refreshAskedMs = SDL_GetTicks();
        gevrFriendsSend("discord-refresh");
    }
    if (s_state == GEVR_DISCORD_CONNECTED &&
        (s_friendsDirty || SDL_GetTicks() - s_friendsSentMs > 10u * 60u * 1000u)) sendFriends();
}

void gevrDiscordOnLinked(bool linked, const std::string &name) {
    s_linked = linked;
    if (!name.empty()) s_name = name;
    if (!linked) {
        if (s_client) s_client->Disconnect();
        s_haveToken = false;
        s_state = GEVR_DISCORD_UNLINKED;
        s_name.clear();
    } else if (s_state == GEVR_DISCORD_UNLINKED) {
        s_state = GEVR_DISCORD_CONNECTING;
    }
}

void gevrDiscordOnToken(const std::string &accessToken) {
    ensureClient();
    s_linked = true;
    s_refreshAsked = true;
    s_haveToken = true;
    s_state = GEVR_DISCORD_CONNECTING;
    s_client->UpdateToken(discordpp::AuthorizationTokenType::Bearer, accessToken, [](discordpp::ClientResult result) {
        if (result.Successful()) s_client->Connect();
        else vr_log("discord: token refused: %s", result.Error().c_str());
    });
}

void gevrDiscordPresence(const std::string &state, const std::string &detail, int players, int maxPlayers,
                         const std::string &lobbyCode, bool joinable, bool offline) {
    if (s_state != GEVR_DISCORD_CONNECTED) return;
    const std::string key = state + "|" + detail + "|" + std::to_string(players) + "/" + std::to_string(maxPlayers) +
                            "|" + lobbyCode + "|" + (joinable ? "1" : "0") + "|" + (offline ? "1" : "0");
    if (key == s_presenceKey) return;
    s_presenceKey = key;
    if (offline) {
        s_client->ClearRichPresence();
        s_secretLobby.clear();
        return;
    }
    discordpp::Activity activity;
    activity.SetType(discordpp::ActivityTypes::Playing);
    activity.SetState(state == "lobby" ? "In a lobby" : state == "match" ? "In an online match"
                      : state == "solo" ? "Playing solo" : "In the launcher");
    if (!detail.empty()) activity.SetDetails(detail);
    activity.SetSupportedPlatforms(discordpp::ActivityGamePlatforms::Android);   // a Quest joins a Quest
    if (!lobbyCode.empty() && maxPlayers > 0) {
        discordpp::ActivityParty party;
        // the party id groups the players; it is not the code, which a private lobby keeps to itself
        party.SetId(std::to_string(std::hash<std::string>()(std::string("gevr:") + lobbyCode)));
        party.SetCurrentSize(players);
        party.SetMaxSize(maxPlayers);
        // public and friends-only lobbies: Discord friends may join; private ones by invite
        party.SetPrivacy(joinable ? discordpp::ActivityPartyPrivacy::Public : discordpp::ActivityPartyPrivacy::Private);
        activity.SetParty(party);
        discordpp::ActivitySecrets secrets;
        secrets.SetJoin(kSecretPrefix + lobbyCode);
        activity.SetSecrets(secrets);
    }
    const std::string lobby = lobbyCode;
    s_client->UpdateRichPresence(std::move(activity), [lobby](discordpp::ClientResult result) {
        if (!result.Successful()) {
            vr_log("discord: presence refused: %s", result.Error().c_str());
            return;
        }
        s_secretLobby = lobby;
        if (lobby.empty()) return;
        for (uint64_t id : s_inviteQueue) sendInvite(id);   // invites waiting for this lobby
        s_inviteQueue.clear();
    });
    if (lobbyCode.empty() && !s_secretLobby.empty()) s_secretLobby.clear();
}

void gevrDiscordInvite(uint64_t userId) {
    if (s_state != GEVR_DISCORD_CONNECTED) return;
    if (!s_secretLobby.empty()) {
        sendInvite(userId);
        return;
    }
    // no lobby yet: the Friends panel's Invite path hosts a private one, then this goes out
    if (std::find(s_inviteQueue.begin(), s_inviteQueue.end(), userId) == s_inviteQueue.end()) s_inviteQueue.push_back(userId);
    if (!gevrFriendsRequestLobby()) {
        s_inviteQueue.clear();
        gevrFriendsMessage("Invite from a lobby: host one on the launcher's Multiplayer page.");
    }
}

void gevrDiscordAnswer(int key, bool join) {
    for (auto it = s_received.begin(); it != s_received.end(); ++it) {
        if (it->key != key) continue;
        if (join) {
            const std::string who = it->name;
            s_client->AcceptActivityInvite(it->invite, [who](discordpp::ClientResult result, std::string secret) {
                if (result.Successful()) joinFromSecret(secret, who);
                else gevrFriendsMessage("Discord could not open that invite: " + result.Error());
            });
        }
        s_received.erase(it);
        refreshReceivedView();
        return;
    }
}

#else
int gevrDiscordState(void) { return GEVR_DISCORD_UNAVAILABLE; }
const char *gevrDiscordName(void) { return ""; }
void gevrDiscordLink(void) {}
void gevrDiscordUnlink(void) {}
void gevrDiscordTick(void) {}
void gevrDiscordOnLinked(bool, const std::string &) {}
void gevrDiscordOnToken(const std::string &) {}
void gevrDiscordPresence(const std::string &, const std::string &, int, int, const std::string &, bool, bool) {}
const std::vector<GevrDiscordFriend> &gevrDiscordFriends(void) { static std::vector<GevrDiscordFriend> none; return none; }
const std::vector<GevrDiscordInvite> &gevrDiscordInvites(void) { static std::vector<GevrDiscordInvite> none; return none; }
void gevrDiscordInvite(uint64_t) {}
void gevrDiscordAnswer(int, bool) {}
#endif
