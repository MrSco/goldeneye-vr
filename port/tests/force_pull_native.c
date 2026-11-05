#include "gevr_pull.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int s_fail;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); s_fail++; } } while (0)

static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

int main(void)
{
    const float p[3] = { 0.0f, 0.0f, 0.0f };
    const float f[3] = { 0.0f, 0.0f, -1.0f };   /* pointing ahead */
    const float cm = 1.0f;
    float t[3], out[3];
    float s0, s1;

    /* on the ray, within reach */
    t[0] = 0.0f; t[1] = 0.0f; t[2] = -200.0f;
    s0 = gevrPullScore(p, f, t, cm, 0.0f);
    CHECK(s0 >= 0.0f && s0 < 0.002f);

    /* past 3.5 m: out */
    t[2] = -360.0f;
    CHECK(gevrPullScore(p, f, t, cm, 0.0f) < 0.0f);

    /* 10 degrees off: in, and scored worse than on the ray */
    t[0] = 200.0f * sinf(10.0f * 3.14159265f / 180.0f);
    t[2] = -200.0f * cosf(10.0f * 3.14159265f / 180.0f);
    s1 = gevrPullScore(p, f, t, cm, 0.0f);
    CHECK(s1 > s0);

    /* 20 degrees off: out level, in when pointing down */
    t[0] = 200.0f * sinf(20.0f * 3.14159265f / 180.0f);
    t[2] = -200.0f * cosf(20.0f * 3.14159265f / 180.0f);
    CHECK(gevrPullScore(p, f, t, cm, 0.0f) < 0.0f);
    CHECK(gevrPullScore(p, f, t, cm, 0.8f) >= 0.0f);

    /* behind the hand: out even when close */
    t[0] = 0.0f; t[1] = 0.0f; t[2] = 50.0f;
    CHECK(gevrPullScore(p, f, t, cm, 0.8f) < 0.0f);

    /* the angle outweighs distance: 5 degrees off at 1 m loses to on the ray at 3 m */
    {
        float a[3] = { 100.0f * sinf(5.0f * 3.14159265f / 180.0f), 0.0f, -100.0f * cosf(5.0f * 3.14159265f / 180.0f) };
        float b[3] = { 0.0f, 0.0f, -300.0f };
        CHECK(gevrPullScore(p, f, b, cm, 0.0f) < gevrPullScore(p, f, a, cm, 0.0f));
    }

    /* units: the same layout at 5 view units a cm (the Dam) */
    t[0] = 0.0f; t[1] = 0.0f; t[2] = -300.0f * 5.0f;
    CHECK(gevrPullScore(p, f, t, 5.0f, 0.0f) >= 0.0f);
    t[2] = -360.0f * 5.0f;
    CHECK(gevrPullScore(p, f, t, 5.0f, 0.0f) < 0.0f);

    /* durations */
    CHECK(near(gevrPullDuration(0.0f), 0.30f, 1e-5f));
    CHECK(near(gevrPullDuration(350.0f), 0.40f, 1e-5f));
    CHECK(near(gevrPullDuration(9999.0f), 0.40f, 1e-5f));

    /* the flight: starts at from, ends at to, lifted in the middle, eased out */
    {
        const float from[3] = { 0.0f, 0.0f, 0.0f };
        const float to[3] = { 100.0f, 0.0f, 0.0f };
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        float prev = -1.0f;
        int i;

        gevrPullFlight(from, to, up, 20.0f, 0.0f, out);
        CHECK(near(out[0], 0.0f, 1e-4f) && near(out[1], 0.0f, 1e-4f));
        gevrPullFlight(from, to, up, 20.0f, 1.0f, out);
        CHECK(near(out[0], 100.0f, 1e-4f) && near(out[1], 0.0f, 1e-4f));
        gevrPullFlight(from, to, up, 20.0f, 0.5f, out);
        CHECK(near(out[1], 20.0f, 1e-4f));
        CHECK(out[0] > 50.0f);   /* eased out: past halfway at half time */
        for (i = 0; i <= 20; i++)
        {
            gevrPullFlight(from, to, up, 20.0f, i / 20.0f, out);
            CHECK(out[0] >= prev);   /* never back */
            prev = out[0];
        }
        gevrPullFlight(from, to, up, 20.0f, 2.0f, out);   /* clamped */
        CHECK(near(out[0], 100.0f, 1e-4f));
    }

    /* the hand rules */
    CHECK(gevrPullChooseHand(1, 1, 1, 1) == 1);    /* off hand allowed: off hand */
    CHECK(gevrPullChooseHand(1, 0, 1, 1) == 0);    /* off hand can't: empty gun hand */
    CHECK(gevrPullChooseHand(1, 0, 0, 1) == -1);   /* gun hand busy: inventory */
    CHECK(gevrPullChooseHand(1, 0, 1, 0) == -1);   /* gun hand may not: inventory */
    CHECK(gevrPullChooseHand(0, 1, 1, 1) == 0);    /* gun hand pulls: gun hand */
    CHECK(gevrPullChooseHand(0, 0, 1, 0) == -1);

    if (s_fail)
    {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("force pull: all passed\n");
    return 0;
}
