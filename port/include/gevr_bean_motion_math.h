#ifndef GEVR_BEAN_MOTION_MATH_H
#define GEVR_BEAN_MOTION_MATH_H
#include <math.h>
/* A normalized UV transform, in column order plus translation. Rotation is
 * around the picture centre; game-clock sampling is shared by CPU and GPU. */
static inline void gevrBeanUvTransform(float seconds, float su, float sv, float degrees, float m[6])
{
    float a = fmodf(seconds * degrees, 360.0f) * (3.14159265358979323846f / 180.0f);
    float c = cosf(a), s = sinf(a);
    /* Sample where the rotating picture came from (inverse rotation). */
    m[0]=c; m[1]=-s; m[2]=s; m[3]=c;
    m[4]=.5f-.5f*c-.5f*s+fmodf(seconds*su,1.0f);
    m[5]=.5f+.5f*s-.5f*c+fmodf(seconds*sv,1.0f);
}
#endif
