#include "gevr_disarm.h"
#include <stdio.h>

static int s_fail;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); s_fail++; } } while (0)

/* run the reaction beat out */
static void react(GevrDisarmState *s)
{
    int i;
    for (i = 0; i < GEVR_DISARM_REACT; i++)
    {
        CHECK(gevrDisarmStep(s, 1, -1.0f, 500.0f, 1, 1, 0) == GEVR_DISARM_FACE);
    }
}

int main(void)
{
    GevrDisarmState s;
    int i, act;

    /* the beat: he faces the player, or does his own thing with no one there */
    gevrDisarmInit(&s, 1);
    CHECK(gevrDisarmStep(&s, 1, 300.0f, -1.0f, 1, 1, 0) == GEVR_DISARM_IDLE);
    gevrDisarmInit(&s, 1);
    CHECK(gevrDisarmWantsGun(&s));
    react(&s);

    /* a gun in sight: run to it, stand at it, take it */
    CHECK(gevrDisarmStep(&s, 1, 600.0f, 500.0f, 1, 1, 0) == GEVR_DISARM_GO_GUN);
    CHECK(gevrDisarmStep(&s, 1, 90.0f, 500.0f, 1, 1, 0) == GEVR_DISARM_AT_GUN);
    for (i = 2, act = 0; i <= GEVR_DISARM_TAKE_PAUSE; i++)
    {
        act = gevrDisarmStep(&s, 1, 90.0f, 500.0f, 1, 1, 0);
    }
    CHECK(act == GEVR_DISARM_TAKE_GUN);

    /* stepping away from it starts the pause again */
    gevrDisarmInit(&s, 1);
    react(&s);
    CHECK(gevrDisarmStep(&s, GEVR_DISARM_TAKE_PAUSE / 2, 90.0f, 500.0f, 1, 1, 0) == GEVR_DISARM_AT_GUN);
    CHECK(gevrDisarmStep(&s, 1, 200.0f, 500.0f, 1, 1, 0) == GEVR_DISARM_GO_GUN);
    CHECK(gevrDisarmStep(&s, GEVR_DISARM_TAKE_PAUSE / 2, 90.0f, 500.0f, 1, 1, 0) == GEVR_DISARM_AT_GUN);

    /* not reached in time (PD 300 ticks): given up, fists */
    gevrDisarmInit(&s, 1);
    react(&s);
    for (i = 0, act = 0; i < 400 && act != GEVR_DISARM_GIVE_UP_GUN; i++)
    {
        act = gevrDisarmStep(&s, 1, 600.0f, 500.0f, 1, 1, 0);
        CHECK(act == GEVR_DISARM_GO_GUN || act == GEVR_DISARM_GIVE_UP_GUN);
    }
    CHECK(act == GEVR_DISARM_GIVE_UP_GUN && i > GEVR_DISARM_FETCH_MAX - 2 && s.phase == GEVR_DISARM_MELEE);

    /* no gun: chase, face, punch, then wait out the cooldown */
    gevrDisarmInit(&s, 1);
    react(&s);
    CHECK(gevrDisarmStep(&s, 1, -1.0f, 500.0f, 1, 1, 0) == GEVR_DISARM_CHASE);
    CHECK(s.phase == GEVR_DISARM_MELEE && !gevrDisarmWantsGun(&s));
    CHECK(gevrDisarmStep(&s, 1, -1.0f, 100.0f, 0, 1, 0) == GEVR_DISARM_CHASE);   /* a door between */
    CHECK(gevrDisarmStep(&s, 1, -1.0f, 100.0f, 1, 0, 0) == GEVR_DISARM_FACE);    /* not in front yet */
    CHECK(gevrDisarmStep(&s, 1, -1.0f, 100.0f, 1, 1, 0) == GEVR_DISARM_STRIKE);
    CHECK(gevrDisarmStep(&s, 30, -1.0f, 100.0f, 1, 1, 1) == GEVR_DISARM_WAIT);   /* the punch plays */
    CHECK(gevrDisarmStep(&s, 1, -1.0f, 100.0f, 1, 1, 0) == GEVR_DISARM_FACE);    /* cooling down */
    CHECK(gevrDisarmStep(&s, GEVR_DISARM_COOLDOWN, -1.0f, 100.0f, 1, 1, 0) == GEVR_DISARM_STRIKE);
    /* the player dies or leaves: his own AI */
    CHECK(gevrDisarmStep(&s, 1, -1.0f, -1.0f, 1, 1, 0) == GEVR_DISARM_IDLE);

    /* in melee he looks again once a second, and goes for a gun unless the player is in reach */
    gevrDisarmInit(&s, 1);
    react(&s);
    CHECK(gevrDisarmStep(&s, 1, -1.0f, 500.0f, 1, 1, 0) == GEVR_DISARM_CHASE);
    for (i = 0; i < GEVR_DISARM_SCAN; i++)
    {
        CHECK(!gevrDisarmWantsGun(&s));
        gevrDisarmStep(&s, 1, -1.0f, 500.0f, 1, 1, 0);
    }
    CHECK(gevrDisarmWantsGun(&s));
    CHECK(gevrDisarmStep(&s, 1, 100.0f, 110.0f, 1, 1, 0) == GEVR_DISARM_STRIKE);   /* player in reach: punch */
    CHECK(!gevrDisarmWantsGun(&s));
    for (i = 0; i < GEVR_DISARM_SCAN; i++)
    {
        gevrDisarmStep(&s, 1, -1.0f, 500.0f, 1, 1, 0);
    }
    CHECK(gevrDisarmWantsGun(&s));
    CHECK(gevrDisarmStep(&s, 1, 400.0f, 500.0f, 1, 1, 0) == GEVR_DISARM_GO_GUN);
    CHECK(s.phase == GEVR_DISARM_FETCH && s.fetch60 == 0);

    /* online (no fetching): fists only, never asked to look */
    gevrDisarmInit(&s, 0);
    CHECK(s.phase == GEVR_DISARM_MELEE && !gevrDisarmWantsGun(&s));
    react(&s);
    for (i = 0; i < 200; i++)
    {
        CHECK(!gevrDisarmWantsGun(&s));
        act = gevrDisarmStep(&s, 1, 50.0f, 500.0f, 1, 1, 0);
        CHECK(act == GEVR_DISARM_CHASE);
    }

    if (s_fail)
    {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("disarm: all passed\n");
    return 0;
}
