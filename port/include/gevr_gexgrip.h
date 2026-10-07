#ifndef GEVR_GEXGRIP_H
#define GEVR_GEXGRIP_H

enum { GEVR_GEXGRIP_NONE, GEVR_GEXGRIP_SUPPORT, GEVR_GEXGRIP_MAG, GEVR_GEXGRIP_SPENT };

/* Relative motion along the gun's up axis, invariant when both hands move. */
static inline float gevrGexPullAmount(int up, float start, float now, float distance)
{
    return distance > 0 ? (up ? now-start : start-now)/distance : 0;
}

/* Real centimetres, from UNSNAPPED controller poses. Ambiguous overlap belongs
 * to support. Selection is latched by the caller until grip release. */
static inline int gevrGexPistolGripPick(float magazine, float support, float below)
{
    if (magazine <= 4.0f && below >= 2.0f && magazine + 1.0f < support)
        return GEVR_GEXGRIP_MAG;
    if (support < 12.0f) return GEVR_GEXGRIP_SUPPORT;
    return GEVR_GEXGRIP_NONE;
}

#endif
