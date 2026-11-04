/* Friends, presence and invites (docs/friends-and-invites.md); see vr_friends.h.
 *
 * FriendsClient (Java) does the HTTP on its own thread. This side sends it a
 * presence line about once a second when it changes (and every 20 s when it
 * does not, which keeps it posting), drains its events into the model the
 * launcher's Friends page and the watch draw, and turns invites into the
 * launcher's ordinary join by code. */
#ifdef ANDROID
// The launcher's order: the campaign header first, the game's headers (net_*) after SDL.
#include "gevr_romlayout.h"
#include "vr_friends.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <SDL.h>
#include <jni.h>
#include "vr_settings.h"
#include "vr_log.h"
#include "net_core.h"
#include "net_match.h"

extern "C" {
int gevrVrSessionRunning(void);   /* vr_openxr.cpp */
int bossGetStageNum(void);        /* boss.c */
int lvlGetSelectedDifficulty(void);   /* lv.c (a DIFFICULTY) */
void vrEnsurePlayerName(void);        /* vr_settings.cpp */
}

namespace {
const char *kPendingJoinFile = "gevr_pending_join.txt";   // beside goldeneye-vr.ini
const uint32_t kNoticeMs = 8000;

GevrFriendsModel s_model, s_staging;
bool s_staging_open = false;
std::vector<std::string> s_seenInvites;   // invite ids already announced
std::vector<std::string> s_inviteQueue;   // friends to invite once a lobby is up
bool s_hostRequest = false;
std::string s_joinCode;
std::string s_notice;                     // in-mission notice, game font
uint32_t s_noticeUntil = 0;
std::string s_promptLine;
std::string s_lastLobby;                  // the lobby the "invited" marks are for

std::string javaString(const char *method) {
    std::string out;
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return out;
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, method, "()Ljava/lang/String;");
    if (m) {
        jstring s = (jstring)env->CallObjectMethod(activity, m);
        if (!env->ExceptionCheck() && s) {
            if (const char *c = env->GetStringUTFChars(s, nullptr)) {
                out = c;
                env->ReleaseStringUTFChars(s, c);
            }
        }
        if (s) env->DeleteLocalRef(s);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return out;
}

void javaCommand(const std::string &command) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, "friendsCommand", "(Ljava/lang/String;)V");
    if (m) {
        jstring s = env->NewStringUTF(command.c_str());
        env->CallVoidMethod(activity, m, s);
        env->DeleteLocalRef(s);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}

std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t bar = line.find('|', start);
        out.push_back(line.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) return out;
        start = bar + 1;
    }
}

// Text that goes into a '|'-separated command line.
std::string field(const std::string &s) {
    std::string out = s;
    for (char &c : out)
        if (c == '|' || c == '\n' || c == '\r' || c == '\t') c = ' ';
    return out;
}

int num(const std::string &s) { return atoi(s.c_str()); }

void setMessage(const std::string &text) {
    s_model.message = text;
    s_model.messageMs = SDL_GetTicks();
}

// The game's HUD font: capitals, digits and a little punctuation.
std::string hudText(const std::string &s) {
    std::string out;
    for (unsigned char c : s) {
        c = (unsigned char)toupper(c);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(" .,'!?:/()-", c)) out.push_back((char)c);
    }
    return out;
}

std::string inviteWhat(const GevrFriendInvite &invite) {
    char text[96];
    if (invite.maxPlayers > 0) snprintf(text, sizeof(text), "%s (%d/%d)", invite.lobbyName.c_str(), invite.players, invite.maxPlayers);
    else snprintf(text, sizeof(text), "%s", invite.lobbyName.c_str());
    return text;
}

