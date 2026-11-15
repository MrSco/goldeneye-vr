#ifndef GEVR_HAND_RELOAD_H
#define GEVR_HAND_RELOAD_H

/* Local stereo gameplay; also used to reserve B/Y exclusively for eject. */
int gevrHandReloadActive(void);
int gevrManualReloadOn(int hand);
/* A single gun's slot; its index is also the opposite controller's role. */
int gevrReloadSupportGun(void);
int gevrReloadStow(int hand, int item);
int gevrReloadDraw(int hand);
int gevrReloadStoredRounds(int hand, int item);
int gevrReloadReservedRounds(int ammoType);
int gevrReloadCarriesRounds(int hand);
void gevrReloadInventoryReset(void);

/* Which gun supplies the magazine at this controller's fitted belt, -1 none. */
int gevrReloadBeltAmmo(int ctrl);
int gevrReloadBeltHover(int ctrl);
int gevrReloadGrabBelt(int ctrl);
int gevrReloadBeltPoint(int ctrl, float out[3]);
int gevrReloadSeatHover(float out[3]);

/*
 * The label over a GoldenEye X gun reloaded by hand (gun.c
 * gevrGexDrawReloadLabel): where its magazine is and the next step, as
 * House of the Dead VR's label over its gun (quest_xr.c draw_rack_hint).
 * mag is bondview2.c's GEVR_GEXMAG_*: 0 in, 1 gripped (the off hand has it,
 * still in), 2 out in the off hand, 3 out; loaded the rounds in the gun,
 * reserve the gun's spare rounds, seated the ticks left of the "in" flash.
 */
enum {
    GEVR_RELOAD_LABEL_NONE, GEVR_RELOAD_LABEL_EMPTY, GEVR_RELOAD_LABEL_NOAMMO, GEVR_RELOAD_LABEL_OUT,
    GEVR_RELOAD_LABEL_INSERT, GEVR_RELOAD_LABEL_GRIPPED, GEVR_RELOAD_LABEL_SEATED, GEVR_RELOAD_LABEL_COUNT
};
static inline int gevrReloadLabelFor(int mag, int loaded, int reserve, int seated)
{
    switch (mag) {
    case 1: return GEVR_RELOAD_LABEL_GRIPPED;
    case 2: return GEVR_RELOAD_LABEL_INSERT;
    case 3: return reserve > 0 ? GEVR_RELOAD_LABEL_OUT : GEVR_RELOAD_LABEL_NOAMMO;
    }
    if (loaded > 0) return seated > 0 ? GEVR_RELOAD_LABEL_SEATED : GEVR_RELOAD_LABEL_NONE;
    return reserve > 0 ? GEVR_RELOAD_LABEL_EMPTY : GEVR_RELOAD_LABEL_NOAMMO;
}
int gevrReloadLabel(int hand);

/* Flat tint with real alpha for the missing-magazine guide. */
#define GEVR_MODEL_RELOAD_GHOST 0x47
#define GEVR_RELOAD_TINT 0x40C8FF00u
#define GEVR_RELOAD_READY_TINT 0x70FF9000u
#endif
