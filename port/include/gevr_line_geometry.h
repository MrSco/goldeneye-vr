#ifndef GEVR_LINE_GEOMETRY_H
#define GEVR_LINE_GEOMETRY_H
#include <stdint.h>
/* GLES supports indexed lines. Retain every triangle edge and its absolute
 * offset into the persistent vertex ring; no geometry or model scale changes. */
static inline void gevrTriangleEdgeIndices(uint32_t first, uint32_t count, uint32_t *out) {
    for (uint32_t v=0, i=0; v<count; v+=3) {
        out[i++]=first+v; out[i++]=first+v+1;
        out[i++]=first+v+1; out[i++]=first+v+2;
        out[i++]=first+v+2; out[i++]=first+v;
    }
}
#endif
