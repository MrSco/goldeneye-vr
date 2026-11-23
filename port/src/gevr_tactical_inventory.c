#include "gevr_tactical_inventory.h"
#include <math.h>
#include <string.h>

static int slotValid(int slot) { return slot >= 0 && slot < TACTICAL_SLOT_COUNT; }
static int isWell(int slot) { return slot >= TACTICAL_PRIMARY_WELL && slot < TACTICAL_WELL_COUNT; }
static int isHand(int slot) { return slot == TACTICAL_LEFT_HAND || slot == TACTICAL_RIGHT_HAND; }
static int compatible(const TacticalInventory *inv, int slot, TacticalMagazine mag)
{
    if (isWell(slot)) {
        int cls = slot == TACTICAL_PRIMARY_WELL || slot == TACTICAL_LEFT_PRIMARY_WELL
            ? TACTICAL_MAG_PRIMARY : TACTICAL_MAG_PISTOL;
        return mag.mag_class == cls && inv->well_type[slot] != 0 && inv->well_type[slot] == mag.type;
    }
    if (slot >= TACTICAL_RIG_PRIMARY_1 && slot <= TACTICAL_RIG_PRIMARY_3)
        return mag.mag_class == TACTICAL_MAG_PRIMARY;
    if (slot >= TACTICAL_RIG_PISTOL_1 && slot <= TACTICAL_RIG_PISTOL_2)
        return mag.mag_class == TACTICAL_MAG_PISTOL;
    return isHand(slot);
}
void tacticalInventoryInit(TacticalInventory *inv) { if (inv) memset(inv, 0, sizeof(*inv)); }
int tacticalSetWell(TacticalInventory *inv, int well, unsigned int type)
{
    if (!inv || !isWell(well)) return TACTICAL_INVALID;
    if (inv->slots[well].id || inv->chambered[well]) return TACTICAL_OCCUPIED;
    inv->well_type[well] = type;
    return TACTICAL_OK;
}
int tacticalAddMagazine(TacticalInventory *inv, int slot, TacticalMagazine mag)
{
    int i;
    if (!inv || !slotValid(slot) || !mag.id || !mag.type || !mag.capacity || mag.rounds > mag.capacity ||
        (mag.mag_class != TACTICAL_MAG_PRIMARY && mag.mag_class != TACTICAL_MAG_PISTOL)) return TACTICAL_INVALID;
    if (inv->slots[slot].id) return TACTICAL_OCCUPIED;
    for (i = 0; i < TACTICAL_SLOT_COUNT; ++i) if (inv->slots[i].id == mag.id) return TACTICAL_DUPLICATE;
    if (!compatible(inv, slot, mag)) return TACTICAL_INCOMPATIBLE;
    inv->slots[slot] = mag;
    return TACTICAL_OK;
}
int tacticalMoveMagazine(TacticalInventory *inv, int from, int to)
{
    TacticalMagazine mag;
    if (!inv || !slotValid(from) || !slotValid(to) || from == to || (!isHand(from) && !isHand(to))) return TACTICAL_INVALID;
    if (!inv->slots[from].id) return TACTICAL_EMPTY;
    if (inv->slots[to].id) return TACTICAL_OCCUPIED;
    mag = inv->slots[from];
    if (!compatible(inv, to, mag)) return TACTICAL_INCOMPATIBLE;
    inv->slots[to] = mag;
    memset(&inv->slots[from], 0, sizeof(inv->slots[from]));
    return TACTICAL_OK;
}
int tacticalDropMagazine(TacticalInventory *inv, int hand, TacticalMagazine *out)
{
    if (!inv || !out || !isHand(hand)) return TACTICAL_INVALID;
    if (!inv->slots[hand].id) return TACTICAL_EMPTY;
    *out = inv->slots[hand];
    memset(&inv->slots[hand], 0, sizeof(inv->slots[hand]));
    return TACTICAL_OK;
}
int tacticalEjectMagazine(TacticalInventory *inv, int well, TacticalMagazine *out)
{
    if (!inv || !out || !isWell(well)) return TACTICAL_INVALID;
    if (!inv->slots[well].id) return TACTICAL_EMPTY;
    *out = inv->slots[well];
    memset(&inv->slots[well], 0, sizeof(inv->slots[well]));
    return TACTICAL_OK;
}
int tacticalRack(TacticalInventory *inv, int well, unsigned int *ejected)
{
    TacticalMagazine *mag;
    if (!inv || !isWell(well) || !ejected || !inv->well_type[well]) return TACTICAL_INVALID;
    *ejected = inv->chambered[well] ? 1u : 0u;
    inv->chambered[well] = 0;
    mag = &inv->slots[well];
    if (mag->id && mag->rounds) { --mag->rounds; inv->chambered[well] = 1; }
    return TACTICAL_OK;
}
int tacticalFire(TacticalInventory *inv, int well)
{
    TacticalMagazine *mag;
    if (!inv || !isWell(well) || !inv->well_type[well]) return TACTICAL_INVALID;
    if (!inv->chambered[well]) return TACTICAL_EMPTY_CHAMBER;
    inv->chambered[well] = 0;
    mag = &inv->slots[well];
    if (mag->id && mag->rounds) { --mag->rounds; inv->chambered[well] = 1; }
    return TACTICAL_OK;
}
unsigned int tacticalCountRounds(const TacticalInventory *inv)
{
    unsigned int total = 0; int i;
    if (!inv) return 0;
    for (i = 0; i < TACTICAL_SLOT_COUNT; ++i) total += inv->slots[i].rounds;
    for (i = 0; i < TACTICAL_WELL_COUNT; ++i) total += inv->chambered[i] ? 1u : 0u;
    return total;
}

