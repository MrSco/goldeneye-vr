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
 * of the belt on the off side. The game side is src/game/gevr_bodyslots.c;
 * this is its arithmetic, with no game state (port/tests/test_body_slots.py).
 *
 * Precedents: Doom3Quest's waist frame and hip/back slots, Quake VR's hip,
 * shoulder and chest holsters, RTCWQuest's and Lambda1VR's over-the-shoulder
 * reach, Perfect Dark VR's smoothed torso yaw (VrBodyYaw), GEVR PC's hip swap.
 *
 * Frames. The level frame has the world's axes with the eye at its origin
 * (+Y up), in centimetres; the game side rotates view space into it. Headings
 * are level directions as (x, z) pairs. The play frame is the headset's own,
 * which stick turning never moves: the torso is tracked there so a snap or
 * smooth turn carries it with the camera.
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

/* The wheel's categories (bondview2.c GEVR_WC_*, same order; the test checks). */
enum {
    GEVR_BODY_CAT_PISTOLS, GEVR_BODY_CAT_RIFLES, GEVR_BODY_CAT_HEAVY,
    GEVR_BODY_CAT_GADGETS, GEVR_BODY_CAT_THROWN
};

/* The neck model: the eye this far above and ahead of the neck's pivot. */
#define GEVR_BODY_NECK_UP_CM    7.5f
#define GEVR_BODY_NECK_AHEAD_CM 8.0f

typedef struct GevrBodyTorso {
    float heading[2];   /* the torso's heading, play frame */
    float last[2];      /* the head's heading at the last update */
    int valid;
} GevrBodyTorso;

typedef struct GevrBodyFrame {
    float origin[3];    /* cm, level frame: the eye as it would be, upright over the torso */
    float up[3], fwd[3], right[3];   /* the torso's axes, level frame */
    float twist;        /* degrees, the torso's heading from the head's */
    float pitch;        /* degrees, the head's; negative looking down */
} GevrBodyFrame;

/* q * v * q^-1, q {x, y, z, w} (Perfect Dark's vr_rotate_vector_by_quaternion) */
void gevrBodyQuatRotate(const float q[4], const float v[3], float out[3]);
/* The level heading of a head looking along fwd with up: still defined
 * looking straight down or up (Doom3Quest's waist yaw), where the forward
 * alone has none. 0 if neither gives one. */
int gevrBodyHeading(const float fwd[3], const float up[3], float out[2]);
/* signed degrees from heading a to b; gevrBodyTurn turns a by degrees */
float gevrBodyAngle(const float a[2], const float b[2]);
void gevrBodyTurn(const float a[2], float degrees, float out[2]);

/*
 * The torso chases the head's play heading: Perfect Dark VR's VrBodyYaw,
 * rate per 60 Hz tick (ArmBodyFollow) over ticks, never further than
 * maxTwist degrees from the head, held while freeze (a hand reaching for a
 * slot, the head looking down). A jump of more than jumpDeg in one update
 * (a recentre, tracking coming back) starts it again at the head. Returns 1
 * when it (re)started.
 */
int gevrBodyTorsoUpdate(GevrBodyTorso *t, const float head[2], float rate, float ticks,
                        int freeze, float maxTwist, float jumpDeg);
void gevrBodyTorsoReset(GevrBodyTorso *t);

/*
 * The torso's frame in the level frame, from the head's forward and up there
 * (the camera's) and the torso: its heading is carried across as an angle
 * from the head's, which both frames share. Right is the camera's right
 * levelled, turned with the torso.
 */
void gevrBodyFrameBuild(GevrBodyFrame *f, const GevrBodyTorso *t, const float headPlay[2],
                        const float fwd[3], const float up[3], const float right[3]);

/* A slot's offsets: cm below the frame's eye, out to its own side, ahead.
 * eyeHeight is the standing eye height (VrPlayerHeight). */
