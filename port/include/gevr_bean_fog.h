#ifndef GEVR_BEAN_FOG_H
#define GEVR_BEAN_FOG_H
#include <math.h>
#include <stdint.h>

/* DAB beanFogsRetail/gebeanStageFogLine, MIT (port/licenses/dabs-mod.txt).
 * Dam's verified retail HD fog starts at -100 world units, ends at 15000
 * on the plateau, and extends lower down. Other stages retain their native
 * fog, including environment transitions, until their HD states are ported. */
static inline int gevrBeanFogLine(int stage, float height, float depth, float line[2], uint8_t rgb[3])
{
    if (stage != 33 || !isfinite(depth) || depth <= 0 || !isfinite(height)) return 0;
    /* GoldenEye's coordinates omit PD's +13219 conversion offset. */
    float t = (9161.0f - height) / (9161.0f - 2782.0f);
    float end = 15000.0f + 65000.0f * fminf(1.0f, fmaxf(0.0f, t));
    line[0] = 255.0f / ((end + 100.0f) * depth);
    line[1] = 100.0f * 255.0f / (end + 100.0f);
    rgb[0] = 0x85; rgb[1] = 0xad; rgb[2] = 0xca;
    return 1;
}
#endif