/* states: 0 idle, 1 grabbed, 2 pulled, 3 tracking lost: wait for release */
void tacticalRackGestureReset(TacticalRackGesture *g) { if (g) memset(g, 0, sizeof(*g)); }
int tacticalRackGestureTick(TacticalRackGesture *g, int tracked, int grip,
                            int near_action, float pull_cm, float threshold_cm)
{
    int emitted = 0;
    if (!g || !(threshold_cm > 0.0f)) return 0;
    if (!tracked) {
        g->state = 3;
        g->was_gripped = grip != 0;
        return 0;
    }
    if (g->state == 3) {
        if (!grip) g->state = 0;
    } else if (!grip) {
        emitted = g->state == 2;
        g->state = 0;
    } else if (g->state == 0) {
        if (!g->was_gripped && near_action) { g->state = 1; g->start_cm = pull_cm; }
    } else if (g->state == 1 && pull_cm - g->start_cm >= threshold_cm) {
        g->state = 2;
    }
    g->was_gripped = grip != 0;
    return emitted;
}

/* 0 idle, 1 pulling an inserted magazine, 2 holding one, 3 pulling action. */
void tacticalInteractionInit(TacticalInteraction *i, int well, int hand)
{
    if (!i) return;
    memset(i, 0, sizeof(*i));
    i->well = well; i->hand = hand;
}
int tacticalInteractionTick(TacticalInteraction *i, TacticalInventory *inv,
    const TacticalInteractionInput *in, TacticalMagazine *dropped, unsigned int *ejected)
{
    const float grab = in && isfinite(in->grab_radius_cm) && in->grab_radius_cm > 0 ? in->grab_radius_cm : 10.0f;
    const float magGrab = in && isfinite(in->magazine_grab_radius_cm) && in->magazine_grab_radius_cm > 0
        ? in->magazine_grab_radius_cm : grab;
    const float actionGrab = in && isfinite(in->action_grab_radius_cm) && in->action_grab_radius_cm > 0
        ? in->action_grab_radius_cm : grab;
    const float seat = in && isfinite(in->seat_radius_cm) && in->seat_radius_cm > 0 ? in->seat_radius_cm : 6.0f;
    const float rigGrab = in && isfinite(in->rig_pickup_radius_cm) && in->rig_pickup_radius_cm > 0
        ? in->rig_pickup_radius_cm : grab;
    const float pull = 5.0f, rack = 4.0f;
    int event = TACTICAL_EVENT_NONE;
    if (!i || !inv || !in || !dropped || !ejected || !isWell(i->well) || !isHand(i->hand))
        return TACTICAL_EVENT_NONE;
    *ejected = 0;
    if (!in->tracked) {
        if (i->state == 2) i->require_regrip = 1;
        else i->state = 0;
        tacticalRackGestureTick(&i->rack, 0, in->grip, 0, 0, rack);
        i->was_gripped = 1;
        return event;
    }
    if (i->require_regrip) {
        if (in->grip) i->require_regrip = 0;
        i->was_gripped = in->grip != 0;
        return event;
    }
    if (i->state == 3) {
        if (tacticalRackGestureTick(&i->rack, 1, in->grip, 0, in->action_pull_cm, rack)) {
            if (tacticalRack(inv, i->well, ejected) == TACTICAL_OK) event = TACTICAL_EVENT_RACKED;
        }
        if (!in->grip) i->state = 0;
    } else if (i->state == 1) {
        if (!in->grip) i->state = 0;
        else if (isfinite(in->magazine_pull_cm) && in->magazine_pull_cm - i->pull_start_cm >= pull) {
            if (tacticalMoveMagazine(inv, i->well, i->hand) == TACTICAL_OK) {
                i->state = 2; event = TACTICAL_EVENT_MAG_TAKEN;
            } else i->state = 0;
        }
    } else if (i->state == 2) {
        if (!in->grip) {
            int moved = 0, best = -1;
            float nearest = grab;
            for (int k = 0; k < 5; k++) {
                int slot = TACTICAL_RIG_PRIMARY_1 + k;
                float d = in->rig_distance_cm[k];
                if (isfinite(d) && d >= 0 && d <= nearest && !inv->slots[slot].id
                    && compatible(inv, slot, inv->slots[i->hand])) { nearest = d; best = slot; }
            }
            if (isfinite(in->magazine_distance_cm) && in->magazine_distance_cm >= 0 && in->magazine_distance_cm <= seat
                && (best < 0 || in->magazine_distance_cm <= nearest)
                && tacticalMoveMagazine(inv, i->hand, i->well) == TACTICAL_OK) {
                event = TACTICAL_EVENT_MAG_SEATED; moved = 1;
            }
            if (!moved && best >= 0 && tacticalMoveMagazine(inv, i->hand, best) == TACTICAL_OK) {
                event = TACTICAL_EVENT_MAG_STORED; moved = 1;
            }
            if (!moved && tacticalDropMagazine(inv, i->hand, dropped) == TACTICAL_OK)
                event = TACTICAL_EVENT_MAG_DROPPED;
            i->state = 0;
        }
    } else if (in->grip && !i->was_gripped && !inv->slots[i->hand].id) {
        float magd = in->magazine_distance_cm, actiond = in->action_distance_cm, nearest = grab;
        int best = -1;
        for (int k = 0; k < 5; k++) {
            int slot = TACTICAL_RIG_PRIMARY_1 + k; float d = in->rig_distance_cm[k];
            float shape = in->rig_pickup_bounds_valid ? in->rig_pickup_distance_cm[k] : d;
            if (isfinite(shape) && shape >= 0 && shape <= rigGrab
                && shape <= nearest && inv->slots[slot].id) { nearest = shape; best = slot; }
        }
        const int nearAction = isfinite(actiond) && actiond >= 0 && actiond <= actionGrab;
        if (isfinite(magd) && magd >= 0 && magd <= nearest && magd <= magGrab && inv->slots[i->well].id
            && (!nearAction || magd <= actiond)) {
            if (in->snap_inserted_magazine && tacticalMoveMagazine(inv, i->well, i->hand) == TACTICAL_OK) {
                i->state = 2; event = TACTICAL_EVENT_MAG_TAKEN;
            } else { i->state = 1; i->pull_start_cm = in->magazine_pull_cm; }
        } else if (nearAction && actiond <= nearest) {
            tacticalRackGestureReset(&i->rack);
            tacticalRackGestureTick(&i->rack, 1, 1, 1, in->action_pull_cm, rack);
            i->state = 3;
        } else if (best >= 0 && tacticalMoveMagazine(inv, best, i->hand) == TACTICAL_OK) {
            i->state = 2; event = TACTICAL_EVENT_MAG_TAKEN;
        }
    }
    i->was_gripped = in->grip != 0;
    return event;
}
