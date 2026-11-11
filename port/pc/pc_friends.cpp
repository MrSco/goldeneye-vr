/*
 * Friends, presence and invites on PC (docs/friends-and-invites.md,
 * docs/pc-platforms.md P3): a port of the Quest's
 * android/app/src/main/java/com/gevr/port/FriendsClient.java.
 *
 * port/vr/vr_friends.cpp sends it one-line commands (friendsCommand) and drains
 * one-line events (friendsEvent) through pc_services.cpp, as with LobbyClient:
 *
 *   presence|state|lobby|version|name|detail   settings|showDetail|appearOffline[|searchable]
 *   search|text  refresh  add|code  answer|id|1/0  remove|id  decline-all  block|id
 *   unblock|id  invite|id|lobby  drop|id  restore|friendCode|restoreCode  reset
 *   discord-link|code|verifier|redirect  discord-refresh  discord-unlink  discord-friends|ids
 *
 *   ME|status|friendCode|restoreCode|showDetail|appearOffline|searchable
 *   FRIENDS_BEGIN, FRIEND|..., REQUEST|..., SENT|..., BLOCKED|..., INVITE|..., FRIENDS_END
 *   SEARCH_BEGIN|text, SEARCH|id|name|code|relation, SEARCH_END
 *   NOTICE|text  INVITED|id  DISCORD|linked/unlinked|name  DISCORD_TOKEN|token  ERROR|text
 *
 * The player is kept in gevr_player.json beside the exe (the Quest's
 * files/data/gevr_player.json; the PC's data folder is the exe's). Same worker
 * model: one thread runs the commands in order and polls every second; nothing
 * is sent until vr_friends.cpp has sent a presence line (it carries the name),
 * and presence stops a minute after the last one. The Discord commands are
 * here as in Java, though the PC never sends them (vr_discord.cpp has no SDK on
 * PC, so linking stays unavailable).
 *
 * The service is lobbies.goldeneyevr.com/v1, or GEVR_FRIENDS_URL (tests point
 * it at a local mock).
 */

#include <string>
#include <deque>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pc_http.h"

extern "C" void sysLogPrintf(int level, const char *fmt, ...);

using namespace pcnet;

namespace {

const long long kKeepaliveMs = 60000;
const long long kPresenceMs = 30000;
const long long kMinGapMs = 5000;   // a changed state goes out at once, but never closer than this to the last send
const long long kRetryMs = 60000;
const size_t kMaxEvents = 4000;

/* org.json's getString: a missing field is an error */
std::string need(jspan obj, const char *key)
{
    jspan v;
    std::string s;
    if (!jsonGet(obj, key, &v) || jsonIsNull(v)) throw HttpError{0, std::string("JSONObject[\"") + key + "\"] not found."};
    if (!jsonStr(v, &s)) s.assign(v.p, v.e);   // a number read as text, as getString does
    return s;
}

/* optString / optInt */
std::string opt(jspan obj, const char *key)
{
    jspan v;
    std::string s;
    if (!jsonGet(obj, key, &v) || jsonIsNull(v)) return std::string();
    if (!jsonStr(v, &s)) s.assign(v.p, v.e);
    return s;
}

std::string optInt(jspan obj, const char *key) { return std::to_string(jsonGetInt(obj, key, 0)); }

std::string b(bool v) { return v ? "true" : "false"; }

class FriendsClient {
public:
    explicit FriendsClient(const std::string &file)
        : file_(file), base_(envOr("GEVR_FRIENDS_URL", "https://lobbies.goldeneyevr.com/v1"))
    {
        if (base_ != "https://lobbies.goldeneyevr.com/v1") sysLogPrintf(0, "friends: service %s", base_.c_str());
        worker_ = std::thread(&FriendsClient::run, this);
        worker_.detach();   // lives with the process
    }

