#ifndef GEVR_TACTICAL_INVENTORY_H
#define GEVR_TACTICAL_INVENTORY_H

/* Magazine and chamber state for tactical mode. IDs are local to a mission. */
#ifdef __cplusplus
extern "C" {
#endif

enum TacticalSlot {
    TACTICAL_PRIMARY_WELL, TACTICAL_LEFT_PISTOL_WELL, TACTICAL_RIGHT_PISTOL_WELL,
    TACTICAL_LEFT_PRIMARY_WELL,
    TACTICAL_RIG_PRIMARY_1, TACTICAL_RIG_PRIMARY_2, TACTICAL_RIG_PRIMARY_3,
    TACTICAL_RIG_PISTOL_1, TACTICAL_RIG_PISTOL_2,
    TACTICAL_LEFT_HAND, TACTICAL_RIGHT_HAND, TACTICAL_SLOT_COUNT
};
enum { TACTICAL_WELL_COUNT = TACTICAL_LEFT_PRIMARY_WELL + 1 };
enum TacticalMagClass { TACTICAL_MAG_PRIMARY = 1, TACTICAL_MAG_PISTOL = 2 };
enum TacticalResult {
    TACTICAL_OK, TACTICAL_INVALID, TACTICAL_EMPTY, TACTICAL_OCCUPIED,
    TACTICAL_INCOMPATIBLE, TACTICAL_DUPLICATE, TACTICAL_EMPTY_CHAMBER
};
typedef struct TacticalMagazine {
    unsigned int id;       /* nonzero, unique among carried magazines */
    unsigned int type;     /* compatibility key, e.g. AK magazine type */
    unsigned int capacity;
    unsigned int rounds;
    int mag_class;
    unsigned int gear_part; /* zero for stock art, otherwise catalog index plus one */
} TacticalMagazine;
typedef struct TacticalInventory {
    TacticalMagazine slots[TACTICAL_SLOT_COUNT]; /* id 0 means empty */
    unsigned int well_type[TACTICAL_WELL_COUNT]; /* zero means no weapon in the well */
    unsigned char chambered[TACTICAL_WELL_COUNT]; /* one round stays with its gun during a reload */
} TacticalInventory;

void tacticalInventoryInit(TacticalInventory *inv);
/* A well may only change weapon type when its magazine and chamber are empty. */
int tacticalSetWell(TacticalInventory *inv, int well, unsigned int type);
/* Add/pick up a new magazine; rejects duplicate IDs and incompatible slots. */
int tacticalAddMagazine(TacticalInventory *inv, int slot, TacticalMagazine mag);
/* Transfers require a hand on at least one end. Failure changes nothing. */
int tacticalMoveMagazine(TacticalInventory *inv, int from, int to);
/* Removes a magazine from a hand for a world drop, returning its full state. */
int tacticalDropMagazine(TacticalInventory *inv, int hand, TacticalMagazine *out);
/* Eject the actual installed magazine without changing its remaining rounds or chamber. */
int tacticalEjectMagazine(TacticalInventory *inv, int well, TacticalMagazine *out);
/* Cycle the action: optionally eject the chambered round and feed one from
 * the inserted magazine. The caller owns the ejected round count (0 or 1). */
int tacticalRack(TacticalInventory *inv, int well, unsigned int *ejected);
/* Fire a chambered round and automatically feed the next, if available. */
int tacticalFire(TacticalInventory *inv, int well);
unsigned int tacticalCountRounds(const TacticalInventory *inv);

/* Tracked off-hand interaction. A fresh grip must begin near the action;
 * pulling by threshold_cm while holding, then releasing, emits one rack.
 * pull_cm is projection onto a weapon-specific rearward axis supplied by
 * the VR adapter, not a world-space distance. Tracking loss cancels and
 * blocks re-arming until release. This state does not read input devices. */
typedef struct TacticalRackGesture {
    int state, was_gripped;
    float start_cm;
} TacticalRackGesture;
void tacticalRackGestureReset(TacticalRackGesture *gesture);
int tacticalRackGestureTick(TacticalRackGesture *gesture, int tracked, int grip,
                            int near_action, float pull_cm, float threshold_cm);

/* Device-independent off-hand interaction. The adapter supplies distances in
 * centimetres relative to tracked body/gun anchors. A magazine moves on a
 * deliberate pull or a fresh grip at the rig. Releasing seats, stores, or
 * returns it as a world drop. Tracking loss never silently destroys it. */
enum TacticalInteractionEvent {
    TACTICAL_EVENT_NONE, TACTICAL_EVENT_MAG_TAKEN, TACTICAL_EVENT_MAG_SEATED,
    TACTICAL_EVENT_MAG_STORED, TACTICAL_EVENT_MAG_DROPPED, TACTICAL_EVENT_RACKED
};
typedef struct TacticalInteractionInput {
    int tracked, grip;
    int snap_inserted_magazine; /* fresh press attaches it immediately to the hand */
    float grab_radius_cm, seat_radius_cm; /* zero selects the default radii */
    float magazine_distance_cm, action_distance_cm;
    float rig_distance_cm[5];
    float magazine_pull_cm, action_pull_cm;
    float magazine_grab_radius_cm; /* optional installed-mag radius; zero uses grab_radius_cm */
    float action_grab_radius_cm;   /* optional slide/handle radius; zero uses grab_radius_cm */
    /* For pickups only: distance from the tracked hand to each visible
     * magazine's bounds. The original rig distance still governs storing. */
    int rig_pickup_bounds_valid;
    float rig_pickup_distance_cm[5], rig_pickup_radius_cm;
} TacticalInteractionInput;
typedef struct TacticalInteraction {
    int state, was_gripped, require_regrip, well, hand;
    float pull_start_cm;
    TacticalRackGesture rack;
} TacticalInteraction;
void tacticalInteractionInit(TacticalInteraction *i, int well, int hand);
int tacticalInteractionTick(TacticalInteraction *i, TacticalInventory *inv,
    const TacticalInteractionInput *input, TacticalMagazine *dropped, unsigned int *ejected);

#ifdef __cplusplus
}
#endif
#endif
