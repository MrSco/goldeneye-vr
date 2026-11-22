#ifndef GEVR_RACK_H
#define GEVR_RACK_H

/*
 * Hand reload's third step (docs/hand-reload-rack.md): a gun that ran dry
 * with its magazine in has an empty chamber, and a magazine then seated by
 * the off hand does not make it ready. The off hand takes the gun by its
 * slide (a pistol) or its bolt handle (a submachine gun, the KF7), pulls back
 * and lets go, and the first round is chambered. A magazine changed with
 * rounds still in the gun needs no rack: one is in the chamber.
 *
 * Until the rack the seated magazine's rounds are kept here, not in the gun,
 * so the gun is empty to everything else: it dry-fires, its slide stays
 * locked back (gun.c: GoldenEye's own slide clock), and the count on the hand
 * reads 0. They are still the player's (bondview2.c gevrReloadReservedRounds).
 *
 * The state and its arithmetic, with no game state (port/tests/rack_native.c);
 * the game side is bondview2.c (gevrHandReloadTick). Distances are cm.
 */

#define GEVR_RACK_REACH_CM   12.0f   /* a fresh grip this near the slide or the handle takes it */
#define GEVR_RACK_SLIDE_CM   3.5f    /* a pistol's slide: pulled back this far it is racked */
#define GEVR_RACK_BOLT_CM    5.0f    /* a bolt handle's longer throw */
#define GEVR_RACK_RETURN     0.5f    /* of the throw: back in this far, still gripped, it has gone home */
#define GEVR_RACK_NUDGE_CM   1.2f    /* the gun gives this much with the pull */
#define GEVR_RACK_KICK_CM    0.8f    /* and jumps forward this much as the bolt goes home */
#define GEVR_RACK_KICK_TICKS 6

typedef struct GevrRack {
    int dry;        /* the chamber is empty */
    int pending;    /* a seated magazine's rounds, waiting for the rack */
    int held;       /* the off hand has the slide */
    int cocked;     /* and has pulled it all the way back */
    float grabAt;   /* where along the gun's back the hand took it */
    float pull;     /* how far back it is now, 0..throw */
    float throwCm;  /* this gun's throw */
    int kick;       /* ticks left of the jump forward */
} GevrRack;

/* gevrRackStep's answers */
enum {
    GEVR_RACK_IDLE,
    GEVR_RACK_GRABBED,   /* the hand took it */
    GEVR_RACK_COCKED,    /* pulled all the way back: the click */
    GEVR_RACK_DONE,      /* let go (or run back in) after that: chamber the round */
    GEVR_RACK_SHORT      /* let go before that: nothing */
};

/*
 * What the gun shows each tick: with its magazine in and no round anywhere it
 * is dry, and with rounds in it and none waiting one is chambered (the game's
 * own reload from the belt or the chest, which needs no rack).
 */
static inline void gevrRackNote(GevrRack *r, int magIn, int loaded)
{
    if (r->pending > 0) return;
    if (loaded > 0) r->dry = 0;
    else if (magIn) r->dry = 1;
}

/* A magazine seated by hand: on an empty chamber its rounds wait. 1 if they do. */
static inline int gevrRackHold(GevrRack *r, int *loaded)
{
    if (!r->dry || *loaded <= 0) return 0;
    r->pending = *loaded;
    *loaded = 0;
    r->held = r->cocked = 0;
    r->pull = 0.0f;
    return 1;
}

/* The waiting rounds back into the magazine as it leaves the gun or the hand:
 * they go where it goes. The chamber is as it was. */
static inline void gevrRackGiveBack(GevrRack *r, int *loaded)
{
    if (r->pending > 0) *loaded = r->pending;
    r->pending = 0;
    r->held = r->cocked = 0;
    r->pull = 0.0f;
}

/* Racked (or chambered some other way): the waiting rounds are the gun's. */
static inline void gevrRackChamber(GevrRack *r, int *loaded)
{
    if (r->pending > 0)
    {
        *loaded = r->pending;
        r->dry = 0;
    }
    r->pending = 0;
    r->held = r->cocked = 0;
    r->pull = 0.0f;
}

/*
 * The off hand this tick: grip held, fresh on this tick's press, dist from the
 * slide or handle, along its place on the gun's back axis (more is farther
 * back, relative to the gun: pushing the gun forward racks it as well as
 * pulling the hand back). throwCm is this gun's.
 */
static inline int gevrRackStep(GevrRack *r, int grip, int fresh, float dist, float along, float throwCm)
{
    if (r->pending <= 0)
    {
        r->held = r->cocked = 0;
        r->pull = 0.0f;
        return GEVR_RACK_IDLE;
    }
    r->throwCm = throwCm;
    if (!r->held)
    {
        if (!(grip && fresh && dist <= GEVR_RACK_REACH_CM)) return GEVR_RACK_IDLE;
        r->held = 1;
        r->cocked = 0;
        r->grabAt = along;
        r->pull = 0.0f;
        return GEVR_RACK_GRABBED;
    }
    r->pull = along - r->grabAt;
    if (!(r->pull > 0.0f)) r->pull = 0.0f;   /* forward of where it was taken, or not a number */
    if (r->pull > throwCm) r->pull = throwCm;
    if (!grip)
    {
        const int was = r->cocked;
        r->held = r->cocked = 0;
        r->pull = 0.0f;
        if (was) r->kick = GEVR_RACK_KICK_TICKS;
        return was ? GEVR_RACK_DONE : GEVR_RACK_SHORT;
    }
    if (!r->cocked)
    {
        if (r->pull >= throwCm)
        {
            r->cocked = 1;
            return GEVR_RACK_COCKED;
        }
        return GEVR_RACK_IDLE;
    }
    if (r->pull <= throwCm * GEVR_RACK_RETURN)
    {
        r->held = r->cocked = 0;
        r->pull = 0.0f;
        r->kick = GEVR_RACK_KICK_TICKS;
        return GEVR_RACK_DONE;
    }
    return GEVR_RACK_IDLE;
}

/* The jump forward runs out. */
static inline void gevrRackTick(GevrRack *r, int ticks)
{
    if (r->kick > 0) r->kick -= ticks > 0 ? ticks : 0;
    if (r->kick < 0) r->kick = 0;
}

/* How far back of its place the gun is drawn, cm: with the pull, then a short jump forward. */
static inline float gevrRackNudge(const GevrRack *r)
{
    if (r->held && r->throwCm > 0.0f) return r->pull / r->throwCm * GEVR_RACK_NUDGE_CM;
    if (r->kick > 0) return -GEVR_RACK_KICK_CM * (float)r->kick / GEVR_RACK_KICK_TICKS;
    return 0.0f;
}

#endif
