/*
 * LiveSplit (launcher Play > LiveSplit, docs/speedrun.md): the mission clock to
 * a runner's LiveSplit through the TCP server built into it since 1.8.29
 * (Control > Start TCP Server, port 16834; 1.8.34's Startup Behavior option
 * starts it with LiveSplit). It is the one hook that reaches a Quest:
 * autosplitters read the memory of a process on the same PC, and none exists
 * for GoldenEye (the speedrun research, 2026-11-11). Dab's Mod has no LiveSplit
 * side to port. The commands are LiveSplit's own
 * (src/LiveSplit.Core/Server/CommandServer.cs): a line each, and a line back
 * for the get* ones.
 *
 * What it tells LiveSplit:
 * - Game time is the debrief's clock: this mission's, plus every earlier
 *   mission's in the run (a try that failed included, as a wall clock would
 *   have it). It moves only when this sets it (alwayspausegametime), so it
 *   never runs on through a cutscene or the watch. LiveSplit compares by it
 *   once the runner picks Game Time.
 * - A mission's clock starting with LiveSplit's timer stopped, or a run over,
 *   starts it; with a run going, the mission is its next segment.
 * - A completed mission splits, at the debrief's time.
 * - A mission that ends unfinished (killed, aborted, quit) while the run is on
 *   its first segment resets the timer: a level's retry, or a full game lost
 *   on its first mission. Later in a run it does nothing: the runner decides.
 *
 * Never in the game's way: a thread of its own connects and talks, with
 * timeouts of a second or two, and the game only posts what happened (as
 * RUMBLE's splitter does, "asynchronously with a 2-second timeout to prevent
 * game lag").
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include "system.h"
#include "gevr_livesplit.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET ls_socket;
#define LS_INVALID INVALID_SOCKET
#define ls_close closesocket
#define LS_SEND_FLAGS 0
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
typedef int ls_socket;
#define LS_INVALID (-1)
#define ls_close close
#ifdef MSG_NOSIGNAL
#define LS_SEND_FLAGS MSG_NOSIGNAL
#else
#define LS_SEND_FLAGS 0
#endif
#endif

extern int VrLiveSplit;              /* vr_settings_defaults.c */
extern char VrLiveSplitHost[64];

/* a test's own server (port/tests/test_livesplit.py), never a runner's LiveSplit */
#ifdef GEVR_LIVESPLIT_TEST_PORT
#define LS_PORT GEVR_LIVESPLIT_TEST_PORT
#else
#define LS_PORT GEVR_LIVESPLIT_PORT
#endif

#define LS_TIMEOUT_MS   1500
#define LS_RETRY_MS     5000
#define LS_SETEVERY_MS  100          /* game time at most ten times a second */
#define LS_EVENTS       16

enum { LS_EV_START = 1, LS_EV_SPLIT, LS_EV_FAIL };

struct lsevent {
    int kind;
    int time60;
};

static SDL_mutex *s_lock;
/* shared, under s_lock */
static int s_enabled;
static char s_wantHost[64];
static struct lsevent s_events[LS_EVENTS];
static int s_eventCount;
static int s_clock60 = -1;
static char s_status[160];
static int s_connected;
/* the game's thread only */
static int s_missionLive;            /* this mission's clock started, and it has not completed */

static int lsWorker(void *arg);

/* the setting, copied for the thread; the thread starts the first time it is on */
static void lsConfigure(void)
{
    if (!VrLiveSplit && s_lock == NULL)
    {
        return;
    }
    if (s_lock == NULL)
    {
        SDL_Thread *thread;

        s_lock = SDL_CreateMutex();
        if (s_lock == NULL)
        {
            return;
        }
        thread = SDL_CreateThread(lsWorker, "livesplit", NULL);
        if (thread != NULL)
        {
            SDL_DetachThread(thread);
        }
    }
    SDL_LockMutex(s_lock);
    s_enabled = VrLiveSplit != 0;
    memcpy(s_wantHost, VrLiveSplitHost, sizeof(s_wantHost));
    s_wantHost[sizeof(s_wantHost) - 1] = '\0';
    SDL_UnlockMutex(s_lock);
}

