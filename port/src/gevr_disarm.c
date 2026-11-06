#include "gevr_disarm.h"

void gevrDisarmInit(GevrDisarmState *s, int canFetch)
{
    s->phase = canFetch ? GEVR_DISARM_FETCH : GEVR_DISARM_MELEE;
    s->canFetch = canFetch != 0;
    s->react60 = GEVR_DISARM_REACT;
    s->fetch60 = 0;
    s->take60 = 0;
    s->scan60 = GEVR_DISARM_SCAN;
    s->cool60 = 0;
}

int gevrDisarmWantsGun(const GevrDisarmState *s)
{
    return s->canFetch && (s->phase == GEVR_DISARM_FETCH || s->scan60 <= 0);
}

int gevrDisarmStep(GevrDisarmState *s, int ticks, float gunCm, float playerCm, int clear, int front, int striking)
{
    if (ticks < 0)
    {
        ticks = 0;
    }
    s->cool60 = s->cool60 > ticks ? s->cool60 - ticks : 0;
    if (striking)
    {
        return GEVR_DISARM_WAIT;
    }
    if (s->react60 > 0)
    {
        s->react60 -= ticks;
        return playerCm >= 0.0f ? GEVR_DISARM_FACE : GEVR_DISARM_IDLE;
    }
    if (s->phase == GEVR_DISARM_FETCH)
    {
        if (gunCm >= 0.0f)
        {
            s->fetch60 += ticks;
            if (s->fetch60 > GEVR_DISARM_FETCH_MAX)
            {
                s->phase = GEVR_DISARM_MELEE;
                s->scan60 = GEVR_DISARM_SCAN;
                return GEVR_DISARM_GIVE_UP_GUN;
            }
            if (gunCm <= GEVR_DISARM_TAKE_CM)
            {
                s->take60 += ticks;
                return s->take60 >= GEVR_DISARM_TAKE_PAUSE ? GEVR_DISARM_TAKE_GUN : GEVR_DISARM_AT_GUN;
            }
            s->take60 = 0;
            return GEVR_DISARM_GO_GUN;
        }
        s->phase = GEVR_DISARM_MELEE;   /* nothing to go for: fists */
        s->scan60 = GEVR_DISARM_SCAN + ticks;   /* this tick's look is spent: the next a second off */
        gunCm = -1.0f;
    }
    /* a look was due (gevrDisarmWantsGun): the next is a second off; else count down to it */
    s->scan60 = s->scan60 <= 0 ? GEVR_DISARM_SCAN : s->scan60 - ticks;
    if (gunCm >= 0.0f && s->canFetch && (playerCm < 0.0f || playerCm > GEVR_DISARM_STRIKE_CM))
    {
        /* a gun turned up, and the player isn't already in reach: go for it */
        s->phase = GEVR_DISARM_FETCH;
        s->fetch60 = 0;
        s->take60 = 0;
        return gunCm <= GEVR_DISARM_TAKE_CM ? GEVR_DISARM_AT_GUN : GEVR_DISARM_GO_GUN;
    }
    if (playerCm < 0.0f)
    {
        return GEVR_DISARM_IDLE;
    }
    if (playerCm > GEVR_DISARM_STRIKE_CM || !clear)
    {
        return GEVR_DISARM_CHASE;
    }
    if (!front || s->cool60 > 0)
    {
        return GEVR_DISARM_FACE;
    }
    s->cool60 = GEVR_DISARM_COOLDOWN;
    return GEVR_DISARM_STRIKE;
}
