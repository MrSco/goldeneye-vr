/*
 * The internet lobby for the PC game (docs/pc-platforms.md P3): a port of the
 * Quest's android/app/src/main/java/com/gevr/port/LobbyClient.java.
 *
 * The launcher (port/vr/vr_launcher.cpp) sends it one-line commands and reads
 * one-line events, through pc_services.cpp as it reaches Java on the Quest:
 *
 *   create|visibility|name|version|stage|weapons|maxPlayers[|vrOnly]   CREATED|code|ownerToken
 *   resume|code|ownerToken|maxPlayers|phase|players[|name]     CREATED|code|ownerToken
 *   refresh|players|open[|maxPlayers[|vrOnly[|flatPlayers]]]   phase|warmup|players   stop   leave
 *   list|version[|inputClass[|hideFlat]]
 *       LIST_BEGIN, LOBBY|code|name|stage|weapons|players|max|phase|flatPlayers|vrOnly, LIST_END
 *   join|code|version[|inputClass]   JOINED|joinId|user|credential
 * (vrOnly and inputClass: "VR players only"; a flat player neither sees nor joins one.
 *  flatPlayers and hideFlat: a VR player may hide games with flat players in them)
 *   offer|sdp64         answer|joinId|sdp64
 *   (polled)            HOST_PEER|id|offer64|user|credential, ANSWER|joinId|sdp64,
 *                       LOBBY_LOST|code|why, ERROR|message
 *
 * Same worker model: one thread runs the commands in order and polls every
 * 2 s (heartbeat every 15 s, join requests every 4 s while waiting and 10 s
 * in a match, an answer for a sent offer). HTTPS is WinHTTP, as Dab's Mod's
 * ghostnet.c does it; JSON is Dab's bounded span reader (netlobby.c) with
 * strings unescaped in full, since offers and answers are SDP. Both live in
 * pc_http.cpp now, shared with the other services.
 */

#include <string>
#include <deque>
#include <set>
#include <vector>
#include <algorithm>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pc_http.h"

extern "C" void sysLogPrintf(int level, const char *fmt, ...);

using namespace pcnet;   // the JSON, Base64 and HTTPS the PC services share (pc_http.h)

namespace {

const char *const kBase = "https://lobbies.goldeneyevr.com/v1/lobbies";

/* The reply's JSON object; throws HttpError as LobbyClient.http does (status >= 400,
 * the reply's "error" when it has one, status 0 for a network failure). */
#ifdef GEVR_LOBBY_TEST_HTTP   /* port/tests/pc_lobby_native.cpp: a fake service, nothing sent */
std::string GEVR_LOBBY_TEST_HTTP(const char *method, const std::string &url, const std::string *body, const std::string &token);
#endif
std::string http(const char *method, const std::string &url, const std::string *body, const std::string &token)
{
#ifdef GEVR_LOBBY_TEST_HTTP
    return GEVR_LOBBY_TEST_HTTP(method, url, body, token);
#else
    return httpJson(method, url, body, token);
#endif
}

/* ---- LobbyClient ---- */

class LobbyClient {
public:
    LobbyClient() : worker_(&LobbyClient::run, this) {}

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

