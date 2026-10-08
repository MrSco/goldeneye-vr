#ifndef GEVR_BODYSLOT_H
#define GEVR_BODYSLOT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Body slots: the weapon wheel's categories on the body, an option beside the
 * wheel (launcher Gestures page, watch VR page; ini BodySlots). Pistols at
 * each hip for that hand, rifles and SMGs over the gun hand's shoulder, heavy
 * guns over the off hand's, thrown things on the chest, gadgets at the front
 * of the belt on the off side. The game side is src/game/gevr_bodyslots.c.
 *
 * Precedents: Doom3Quest's waist frame and hip/back slots, Quake VR's hip,
 * shoulder and chest holsters, RTCWQuest's and Lambda1VR's over-the-shoulder
 * reach, Perfect Dark VR's smoothed torso yaw (VrBodyYaw), GEVR PC's hip swap.
 */
enum {
    GEVR_BS_HIP_GUN,    /* the gun hand's hip: pistols */
    GEVR_BS_HIP_OFF,    /* the off hand's hip: pistols */
    GEVR_BS_BACK_GUN,   /* over the gun hand's shoulder: rifles and SMGs */
    GEVR_BS_BACK_OFF,   /* over the off hand's shoulder: heavy */
    GEVR_BS_CHEST,      /* the chest: grenades, mines, knives */
    GEVR_BS_BELT,       /* the off side, front of the belt: gadgets */
    GEVR_BODY_SLOTS
};

#ifdef __cplusplus
}
#endif

#endif
