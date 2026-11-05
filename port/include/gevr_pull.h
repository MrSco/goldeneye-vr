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

#ifdef __cplusplus
}
#endif

#endif
