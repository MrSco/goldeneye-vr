/* Sequence-player stop wait: a dropped stop must not spin forever. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef int s32;
typedef long long s64;

#define AL_STOPPED 0
#define AL_PLAYING 1
#define AL_STOPPING 2
#define LOG_ERROR 0

typedef struct {
    s32 state;
} ALCSPlayer;

static int g_stops;
static int g_frames;
static int g_sleeps;
static int g_stop_on_call; /* 0 = never, else the call that reaches AL_STOPPED */

s32 alCSPGetState(ALCSPlayer *seqp)
{
    return seqp->state;
}

void alCSPStop(ALCSPlayer *seqp)
{
    g_stops++;
    if (g_stop_on_call && g_stops >= g_stop_on_call)
    {
        seqp->state = AL_STOPPED;
    }
}

void gevrAudioFrame(void)
{
    g_frames++;
}

void sysLogPrintf(s32 level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

void sysSleep(const s64 hns)
{
    (void)hns;
    g_sleeps++;
}

/* INSERT_WAIT */

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "fail %s line %d\n", #x, __LINE__); return 1; } } while (0)

static void reset(ALCSPlayer *seqp, s32 state, int stop_on)
{
    memset(seqp, 0, sizeof(*seqp));
    seqp->state = state;
    g_stops = 0;
    g_frames = 0;
    g_sleeps = 0;
    g_stop_on_call = stop_on;
}

int main(void)
{
    ALCSPlayer seqp;

    reset(&seqp, AL_STOPPED, 0);
    gevrWaitSeqStopped(&seqp);
    CHECK(g_stops == 0 && g_frames == 0 && g_sleeps == 0);
    CHECK(seqp.state == AL_STOPPED);

    /* A queued stop lands on the second post. The wait returns. */
    reset(&seqp, AL_PLAYING, 2);
    gevrWaitSeqStopped(&seqp);
    CHECK(seqp.state == AL_STOPPED);
    CHECK(g_stops == 2);
    CHECK(g_frames == 2);
    CHECK(g_sleeps == 2);

    /* The stop event never lands. The wait forces AL_STOPPED inside the bound. */
    reset(&seqp, AL_PLAYING, 0);
    gevrWaitSeqStopped(&seqp);
    CHECK(seqp.state == AL_STOPPED);
    CHECK(g_stops == 120);
    CHECK(g_frames == 120);
    CHECK(g_sleeps == 119);

    /* Already stopping, and the follow-up event was dropped. */
    reset(&seqp, AL_STOPPING, 0);
    gevrWaitSeqStopped(&seqp);
    CHECK(seqp.state == AL_STOPPED);
    CHECK(g_stops == 0);
    CHECK(g_frames == 120);
    CHECK(g_sleeps == 119);

    printf("music wait ok\n");
    return 0;
}
