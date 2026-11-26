#ifndef GEVR_MODACTION_H
#define GEVR_MODACTION_H
/*
 * How far a hand's gun action is open, for a pack's moving parts
 * (mod.weapon.model's action and handle, mod.attach.action): 0 closed, 1
 * fully back. It runs GoldenEye's own slide rule (gun.c sub_GAME_7F05E83C,
 * which moves a cartridge gun's Switches[7] by its BoltRecoilBack) on a
 * stroke of 1, so it works for a carrier whose stat is 0 (the sniper rifle)
 * and on the same tick and trigger: a shot (sub_GAME_7F05E808) opens it a
 * quarter of the stroke a tick; it closes a sixth a tick while the magazine
 * has rounds, and stays back on an empty one unless the pack body says its
 * action doesn't hold open. There is no separate animation clock.
 */
typedef struct GevrModAction {
    float open;     /* 0..1 */
    int opening;    /* a shot is opening it (GoldenEye's field_A8C) */
    int item;       /* the hand's item it is for: another starts closed */
} GevrModAction;

static inline void gevrModActionShot(GevrModAction *a, int item)
{
    if (item != a->item) {
        a->item = item;
        a->open = 0.0f;
    }
    a->opening = 1;
}

static inline void gevrModActionStep(GevrModAction *a, int item, float ticks, int loaded, int holdOpen)
{
    if (item != a->item) {
        a->item = item;
        a->open = 0.0f;
        a->opening = 0;
    }
    if (!(ticks > 0.0f)) return;
    if (a->opening) {
        if (a->open < 1.0f) a->open += 0.25f * ticks;
        if (a->open >= 1.0f) {
            a->open = 1.0f;
            a->opening = 0;
        }
    } else if (loaded > 0 || !holdOpen) {
        if (a->open > 0.0f) a->open -= ticks / 6.0f;
        if (a->open < 0.0f) a->open = 0.0f;
    }
}

/* A part's travel this frame, 0..1: an action part follows the slide and
 * the hand rack's pull (bondview2.c gevrRackPullShare), a charging handle that
 * doesn't reciprocate only the pull. */
static inline float gevrModActionShare(const GevrModAction *a, float rack, int handle)
{
    float open = a != NULL && !handle ? a->open : 0.0f;
    if (!(rack > 0.0f)) rack = 0.0f;
    if (rack > 1.0f) rack = 1.0f;
    return rack > open ? rack : open;
}
#endif
