/* The LiveSplit client (port/src/gevr_livesplit.c) against test_livesplit.py's
 * mock LiveSplit server: a two-mission run, a failed try in it, the run over,
 * and a new run lost on its first mission. The game's side is played here:
 * frames of a running clock, completions and stage loads, a frame's time
 * apart. */
#include <stdarg.h>
#include <stdio.h>
#include <PR/ultratypes.h>
#include <SDL.h>   /* the test's: SDL's delay on pthreads (test_livesplit.py) */
#include "gevr_livesplit.h"

int VrLiveSplit = 1;
char VrLiveSplitHost[64] = "127.0.0.1";

void sysLogPrintf(s32 level, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    printf("log %d: ", (int)level);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    fflush(stdout);
}


/* a mission's clock running from 0 to last, a frame (16 ms) each sixtieth */
static void frames(int last)
{
    int t;

    for (t = 0; t <= last; t++)
    {
        gevrLiveSplitFrame(1, t);
        SDL_Delay(16);
    }
}

static void settle(void)
{
    SDL_Delay(400);
}

int main(void)
{
    int i;

    /* connected before the first mission, as a runner would start LiveSplit first */
    gevrLiveSplitFrame(0, 0);
    for (i = 0; i < 100 && !gevrLiveSplitConnected(); i++)
    {
        SDL_Delay(50);
    }
    printf("status: %s\n", gevrLiveSplitStatus());
    if (!gevrLiveSplitConnected())
    {
        printf("FAIL: never connected\n");
        return 1;
    }
    /* mission 1, completed at 1.17 s */
    frames(60);
    gevrLiveSplitMissionComplete(70);
    gevrLiveSplitStageReset();       /* the menus load: not a failure */
    settle();
    /* mission 2, a try that fails at 0.5 s (not the first segment: no reset) */
    frames(30);
    gevrLiveSplitStageReset();
    settle();
    /* mission 2 again, completed at 0.75 s: the run's last segment */
    frames(40);
    gevrLiveSplitMissionComplete(45);
    gevrLiveSplitStageReset();
    settle();
    /* a new run, lost on its first mission: reset */
    frames(20);
    gevrLiveSplitStageReset();
    settle();
    printf("done\n");
    return 0;
}
