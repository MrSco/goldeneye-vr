/* Discord's Social SDK for Link Discord, Rich Presence and Discord friends;
 * see vr_discord.h. Without the SDK (no GEVR_DISCORD) only the stubs at the
 * end are built. */
#include "vr_discord.h"
#include "vr_friends.h"
#include "vr_log.h"

#if defined(ANDROID) && defined(GEVR_DISCORD)
#define DISCORDPP_IMPLEMENTATION
#include "discordpp.h"
#include <SDL.h>
#include <memory>
#include <optional>

namespace {
const uint64_t kApplicationId = 1567606949862711468ULL;   // the GoldenEye VR Discord application
std::shared_ptr<discordpp::Client> s_client;
std::optional<discordpp::AuthorizationCodeVerifier> s_verifier;
int s_state = GEVR_DISCORD_UNLINKED;
bool s_linked = false;              // FriendsClient holds a refresh token
bool s_refreshAsked = false;
bool s_friendsDirty = true;
uint32_t s_friendsSentMs = 0;
std::string s_name;
std::string s_presenceKey;          // the presence last sent, so an unchanged one is not sent again

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
}

// The linked players among the user's Discord friends become friends here
// (the Worker pairs them only when both clients report each other).
void sendFriends() {
    std::string ids;
    for (const discordpp::RelationshipHandle &r : s_client->GetRelationships()) {
        if (r.DiscordRelationshipType() != discordpp::RelationshipType::Friend) continue;
        if (!ids.empty()) ids += ',';
        ids += std::to_string(r.Id());
    }
    gevrFriendsSend("discord-friends|" + ids);
    s_friendsDirty = false;
    s_friendsSentMs = SDL_GetTicks();
}
}  // namespace

int gevrDiscordState(void) { return s_state; }
const char *gevrDiscordName(void) { return s_name.c_str(); }

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
}

void gevrDiscordTick(void) {
    if (!s_client && !s_linked) return;   // nothing to run until a link or a sign-in
    ensureClient();
    discordpp::RunCallbacks();
    if (s_linked && !s_refreshAsked) {
        s_refreshAsked = true;            // a saved link: a fresh token for this run
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
    s_state = GEVR_DISCORD_CONNECTING;
    s_client->UpdateToken(discordpp::AuthorizationTokenType::Bearer, accessToken, [](discordpp::ClientResult result) {
        if (result.Successful()) s_client->Connect();
        else vr_log("discord: token refused: %s", result.Error().c_str());
    });
}

void gevrDiscordPresence(const std::string &state, const std::string &detail, int players, int maxPlayers,
                         const std::string &lobby, bool offline) {
    if (s_state != GEVR_DISCORD_CONNECTED) return;
    const std::string key = state + "|" + detail + "|" + std::to_string(players) + "/" + std::to_string(maxPlayers) +
                            "|" + lobby + "|" + (offline ? "1" : "0");
    if (key == s_presenceKey) return;
    s_presenceKey = key;
    if (offline) {
        s_client->ClearRichPresence();
        return;
    }
    discordpp::Activity activity;
    activity.SetType(discordpp::ActivityTypes::Playing);
    activity.SetState(state == "lobby" ? "In a lobby" : state == "match" ? "In an online match"
                      : state == "solo" ? "Playing solo" : "In the launcher");
    if (!detail.empty()) activity.SetDetails(detail);
    if (!lobby.empty() && maxPlayers > 0) {
        discordpp::ActivityParty party;
        party.SetId("gevr-" + lobby);
        party.SetCurrentSize(players);
        party.SetMaxSize(maxPlayers);
        activity.SetParty(party);
    }
    s_client->UpdateRichPresence(std::move(activity), [](discordpp::ClientResult result) {
        if (!result.Successful()) vr_log("discord: presence refused: %s", result.Error().c_str());
    });
}

#else
int gevrDiscordState(void) { return GEVR_DISCORD_UNAVAILABLE; }
const char *gevrDiscordName(void) { return ""; }
void gevrDiscordLink(void) {}
void gevrDiscordUnlink(void) {}
void gevrDiscordTick(void) {}
void gevrDiscordOnLinked(bool, const std::string &) {}
void gevrDiscordOnToken(const std::string &) {}
void gevrDiscordPresence(const std::string &, const std::string &, int, int, const std::string &, bool) {}
#endif