static void lsPost(int kind, int time60)
{
    if (s_lock == NULL)
    {
        return;
    }
    SDL_LockMutex(s_lock);
    if (s_eventCount < LS_EVENTS)
    {
        s_events[s_eventCount].kind = kind;
        s_events[s_eventCount].time60 = time60;
        s_eventCount++;
    }
    SDL_UnlockMutex(s_lock);
}

void gevrLiveSplitFrame(int running, int time60)
{
    if (!VrLiveSplit)
    {
        s_missionLive = 0;
        return;
    }
    lsConfigure();
    if (!running || s_lock == NULL)
    {
        return;
    }
    SDL_LockMutex(s_lock);
    s_clock60 = time60;
    SDL_UnlockMutex(s_lock);
    if (!s_missionLive)
    {
        s_missionLive = 1;
        lsPost(LS_EV_START, time60);
    }
}

void gevrLiveSplitMissionComplete(int time60)
{
    if (s_missionLive)
    {
        s_missionLive = 0;
        lsPost(LS_EV_SPLIT, time60);
    }
}

void gevrLiveSplitStageReset(void)
{
    if (s_missionLive)
    {
        s_missionLive = 0;
        lsPost(LS_EV_FAIL, 0);
    }
}

const char *gevrLiveSplitStatus(void)
{
    static char out[sizeof(s_status)];

    lsConfigure();   /* the launcher's line: a changed setting or address is tried at once */
    if (s_lock == NULL || !VrLiveSplit)
    {
        return "";
    }
    SDL_LockMutex(s_lock);
    memcpy(out, s_status, sizeof(out));
    SDL_UnlockMutex(s_lock);
    return out;
}

int gevrLiveSplitConnected(void)
{
    int connected;

    if (s_lock == NULL)
    {
        return 0;
    }
    SDL_LockMutex(s_lock);
    connected = s_connected;
    SDL_UnlockMutex(s_lock);
    return connected;
}

/* ---- the thread --------------------------------------------------------- */

static void lsSetStatus(int connected, const char *fmt, const char *host)
{
    SDL_LockMutex(s_lock);
    s_connected = connected;
    snprintf(s_status, sizeof(s_status), fmt, host);
    SDL_UnlockMutex(s_lock);
}

static void lsTimeouts(ls_socket sock)
{
#ifdef _WIN32
    DWORD ms = LS_TIMEOUT_MS;

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
#else
    struct timeval tv;

    tv.tv_sec = LS_TIMEOUT_MS / 1000;
    tv.tv_usec = (LS_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
    {
        int one = 1;

        setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    }
#endif
#endif
}

static void lsBlocking(ls_socket sock, int blocking)
{
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;

    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);

    fcntl(sock, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
#endif
}

/* a connection to host:16834 within the timeout, or LS_INVALID */
static ls_socket lsConnect(const char *host)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *ai;
    char port[8];
    ls_socket sock = LS_INVALID;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port, sizeof(port), "%d", LS_PORT);
    if (getaddrinfo(host, port, &hints, &res) != 0)
    {
        return LS_INVALID;
    }
    for (ai = res; ai != NULL && sock == LS_INVALID; ai = ai->ai_next)
    {
        fd_set wr;
        struct timeval tv;
        int err = 0;
        socklen_t len = sizeof(err);
        int one = 1;

        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == LS_INVALID)
        {
            continue;
        }
        lsBlocking(sock, 0);
        connect(sock, ai->ai_addr, (int)ai->ai_addrlen);
        FD_ZERO(&wr);
        FD_SET(sock, &wr);
        tv.tv_sec = LS_TIMEOUT_MS / 1000;
        tv.tv_usec = (LS_TIMEOUT_MS % 1000) * 1000;
        if (select((int)sock + 1, NULL, &wr, NULL, &tv) != 1
            || getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0 || err != 0)
        {
            ls_close(sock);
            sock = LS_INVALID;
            continue;
        }
        lsBlocking(sock, 1);
        lsTimeouts(sock);
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
    }
    freeaddrinfo(res);
    return sock;
}

