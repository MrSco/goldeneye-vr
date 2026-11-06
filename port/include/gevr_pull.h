#ifndef GEVR_PULL_H
#define GEVR_PULL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Force pull (docs/force-pull.md): GEVR PC vr457's force pull, an empty hand
 * pointed at something within reach and squeezed flies it to that hand. The
 * game side is src/game/gevr_forcepull.c; this is its arithmetic, with no
 * game state (port/tests/test_force_pull.py). No atan2f/acosf here: the
 * game's own replace the C library's (see gevr_bodyslot.c), so angles are
 * compared as cosines.
 */

#define GEVR_PULL_RANGE_CM      350.0f   /* the Magnet watch's reach (propobj.c 122500 = 350^2) */
#define GEVR_PULL_COS_CONE      0.9659f  /* cos 15 degrees: the pointing cone */
#define GEVR_PULL_COS_CONE_DOWN 0.9063f  /* cos 25 degrees: pointing down at small things searches wider (GEVR PC) */
#define GEVR_PULL_DOWN_COS      0.5f     /* "pointing down": the ray at least 30 degrees below level */
#define GEVR_PULL_LIFT_CM       20.0f    /* the flight's arc at its middle */

/*
 * How well a target at t suits a hand at p pointing along unit dir f, all in
 * one frame and unit (cm units given by cm): negative when out of reach or
 * outside the cone, else a score where lower is better (the angle off the
 * ray counts most, distance a little). down is the cosine between f and the
 * room's down (unit), widening the cone for a hand pointed at the floor.
 */
float gevrPullScore(const float p[3], const float f[3], const float t[3], float cm, float downcos);

/*
 * The pointing ray (headset: aiming by the controller's barrel "depends on
 * the wrist" and felt like pointing the back of the hand): from the shoulder
 * through the hand, as a pointing arm aims, so the wrist's angle leaves it
 * alone. The shoulder is guessed from the eye (the frame's origin): drop
 * below it along unit down, and side across the head's right (+1 the right
 * hand, -1 the left). A hand close to the shoulder gives no direction of its
 * own: there the barrel f takes over, fully at GEVR_PULL_ARM_MIN_CM and not
 * at all from GEVR_PULL_ARM_FULL_CM. out: unit.
 */
#define GEVR_PULL_SHOULDER_DROP_CM 18.0f
#define GEVR_PULL_SHOULDER_SIDE_CM 17.0f
#define GEVR_PULL_ARM_MIN_CM       15.0f
#define GEVR_PULL_ARM_FULL_CM      35.0f
void gevrPullArmRay(const float p[3], const float f[3], const float down[3], float side, float cm, float out[3]);

/* The flight's length in seconds for a distance in cm: 0.30 s close, 0.40 s at full reach. */
float gevrPullDuration(float distcm);

/*
 * Where the flight is at time fraction u (0..1): from start to the hand's
 * current point, eased out (cubic), lifted along unit up by lift at the
 * middle (a parabola, nothing at either end).
 */
void gevrPullFlight(const float from[3], const float to[3], const float up[3], float lift, float u, float out[3]);

/*
 * Which hand a pulled gun goes to (docs/force-pull.md "Which hand"):
 * the pulling hand when the rules let it hold the gun, else the gun hand when
 * that is empty and may hold it, else none (-1: the inventory). Hands are
 * 0 the gun hand (GUNRIGHT), 1 the off hand (GUNLEFT).
 */
int gevrPullChooseHand(int pulling, int pullingAllowed, int gunHandEmpty, int gunHandAllowed);

/*
 * Online (protocol 26, NET_MSG_FORCE_PULL): what a player's pull does, sent
 * by that player's headset, relayed by the host; the host also refuses a
 * second claim on the same pickup and runs a disarm's guard.
 */
enum
{
    GEVR_PULL_NET_FLY = 1,       /* a flight to the sender's hand begins */
    GEVR_PULL_NET_HOLD = 2,      /* caught and kept in the hand (a grenade, a refused item) */
    GEVR_PULL_NET_RELEASE = 3,   /* let go: from pos at vel; a grenade's fuse is timer, its owner the sender */
    GEVR_PULL_NET_DONE = 4,      /* collected (aux 0) or absorbed (aux 1): gone */
    GEVR_PULL_NET_CANCEL = 5,    /* the flight is lost: it stays where it is */
    GEVR_PULL_NET_DISARM = 6,    /* the guard (host slot) lets go of the gun in his hand aux */
    GEVR_PULL_NET_REFUSED = 7,   /* host -> the claimant aux: someone else's pull has it */
    GEVR_PULL_NET_PHASES
};
#define GEVR_PULL_NET_NONE 0xFFFF   /* index or guard: none (a dropped gun has no shared index) */

#ifdef __cplusplus
}
#endif

#endif
