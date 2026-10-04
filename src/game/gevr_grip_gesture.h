#ifndef GEVR_GRIP_GESTURE_H
#define GEVR_GRIP_GESTURE_H

/*
 * GEVR PC's grip gestures (its docs/CONTROLS.md, vr450..vr453): a grip press
 * with the hand at the hip holsters, at a door or switch uses it, at a gun on
 * the floor picks it up into that hand, at the player's own stuck mine takes
 * it back. bondview2.c decides (gevrGripGestureTry); chrprop.c finds what the
 * hand touches; port/src/input.c holds the aim back while a press is decided.
 */

#define GEVR_HAND_USE    0   /* a door, switch or console B would use */
#define GEVR_HAND_PICKUP 1   /* something to collect off the floor */
#define GEVR_HAND_MINE   2   /* the player's own remote mine, or a proximity mine still arming */

struct PropRecord;

/* chrprop.c, view space (bondview2.c gevrGripAxesRaw's units) */
struct PropRecord *gevrHandFindProp(const float p[3], float reach, int kind);
int gevrHandInteract(struct PropRecord *prop);
int gevrHandPickup(struct PropRecord *prop, int mine);

/* bondview2.c: port/src/input.c reports each grip; lv.c ticks after propsTick */
void gevrGripGestureInput(int ctrl, int pressed, int held);
int gevrGripGestureTaken(int ctrl);
void gevrGripGestureTick(void);

#endif
