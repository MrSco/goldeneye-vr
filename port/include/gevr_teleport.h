#ifndef GEVR_TELEPORT_H
#define GEVR_TELEPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Teleport locomotion (docs/teleport.md; Comfort > Movement, ini Teleport):
 * the move stick points an arc from its hand instead of sliding Bond along.
 * Push it forward to aim, let go to jump there behind a short blink; a flick
 * back, left or right is a short hop that way. The game side is
 * src/game/gevr_teleport.c; this is its arithmetic and its state, with no
 * game state (port/tests/test_teleport.py). Distances are game units
 * (centimetres), times seconds.
 *
 * Fair against a player who walks: a jump is paid for from a stamina of
 * distance, which fills at nine tenths of Bond's forward run and holds one
 * full jump. Over any stretch of time a teleporting player covers less
 * ground than a running one, and far less than one running diagonally
 * (docs/speedrun.md: 710 units a second). The jump itself goes only where
 * the body could walk in a straight line (gevr_teleport.c).
 */

#define GEVR_TP_MAX_CM       600.0f   /* the farthest jump, and a rested player's stamina */
#define GEVR_TP_MIN_CM       50.0f    /* nearer than this is no jump (point at your feet to cancel) */
#define GEVR_TP_HOP_CM       100.0f   /* a flick back or sideways */
#define GEVR_TP_RUN_CM_S     503.0f   /* Bond's forward run at 100% (docs/speedrun.md, measured) */
#define GEVR_TP_RECHARGE     0.9f     /* of that run: what the stamina fills by */
#define GEVR_TP_COOLDOWN_S   0.25f    /* between two jumps */
#define GEVR_TP_FADE_OUT_S   0.05f    /* the blink: to black, the jump, */
#define GEVR_TP_FADE_IN_S    0.12f    /* and back */
#define GEVR_TP_ARC_CM_S     750.0f   /* the arc's launch speed: 45 degrees up reaches about the full jump */
#define GEVR_TP_GRAVITY      980.0f

#define GEVR_TP_STICK_PUSH   0.6f     /* past this from the centre starts an aim or a hop */
#define GEVR_TP_STICK_REST   0.3f     /* inside this the stick is let go */

/* gevrTeleportStick's answers */
enum {
    GEVR_TP_NONE,
    GEVR_TP_AIM,        /* held forward: show the arc */
    GEVR_TP_CONFIRM,    /* let go after aiming: jump to the arc's end */
    GEVR_TP_HOP_BACK,
    GEVR_TP_HOP_LEFT,
    GEVR_TP_HOP_RIGHT
};

enum { GEVR_TP_FADE_IDLE, GEVR_TP_FADE_OUT, GEVR_TP_FADE_IN };

typedef struct GevrTeleport {
    float stamina;      /* cm of travel in hand, 0..GEVR_TP_MAX_CM */
    float cooldown;     /* seconds until the next jump may start */
    int armed;          /* the stick has been let go since its last push */
    int aiming;
    int fade;           /* GEVR_TP_FADE_* */
    float fadeTime;     /* seconds into it */
} GevrTeleport;

/* A level's start, a respawn: rested, nothing held. */
void gevrTeleportReset(GevrTeleport *t);

/*
 * The move stick this tick (x right, y forward, -1..1) to what it asks. An
 * aim starts on a push within 50 degrees of forward and lasts until the stick
 * is let go, wherever it wanders meanwhile; any other push is one hop, and
 * the next needs the stick let go first.
 */
int gevrTeleportStick(GevrTeleport *t, float x, float y);
/* The stick was taken away (a menu, the weapon wheel, a vehicle): nothing held, no jump. */
void gevrTeleportStickDrop(GevrTeleport *t);

/* Time passes: the stamina fills (speed: the movement speed rule's factor) and the cooldown runs. */
void gevrTeleportRest(GevrTeleport *t, float dt, float speed);
/* How far the stamina reaches now: 0 with less than the shortest jump in hand. */
float gevrTeleportReach(const GevrTeleport *t);
/* Pay for a jump of cm and start the cooldown; 0 (and nothing spent) during
 * a cooldown, or if it is shorter than a jump or farther than the reach. */
int gevrTeleportSpend(GevrTeleport *t, float cm);

/* The blink. Start: 1 if it began (0: one is under way). */
int gevrTeleportBlinkStart(GevrTeleport *t);
/* Advance it; 1 on the tick the view is black and the body should move. */
int gevrTeleportBlinkStep(GevrTeleport *t, float dt);
/* How black the view is, 0..1. */
float gevrTeleportBlack(const GevrTeleport *t);

/* The arc t seconds from the hand, launched along unit dir (y up). */
void gevrTeleportArcPoint(const float hand[3], const float dir[3], float t, float out[3]);
/*
 * A drawn curve from the hand to where the jump lands (the arc itself ends
 * where it meets the floor, which a wall or the stamina may cut short): a
 * quadratic through a control point out along the hand's aim, so it leaves
 * the hand the way it points and comes down on the mark. u 0..1.
 */
void gevrTeleportCurvePoint(const float hand[3], const float dir[3], const float end[3], float u, float out[3]);

#ifdef __cplusplus
}
#endif
#endif
