#ifndef GEVR_DISARM_H
#define GEVR_DISARM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A guard whose gun force pull took (docs/force-pull.md "After a disarm"):
 * Perfect Dark's lost-gun script (gailists.c func0007_alerted) and hand
 * combat (func000f_hand_combat), as GoldenEye has neither. He goes for a
 * free gun he can see, else (none, or 300 ticks without reaching it) runs
 * at the player and punches, looking for a gun again every second. The
 * game side is src/game/gevr_disarmed.c; this is the decision alone, with no
 * game state (port/tests/test_disarm.py). Times are 60 Hz ticks, distances cm.
 */

#define GEVR_DISARM_REACT      20      /* the beat after losing it: he faces the player */
#define GEVR_DISARM_FETCH_MAX  300     /* PD: the gun not reached in 300 ticks, he gives up on it */
#define GEVR_DISARM_TAKE_CM    100.0f  /* PD if_distance_to_gun_lt(100) */
#define GEVR_DISARM_TAKE_PAUSE 20      /* stood at the gun before it is in his hand */
#define GEVR_DISARM_SCAN       60      /* in melee: a look for a gun once a second */
#define GEVR_DISARM_STRIKE_CM  120.0f  /* PD hand combat: run in to 120 */
#define GEVR_DISARM_REACH_CM   150.0f  /* the punch lands within this at its hit frame */
#define GEVR_DISARM_COOLDOWN   75      /* from one punch's start to the next */
#define GEVR_DISARM_SEARCH_CM  1500.0f /* a gun further off is not looked at */

enum
{
    GEVR_DISARM_FETCH = 1,   /* going for a gun */
    GEVR_DISARM_MELEE = 2    /* hand combat */
};

/* what the guard does this tick */
enum
{
    GEVR_DISARM_IDLE = 0,     /* no one to fight: his own AI */
    GEVR_DISARM_FACE,         /* stand and turn to the player */
    GEVR_DISARM_GO_GUN,       /* run to the gun */
    GEVR_DISARM_AT_GUN,       /* stand at it */
    GEVR_DISARM_TAKE_GUN,     /* it goes into his hand */
    GEVR_DISARM_GIVE_UP_GUN,  /* not reached in time: that gun is left alone */
    GEVR_DISARM_CHASE,        /* run at the player */
    GEVR_DISARM_STRIKE,       /* start a punch */
    GEVR_DISARM_WAIT          /* a punch is playing: leave it be */
};

typedef struct GevrDisarmState
{
    int phase;
    int canFetch;   /* guns may be gone for (solo; online the floor copies would part) */
    int react60;
    int fetch60;
    int take60;
    int scan60;
    int cool60;
} GevrDisarmState;

void gevrDisarmInit(GevrDisarmState *s, int canFetch);

/* the game should look for a gun this tick (and pass its distance, or -1) */
int gevrDisarmWantsGun(const GevrDisarmState *s);

/*
 * One think, ticks since the last. gunCm: the gun he is after, or the nearest
 * free one found when gevrDisarmWantsGun said to look, -1 none. playerCm: his
 * target, -1 none (dead, gone). clear: no door or wall between them; front:
 * the player within his punch's arc. striking: a punch is playing.
 */
int gevrDisarmStep(GevrDisarmState *s, int ticks, float gunCm, float playerCm, int clear, int front, int striking);

#ifdef __cplusplus
}
#endif

#endif
