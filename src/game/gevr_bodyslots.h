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

#endif