    std::string event()
    {
        std::lock_guard<std::mutex> lock(eventsLock_);
        if (events_.empty()) return std::string();
        std::string e = events_.front();
        events_.pop_front();
        return e;
    }

    void command(const std::string &raw)
    {
        std::lock_guard<std::mutex> lock(cmdLock_);
        commands_.push_back(raw);
        cmdWake_.notify_one();
    }

private:
    /* scheduleWithFixedDelay(poll, 1, 1, SECONDS), with the commands on the same thread */
    void run()
    {
        load();
        long long nextPoll = nowMs() + 1000;
        for (;;) {
            std::string cmd;
            bool have = false;
            {
                std::unique_lock<std::mutex> lock(cmdLock_);
                cmdWake_.wait_for(lock, std::chrono::milliseconds(std::max(0LL, nextPoll - nowMs())),
                                  [this] { return !commands_.empty(); });
                if (!commands_.empty()) {
                    cmd = commands_.front();
                    commands_.pop_front();
                    have = true;
                }
            }
            if (have) {
                try { execute(cmd); }
                catch (const HttpError &e) {
                    if (e.status > 0) failed(e);
                    else error("Friends: " + e.message);
                } catch (...) { error("Friends: failed"); }
                continue;
            }
            if (nowMs() >= nextPoll) {
                poll();
                nextPoll = nowMs() + 1000;
            }
        }
    }

    void emit(const std::string &event)
    {
        std::lock_guard<std::mutex> lock(eventsLock_);
        if (events_.size() > kMaxEvents) events_.clear();   // nobody is draining: start over
        events_.push_back(event);
    }

    std::string http(const char *method, const std::string &path, const std::string *body, const std::string &token)
    {
        return httpJson(method, base_ + path, body, token);
    }