static int lsSend(ls_socket sock, const char *line)
{
    char buf[96];
    int len = snprintf(buf, sizeof(buf), "%s\n", line);
    int at = 0;

    while (at < len)
    {
        int n = send(sock, buf + at, len - at, LS_SEND_FLAGS);

        if (n <= 0)
        {
            return 0;
        }
        at += n;
    }
    return 1;
}

/* a get* command's answer line, without its line end; 0 if none came */
static int lsAsk(ls_socket sock, const char *command, char *out, int n)
{
    int at = 0;

    if (!lsSend(sock, command))
    {
        return 0;
    }
    while (at < n - 1)
    {
        char c;

        if (recv(sock, &c, 1, 0) != 1)
        {
            return 0;
        }
        if (c == '\n')
        {
            break;
        }
        if (c != '\r')
        {
            out[at++] = c;
        }
    }
    out[at] = '\0';
    return 1;
}

/* LiveSplit's formatted time ("1:23.45", "0:01:23.4500000", "-") in sixtieths */
static int lsParseTime60(const char *text)
{
    double parts[3] = { 0, 0, 0 };
    int count = 0;
    const char *p = text;

    while (*p == '-' || *p == ' ')
    {
        p++;
    }
    while (*p && count < 3)
    {
        parts[count++] = strtod(p, (char **)&p);
        if (*p != ':')
        {
            break;
        }
        p++;
    }
    if (count == 0)
    {
        return 0;
    }
    {
        double seconds = count == 3 ? parts[0] * 3600 + parts[1] * 60 + parts[2]
                       : count == 2 ? parts[0] * 60 + parts[1] : parts[0];

        return seconds > 0 ? (int)(seconds * 60.0 + 0.5) : 0;
    }
}

/* "setgametime H:MM:SS.ff", which LiveSplit's TimeSpanParser reads */
static int lsSetGameTime(ls_socket sock, int time60)
{
    char line[64];
    int hundredths = (int)(((long long)time60 * 100) / 60);

    snprintf(line, sizeof(line), "setgametime %d:%02d:%02d.%02d", hundredths / 360000, (hundredths / 6000) % 60,
             (hundredths / 100) % 60, hundredths % 100);
    return lsSend(sock, line);
}

