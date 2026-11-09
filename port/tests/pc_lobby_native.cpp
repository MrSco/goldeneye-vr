/*
 * The PC lobby client's parsing (port/pc/pc_lobby.cpp, LobbyClient.java's port):
 * the lobby list, a join request's SDP offer with its escapes, and Base64 as
 * android.util.Base64 (NO_WRAP | URL_SAFE) writes it, which the launcher and the
 * Quest's LobbyClient share.
 *
 *   clang++ -std=c++20 -I port/pc port/tests/pc_lobby_native.cpp -lwinhttp -o pc_lobby_test.exe
 */
#include <cstdarg>
#include <cstdio>

extern "C" void sysLogPrintf(int, const char *, ...) {}

#include "../pc/pc_lobby.cpp"

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

    printf(failures ? "pc_lobby: %d failed\n" : "pc_lobby: all passed\n", failures);
    return failures ? 1 : 0;
}
