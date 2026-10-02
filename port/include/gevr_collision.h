#ifndef GEVR_COLLISION_H
#define GEVR_COLLISION_H
#include <math.h>

/* Signed distance to a convex door perimeter, independent of winding. */
static inline float gevrPolygonClearance(const float *points, int count, float x, float z) {
    float closest = 1e30f;
    int positive = 0, negative = 0;
    for (int i=0;i<count;i++) {
        int j=(i+1)%count;
        float ax=points[2*i], az=points[2*i+1];
        float dx=points[2*j]-ax, dz=points[2*j+1]-az;
        float cross=dx*(z-az)-dz*(x-ax);
        positive |= cross > 0.001f; negative |= cross < -0.001f;
        float length=dx*dx+dz*dz;
        if (length < 0.0001f) continue;
        float t=((x-ax)*dx+(z-az)*dz)/length;
        t=fmaxf(0,fminf(1,t));
        float ex=x-ax-t*dx, ez=z-az-t*dz;
        closest=fminf(closest,sqrtf(ex*ex+ez*ez));
    }
    return positive && negative ? closest : -closest;
}
static inline int gevrDoorEscape(const float *points, int count, float radius,
                                float x, float z, float dest_x, float dest_z) {
    if (!points || count < 3 || count > 8 || radius <= 0) return 0;
    float before=gevrPolygonClearance(points,count,x,z)-radius;
    float after=gevrPolygonClearance(points,count,dest_x,dest_z)-radius;
    /* Only escape an existing overlap. Approaching or traversing a door
     * from outside still goes through the normal collision tests. */
    return before < -0.01f && after > before + 0.01f;
}
#endif
