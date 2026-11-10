/*
 * The PC lobby client's parsing (port/pc/pc_lobby.cpp, LobbyClient.java's port):
 * the lobby list, a join request's SDP offer with its escapes, and Base64 as
 * android.util.Base64 (NO_WRAP | URL_SAFE) writes it, which the launcher and the
 * Quest's LobbyClient share.
 *
 *   clang++ -std=c++20 -I port/pc port/tests/pc_lobby_native.cpp port/pc/pc_http.cpp -lwinhttp -lbcrypt -o pc_lobby_test.exe
 */
#include <cstdarg>
#include <cstdio>

extern "C" void sysLogPrintf(int, const char *, ...) {}

#define GEVR_LOBBY_TEST_HTTP fakeHttp   // the client's requests go to fakeHttp below
#include "../pc/pc_lobby.cpp"
#include <functional>

namespace {
struct Call { std::string method, url, body; };
std::mutex s_callsLock;
std::vector<Call> s_calls;
std::string fakeHttp(const char *method, const std::string &url, const std::string *body, const std::string &)
{
    std::lock_guard<std::mutex> lock(s_callsLock);
    s_calls.push_back({method, url, body ? *body : ""});
    const std::string m = method;
    if (url.find("/turn") != std::string::npos) return "{}";
    if (m == "POST" && url.find("/joins") != std::string::npos) return R"({"id":"j1","joinToken":"jt"})";
    if (m == "POST") return R"({"code":"ABCDEFGH","ownerToken":"owner"})";
    if (m == "GET" && url.find("/joins") != std::string::npos) return R"({"requests":[]})";
    if (m == "GET")
        return R"({"lobbies":[{"code":"ABCDEFGH","name":"A","stage":1,"weapons":2,"players":2,"maxPlayers":4,"phase":"warmup","flatPlayers":1,"vrOnly":false},)"
               R"({"code":"BCDEFGHJ","name":"B","stage":1,"weapons":2,"players":1,"maxPlayers":4,"vrOnly":true}]})";
    return "{}";
}
/* The last call matching, once one comes (the client's thread), or an empty one. */
Call waitCall(const std::function<bool(const Call &)> &match, int ms = 4000)
{
    for (int waited = 0; waited < ms; waited += 20) {
        {
            std::lock_guard<std::mutex> lock(s_callsLock);
            for (auto it = s_calls.rbegin(); it != s_calls.rend(); ++it)
                if (match(*it)) return *it;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return {};
}
void clearCalls() { std::lock_guard<std::mutex> lock(s_callsLock); s_calls.clear(); }
}

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main()
{
    // The Worker's lobby list (GET /v1/lobbies?version=N)
    const std::string list = R"({"lobbies":[{"code":"ABCD1234","name":"Bond's | game","stage":34,"weapons":2,"players":3,"maxPlayers":8,"phase":"warmup"},
        {"code":"ZZ","name":"x","stage":9,"weapons":0,"players":1,"maxPlayers":4}]})";
    jspan obj, rows, row;
    CHECK(jsonParse(list, &obj));
    CHECK(jsonGet(obj, "lobbies", &rows));
    CHECK(jsonAt(rows, 0, &row));
    CHECK(jsonGetStr(row, "code") == "ABCD1234");
    CHECK(clean(jsonGetStr(row, "name")) == "Bond's   game");
    CHECK(jsonGetInt(row, "maxPlayers", 0) == 8);
    CHECK(jsonGetStr(row, "phase", "waiting") == "warmup");
    CHECK(jsonAt(rows, 1, &row));
    CHECK(jsonGetStr(row, "phase", "waiting") == "waiting");
    CHECK(!jsonAt(rows, 2, &row));

    // A join request's offer: SDP lines, escaped quote and a \u escape
    const std::string reqs = R"({"requests":[{"id":"j1","offer":"v=0\r\na=ice-ufrag:ab\"c\r\na=x:\u00e9\r\n"}]})";
    jspan r, a;
    CHECK(jsonParse(reqs, &obj));
    CHECK(jsonGet(obj, "requests", &rows));
    CHECK(jsonAt(rows, 0, &r));
    const std::string sdp = jsonGetStr(r, "offer");
    CHECK(sdp == "v=0\r\na=ice-ufrag:ab\"c\r\na=x:\xc3\xa9\r\n");

    // An answer not there yet
    CHECK(jsonParse(R"({"answer":null})", &obj));
    CHECK(jsonGet(obj, "answer", &a) && jsonIsNull(a));

    // JSON out and back (spans point into the text: it must outlive them)
    const std::string body = "{\"sdp\":" + q(sdp) + "}";
    CHECK(jsonParse(body, &obj));
    CHECK(jsonGetStr(obj, "sdp") == sdp);

    // Base64 URL-safe with padding, as Python's urlsafe_b64encode and Android's URL_SAFE
    CHECK(encode64("v=0\r\n") == "dj0wDQo=");
    CHECK(encode64("\xfb\xff") == "-_8=");
    CHECK(encode64("abc") == "YWJj");
    CHECK(decode64("dj0wDQo=") == "v=0\r\n");
    CHECK(decode64("-_8=") == "\xfb\xff");
    CHECK(decode64(encode64(sdp)) == sdp);

    // Command lines split as Java's split("\\|", -1)
    CHECK(splitBar("refresh|2|1|").size() == 4);
    CHECK(splitBar("stop").size() == 1);

    // "VR players only": the host's setting with create and the heartbeat, what the
    // player plays with on list and join (the Worker's vrOnly and inputClass)
    {
        LobbyClient &c = *new LobbyClient();   // lives as long as the process, as the game's (its thread is never joined)
        c.command("create|public|A's game|26|1|2|4|1");
        Call created = waitCall([](const Call &x) { return x.method == "POST" && x.url == kBase; });
        CHECK(created.body.find("\"vrOnly\":true") != std::string::npos);
        clearCalls();
        c.command("refresh|1|1|4|0");   // switched off in the lobby: the next heartbeat says so
        Call beat = waitCall([](const Call &x) { return x.method == "PUT"; });
        CHECK(beat.body.find("\"vrOnly\":false") != std::string::npos);
        clearCalls();
        c.command("list|26|3");
        CHECK(waitCall([](const Call &x) { return x.method == "GET" && x.url.find("?version=") != std::string::npos; }).url
              == std::string(kBase) + "?version=26&inputClass=3");
        c.command("list|26");   // the old command
        CHECK(waitCall([](const Call &x) { return x.method == "GET" && x.url == std::string(kBase) + "?version=26"; }).method == "GET");
        clearCalls();
        c.command("join|abcdefgh|26|1");
        Call join = waitCall([](const Call &x) { return x.method == "POST" && x.url.find("/joins") != std::string::npos; });
        CHECK(join.url == std::string(kBase) + "/ABCDEFGH/joins");
        CHECK(join.body == "{\"version\":26,\"inputClass\":1}");
        clearCalls();
        c.command("join|abcdefgh|26");
        CHECK(waitCall([](const Call &x) { return x.method == "POST" && x.url.find("/joins") != std::string::npos; }).body == "{\"version\":26}");
        clearCalls();
        c.command("create|public|A's game|26|1|2|4");   // the old command: no field
        created = waitCall([](const Call &x) { return x.method == "POST" && x.url == kBase; });
        CHECK(!created.body.empty() && created.body.find("vrOnly") == std::string::npos);

        // who plays flat: the host's count with the heartbeat, never above the players;
        // a VR player's list may leave such games out
        clearCalls();
        c.command("refresh|3|1|4|0|2");
        beat = waitCall([](const Call &x) { return x.method == "PUT"; });
        CHECK(beat.body.find("\"flatPlayers\":2") != std::string::npos);
        clearCalls();
        c.command("phase|warmup|1");
        beat = waitCall([](const Call &x) { return x.method == "PUT"; });
        CHECK(beat.body.find("\"flatPlayers\":1") != std::string::npos);
        while (!c.event().empty()) {}
        clearCalls();
        c.command("list|26|0|1");
        CHECK(waitCall([](const Call &x) { return x.method == "GET" && x.url.find("?version=") != std::string::npos; }).url
              == std::string(kBase) + "?version=26&inputClass=0&hideFlat=1");
        std::vector<std::string> listed;
        for (int waited = 0; waited < 4000 && (listed.empty() || listed.back() != "LIST_END"); waited += 20) {
            const std::string e = c.event();
            if (!e.empty()) listed.push_back(e);
            else std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(listed.size() == 4);
        CHECK(listed.size() == 4 && listed[1] == "LOBBY|ABCDEFGH|A|1|2|2|4|warmup|1|0");
        CHECK(listed.size() == 4 && listed[2] == "LOBBY|BCDEFGHJ|B|1|2|1|4|waiting|-1|1");
        c.stopAndWait(1000);
    }

    printf(failures ? "pc_lobby: %d failed\n" : "pc_lobby: all passed\n", failures);
    return failures ? 1 : 0;
}
