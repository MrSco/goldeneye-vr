#include "gevr_teleport.h"
#include <math.h>
#include <stdio.h>

static int s_fail;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); s_fail++; } } while (0)

static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static const float TICK = 1.0f / 60.0f;

/* the farthest a player gets in `seconds`, jumping `hop` cm whenever it is allowed */
static float travel(float seconds, float hop, float speed)
{
    GevrTeleport t;
    float gone = 0.0f, at;

    gevrTeleportReset(&t);
    for (at = 0.0f; at < seconds; at += TICK)
    {
        float want = hop < gevrTeleportReach(&t) ? hop : gevrTeleportReach(&t);

        gevrTeleportRest(&t, TICK, speed);
        if (gevrTeleportSpend(&t, want))
        {
            gone += want;
        }
    }
    return gone;
}

int main(void)
{
    GevrTeleport t;
    float hand[3] = { 0.0f, 120.0f, 0.0f }, dir[3], end[3], p[3], q[3];
    int i, jumps;

    /* the stick: forward aims until let go, then confirms once */
    gevrTeleportReset(&t);
    CHECK(gevrTeleportStick(&t, 0.0f, 0.9f) == GEVR_TP_NONE);     /* held from before: not armed */
    CHECK(gevrTeleportStick(&t, 0.0f, 0.0f) == GEVR_TP_NONE);
    CHECK(gevrTeleportStick(&t, 0.0f, 0.5f) == GEVR_TP_NONE);     /* not far enough */
    CHECK(gevrTeleportStick(&t, 0.0f, 0.9f) == GEVR_TP_AIM);
    CHECK(gevrTeleportStick(&t, 0.8f, 0.4f) == GEVR_TP_AIM);      /* wandering keeps the aim */
    CHECK(gevrTeleportStick(&t, 0.0f, -0.9f) == GEVR_TP_AIM);
    CHECK(gevrTeleportStick(&t, 0.0f, 0.1f) == GEVR_TP_CONFIRM);
    CHECK(gevrTeleportStick(&t, 0.0f, 0.0f) == GEVR_TP_NONE);     /* once */

    /* 40 degrees off forward still aims; 60 is a side hop */
    CHECK(gevrTeleportStick(&t, sinf(0.698f), cosf(0.698f)) == GEVR_TP_AIM);
    CHECK(gevrTeleportStick(&t, 0.0f, 0.0f) == GEVR_TP_CONFIRM);
    CHECK(gevrTeleportStick(&t, sinf(1.047f), cosf(1.047f)) == GEVR_TP_HOP_RIGHT);
    CHECK(gevrTeleportStick(&t, 1.0f, 0.0f) == GEVR_TP_NONE);     /* one hop a push */
    CHECK(gevrTeleportStick(&t, 0.0f, 0.0f) == GEVR_TP_NONE);
    CHECK(gevrTeleportStick(&t, -1.0f, 0.0f) == GEVR_TP_HOP_LEFT);
    CHECK(gevrTeleportStick(&t, 0.0f, 0.0f) == GEVR_TP_NONE);
    CHECK(gevrTeleportStick(&t, 0.2f, -1.0f) == GEVR_TP_HOP_BACK);
    CHECK(gevrTeleportStick(&t, 0.0f, 0.0f) == GEVR_TP_NONE);

    /* the stick taken away mid-aim (a menu): no jump, and held on return starts nothing */
    CHECK(gevrTeleportStick(&t, 0.0f, 1.0f) == GEVR_TP_AIM);
    gevrTeleportStickDrop(&t);
    CHECK(gevrTeleportStick(&t, 0.0f, 1.0f) == GEVR_TP_NONE);
    CHECK(gevrTeleportStick(&t, 0.0f, 0.0f) == GEVR_TP_NONE);
    CHECK(gevrTeleportStick(&t, 0.0f, 1.0f) == GEVR_TP_AIM);
    CHECK(gevrTeleportStick(&t, NAN, 1.0f) == GEVR_TP_NONE);
    CHECK(!t.aiming);

    /* stamina: a rested player has one full jump, and pays for what is used */
    gevrTeleportReset(&t);
    CHECK(near(gevrTeleportReach(&t), GEVR_TP_MAX_CM, 0.01f));
    CHECK(!gevrTeleportSpend(&t, GEVR_TP_MAX_CM + 50.0f));        /* farther than the reach */
    CHECK(!gevrTeleportSpend(&t, GEVR_TP_MIN_CM - 1.0f));         /* shorter than a jump */
    CHECK(!gevrTeleportSpend(&t, NAN));
    CHECK(near(t.stamina, GEVR_TP_MAX_CM, 0.01f));                /* a refusal costs nothing */
    CHECK(gevrTeleportSpend(&t, 400.0f));
    CHECK(near(t.stamina, 200.0f, 0.01f));
    CHECK(!gevrTeleportSpend(&t, 100.0f));                        /* the cooldown */
    CHECK(near(t.stamina, 200.0f, 0.01f));
    gevrTeleportRest(&t, GEVR_TP_COOLDOWN_S + 0.001f, 1.0f);
    CHECK(gevrTeleportSpend(&t, 100.0f));
    /* spent: nothing until a shortest jump is back in hand */
    gevrTeleportReset(&t);
    CHECK(gevrTeleportSpend(&t, GEVR_TP_MAX_CM));
    CHECK(gevrTeleportReach(&t) == 0.0f);
    gevrTeleportRest(&t, 0.05f, 1.0f);
    CHECK(gevrTeleportReach(&t) == 0.0f);
    gevrTeleportRest(&t, 0.2f, 1.0f);
    CHECK(gevrTeleportReach(&t) >= GEVR_TP_MIN_CM);
    /* it never holds more than one jump, and a stall is no rest */
    gevrTeleportReset(&t);
    CHECK(gevrTeleportSpend(&t, GEVR_TP_MAX_CM));
    gevrTeleportRest(&t, 100.0f, 1.0f);
    CHECK(t.stamina <= GEVR_TP_RUN_CM_S * 0.25f + 1.0f);
    for (i = 0; i < 600; i++) gevrTeleportRest(&t, TICK, 1.0f);
    CHECK(near(t.stamina, GEVR_TP_MAX_CM, 0.01f));
    gevrTeleportRest(&t, -1.0f, 1.0f);
    gevrTeleportRest(&t, NAN, 1.0f);
    CHECK(near(t.stamina, GEVR_TP_MAX_CM, 0.01f));

    /*
     * The fairness rule: over a minute, however the jumps are sized, a
     * teleporting player covers less ground than one running forward
     * (the first full jump is the only head start), and the movement
     * speed rule scales both alike.
     */
    {
        static const float hops[] = { GEVR_TP_MIN_CM, GEVR_TP_HOP_CM, 250.0f, GEVR_TP_MAX_CM };

        for (i = 0; i < 4; i++)
        {
            const float gone = travel(60.0f, hops[i], 1.0f);

            CHECK(gone <= GEVR_TP_RUN_CM_S * 60.0f);
            CHECK(gone <= GEVR_TP_MAX_CM + GEVR_TP_RUN_CM_S * GEVR_TP_RECHARGE * 60.0f + 1.0f);
        }
        CHECK(travel(60.0f, GEVR_TP_MAX_CM, 1.0f) > GEVR_TP_RUN_CM_S * 0.75f * 60.0f);   /* nor crippled */
        CHECK(travel(60.0f, GEVR_TP_MAX_CM, 0.5f) <= GEVR_TP_RUN_CM_S * 0.5f * 60.0f);
        CHECK(travel(60.0f, GEVR_TP_MAX_CM, 2.0f) <= GEVR_TP_RUN_CM_S * 2.0f * 60.0f);
        /* two seconds of burst: the stamina in hand plus what fills, no more */
        CHECK(travel(2.0f, GEVR_TP_MAX_CM, 1.0f) <= GEVR_TP_MAX_CM + GEVR_TP_RUN_CM_S * GEVR_TP_RECHARGE * 2.0f + 1.0f);
    }
    /* the cooldown bounds the jumps a second whatever their size */
    gevrTeleportReset(&t);
    jumps = 0;
    for (i = 0; i < 60; i++)
    {
        gevrTeleportRest(&t, TICK, 1.0f);
        jumps += gevrTeleportSpend(&t, GEVR_TP_MIN_CM);
    }
    CHECK(jumps <= 4);

    /* the blink: black exactly when the body moves, clear again after, never stuck */
    gevrTeleportReset(&t);
    CHECK(gevrTeleportBlack(&t) == 0.0f);
    CHECK(!gevrTeleportBlinkStep(&t, TICK));
    CHECK(gevrTeleportBlinkStart(&t));
    CHECK(!gevrTeleportBlinkStart(&t));                           /* one at a time */
    jumps = 0;
    for (i = 0; i < 60 && t.fade != GEVR_TP_FADE_IDLE; i++)
    {
        const float before = gevrTeleportBlack(&t);

        if (gevrTeleportBlinkStep(&t, TICK))
        {
            jumps++;
            CHECK(near(gevrTeleportBlack(&t), 1.0f, 0.001f));     /* the jump's frame is black */
            CHECK(before > 0.0f);                                 /* and it darkened first */
        }
    }
    CHECK(jumps == 1);
    CHECK(t.fade == GEVR_TP_FADE_IDLE && gevrTeleportBlack(&t) == 0.0f);
    CHECK(i * TICK <= GEVR_TP_FADE_OUT_S + GEVR_TP_FADE_IN_S + 3.0f * TICK);
    /* a second blink while the first clears darkens from where it is, not from clear */
    CHECK(gevrTeleportBlinkStart(&t));
    while (!gevrTeleportBlinkStep(&t, TICK)) { }
    gevrTeleportBlinkStep(&t, TICK);
    {
        const float mid = gevrTeleportBlack(&t);

        CHECK(mid > 0.0f && mid < 1.0f);
        CHECK(gevrTeleportBlinkStart(&t));
        CHECK(gevrTeleportBlack(&t) >= mid - 0.01f);
    }

    /* the arc: level from 1.2 m it comes down between 3 and 4.5 m; 40 degrees up passes the full jump */
    dir[0] = 0.0f; dir[1] = 0.0f; dir[2] = 1.0f;
    for (i = 1; i < 200; i++)
    {
        gevrTeleportArcPoint(hand, dir, i * 0.01f, p);
        if (p[1] <= 0.0f) break;
    }
    CHECK(p[2] > 300.0f && p[2] < 450.0f);
    dir[1] = sinf(0.698f); dir[2] = cosf(0.698f);
    for (i = 1; i < 400; i++)
    {
        gevrTeleportArcPoint(hand, dir, i * 0.01f, p);
        if (p[1] <= 0.0f) break;
    }
    CHECK(p[2] > GEVR_TP_MAX_CM);
    /* straight down lands at the feet: under the shortest jump, so a cancel */
    dir[0] = 0.0f; dir[1] = -1.0f; dir[2] = 0.0f;
    gevrTeleportArcPoint(hand, dir, 0.15f, p);
    CHECK(p[1] < 0.0f && fabsf(p[0]) < 1.0f && fabsf(p[2]) < 1.0f);

    /* the drawn curve starts at the hand, leaves it along its aim and ends on the mark */
    dir[0] = 0.0f; dir[1] = 0.6f; dir[2] = 0.8f;
    end[0] = 100.0f; end[1] = 0.0f; end[2] = 400.0f;
    gevrTeleportCurvePoint(hand, dir, end, 0.0f, p);
    CHECK(near(p[0], hand[0], 0.01f) && near(p[1], hand[1], 0.01f) && near(p[2], hand[2], 0.01f));
    gevrTeleportCurvePoint(hand, dir, end, 1.0f, p);
    CHECK(near(p[0], end[0], 0.01f) && near(p[1], end[1], 0.01f) && near(p[2], end[2], 0.01f));
    gevrTeleportCurvePoint(hand, dir, end, 0.01f, q);
    {
        const float dx = q[0] - hand[0], dy = q[1] - hand[1], dz = q[2] - hand[2];
        const float len = sqrtf(dx * dx + dy * dy + dz * dz);

        CHECK(len > 0.0f && (dx * dir[0] + dy * dir[1] + dz * dir[2]) / len > 0.99f);
    }
    gevrTeleportCurvePoint(hand, dir, end, 0.5f, p);
    CHECK(p[1] > 60.0f);                                          /* it arcs over the straight line */

    if (s_fail)
    {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("teleport: ok\n");
    return 0;
}
