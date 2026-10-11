#include "gevr_speedboat.h"
#include <math.h>
#include <stddef.h>

float gevrDamBoatVisualScale(float retail)
{
    return retail * GEVR_DAM_BOAT_SCALE;
}

/* The reservoir is horizontal. A pad high on the dam wall is not an island. */
static float gevrBoatDistXZ2(float ax, float az, float bx, float bz)
{
    float dx = ax - bx, dz = az - bz;
    return dx * dx + dz * dz;
}

int gevrBoatChoosePads(const GevrBoatPad *pads, int count, GevrBoatPlace *out)
{
    int i, n, island, jetty;
    float cx, cz, best, island_d, cluster, jetty_d, slack;

    if (out == NULL)
    {
        return 0;
    }
    out->jetty = -1;
    out->island = -1;
    if (pads == NULL || count < 2)
    {
        return 0;
    }

    cx = cz = 0.0f;
    n = 0;
    for (i = 0; i < count; i++)
    {
        if (!pads[i].has_stan)
        {
            continue;
        }
        cx += pads[i].x;
        cz += pads[i].z;
        n++;
    }
    if (n < 2)
    {
        return 0;
    }
    cx /= (float)n;
    cz /= (float)n;

    island = -1;
    best = 0.0f;
    for (i = 0; i < count; i++)
    {
        float d;

        if (!pads[i].has_stan)
        {
            continue;
        }
        d = gevrBoatDistXZ2(pads[i].x, pads[i].z, cx, cz);
        if (island < 0 || d > best)
        {
            best = d;
            island = i;
        }
    }
    island_d = sqrtf(best);
    if (island < 0 || island_d < GEVR_DAM_BOAT_MIN_CROSSING)
    {
        return 0;
    }

    cluster = GEVR_DAM_BOAT_CLUSTER * island_d;
    jetty = -1;
    jetty_d = 0.0f;
    for (i = 0; i < count; i++)
    {
        float d;

        if (!pads[i].has_stan || i == island)
        {
            continue;
        }
        d = sqrtf(gevrBoatDistXZ2(pads[i].x, pads[i].z, pads[island].x, pads[island].z));
        if (d <= cluster)
        {
            continue;
        }
        if (jetty < 0 || d < jetty_d)
        {
            jetty = i;
            jetty_d = d;
        }
    }
    if (jetty < 0)
    {
        return 0;
    }

    /* The planned hull pad is unused. Take it when it is as far out as the
     * nearest standable pad, within a few metres. */
    slack = jetty_d + GEVR_DAM_BOAT_UNUSED_SLACK;
    if (pads[jetty].used)
    {
        int unused = -1;
        float unused_d = 0.0f;

        for (i = 0; i < count; i++)
        {
            float d;

            if (!pads[i].has_stan || pads[i].used || i == island)
            {
                continue;
            }
            d = sqrtf(gevrBoatDistXZ2(pads[i].x, pads[i].z, pads[island].x, pads[island].z));
            if (d <= cluster || d > slack)
            {
                continue;
            }
            if (unused < 0 || d < unused_d)
            {
                unused = i;
                unused_d = d;
            }
        }
        if (unused >= 0)
        {
            jetty = unused;
        }
    }

    out->jetty = jetty;
    out->island = island;
    return 1;
}

static float gevrBoatClampStick(float stick)
{
    float v = stick / 80.0f;

    if (v > 1.0f)
    {
        v = 1.0f;
    }
    if (v < -1.0f)
    {
        v = -1.0f;
    }
    if (v > -0.08f && v < 0.08f)
    {
        v = 0.0f;
    }
    return v;
}

static void gevrBoatClampToReservoir(GevrBoatRide *ride)
{
    float dx = ride->x - ride->anchor_x;
    float dz = ride->z - ride->anchor_z;
    float d = sqrtf(dx * dx + dz * dz);

    /* The deck stays at the jetty's waterline. The clamp is horizontal, out
     * to the island plus the landing, so the hull cannot sail off the map. */
    ride->y = ride->jetty_y;
    if (d > ride->limit && d > 0.0001f)
    {
        float s = ride->limit / d;

        ride->x = ride->anchor_x + dx * s;
        ride->z = ride->anchor_z + dz * s;
    }
}

void gevrBoatBegin(GevrBoatRide *ride, float jx, float jy, float jz, float ix, float iy, float iz)
{
    float dx = ix - jx;
    float dz = iz - jz;
    float dist = sqrtf(dx * dx + dz * dz);
    float nudge = GEVR_DAM_BOAT_LAUNCH_NUDGE;

    if (ride == NULL)
    {
        return;
    }
    if (dist > 1.0f && nudge > dist * 0.25f)
    {
        nudge = dist * 0.25f;
    }
    ride->yaw = atan2f(dx, dz);
    ride->jetty_x = jx;
    ride->jetty_y = jy;
    ride->jetty_z = jz;
    ride->island_x = ix;
    ride->island_y = iy;
    ride->island_z = iz;
    ride->x = jx + sinf(ride->yaw) * nudge;
    ride->y = jy;
    ride->z = jz + cosf(ride->yaw) * nudge;
    ride->anchor_x = (jx + ix) * 0.5f;
    ride->anchor_y = jy;
    ride->anchor_z = (jz + iz) * 0.5f;
    ride->limit = (dist > 1.0f ? dist * 0.5f : 1.0f) + GEVR_DAM_BOAT_LANDING_UNITS;
    ride->aboard = 0;
}

void gevrBoatDrive(GevrBoatRide *ride, float stick_x, float stick_y, float dt_frames)
{
    float turn, throttle, step;

    if (ride == NULL || !ride->aboard || !(dt_frames > 0.0f))
    {
        return;
    }
    turn = gevrBoatClampStick(stick_x);
    throttle = gevrBoatClampStick(stick_y);
    ride->yaw += turn * GEVR_DAM_BOAT_TURN_PER_SEC * dt_frames / 60.0f;
    if (ride->yaw >= 6.2831855f)
    {
        ride->yaw -= 6.2831855f;
    }
    if (ride->yaw < 0.0f)
    {
        ride->yaw += 6.2831855f;
    }
    step = throttle * GEVR_DAM_BOAT_UNITS_PER_SEC * dt_frames / 60.0f;
    ride->x += sinf(ride->yaw) * step;
    ride->z += cosf(ride->yaw) * step;
    gevrBoatClampToReservoir(ride);
}

int gevrBoatWithin(const GevrBoatRide *ride, float x, float y, float z, float radius)
{
    if (ride == NULL || !(radius > 0.0f))
    {
        return 0;
    }
    {
        float dx = ride->x - x, dy = ride->y - y, dz = ride->z - z;
        return dx * dx + dy * dy + dz * dz <= radius * radius;
    }
}

int gevrBoatAtLanding(const GevrBoatRide *ride)
{
    float dx, dz;

    if (ride == NULL)
    {
        return 0;
    }
    dx = ride->x - ride->island_x;
    dz = ride->z - ride->island_z;
    return dx * dx + dz * dz <= GEVR_DAM_BOAT_LANDING_UNITS * GEVR_DAM_BOAT_LANDING_UNITS;
}

int gevrBoatHandOnHull(float view_distance, float view_units_per_metre)
{
    if (!(view_units_per_metre > 0.0f) || !(view_distance >= 0.0f))
    {
        return 0;
    }
    return view_distance <= GEVR_DAM_BOAT_HAND_METRES * view_units_per_metre;
}
