#ifndef GEVR_SPEEDBOAT_H
#define GEVR_SPEEDBOAT_H
/*
 * Dam's speedboat. Retail built the hull and left an unused pad on the north
 * jetty, and the island across the reservoir, but never the ride (Mark
 * Edmonds). This is that ride: 1.75x the retail hull, board, cross, step off.
 */

#define GEVR_DAM_BOAT_SCALE 1.75f
#define GEVR_DAM_BOAT_BOARD_UNITS 350.0f       /* ~3.5 m, jetty to hull */
#define GEVR_DAM_BOAT_LANDING_UNITS 800.0f     /* ~8 m, step off at the island */
#define GEVR_DAM_BOAT_LAUNCH_NUDGE 250.0f      /* ~2.5 m off the planks, into the water */
#define GEVR_DAM_BOAT_MIN_CROSSING 2000.0f     /* the reservoir, not a pad in the same yard */
#define GEVR_DAM_BOAT_UNUSED_SLACK 300.0f
#define GEVR_DAM_BOAT_CLUSTER 0.28f
#define GEVR_DAM_BOAT_UNITS_PER_SEC 1400.0f    /* ~14 m/s */
#define GEVR_DAM_BOAT_TURN_PER_SEC 1.2f
#define GEVR_DAM_BOAT_HAND_METRES 0.55f

typedef struct GevrBoatPad {
    float x, y, z;
    int used;
    int has_stan;
} GevrBoatPad;

typedef struct GevrBoatPlace {
    int jetty;
    int island;
} GevrBoatPlace;

typedef struct GevrBoatRide {
    float x, y, z;
    float yaw;
    float jetty_x, jetty_y, jetty_z;
    float island_x, island_y, island_z;
    float anchor_x, anchor_y, anchor_z;
    float limit;
    int aboard;
} GevrBoatRide;

/* Retail prop scale times 1.75. Larger than stock, not double. */
float gevrDamBoatVisualScale(float retail);

/* North jetty (unused pad preferred) and the island pad. 0 when this set of
 * pads has no reservoir between them. */
int gevrBoatChoosePads(const GevrBoatPad *pads, int count, GevrBoatPlace *out);

void gevrBoatBegin(GevrBoatRide *ride, float jx, float jy, float jz, float ix, float iy, float iz);

/* stick_x/stick_y are the raw stick, about -80..80. dt_frames is the game's
 * 60 Hz delta (1 at a full frame). */
void gevrBoatDrive(GevrBoatRide *ride, float stick_x, float stick_y, float dt_frames);

int gevrBoatWithin(const GevrBoatRide *ride, float x, float y, float z, float radius);
int gevrBoatAtLanding(const GevrBoatRide *ride);

/* view_distance is the hand-to-hull distance in view units. */
int gevrBoatHandOnHull(float view_distance, float view_units_per_metre);

#endif