    /* Wait behind queued commands for the lobby's removal, up to ms (the app is quitting). */
    void stopAndWait(int ms)
    {
        command("stop");
        const long long until = nowMs() + ms;
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(cmdLock_);
                if (commands_.empty() && !busy_) break;
            }
            if (nowMs() >= until) {
                sysLogPrintf(1, "lobby: removal not confirmed within %d ms", ms);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

private:
    void push(const std::string &e)
    {
        std::lock_guard<std::mutex> lock(eventsLock_);
        events_.push_back(e);
    }

    void error(const std::string &message)
    {
        sysLogPrintf(1, "lobby: %s", message.c_str());
        push("ERROR|" + clean(message));
    }

    void run()
    {
        long long nextPoll = nowMs() + 2000;
        for (;;) {
            std::string cmd;
            {
                std::unique_lock<std::mutex> lock(cmdLock_);
                cmdWake_.wait_for(lock, std::chrono::milliseconds(std::max(0LL, nextPoll - nowMs())),
                                  [this] { return !commands_.empty(); });
                if (!commands_.empty()) {
                    cmd = commands_.front();
                    commands_.pop_front();
                    busy_ = true;
                }
            }
            if (!cmd.empty()) {
                try { execute(cmd); }
                catch (const HttpError &e) { error("Lobby service: " + e.message); }
                catch (...) { error("Lobby service: failed"); }
                std::lock_guard<std::mutex> lock(cmdLock_);
                busy_ = false;
                continue;
            }
            if (nowMs() >= nextPoll) {
                poll();
                nextPoll = nowMs() + 2000;
            }
        }
    }

    void execute(const std::string &raw)
    {
        const std::vector<std::string> f = splitBar(raw);
        const std::string &c = f[0];
        if (c == "create") {
            if (f.size() != 7 && f.size() != 8) return;
            stop();
            name_ = f[2];
            lastKeepalive_ = nowMs();
            players_ = 1;
            maxPlayers_ = atoi(f[6].c_str());
            open_ = true;
            phase_ = "waiting";
            pendingCreate_ = "{\"visibility\":" + q(f[1]) + ",\"name\":" + q(f[2]) + ",\"version\":" + std::to_string(atoi(f[3].c_str()))
                + ",\"stage\":" + std::to_string(atoi(f[4].c_str())) + ",\"weapons\":" + std::to_string(atoi(f[5].c_str()))
                + ",\"maxPlayers\":" + std::to_string(atoi(f[6].c_str()));
            vrOnly_ = f.size() == 8 ? (f[7] == "1" ? 1 : 0) : -1;
            flatPlayers_ = -1;
            if (vrOnly_ >= 0) pendingCreate_ += std::string(",\"vrOnly\":") + (vrOnly_ ? "true" : "false");
            pendingCreate_ += "}";
            registerPending();
        } else if (c == "stop") {
            stop();
        } else if (c == "leave") {
            forget();   // a host leaving players behind: the lobby stays for the one they elect
        } else if (c == "resume") {
            if (f.size() != 6 && f.size() != 7) return;
            pendingCreate_.clear();
            code_ = f[1];
            ownerToken_ = f[2];
            name_ = f.size() == 7 ? f[6] : "";
            lastKeepalive_ = nowMs();
            heartbeatPaused_ = false;
            maxPlayers_ = atoi(f[3].c_str());
            vrOnly_ = -1;   // the Worker keeps it; the next refresh says it again
            flatPlayers_ = -1;
            phase_ = f[4];
            players_ = atoi(f[5].c_str());
            open_ = players_ < maxPlayers_;
            joinId_.clear();
            joinToken_.clear();
            offerSent_ = answerReceived_ = false;
            hosting_ = true;
            nextHeartbeat_ = nextRequests_ = 0;
            seenRequests_.clear();
            push("CREATED|" + code_ + "|" + ownerToken_);
        } else if (c == "refresh") {
            lastKeepalive_ = nowMs();
            if (f.size() >= 3 && f.size() <= 6) {
                const int newPlayers = atoi(f[1].c_str());
                const bool newOpen = f[2] == "1";
                const int newMax = f.size() >= 4 ? atoi(f[3].c_str()) : maxPlayers_;
                const int newVrOnly = f.size() >= 5 ? (f[4] == "1" ? 1 : 0) : vrOnly_;
                const int newFlat = f.size() == 6 ? std::max(0, std::min(atoi(f[5].c_str()), newPlayers)) : flatPlayers_;
                if (newPlayers != players_ || newOpen != open_ || newMax != maxPlayers_ || newVrOnly != vrOnly_ ||
                    newFlat != flatPlayers_) {
                    players_ = newPlayers;
                    open_ = newOpen;
                    maxPlayers_ = newMax;
                    vrOnly_ = newVrOnly;
                    flatPlayers_ = newFlat;
                    nextHeartbeat_ = 0;
                }
            }
        } else if (c == "phase") {
            lastKeepalive_ = nowMs();
            if (f.size() == 3 && (f[1] == "warmup" || f[1] == "in_progress")) {
                phase_ = f[1];
                players_ = atoi(f[2].c_str());
                open_ = players_ < maxPlayers_;
                nextHeartbeat_ = nextRequests_ = 0;
            }
        } else if (c == "list") {
            const int version = atoi(f.size() > 1 ? f[1].c_str() : "0");
            std::string input = f.size() > 2 && !f[2].empty() ? "&inputClass=" + std::to_string(atoi(f[2].c_str())) : "";
            if (f.size() > 3 && f[3] == "1") input += "&hideFlat=1";
            const std::string reply = http("GET", std::string(kBase) + "?version=" + std::to_string(version) + input, nullptr, "");
            jspan obj, rows, row;
            jsonParse(reply, &obj);
            push("LIST_BEGIN");
            int listed = 0;
            if (jsonGet(obj, "lobbies", &rows)) {
                for (int i = 0; jsonAt(rows, i, &row); i++, listed++) {
                    push("LOBBY|" + jsonGetStr(row, "code") + "|" + clean(jsonGetStr(row, "name")) + "|"
                         + std::to_string(jsonGetInt(row, "stage", 0)) + "|" + std::to_string(jsonGetInt(row, "weapons", 0)) + "|"
                         + std::to_string(jsonGetInt(row, "players", 0)) + "|" + std::to_string(jsonGetInt(row, "maxPlayers", 0)) + "|"
                         + jsonGetStr(row, "phase", "waiting") + "|"
                         + std::to_string(jsonGetInt(row, "flatPlayers", -1)) + "|" + (jsonGetBool(row, "vrOnly", false) ? "1" : "0"));
                }
            }
            push("LIST_END");
            sysLogPrintf(0, "lobby: %d open games (version %d)", listed, version);
        } else if (c == "join") {
            if (f.size() != 3 && f.size() != 4) return;
            joinId_.clear();
            joinToken_.clear();
            offerSent_ = answerReceived_ = false;
            std::string requested = f[1];
            while (!requested.empty() && requested.back() == ' ') requested.pop_back();
            while (!requested.empty() && requested.front() == ' ') requested.erase(0, 1);
            for (char &ch : requested) ch = (char)toupper((unsigned char)ch);
            std::string body = "{\"version\":" + std::to_string(atoi(f[2].c_str()));
            if (f.size() == 4 && !f[3].empty()) body += ",\"inputClass\":" + std::to_string(atoi(f[3].c_str()));
            body += "}";
            const std::string reply = http("POST", std::string(kBase) + "/" + requested + "/joins", &body, "");
            jspan obj;
            jsonParse(reply, &obj);
            joinId_ = jsonGetStr(obj, "id");
            joinToken_ = jsonGetStr(obj, "joinToken");
            code_ = requested;
            push("JOINED|" + joinId_ + "|" + relayCredentials("{\"id\":" + q(joinId_) + "}", joinToken_));
        } else if (c == "offer") {
            if (f.size() != 2 || joinId_.empty()) return;
            const std::string body = "{\"sdp\":" + q(decode64(f[1])) + "}";
            http("PUT", std::string(kBase) + "/" + code_ + "/joins/" + joinId_ + "/offer", &body, joinToken_);
            offerSent_ = true;
        } else if (c == "answer") {
            if (f.size() != 3 || !hosting_) return;
            const std::string body = "{\"sdp\":" + q(decode64(f[2])) + "}";
            http("PUT", std::string(kBase) + "/" + code_ + "/joins/" + f[1] + "/answer", &body, ownerToken_);
        }
    }

    void poll()
    {
        try {
            const long long now = nowMs();
            const bool keepalive = now - lastKeepalive_ <= 60000;
            if (hosting_ || !pendingCreate_.empty()) {
                if (!keepalive && !heartbeatPaused_) {
                    sysLogPrintf(0, "lobby: heartbeat paused for %s: no keepalive for 60 s", code_.c_str());
                    heartbeatPaused_ = true;
                } else if (keepalive && heartbeatPaused_) {
                    sysLogPrintf(0, "lobby: heartbeat resumed for %s", code_.c_str());
                    heartbeatPaused_ = false;
                    nextHeartbeat_ = nextRequests_ = 0;
                }
            }
            if (!pendingCreate_.empty() && keepalive) registerPending();
            if (hosting_ && !code_.empty()) {
                if (!keepalive) return;
                if (now >= nextHeartbeat_) {
                    std::string state = "{\"players\":" + std::to_string(players_) + ",\"open\":" + (open_ ? "true" : "false")
                        + ",\"phase\":" + q(phase_) + ",\"maxPlayers\":" + std::to_string(maxPlayers_);
                    if (vrOnly_ >= 0) state += std::string(",\"vrOnly\":") + (vrOnly_ ? "true" : "false");
                    if (flatPlayers_ >= 0) state += ",\"flatPlayers\":" + std::to_string(std::min(flatPlayers_, players_));
                    if (!name_.empty()) state += ",\"name\":" + q(name_);
                    state += "}";
                    try { http("PUT", std::string(kBase) + "/" + code_, &state, ownerToken_); }
                    catch (const HttpError &e) { if (lost(e)) return; throw; }
                    nextHeartbeat_ = nowMs() + 15000;
                }
                if (open_ && nowMs() >= nextRequests_) {
                    std::string reply;
                    try { reply = http("GET", std::string(kBase) + "/" + code_ + "/joins", nullptr, ownerToken_); }
                    catch (const HttpError &e) { if (lost(e)) return; throw; }
                    nextRequests_ = nowMs() + (phase_ == "waiting" ? 4000 : 10000);
                    jspan obj, reqs, r;
                    jsonParse(reply, &obj);
                    if (jsonGet(obj, "requests", &reqs)) {
                        for (int i = 0; jsonAt(reqs, i, &r); i++) {
                            const std::string id = jsonGetStr(r, "id");
                            if (id.empty() || seenRequests_.count(id)) continue;
                            push("HOST_PEER|" + id + "|" + encode64(jsonGetStr(r, "offer")) + "|"
                                 + relayCredentials("{}", ownerToken_));
                            seenRequests_.insert(id);
                        }
                    }
                }
            } else if (!joinId_.empty() && offerSent_ && !answerReceived_) {
                const std::string reply = http("GET", std::string(kBase) + "/" + code_ + "/joins/" + joinId_ + "/answer", nullptr, joinToken_);
                jspan obj, answer;
                jsonParse(reply, &obj);
                if (jsonGet(obj, "answer", &answer) && !jsonIsNull(answer)) {
                    std::string sdp;
                    jsonStr(answer, &sdp);
                    push("ANSWER|" + joinId_ + "|" + encode64(sdp));
                    answerReceived_ = true;
                }
            }
            lastError_.clear();
        } catch (const HttpError &e) {
            const std::string message = "Lobby service: " + e.message;
            if (message != lastError_) error(message);
            lastError_ = message;
        }
    }

    /* TURN is a fallback: a refused relay leaves "|" and ICE gathers STUN only. */
    std::string relayCredentials(const std::string &body, const std::string &token)
    {
        try {
            const std::string reply = http("POST", std::string(kBase) + "/" + code_ + "/turn", &body, token);
            jspan obj;
            jsonParse(reply, &obj);
            return clean(jsonGetStr(obj, "username")) + "|" + clean(jsonGetStr(obj, "credential"));
        } catch (const HttpError &e) {
            sysLogPrintf(1, "lobby: relay credentials unavailable; direct connection only: %s", e.message.c_str());
            return "|";
        }
    }

    void registerPending()
    {
        if (pendingCreate_.empty()) return;
        const std::string body = pendingCreate_;
        const std::string reply = http("POST", kBase, &body, "");
        jspan obj;
        jsonParse(reply, &obj);
        code_ = jsonGetStr(obj, "code");
        ownerToken_ = jsonGetStr(obj, "ownerToken");
        pendingCreate_.clear();
        hosting_ = true;
        nextHeartbeat_ = nextRequests_ = 0;
        seenRequests_.clear();
        push("CREATED|" + code_ + "|" + ownerToken_);
    }

    void stop()
    {
        pendingCreate_.clear();
        if (hosting_ && !code_.empty()) {
            try {
                http("DELETE", std::string(kBase) + "/" + code_, nullptr, ownerToken_);
                sysLogPrintf(0, "lobby: removed %s", code_.c_str());
            } catch (const HttpError &e) {
                if (e.status == 404 || e.status == 410) sysLogPrintf(0, "lobby: %s already gone (HTTP %d)", code_.c_str(), e.status);
                else sysLogPrintf(1, "lobby: failed to remove %s: %s", code_.c_str(), e.message.c_str());
            }
        }
        forget();
    }

    void forget()
    {
        pendingCreate_.clear();
        hosting_ = false;
        phase_ = "waiting";
        code_.clear();
        ownerToken_.clear();
        name_.clear();
        lastKeepalive_ = 0;
        heartbeatPaused_ = false;
        joinId_.clear();
        joinToken_.clear();
        offerSent_ = answerReceived_ = false;
        seenRequests_.clear();
    }

    bool lost(const HttpError &e)
    {
        if (!hosting_ || (e.status != 404 && e.status != 410)) return false;
        const std::string lostCode = code_;
        sysLogPrintf(0, "lobby: %s is gone (HTTP %d: %s)", lostCode.c_str(), e.status, e.message.c_str());
        forget();
        push("LOBBY_LOST|" + lostCode + "|" + clean(e.message));
        return true;
    }

    std::mutex eventsLock_, cmdLock_;
    std::condition_variable cmdWake_;
    std::deque<std::string> events_, commands_;
    bool busy_ = false;
    std::set<std::string> seenRequests_;
    std::string code_, ownerToken_, name_, joinId_, joinToken_, phase_ = "waiting", lastError_, pendingCreate_;
    bool hosting_ = false, offerSent_ = false, answerReceived_ = false, open_ = true, heartbeatPaused_ = false;
    int players_ = 1, maxPlayers_ = 4;
    int vrOnly_ = -1;   // "VR players only" as the host last sent it; -1 not known (a resume before its refresh)
    int flatPlayers_ = -1;   // how many play flat, as the host last counted them; -1 not known
    long long nextHeartbeat_ = 0, nextRequests_ = 0, lastKeepalive_ = 0;
    std::thread worker_;   // last: starts once the state above exists
};

LobbyClient *s_lobby;

void lobbyAtExit(void)
{
    // MainActivity.onDestroy's lobbies.shutdown(): take the hosted lobby down
    if (s_lobby) s_lobby->stopAndWait(1500);
}

LobbyClient &lobby()
{
    if (!s_lobby) {
        s_lobby = new LobbyClient();   // lives as long as the process, as the Quest's
        atexit(lobbyAtExit);
    }
    return *s_lobby;
}

}   // namespace

std::string pcLobbyEvent(void) { return s_lobby ? s_lobby->event() : std::string(); }
void pcLobbyCommand(const char *cmd) { lobby().command(cmd ? cmd : ""); }