    void execute(const std::string &raw)
    {
        const std::vector<std::string> f = splitBar(raw);
        const long long now = nowMs();
        const std::string &c = f[0];
        if (c == "presence") {
            // presence|state|lobby|version|name|detail (detail last: it may hold anything but '|')
            if (f.size() < 6) return;
            const bool changed = f[1] != state_ || f[2] != lobby_;
            const bool first = name_.empty();
            state_ = f[1];
            lobby_ = f[2];
            version_ = atoi(f[3].c_str());
            name_ = f[4];
            detail_ = f[5];
            lastKeepalive_ = now;
            if (changed || first) nextPresence_ = std::min(nextPresence_, std::max(now, lastSent_ + kMinGapMs));
        } else if (c == "settings") {
            // settings|showDetail|appearOffline[|searchable]
            if (f.size() != 3 && f.size() != 4) return;
            showDetail_ = f[1] == "1";
            appearOffline_ = f[2] == "1";
            if (f.size() == 4) searchable_ = f[3] == "1";
            settingsDirty_ = true;
            if (status_ == "ready") putSettings();
        } else if (c == "search") {
            // search|text: players by name (vr_friends.cpp shows SEARCH lines between BEGIN and END)
            if (f.size() != 2 || !ready()) return;
            emit("SEARCH_BEGIN|" + clean(f[1]));
            try {
                const std::string body = "{\"q\":" + q(f[1]) + "}";
                const std::string reply = http("POST", "/me/search", &body, token());
                jspan obj, results, x;
                jsonParse(reply, &obj);
                if (jsonGet(obj, "results", &results)) {
                    for (int i = 0; jsonAt(results, i, &x); i++) {
                        emit("SEARCH|" + need(x, "id") + "|" + clean(need(x, "name")) + "|" + need(x, "code") + "|"
                             + (opt(x, "relation").empty() ? std::string("none") : opt(x, "relation")));
                    }
                }
            } catch (...) {
                emit("SEARCH_END");
                throw;
            }
            emit("SEARCH_END");
        } else if (c == "refresh") {
            nextPresence_ = std::max(now, lastSent_ + 1000);
        } else if (c == "add") {
            if (f.size() != 2 || !ready()) return;
            const std::string body = "{\"code\":" + q(f[1]) + "}";
            jspan r;
            const std::string reply = http("POST", "/me/friends", &body, token());
            jsonParse(reply, &r);
            emit(std::string("NOTICE|") + (opt(r, "status") == "friends" ? "You are now friends" : "Friend request sent"));
            soon();
        } else if (c == "answer") {
            if (f.size() != 3 || !ready()) return;
            const std::string body = "{\"accept\":" + b(f[2] == "1") + "}";
            http("PUT", "/me/friends/" + f[1], &body, token());
            soon();
        } else if (c == "remove") {
            if (f.size() != 2 || !ready()) return;
            http("DELETE", "/me/friends/" + f[1], nullptr, token());
            soon();
        } else if (c == "decline-all") {
            if (!ready()) return;
            http("DELETE", "/me/requests", nullptr, token());
            soon();
        } else if (c == "block") {
            if (f.size() != 2 || !ready()) return;
            const std::string body = "{\"id\":" + q(f[1]) + "}";
            http("POST", "/me/blocks", &body, token());
            soon();
        } else if (c == "unblock") {
            if (f.size() != 2 || !ready()) return;
            http("DELETE", "/me/blocks/" + f[1], nullptr, token());
            soon();
        } else if (c == "invite") {
            // invite|playerId|lobbyCode
            if (f.size() != 3 || !ready()) return;
            const std::string body = "{\"to\":" + q(f[1]) + ",\"lobby\":" + q(f[2]) + "}";
            http("POST", "/me/invites", &body, token());
            emit("INVITED|" + f[1]);
        } else if (c == "drop") {
            if (f.size() != 2 || !ready()) return;
            http("DELETE", "/me/invites/" + f[1], nullptr, token());
            soon();
        } else if (c == "restore") {
            // restore|friendCode|restoreCode
            if (f.size() != 3) return;
            const std::string body = "{\"friendCode\":" + q(f[1]) + ",\"restoreCode\":" + q(f[2]) + "}";
            const std::string reply = http("POST", "/players/restore", &body, "");
            jspan r;
            jsonParse(reply, &r);
            id_ = need(r, "id");
            secret_ = need(r, "secret");
            friendCode_ = need(r, "friendCode");
            std::string code;
            for (char ch : upper(f[2])) if (ch != '-' && ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f') code += ch;
            restoreCode_ = code;
            status_ = "ready";
            save();
            settingsDirty_ = true;
            emitMe();
            emit("NOTICE|Restored GE-" + friendCode_);
            nextPresence_ = 0;
        } else if (c == "discord-link") {
            // discord-link|code|verifier|redirectUri: Client::Authorize's answer, exchanged by the Worker
            if (f.size() != 4) return;
            const std::string body = "{\"code\":" + q(f[1]) + ",\"codeVerifier\":" + q(f[2]) + ",\"redirectUri\":" + q(f[3]) + "}";
            const std::string reply = http("POST", "/discord/link", &body, ready() ? token() : "");
            jspan r, identity;
            jsonParse(reply, &r);
            const bool hasIdentity = jsonGet(r, "identity", &identity) && !jsonIsNull(identity);
            if (hasIdentity) {
                // this Discord account was another player's: that player comes back, as with a restore code
                id_ = need(identity, "id");
                secret_ = need(identity, "secret");
                friendCode_ = need(identity, "friendCode");
                restoreCode_.clear();   // the one written down still works; this PC never saw it
                status_ = "ready";
                settingsDirty_ = true;
                emitMe();
                emit("NOTICE|Discord brought back GE-" + friendCode_);
            }
            discordRefresh_ = opt(r, "refreshToken");
            discordName_ = clean(opt(r, "discordName"));
            save();
            emit("DISCORD|linked|" + discordName_);
            emit("DISCORD_TOKEN|" + need(r, "accessToken"));
            if (!hasIdentity) emit("NOTICE|Discord linked as " + discordName_);
            nextPresence_ = 0;
        } else if (c == "discord-refresh") {
            if (discordRefresh_.empty()) return;
            std::string reply;
            try {
                const std::string body = "{\"refreshToken\":" + q(discordRefresh_) + "}";
                reply = http("POST", "/discord/refresh", &body, "");
            } catch (const HttpError &e) {
                if (e.status != 401) throw;
                discordRefresh_.clear();   // Discord no longer takes it: link again
                save();
                emit("DISCORD|unlinked|");
                emit("NOTICE|Discord sign-in expired; link it again");
                return;
            }
            jspan r;
            jsonParse(reply, &r);
            const std::string refresh = opt(r, "refreshToken");
            if (!refresh.empty()) discordRefresh_ = refresh;
            save();
            emit("DISCORD_TOKEN|" + need(r, "accessToken"));
        } else if (c == "discord-unlink") {
            // forget the link here first: offline, the DELETE fails and must not leave it saved
            discordRefresh_.clear();
            discordName_.clear();
            save();
            emit("DISCORD|unlinked|");
            if (ready()) http("DELETE", "/me/discord", nullptr, token());
            soon();
        } else if (c == "discord-friends") {
            // discord-friends|id,id,...: the user's Discord friends, from the SDK
            if (f.size() != 2 || !ready() || discordRefresh_.empty()) return;
            std::string ids;
            size_t start = 0;
            while (start <= f[1].size()) {
                size_t comma = f[1].find(',', start);
                if (comma == std::string::npos) comma = f[1].size();
                if (comma > start) ids += (ids.empty() ? "" : ",") + q(f[1].substr(start, comma - start));
                start = comma + 1;
            }
            const std::string body = "{\"ids\":[" + ids + "]}";
            http("PUT", "/me/discord/friends", &body, token());
            soon();
        } else if (c == "ghost-board" || c == "ghost-upload" || c == "ghost-get") {
            ghost(f);
        } else if (c == "reset") {
            // Start over with a new friend code (after a sign-out, or by choice).
            id_.clear(); secret_.clear(); friendCode_.clear(); restoreCode_.clear();
            discordRefresh_.clear(); discordName_.clear();
            status_ = "none";
            nextRegister_ = 0;
            if (isFile(file_) && !removeFile(file_)) sysLogPrintf(1, "friends: could not delete %s", file_.c_str());
            emitMe();
            emit("FRIENDS_BEGIN");
            emit("FRIENDS_END");
        }
    }

    /* The ghost boards (the Worker's ghosts.ts; the launcher's Ghosts tab,
     * vr_ghosts.cpp), as FriendsClient.java: each answers with its own events,
     * a failure included, so the tab is never left waiting. */
    void ghost(const std::vector<std::string> &f)
    {
        const std::string &c = f[0];
        const size_t kFile = 128 + 65536 * 24;   // the longest run gevr_ghost.c records
        const size_t kReply = kFile / 3 * 4 + 65536;
        if (c == "ghost-board") {
            // ghost-board|campaign|stage|difficulty|plays (vr, flat or empty)
            if (f.size() != 5) return;
            const std::string key = f[1] + "|" + f[2] + "|" + f[3] + "|" + f[4];
            try {
                const std::string reply = http("GET", "/ghosts?campaign=" + f[1] + "&stage=" + f[2] + "&difficulty=" + f[3]
                                                   + (f[4].empty() ? "" : "&plays=" + f[4]), nullptr, "");
                jspan obj, entries, x;
                jsonParse(reply, &obj);
                emit("GHOSTS_BEGIN|" + key);
                if (jsonGet(obj, "entries", &entries))
                    for (int i = 0; jsonAt(entries, i, &x); i++)
                        // the name last: it may hold anything but '|'
                        emit("GHOST|" + std::to_string(jsonGetInt(x, "id", 0)) + "|" + std::to_string(jsonGetInt(x, "time60", 0)) + "|"
                             + std::to_string(jsonGetInt(x, "flags", 0)) + "|" + clean(need(x, "code")) + "|" + clean(need(x, "name")));
                emit("GHOSTS_END|" + key);
            } catch (const HttpError &e) {
                emit("GHOST_FAIL|board|" + key + "|" + clean(e.message));
            }
        } else if (c == "ghost-upload") {
            // ghost-upload|path: one of this PC's runs, its owner already this friend code
            if (f.size() != 2) return;
            try {
                if (!ready()) throw HttpError{0, "Friends is still signing in; try again in a moment"};
                std::string bytes;
                if (fileSize(f[1]) > (long long)kFile || !readFile(f[1], &bytes, kFile)) throw HttpError{0, "Could not read the run"};
                const std::string body = "{\"data\":" + q(encodeBase64(bytes)) + "}";
                const std::string reply = httpJson("POST", base_ + "/me/ghosts", &body, token(), 65536, 30000);
                jspan r;
                jsonParse(reply, &r);
                emit("GHOST_UPLOADED|" + f[1] + "|" + (jsonGetBool(r, "stored", false) ? "1" : "0") + "|"
                     + std::to_string(jsonGetInt(r, "rank", 0)) + "|" + clean(opt(r, "reason")));
            } catch (const HttpError &e) {
                if (e.status == 401) failed(e);
                emit("GHOST_FAIL|upload|" + f[1] + "|" + clean(e.message));
            }
        } else if (c == "ghost-get") {
            // ghost-get|id|path: a board's run, written where the race reads it
            if (f.size() != 3) return;
            try {
                const std::string reply = httpJson("GET", base_ + "/ghosts/" + f[1], nullptr, "", kReply, 30000);
                jspan r;
                jsonParse(reply, &r);
                const std::string bytes = decode64(need(r, "data"));
                if (bytes.empty() || bytes.size() > kFile) throw HttpError{0, "Not a ghost file"};
                if (!writeFileAtomic(f[2], bytes)) throw HttpError{0, "Could not save the run"};
                emit("GHOST_SAVED|" + f[1] + "|" + f[2]);
            } catch (const HttpError &e) {
                emit("GHOST_FAIL|get|" + f[1] + "|" + clean(e.message));
            }
        }
    }

    bool ready() const { return status_ == "ready"; }
    std::string token() const { return id_ + "." + secret_; }
    void soon() { nextPresence_ = std::max(nowMs(), lastSent_ + 1000); }

    void poll()
    {
        try {
            const long long now = nowMs();
            if (name_.empty() || now - lastKeepalive_ > kKeepaliveMs) return;
            if (status_ == "none") {
                if (now < nextRegister_) return;
                nextRegister_ = now + kRetryMs;
                registerPlayer();
            }
            if (!ready()) return;
            if (settingsDirty_ && now >= nextSettings_) {
                nextSettings_ = now + kRetryMs;
                putSettings();
            }
            if (now >= nextPresence_) presence(now);
            lastError_.clear();
        } catch (const HttpError &e) {
            if (e.status > 0) {
                failed(e);
                return;
            }
            const std::string message = "Friends: " + e.message;
            if (message != lastError_) error(message);
            lastError_ = message;
            nextPresence_ = nowMs() + kPresenceMs;
        }
    }

    void failed(const HttpError &e)
    {
        if (e.status == 401 && ready()) {
            sysLogPrintf(0, "friends: signed out: this player was restored on another device");
            status_ = "signed_out";
            emitMe();
            return;
        }
        nextPresence_ = nowMs() + (e.status == 429 ? kPresenceMs * 2 : kPresenceMs);
        if (e.message != lastError_) error(e.message);
        lastError_ = e.message;
    }

    void registerPlayer()
    {
        const std::string body = "{\"name\":" + q(name_) + "}";
        const std::string reply = http("POST", "/players", &body, "");
        jspan r;
        jsonParse(reply, &r);
        id_ = need(r, "id");
        secret_ = need(r, "secret");
        friendCode_ = need(r, "friendCode");
        restoreCode_ = need(r, "restoreCode");
        status_ = "ready";
        save();
        sysLogPrintf(0, "friends: registered as GE-%s", friendCode_.c_str());
        settingsDirty_ = !showDetail_ || appearOffline_ || !searchable_;
        nextPresence_ = 0;
        emitMe();
    }

    void putSettings()
    {
        const std::string body = "{\"showDetail\":" + b(showDetail_) + ",\"appearOffline\":" + b(appearOffline_)
            + ",\"searchable\":" + b(searchable_) + "}";
        http("PUT", "/me/settings", &body, token());
        settingsDirty_ = false;
        emitMe();
    }

    void presence(long long now)
    {
        std::string body = "{\"state\":" + q(state_) + ",\"version\":" + std::to_string(version_) + ",\"name\":" + q(name_);
        if (!detail_.empty()) body += ",\"detail\":" + q(detail_);
        if (!lobby_.empty()) body += ",\"lobby\":" + q(lobby_);
        body += "}";
        lastSent_ = now;
        nextPresence_ = now + kPresenceMs;
        const std::string reply = http("POST", "/me/presence", &body, token());
        jspan r, me, list, x;
        jsonParse(reply, &r);
        nextPresence_ = nowMs() + std::max(15LL, jsonGetInt(r, "next", 30)) * 1000LL;
        if (jsonGet(r, "me", &me) && !jsonIsNull(me)) {
            showDetail_ = settingsDirty_ ? showDetail_ : jsonGetBool(me, "showDetail", showDetail_);
            appearOffline_ = settingsDirty_ ? appearOffline_ : jsonGetBool(me, "appearOffline", appearOffline_);
            searchable_ = settingsDirty_ ? searchable_ : jsonGetBool(me, "searchable", searchable_);
            // the Worker keeps a name back after ten renames in a day
            jspan kv;
            std::string kept = name_;
            if (jsonGet(me, "name", &kv) && !jsonIsNull(kv)) kept = opt(me, "name");
            if (kept != name_ && name_ != refusedName_) {
                refusedName_ = name_;
                emit("NOTICE|Names can change ten times a day; friends still see " + clean(kept));
            }
        }
        emitMe();
        emit("FRIENDS_BEGIN");
        if (jsonGet(r, "friends", &list)) {
            for (int i = 0; jsonAt(list, i, &x); i++) {
                emit("FRIEND|" + need(x, "id") + "|" + clean(need(x, "name")) + "|" + need(x, "code") + "|"
                              + need(x, "state") + "|" + optInt(x, "version") + "|" + opt(x, "lobby") + "|"
                              + optInt(x, "players") + "|" + optInt(x, "maxPlayers") + "|" + optInt(x, "lobbyVersion") + "|"
                              + (jsonGetBool(x, "discord", false) ? "1" : "0") + "|" + clean(opt(x, "detail")));
            }
        }
        people(r, "requests", "REQUEST");
        people(r, "sent", "SENT");
        people(r, "blocked", "BLOCKED");
        if (jsonGet(r, "invites", &list)) {
            for (int i = 0; jsonAt(list, i, &x); i++) {
                emit("INVITE|" + need(x, "id") + "|" + need(x, "from") + "|" + clean(need(x, "name")) + "|"
                              + need(x, "code") + "|" + need(x, "lobby") + "|" + optInt(x, "stage") + "|"
                              + optInt(x, "players") + "|" + optInt(x, "maxPlayers") + "|" + optInt(x, "lobbyVersion") + "|"
                              + optInt(x, "expiresIn") + "|" + clean(opt(x, "lobbyName")));
            }
        }
        emit("FRIENDS_END");
    }

    void people(jspan r, const char *key, const char *kind)
    {
        jspan list, x;
        if (!jsonGet(r, key, &list)) return;
        for (int i = 0; jsonAt(list, i, &x); i++) {
            emit(std::string(kind) + "|" + need(x, "id") + "|" + clean(need(x, "name")) + "|" + need(x, "code"));
        }
    }

    std::string meLine() const
    {
        return "ME|" + status_ + "|" + friendCode_ + "|" + restoreCode_ + "|" + (showDetail_ ? "1" : "0") + "|"
            + (appearOffline_ ? "1" : "0") + "|" + (searchable_ ? "1" : "0");
    }

    void emitMe() { emit(meLine()); }

    void error(const std::string &message)
    {
        sysLogPrintf(1, "friends: %s", message.c_str());
        emit("ERROR|" + clean(message));
    }

    void load()
    {
        std::string text;
        if (isFile(file_) && readFile(file_, &text, 4096)) {
            try {
                jspan x;
                if (!jsonParse(text, &x)) throw HttpError{0, "not JSON"};
                id_ = need(x, "id");
                secret_ = need(x, "secret");
                friendCode_ = need(x, "friendCode");
                restoreCode_ = opt(x, "restoreCode");
                discordRefresh_ = opt(x, "discordRefresh");
                discordName_ = opt(x, "discordName");
                status_ = "ready";
            } catch (const HttpError &e) {
                id_.clear(); secret_.clear(); friendCode_.clear();
                sysLogPrintf(1, "friends: unreadable %s; a new player will be registered (%s)", file_.c_str(), e.message.c_str());
            }
        }
        emitMe();
        if (!discordRefresh_.empty()) emit("DISCORD|linked|" + discordName_);   // vr_discord.cpp asks for a fresh token
    }

    /* A new file renamed over the old one, as the EEPROM is saved: a crash mid-write keeps the last good one. */
    void save()
    {
        const std::string text = "{\"id\":" + q(id_) + ",\"secret\":" + q(secret_) + ",\"friendCode\":" + q(friendCode_)
            + ",\"restoreCode\":" + q(restoreCode_) + ",\"discordRefresh\":" + q(discordRefresh_)
            + ",\"discordName\":" + q(discordName_) + "}";
        if (!writeFileAtomic(file_, text)) throw HttpError{0, "Could not save " + file_};
    }

    const std::string file_, base_;
    std::mutex eventsLock_, cmdLock_;
    std::condition_variable cmdWake_;
    std::deque<std::string> events_, commands_;
    // none (registers once a name arrives), ready, signed_out (the secret was replaced by a restore elsewhere)
    std::string status_ = "none";
    std::string id_, secret_, friendCode_, restoreCode_;
    // Discord (vr_discord.cpp): the link's refresh token and the account's name, kept with the player.
    std::string discordRefresh_, discordName_;
    std::string state_ = "launcher", lobby_, name_, detail_;
    int version_ = 0;
    bool showDetail_ = true, appearOffline_ = false, searchable_ = true;
    std::string refusedName_;   // a rename the Worker kept back (ten a day), said once
    bool settingsDirty_ = false;
    long long lastKeepalive_ = 0, lastSent_ = 0, nextPresence_ = 0, nextRegister_ = 0;
    long long nextSettings_ = 0;   // a failed settings PUT waits kRetryMs, not the next poll
    std::string lastError_;
    std::thread worker_;
};

FriendsClient *s_friends;

FriendsClient &friends()
{
    // data/gevr_player.json on the Quest; the PC's data folder is the exe's
    if (!s_friends) s_friends = new FriendsClient("gevr_player.json");   // lives as long as the process
    return *s_friends;
}

}   // namespace

std::string pcFriendsEvent(void) { return friends().event(); }
void pcFriendsCommand(const char *cmd) { friends().command(cmd ? cmd : ""); }