void handle(const std::vector<std::string> &f, bool inGame) {
    const std::string &kind = f[0];
    const uint32_t now = SDL_GetTicks();
    if (kind == "ME" && f.size() >= 6) {
        s_model.status = f[1];
        s_model.friendCode = f[2];
        s_model.restoreCode = f[3];
        s_model.showDetail = f[4] == "1";
        s_model.appearOffline = f[5] == "1";
        s_staging.status = s_model.status;
    } else if (kind == "FRIENDS_BEGIN") {
        s_staging = GevrFriendsModel();
        s_staging_open = true;
    } else if (kind == "FRIEND" && f.size() >= 11 && s_staging_open) {
        GevrFriend x;
        x.id = f[1]; x.name = f[2]; x.code = f[3]; x.state = f[4]; x.version = num(f[5]);
        x.lobby = f[6]; x.players = num(f[7]); x.maxPlayers = num(f[8]); x.lobbyVersion = num(f[9]); x.detail = f[10];
        s_staging.friends.push_back(x);
    } else if ((kind == "REQUEST" || kind == "SENT" || kind == "BLOCKED") && f.size() >= 4 && s_staging_open) {
        GevrFriendPerson p{f[1], f[2], f[3]};
        (kind == "REQUEST" ? s_staging.requests : kind == "SENT" ? s_staging.sent : s_staging.blocked).push_back(p);
    } else if (kind == "INVITE" && f.size() >= 12 && s_staging_open) {
        GevrFriendInvite x;
        x.id = f[1]; x.from = f[2]; x.name = f[3]; x.code = f[4]; x.lobby = f[5]; x.stage = num(f[6]);
        x.players = num(f[7]); x.maxPlayers = num(f[8]); x.lobbyVersion = num(f[9]);
        x.expiresAtMs = now + (uint32_t)std::max(0, num(f[10])) * 1000u; x.lobbyName = f[11];
        s_staging.invites.push_back(x);
    } else if (kind == "FRIENDS_END" && s_staging_open) {
        s_staging_open = false;
        // The model's own fields outlive a snapshot.
        s_staging.status = s_model.status;
        s_staging.friendCode = s_model.friendCode;
        s_staging.restoreCode = s_model.restoreCode;
        s_staging.showDetail = s_model.showDetail;
        s_staging.appearOffline = s_model.appearOffline;
        s_staging.invited = s_model.invited;
        s_staging.message = s_model.message;
        s_staging.messageMs = s_model.messageMs;
        s_staging.loaded = true;
        s_model = s_staging;
        for (const GevrFriendInvite &invite : s_model.invites) {
            if (std::find(s_seenInvites.begin(), s_seenInvites.end(), invite.id) != s_seenInvites.end()) continue;
            s_seenInvites.push_back(invite.id);
            vr_log("friends: invite from %s to lobby %s", invite.name.c_str(), invite.lobby.c_str());
            if (inGame) {
                s_notice = hudText(invite.name + " invites you to " + inviteWhat(invite)) + "\nHOLD MENU TO ANSWER";
                s_noticeUntil = now + kNoticeMs;
            }
        }
        if (s_seenInvites.size() > 64) s_seenInvites.erase(s_seenInvites.begin(), s_seenInvites.begin() + 32);
    } else if (kind == "INVITED" && f.size() >= 2) {
        if (std::find(s_model.invited.begin(), s_model.invited.end(), f[1]) == s_model.invited.end())
            s_model.invited.push_back(f[1]);
        for (const GevrFriend &x : s_model.friends)
            if (x.id == f[1]) setMessage("Invited " + x.name);
    } else if ((kind == "NOTICE" || kind == "ERROR") && f.size() >= 2) {
        setMessage(f[1]);
    }
}

