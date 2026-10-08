#ifndef GEVR_BODYSLOTS_H
#define GEVR_BODYSLOTS_H

/*
 * Body slots in play (gevr_bodyslots.c; the arithmetic is
 * port/include/gevr_bodyslot.h). ctrl is the logical controller: 0 the off
 * hand, 1 the gun hand.
 */

/* bondview2.c gevrGripGestureTick, once a game tick before the grips are decided */
void gevrBodySlotsTick(void);
/* a new stage (bondview_r.c gevrStageGunsReset): what the slots last held */
void gevrBodySlotsReset(void);
/* bondview2.c gevrStereoRecenter: the torso starts again at the head */
void gevrBodySlotsRecentre(void);
/* the option is on and the slots are live this tick */
int gevrBodySlotsOn(void);
/* the slot a hand is in (GEVR_BS_*), -1 none */
int gevrBodySlotHover(int ctrl);
/* a fresh grip press: 1 if a slot took it (drew, put away, or refused) */
int gevrBodySlotGrip(int ctrl);
/* the grip is let go (bondview2.c gevrGripGestureInput) */
void gevrBodySlotGripLetGo(int ctrl);
/* a slot took the grip still held: a throw doesn't wind up from it */
int gevrBodySlotHoldsGrip(int ctrl);
/* port/src/input.c: the hand's own A or X pressed, its own stick's X each
 * poll; 1 when the slot takes it (the wheel, the cycle, the strafe or the
 * turn don't) */
int gevrBodySlotButton(int ctrl);
int gevrBodySlotStick(int ctrl, float x, float dtMs);

#endif
