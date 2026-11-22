#include "gevr_rack.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int s_fail;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); s_fail++; } } while (0)

static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

int main(void)
{
    GevrRack r;
    int loaded, i;
    const float T = GEVR_RACK_BOLT_CM;

    /* the chamber: dry once empty with the magazine in, never by a magazine taken out with rounds */
    memset(&r, 0, sizeof(r));
    gevrRackNote(&r, 1, 5);
    CHECK(!r.dry);
    gevrRackNote(&r, 0, 0);            /* its magazine out, the round still chambered */
    CHECK(!r.dry);
    loaded = 30;
    CHECK(!gevrRackHold(&r, &loaded) && loaded == 30 && r.pending == 0);   /* a fresh one in: ready */
    gevrRackNote(&r, 1, 0);            /* shot dry */
    CHECK(r.dry);
    gevrRackNote(&r, 0, 0);
    CHECK(r.dry);
    loaded = 0;
    CHECK(!gevrRackHold(&r, &loaded) && r.pending == 0);                  /* an empty magazine holds nothing */
    loaded = 30;
    CHECK(gevrRackHold(&r, &loaded) && loaded == 0 && r.pending == 30);
    gevrRackNote(&r, 1, 0);            /* waiting: still dry, whatever the gun shows */
    CHECK(r.dry && r.pending == 30);

    /* nothing waiting: no rack to take */
    {
        GevrRack idle;

        memset(&idle, 0, sizeof(idle));
        CHECK(gevrRackStep(&idle, 1, 1, 0.0f, 0.0f, T) == GEVR_RACK_IDLE && !idle.held);
    }

    /* the grab: a fresh grip, near enough */
    CHECK(gevrRackStep(&r, 1, 0, 0.0f, 0.0f, T) == GEVR_RACK_IDLE);                       /* a grip held from before */
    CHECK(gevrRackStep(&r, 1, 1, GEVR_RACK_REACH_CM + 0.1f, 0.0f, T) == GEVR_RACK_IDLE);  /* too far */
    CHECK(gevrRackStep(&r, 0, 0, 0.0f, 0.0f, T) == GEVR_RACK_IDLE);
    CHECK(gevrRackStep(&r, 1, 1, GEVR_RACK_REACH_CM - 0.1f, 10.0f, T) == GEVR_RACK_GRABBED && r.held);
    CHECK(gevrRackNudge(&r) == 0.0f);

    /* pulled part of the way the gun gives with it; forward of the grab is no pull */
    CHECK(gevrRackStep(&r, 1, 0, 99.0f, 10.0f + T * 0.5f, T) == GEVR_RACK_IDLE);          /* the hand may leave the zone once it holds */
    CHECK(near(r.pull, T * 0.5f, 1e-4f) && near(gevrRackNudge(&r), GEVR_RACK_NUDGE_CM * 0.5f, 1e-4f));
    CHECK(gevrRackStep(&r, 1, 0, 0.0f, 5.0f, T) == GEVR_RACK_IDLE && r.pull == 0.0f);
    CHECK(gevrRackStep(&r, 1, 0, 0.0f, NAN, T) == GEVR_RACK_IDLE && r.pull == 0.0f);
    /* let go short: nothing, and the rounds still wait */
    CHECK(gevrRackStep(&r, 0, 0, 0.0f, 10.0f + T * 0.9f, T) == GEVR_RACK_SHORT && !r.held && r.pending == 30);
    CHECK(gevrRackNudge(&r) == 0.0f);

    /* all the way back clicks once, however much farther the hand goes; let go there: done */
    CHECK(gevrRackStep(&r, 1, 1, 1.0f, 0.0f, T) == GEVR_RACK_GRABBED);
    CHECK(gevrRackStep(&r, 1, 0, 1.0f, T, T) == GEVR_RACK_COCKED && r.cocked);
    CHECK(gevrRackStep(&r, 1, 0, 1.0f, T * 3.0f, T) == GEVR_RACK_IDLE);
    CHECK(near(r.pull, T, 1e-4f) && near(gevrRackNudge(&r), GEVR_RACK_NUDGE_CM, 1e-4f));  /* no farther than its throw */
    CHECK(gevrRackStep(&r, 0, 0, 1.0f, T * 3.0f, T) == GEVR_RACK_DONE && !r.held && !r.cocked);
    gevrRackChamber(&r, &loaded);
    CHECK(loaded == 30 && r.pending == 0 && !r.dry);
    /* the jump forward as the bolt goes home, then still */
    CHECK(near(gevrRackNudge(&r), -GEVR_RACK_KICK_CM, 1e-4f));
    gevrRackTick(&r, 3);
    CHECK(gevrRackNudge(&r) < 0.0f && gevrRackNudge(&r) > -GEVR_RACK_KICK_CM);
    for (i = 0; i < 4; i++) gevrRackTick(&r, 1);
    CHECK(gevrRackNudge(&r) == 0.0f);
    gevrRackTick(&r, -5);
    CHECK(r.kick == 0);

    /* gripped all the way through: done when the hand has come most of the way back in */
    memset(&r, 0, sizeof(r));
    r.dry = 1; loaded = 7;
    CHECK(gevrRackHold(&r, &loaded));
    CHECK(gevrRackStep(&r, 1, 1, 0.0f, 0.0f, GEVR_RACK_SLIDE_CM) == GEVR_RACK_GRABBED);
    CHECK(gevrRackStep(&r, 1, 0, 0.0f, GEVR_RACK_SLIDE_CM, GEVR_RACK_SLIDE_CM) == GEVR_RACK_COCKED);
    CHECK(gevrRackStep(&r, 1, 0, 0.0f, GEVR_RACK_SLIDE_CM * 0.6f, GEVR_RACK_SLIDE_CM) == GEVR_RACK_IDLE);
    CHECK(gevrRackStep(&r, 1, 0, 0.0f, GEVR_RACK_SLIDE_CM * 0.5f, GEVR_RACK_SLIDE_CM) == GEVR_RACK_DONE);
    CHECK(r.kick == GEVR_RACK_KICK_TICKS && !r.held);
    gevrRackChamber(&r, &loaded);
    CHECK(loaded == 7);
    CHECK(gevrRackStep(&r, 1, 0, 0.0f, 0.0f, GEVR_RACK_SLIDE_CM) == GEVR_RACK_IDLE);       /* nothing more to rack */

    /* the magazine out again before the rack: its rounds go with it, the chamber still empty */
    memset(&r, 0, sizeof(r));
    r.dry = 1; loaded = 30;
    CHECK(gevrRackHold(&r, &loaded));
    CHECK(gevrRackStep(&r, 1, 1, 0.0f, 0.0f, T) == GEVR_RACK_GRABBED);
    gevrRackGiveBack(&r, &loaded);
    CHECK(loaded == 30 && r.pending == 0 && r.dry && !r.held);
    /* ... and with nothing waiting it leaves the gun's rounds alone */
    loaded = 12;
    gevrRackGiveBack(&r, &loaded);
    gevrRackChamber(&r, &loaded);
    CHECK(loaded == 12 && r.dry);

    if (s_fail)
    {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("rack: ok\n");
    return 0;
}
