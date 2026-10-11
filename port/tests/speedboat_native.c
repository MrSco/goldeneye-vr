#include "gevr_speedboat.h"
#include <math.h>
#include <stdio.h>
#define EXPORT __attribute__((visibility("default")))

static int fail(int code)
{
    fprintf(stderr, "dam boat check %d failed\n", code);
    return code;
}

EXPORT int test_dam_speedboat(void)
{
    struct GevrBoatPad pads[7];
    struct GevrBoatPlace place;
    struct GevrBoatRide ride;
    int i;
    float scale = gevrDamBoatVisualScale(0.1f);
    float dx, dz, from_anchor;

    if (fabsf(scale - 0.175f) > 1.0e-5f || fabsf(GEVR_DAM_BOAT_SCALE - 1.75f) > 1.0e-6f)
    {
        return fail(1);
    }

    /* Towers, the docks, a used pad a step closer than the unused hull pad,
     * the island, and a hatch that is not the crossing. */
    for (i = 0; i < 7; i++)
    {
        pads[i].x = 0.0f;
        pads[i].y = 0.0f;
        pads[i].z = 0.0f;
        pads[i].used = 1;
        pads[i].has_stan = 1;
    }
    pads[0].y = 3000.0f;
    pads[1].z = 500.0f;
    pads[2].z = 1200.0f;
    pads[3].z = 2300.0f;              /* used, slightly closer to the island */
    pads[4].z = 2100.0f;              /* unused hull pad */
    pads[4].used = 0;
    pads[5].y = 10.0f;
    pads[5].z = 9000.0f;
    pads[5].used = 0;
    pads[6].y = -2000.0f;
    pads[6].z = -100.0f;
    pads[6].has_stan = 0;

    if (!gevrBoatChoosePads(pads, 7, &place) || place.jetty != 4 || place.island != 5)
    {
        return fail(2);
    }
    pads[4].has_stan = 0;
    if (gevrBoatChoosePads(pads, 7, &place))
    {
        /* The only outer unused pad cannot be stood on, so the used dock wins. */
        if (place.jetty != 3 || place.island != 5)
        {
            return fail(3);
        }
    }
    else
    {
        return fail(3);
    }
    pads[4].has_stan = 1;

    /* A courtyard has no reservoir. */
    pads[5].z = 400.0f;
    if (gevrBoatChoosePads(pads, 7, &place))
    {
        return fail(4);
    }
    pads[5].z = 9000.0f;

    gevrBoatBegin(&ride, pads[4].x, pads[4].y, pads[4].z, pads[5].x, pads[5].y, pads[5].z);
    if (ride.aboard || ride.z <= pads[4].z || fabsf(ride.y - pads[4].y) > 0.01f)
    {
        return fail(5);
    }
    if (gevrBoatWithin(&ride, pads[4].x, pads[4].y, pads[4].z, GEVR_DAM_BOAT_BOARD_UNITS) == 0)
    {
        return fail(6);
    }
    if (gevrBoatWithin(&ride, 0.0f, 0.0f, 0.0f, GEVR_DAM_BOAT_BOARD_UNITS))
    {
        return fail(7);
    }
    if (gevrBoatAtLanding(&ride))
    {
        return fail(8);
    }

    ride.aboard = 1;
    for (i = 0; i < 400; i++)
    {
        gevrBoatDrive(&ride, 0.0f, 80.0f, 1.0f);
    }
    if (!gevrBoatAtLanding(&ride) || fabsf(ride.y - pads[4].y) > 0.01f)
    {
        return fail(9);
    }
    dx = ride.x - ride.anchor_x;
    dz = ride.z - ride.anchor_z;
    from_anchor = sqrtf(dx * dx + dz * dz);
    if (from_anchor > ride.limit + 0.1f)
    {
        return fail(10);
    }

    /* Hard over must stay in the reservoir. */
    gevrBoatBegin(&ride, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 8000.0f);
    ride.aboard = 1;
    for (i = 0; i < 2000; i++)
    {
        gevrBoatDrive(&ride, 80.0f, 80.0f, 1.0f);
    }
    dx = ride.x - ride.anchor_x;
    dz = ride.z - ride.anchor_z;
    from_anchor = sqrtf(dx * dx + dz * dz);
    if (from_anchor > ride.limit + 0.1f || fabsf(ride.y) > 0.01f)
    {
        return fail(11);
    }

    if (!gevrBoatHandOnHull(10.0f, 20.0f) || gevrBoatHandOnHull(20.0f, 20.0f))
    {
        return fail(12);
    }
    if (gevrBoatHandOnHull(-1.0f, 20.0f) || gevrBoatHandOnHull(1.0f, 0.0f))
    {
        return fail(13);
    }
    return 0;
}

int main(void)
{
    return test_dam_speedboat();
}