// The line FriendsClient posts: what this headset is doing.
std::string presenceLine(bool inGame) {
    const bool online = netIsActive();
    const char *state = inGame ? (online ? "match" : "solo") : (online ? "lobby" : "launcher");
    std::string detail;
    if (VrFriendsShowDetail) {
        char text[96] = "";
        if (online) {
            const NetMatchConfig *cfg = netGetMatchConfig();
            // the people, as the lobby service counts them: a joiner takes a bot's place
            const int players = netGetHumanPlayerCount(), max = netGetMaxPlayers();
            if (cfg->mode == NET_MODE_COOP)
                snprintf(text, sizeof(text), "Co-op, %s, %d/%d", inGame ? netCoopStageName((uint8_t)bossGetStageNum()) : "lobby", players, max);
            else if (cfg->mode == NET_MODE_HORDE)
                snprintf(text, sizeof(text), "Horde, %s, %d/%d", netHordeStageName(cfg->stage), players, max);
            else
                snprintf(text, sizeof(text), "%s, %s, %d/%d", cfg->scenario ? netScenarioName(cfg->scenario) : "Deathmatch",
                         netStageName(netStageIndexOf(cfg->stage)), players, max);
        } else if (inGame) {
            if (VrCampaign == GEVR_CAMPAIGN_GF64) snprintf(text, sizeof(text), "Goldfinger 64");
            else if (VrCampaign == GEVR_CAMPAIGN_TND64) snprintf(text, sizeof(text), "Tomorrow Never Dies 64");
            else {
                const int stage = bossGetStageNum();
                const char *mission = netCoopStageName((uint8_t)stage);
                if (!mission || !mission[0]) snprintf(text, sizeof(text), "Campaign");
                else if (strcmp(mission, "in the menus") == 0) snprintf(text, sizeof(text), "In the menus");
                else snprintf(text, sizeof(text), "%s, %s", mission, netDifficultyName(lvlGetSelectedDifficulty()));
            }
        }
        detail = text;
    }
    const char *lobby = online ? netGetLobbyCode() : "";
    return std::string("presence|") + state + "|" + field(lobby) + "|" + std::to_string(GEVR_NET_VERSION) + "|" +
           field(VrPlayerName) + "|" + field(detail);
}

std::string pendingJoinPath() { return kPendingJoinFile; }
}  // namespace

const GevrFriendsModel &gevrFriends(void) { return s_model; }

void gevrFriendsSend(const std::string &command) {
    // never the restore code: logs go out in debug reports
    vr_log("friends: %s", command.substr(0, command.find('|')).c_str());
    javaCommand(command);
}

std::string gevrFriendCodeText(const std::string &code) { return code.empty() ? std::string("GE-......") : "GE-" + code; }

void gevrFriendsInvite(const std::string &friendId) {
    const char *lobby = netIsActive() ? netGetLobbyCode() : "";
    if (lobby[0]) {
        gevrFriendsSend("invite|" + friendId + "|" + lobby);
        return;
    }
    if (netIsActive()) {
        // a LAN or direct-IP game has no code to send
        setMessage("This game has no internet lobby to invite into.");
        return;
    }
    if (std::find(s_inviteQueue.begin(), s_inviteQueue.end(), friendId) == s_inviteQueue.end())
        s_inviteQueue.push_back(friendId);
    s_hostRequest = true;
    setMessage("Starting a private lobby to invite into...");
}

bool gevrFriendsTakeHostRequest(void) {
    const bool want = s_hostRequest;
    s_hostRequest = false;
    return want;
}

void gevrFriendsJoin(const std::string &lobbyCode) { s_joinCode = lobbyCode; }
bool gevrFriendsJoinPending(void) { return !s_joinCode.empty(); }
std::string gevrFriendsTakeJoin(void) {
    std::string code;
    code.swap(s_joinCode);
    return code;
}

void gevrFriendsAnswerInvite(const GevrFriendInvite &invite, bool join) {
    gevrFriendsSend("drop|" + invite.id);
    auto &list = s_model.invites;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const GevrFriendInvite &x) { return x.id == invite.id; }), list.end());
    if (join) gevrFriendsJoin(invite.lobby);
}

void gevrFriendsLoadPendingJoin(void) {
    FILE *f = fopen(pendingJoinPath().c_str(), "r");
    if (!f) return;
    char code[16] = "";
    long long written = 0;
    const int n = fscanf(f, "%15s %lld", code, &written);
    fclose(f);
    remove(pendingJoinPath().c_str());
    // A stale file (a crash before the restart) is not a join the player asked for now.
    if (n == 2 && strlen(code) == 8 && (long long)time(nullptr) - written < 300) {
        vr_log("friends: joining lobby %s from an invite accepted in a mission", code);
        s_joinCode = code;
    }
}