static int lsWorker(void *arg)
{
    ls_socket sock = LS_INVALID;
    char host[64] = "";
    Uint32 nextConnect = 0;
    Uint32 lastSet = 0;
    int sentClock = -1;
    int base60 = 0;          /* the run's game time before this mission */
    int segment = 0;         /* a mission of the run is being timed */

    (void)arg;
#ifdef _WIN32
    {
        WSADATA wsa;

        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
#endif
    for (;;)
    {
        struct lsevent events[LS_EVENTS];
        char want[64];
        int enabled, clock, count, i, ok = 1;
        Uint32 now = SDL_GetTicks();

        SDL_LockMutex(s_lock);
        enabled = s_enabled;
        memcpy(want, s_wantHost, sizeof(want));
        clock = s_clock60;
        count = s_eventCount;
        memcpy(events, s_events, sizeof(events));
        s_eventCount = 0;
        SDL_UnlockMutex(s_lock);

#ifdef ANDROID
        /* the headset has no LiveSplit of its own: the PC's address is the runner's to give */
        if (want[0] == '\0')
        {
            if (sock != LS_INVALID) { ls_close(sock); sock = LS_INVALID; }
            host[0] = '\0';
            lsSetStatus(0, "Enter the address of the PC running LiveSplit%s", "");
            SDL_Delay(250);
            continue;
        }
#else
        if (want[0] == '\0')
        {
            strcpy(want, "127.0.0.1");
        }
#endif
        if (!enabled || strcmp(want, host) != 0)
        {
            if (sock != LS_INVALID)
            {
                ls_close(sock);
                sock = LS_INVALID;
                sysLogPrintf(LOG_NOTE, "livesplit: disconnected from %s", host);
            }
            strcpy(host, want);
            nextConnect = 0;
            segment = 0;
            lsSetStatus(0, enabled ? "Looking for LiveSplit at %s..." : "", host);
        }
        if (!enabled)
        {
            SDL_Delay(250);
            continue;
        }
        if (sock == LS_INVALID)
        {
            if ((Sint32)(now - nextConnect) < 0)
            {
                SDL_Delay(100);
                continue;   /* what happens while LiveSplit is away is not kept for it */
            }
            sock = lsConnect(host);
            if (sock == LS_INVALID)
            {
                nextConnect = SDL_GetTicks() + LS_RETRY_MS;
                lsSetStatus(0, "No LiveSplit server at %s (LiveSplit: Control > Start TCP Server)", host);
                continue;
            }
            /* game time moves only when it is set, from every start on */
            lsSend(sock, "alwayspausegametime");
            sentClock = -1;
            segment = 0;
            lsSetStatus(1, "Connected to LiveSplit at %s", host);
            sysLogPrintf(LOG_NOTE, "livesplit: connected to %s:%d", host, LS_PORT);
        }

        for (i = 0; i < count && ok; i++)
        {
            char reply[64];

            if (events[i].kind == LS_EV_START)
            {
                ok = lsAsk(sock, "getcurrenttimerphase", reply, sizeof(reply));
                if (ok && strcmp(reply, "Ended") == 0)
                {
                    ok = lsSend(sock, "reset");
                    strcpy(reply, "NotRunning");
                }
                if (ok && strcmp(reply, "NotRunning") == 0)
                {
                    ok = lsSend(sock, "starttimer");
                    base60 = 0;
                }
                else if (ok)
                {
                    /* a run going: its game time so far, as this last set it (or the runner did) */
                    char total[64];

                    ok = lsAsk(sock, "getcurrentgametime", total, sizeof(total));
                    base60 = ok ? lsParseTime60(total) : 0;
                }
                segment = ok;
                sentClock = -1;
                sysLogPrintf(LOG_NOTE, "livesplit: mission started (run %s, game time before it %d)", reply, base60);
            }
            else if (events[i].kind == LS_EV_SPLIT && segment)
            {
                ok = lsSetGameTime(sock, base60 + events[i].time60) && lsSend(sock, "split");
                base60 += events[i].time60;
                segment = 0;
                sysLogPrintf(LOG_NOTE, "livesplit: split at %d (mission %d)", base60, events[i].time60);
            }
            else if (events[i].kind == LS_EV_FAIL && segment)
            {
                /* the try's clock as it ended: a run's total keeps it */
                if (clock >= 0 && clock != sentClock)
                {
                    ok = lsSetGameTime(sock, base60 + clock);
                    sentClock = clock;
                }
                ok = ok && lsAsk(sock, "getsplitindex", reply, sizeof(reply));
                if (ok && strcmp(reply, "0") == 0)
                {
                    ok = lsSend(sock, "reset");
                    sysLogPrintf(LOG_NOTE, "livesplit: the first mission ended unfinished: reset");
                }
                segment = 0;
            }
        }
        if (ok && segment && clock >= 0 && clock != sentClock && (Uint32)(now - lastSet) >= LS_SETEVERY_MS)
        {
            ok = lsSetGameTime(sock, base60 + clock);
            sentClock = clock;
            lastSet = now;
        }
        if (!ok)
        {
            ls_close(sock);
            sock = LS_INVALID;
            nextConnect = SDL_GetTicks() + LS_RETRY_MS;
            lsSetStatus(0, "Lost LiveSplit at %s", host);
            sysLogPrintf(LOG_WARNING, "livesplit: lost the connection to %s", host);
        }
        SDL_Delay(20);
    }
    return 0;
}