void gevrBodySlotDefault(int slot, float eyeHeight, float out[3]);
/* the fit if one was made (not 0 0 0), else the default */
void gevrBodySlotOffsets(int slot, const float fit[3], float eyeHeight, float out[3]);
/* +1: the slot's side is the frame's right, -1 its left */
int gevrBodySlotSide(int slot, int leftHanded);
float gevrBodySlotRadius(int slot, int size);   /* cm; size 0 small, 1 normal, 2 large */
int gevrBodySlotCategory(int slot);
/* which controllers may use the slot: bit 0 the off hand, bit 1 the gun hand */
int gevrBodySlotHands(int slot);
const char *gevrBodySlotName(int slot);
void gevrBodySlotCentre(const GevrBodyFrame *f, int slot, const float off[3], int leftHanded, float out[3]);
/* the inverse: the offsets that put the slot's centre at p */
void gevrBodySlotFitFrom(const GevrBodyFrame *f, int slot, const float p[3], int leftHanded, float out[3]);
/* p in the frame's own terms: cm below its eye, out to the side's sign, ahead */
void gevrBodyLocal(const GevrBodyFrame *f, const float p[3], float out[3]);

/*
 * The slot a hand is in: the current one while the hand stays within its
 * radius plus exitCm; otherwise, or for a slot nearer by margin (in radii),
 * the nearest eligible one it is inside. -1 for none. dist in cm.
 */
int gevrBodyZonePick(int current, const float dist[GEVR_BODY_SLOTS], const float radius[GEVR_BODY_SLOTS],
                     const int eligible[GEVR_BODY_SLOTS], float exitCm, float margin);

/* What a slot last held, newest first. */
#define GEVR_BODY_MRU 8
typedef struct GevrBodyMru {
    int items[GEVR_BODY_MRU];
    int count;
} GevrBodyMru;
void gevrBodyMruTouch(GevrBodyMru *m, int item);

/*
 * A slot's choices for one hand, the order stepping walks: the candidates
 * (what the hand may take from the slot's category, in the wheel's order)
 * that the hand isn't holding, then GEVR_BODY_HOLSTER (put away what it
 * holds) if the hand holds one of the slot's own. Returns the count.
 */
#define GEVR_BODY_HOLSTER (-2)
int gevrBodyChoices(const int *candidates, int n, int held, int heldHere, int *out, int max);
/* The choice a reach starts on: the slot's newest use among them, else the
 * first. -1 for none. */
int gevrBodyDefaultPick(const GevrBodyMru *m, const int *choices, int n);
/* the next choice from pick in dir, wrapping; the first if pick is gone */
int gevrBodyStepPick(const int *choices, int n, int pick, int dir);

/* What a grip in a slot does with the hand's choice. */
enum { GEVR_BODY_FALL, GEVR_BODY_DRAW, GEVR_BODY_STOW, GEVR_BODY_DENY };
int gevrBodyGripAction(int choice, int blocked);

/*
 * A throwable in the hand: with motion throwing its grip winds up a throw,
 * so a slot only takes it once the hand has stayed hoverMs >= dwellMs and is
 * moving slower than maxSpeed (m/s, against the head).
 */
int gevrBodyThrowGate(float hoverMs, float speed, float dwellMs, float maxSpeed);
/* Consecutive time at rest; moving again cancels the ready-to-grab pause. */
float gevrBodySettleMs(float ms, float speed, float dtMs, float maxSpeed);
/* Keep the grip's decision through partial release until a full let-go. */
int gevrBodyGripHeld(int wasHeld, int pressed, float squeeze);

/*
 * The hovering hand's own stick steps through the choices: sideways only, a
 * flick at 0.65, again after 450 ms held and every 250 ms; taken only once it
 * has been centred (<= 0.3) since the hover began, so walking or strafing as
 * the hand reaches keeps going, and held after the hover ends until it
 * centres, so leaving can't snap-turn. Returns the step (-1, 0, +1); *take
 * is 1 while the whole stick belongs to the slot. Both axes must centre.
 */
typedef struct GevrBodyStick {
    int armed, latched, dir;
    float held, next;
} GevrBodyStick;
int gevrBodyStickStep(GevrBodyStick *s, int hovering, float x, float y, float dtMs, int *take);

/*
 * Hand reload's belt with body slots on: entering the belt waits deferMs, and
 * a grip press meanwhile (the hand is holstering) cancels it until the hand
 * has left the belt. Returns 1 when the reload fires.
 */
typedef struct GevrBodyDefer {
    int pending, spent;
    float ms;
} GevrBodyDefer;
int gevrBodyBeltDefer(GevrBodyDefer *d, int entered, int inside, int gripped, float dtMs, float deferMs);

#ifdef __cplusplus
}
#endif

#endif
