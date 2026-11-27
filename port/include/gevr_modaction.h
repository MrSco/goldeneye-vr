#ifndef GEVR_MODACTION_H
#define GEVR_MODACTION_H
/*
 * How far a hand's gun action is open, for a pack's moving parts
 * (mod.weapon.model's action and handle, mod.attach.action): 0 closed, 1
 * fully back. By default it runs GoldenEye's own slide rule (gun.c
 * sub_GAME_7F05E83C, which moves a cartridge gun's Switches[7] by its
 * BoltRecoilBack) on a stroke of 1, so it works for a carrier whose stat is
 * 0 (the sniper rifle), on the same tick and trigger: a shot
 * (sub_GAME_7F05E808) opens it a quarter of the stroke a tick; it closes a
 * sixth a tick while the magazine has rounds, and stays back on an empty one
 * unless the pack says its action doesn't hold open. There is no separate
 * animation clock.
 *
 * A pack may give its own timing as data, still on the game's tick and only
 * for the look: the ticks to open (1 is fully open on the shot's own tick),
 * the ticks to close, each 1 to 30, and an ease a from 0 to 1 that shows the
 * travel as open * (a + (1 - a) * open) (1 is linear). With its own timing,
 * an automatic gun's close is shortened so a cycle fits between its shots.
 */
#define GEVR_MODACTION_GAME_OPEN  4    /* GoldenEye's: a quarter a tick */
#define GEVR_MODACTION_GAME_CLOSE 6    /* and a sixth a tick */
#define GEVR_MODACTION_TICKS_MAX  30

typedef struct GevrModAction {
    float open;     /* 0..1 */
    int opening;    /* a shot is opening it (GoldenEye's field_A8C) */
    int item;       /* the hand's item it is for: another starts closed */
} GevrModAction;

/* A pack's timing; 0 ticks are GoldenEye's own, and so is no ease (1). */
typedef struct GevrModActionTiming {
    int open, close;
    float ease;
} GevrModActionTiming;

static inline void gevrModActionShot(GevrModAction *a, int item)
{
    if (item != a->item) {
        a->item = item;
        a->open = 0.0f;
    }
    a->opening = 1;
}

/* The ticks to open and to close this gun's action: the pack's, else
 * GoldenEye's; with the pack's, a close that would outlast an automatic's
 * interval between shots (ticks, 0 for a shot a press) fits inside it. */
static inline void gevrModActionTicks(const GevrModActionTiming *t, int interval, int *open, int *close)
{
    const int own = t != NULL && t->open > 0 && t->close > 0;
    *open = own ? t->open : GEVR_MODACTION_GAME_OPEN;
    *close = own ? t->close : GEVR_MODACTION_GAME_CLOSE;
    if (own && interval > 0 && *open + *close > interval) {
        *close = interval - *open;
        if (*close < 1) *close = 1;
    }
}

static inline void gevrModActionStep(GevrModAction *a, int item, float ticks, int loaded, int holdOpen,
                                     int openTicks, int closeTicks)
{
    if (item != a->item) {
        a->item = item;
        a->open = 0.0f;
        a->opening = 0;
    }
    if (!(ticks > 0.0f)) return;
    if (a->opening) {
        if (a->open < 1.0f) a->open += ticks / (float)openTicks;
        if (a->open >= 1.0f) {
            a->open = 1.0f;
            a->opening = 0;
        }
    } else if (loaded > 0 || !holdOpen) {
        if (a->open > 0.0f) a->open -= ticks / (float)closeTicks;
        if (a->open < 0.0f) a->open = 0.0f;
    }
}

/* The travel shown for an action this far open: the pack's ease, or linear. */
static inline float gevrModActionEase(float open, const GevrModActionTiming *t)
{
    const float a = t != NULL && t->open > 0 && t->close > 0 ? t->ease : 1.0f;
    return open * (a + (1.0f - a) * open);
}

/* A part's travel this frame, 0..1: an action part follows the slide (eased)
 * and the hand rack's pull (bondview2.c gevrRackPullShare), a charging handle
 * that doesn't reciprocate only the pull. */
static inline float gevrModActionShare(const GevrModAction *a, float rack, int handle, const GevrModActionTiming *t)
{
    const float open = a != NULL && !handle ? gevrModActionEase(a->open, t) : 0.0f;
    if (!(rack > 0.0f)) rack = 0.0f;
    if (rack > 1.0f) rack = 1.0f;
    return rack > open ? rack : open;
}
#endif
