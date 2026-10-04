#ifndef GEVR_PDTEX_H
#define GEVR_PDTEX_H

/*
 * Perfect Dark's texture codec (pdvr src/game/texdecompress.c), for GoldenEye
 * X's textures (port/src/gevr_gex.c). Decodes the base image only.
 */
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GevrPdTex {
    s32 width, height;
    s32 format;      /* PD's TEXFORMAT_*, 0..12 */
    s32 gbiformat;   /* G_IM_FMT_* */
    s32 depth;       /* G_IM_SIZ_* */
    s32 lutmode;     /* G_TT_* */
    s32 numcolours;  /* palette entries, 0 when not paletted */
    u8 *data;        /* the image in the N64's big-endian texel layout, then the palette */
    u32 size;        /* image and palette bytes */
    u32 tlutoffset;  /* where the palette starts in data */
} GevrPdTex;

/* 1 on success; out->data is malloc'd, the caller frees it. */
s32 gevrPdTexDecode(const u8 *comp, u32 len, GevrPdTex *out);

#ifdef __cplusplus
}
#endif

#endif