extern "C" void gevrFriendsTick(int inGame) {
    static uint32_t lastDrain = 0, lastCompose = 0, lastSent = 0;
    static std::string lastLine;
    static bool settingsSent = false;
    const uint32_t now = SDL_GetTicks();
    // a few times a second: each look crosses into Java
    if (now - lastDrain < 200 && lastDrain) return;
    lastDrain = now;
    for (int i = 0; i < 600; ++i) {
        const std::string event = javaString("friendsEvent");
        if (event.empty()) break;
        handle(split(event), inGame != 0);
    }
    if (now - lastCompose < 1000 && lastCompose) return;
    lastCompose = now;
    if (!gevrVrSessionRunning()) return;
    vrEnsurePlayerName();   // the friends list needs a name before the Multiplayer page made one
    if (!settingsSent) {
        settingsSent = true;
        gevrFriendsSend(std::string("settings|") + (VrFriendsShowDetail ? "1" : "0") + "|" + (VrFriendsAppearOffline ? "1" : "0"));
    }
    const std::string line = presenceLine(inGame != 0);
    if (line != lastLine || now - lastSent >= 20000 || !lastSent) {
        lastLine = line;
        lastSent = now;
        javaCommand(line);
    }
    // The lobby the queued invites wait for, and the marks for the last one.
    const std::string lobby = netIsActive() ? netGetLobbyCode() : "";
    if (lobby != s_lastLobby) {
        s_lastLobby = lobby;
        s_model.invited.clear();
    }
    if (!lobby.empty() && !s_inviteQueue.empty()) {
        for (const std::string &id : s_inviteQueue) gevrFriendsSend("invite|" + id + "|" + lobby);
        s_inviteQueue.clear();
    }
    if (!netIsActive() && !s_hostRequest) s_inviteQueue.clear();   // the hosting never started
    // the menu-hold prompt's line: the newest invite still open
    s_promptLine.clear();
    if (inGame) {
        for (const GevrFriendInvite &invite : s_model.invites) {
            if (invite.lobbyVersion != GEVR_NET_VERSION || (int32_t)(invite.expiresAtMs - now) <= 0) continue;
            s_promptLine = hudText("Y: join " + invite.name + "'s game");
            break;
        }
    }
}

extern "C" const char *gevrFriendsNotice(void) {
    return !s_notice.empty() && (int32_t)(s_noticeUntil - SDL_GetTicks()) > 0 ? s_notice.c_str() : nullptr;
}

extern "C" const char *gevrFriendsPromptLine(void) { return s_promptLine.empty() ? nullptr : s_promptLine.c_str(); }

// Keeps the invite's lobby for the launcher the app restarts into (gevrFriendsLoadPendingJoin).
static bool acceptInvite(const GevrFriendInvite &invite) {
    FILE *f = fopen(pendingJoinPath().c_str(), "w");
    if (!f) return false;
    fprintf(f, "%s %lld\n", invite.lobby.c_str(), (long long)time(nullptr));
    fclose(f);
    vr_log("friends: accepted %s's invite to %s; back to the launcher", invite.name.c_str(), invite.lobby.c_str());
    gevrFriendsSend("drop|" + invite.id);
    return true;
}

static bool inviteOpen(const GevrFriendInvite &invite, uint32_t now) {
    return invite.lobbyVersion == GEVR_NET_VERSION && (int32_t)(invite.expiresAtMs - now) > 0;
}

bool gevrFriendsAcceptInviteId(const std::string &id) {
    for (const GevrFriendInvite &invite : s_model.invites)
        if (invite.id == id && inviteOpen(invite, SDL_GetTicks())) return acceptInvite(invite);
    return false;
}

extern "C" int gevrFriendsAcceptInvite(void) {
    for (const GevrFriendInvite &invite : s_model.invites)
        if (inviteOpen(invite, SDL_GetTicks())) return acceptInvite(invite) ? 1 : 0;
    return 0;
}
#endif
