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

/* Flat tint with real alpha for the missing-magazine guide. */
#define GEVR_MODEL_RELOAD_GHOST 0x47
#define GEVR_RELOAD_TINT 0x40C8FF00u
#define GEVR_RELOAD_READY_TINT 0x70FF9000u
#endif
