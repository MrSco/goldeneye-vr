#ifndef GEVR_GEXMODEL_H
#define GEVR_GEXMODEL_H

/*
 * GoldenEye X gun models (Perfect Dark files) as GoldenEye model files
 * (port/src/gevr_gexmodel.c, docs/gex-weapons.md).
 */
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The texture ids GE-X textures are given: above GoldenEye's 3001 and inside
 * the 12-bit texture command field. image.c texLoad serves them. */
#define GEVR_GEX_TEX_FIRST 3100
#define GEVR_GEX_TEX_LAST  4095

/*
 * Builds the cartridge-layout GoldenEye file for GE-X model pdname: the
 * switch table (numSwitches slots; slot i is the node of PD part
 * switchParts[i], or none for -1), the texture table, nodes, records,
 * vertices and display lists.
 * When switch 1 maps to PD flash part 90, outMatrices includes one extra
 * matrix at the end; gunfire.c supplies the fitted muzzle transform there.
 * texturePairs (GE-X texture number, GoldenEye
 * texture id, ..., 0) name GE-X's re-encoded copies of GoldenEye's
 * textures, which take the original's id so the HD packs apply. NULL when
 * the ROM is missing or the model has something not handled; the caller
 * frees the result.
 */
u8 *gevrGexBuildModel(const char *pdname, u32 numSwitches, const s32 *switchParts,
		const u16 *texturePairs, u32 *outLen, u16 *outMatrices, u16 *outTextures);

/* The matrix of PD model part `part` (a position node, as MODELPART_GUN_SLIDE),
 * or -1: the model has no such part, or the ROM is missing. */
s32 gevrGexPartMatrix(const char *pdname, s32 part);

/* The centre of the bounds of PD model parts partList[0..count) (a held round,
 * a speedloader's rounds), in their own coordinates; 0 when they have no
 * vertices or the ROM is missing. */
s32 gevrGexPartCentre(const char *pdname, const s32 *partList, s32 count, f32 out[3]);

/* A mapped id's compressed GE-X texture, or NULL for any other id. */
const u8 *gevrGexTextureForId(s32 id, u32 *len);

/* Set around a load_object_fill_header call: ob.c load_resource takes this
 * file in place of the cartridge's one it just inflated. */
extern u8 *gevrGexPendingFile;
extern u32 gevrGexPendingLen;

#ifdef __cplusplus
}
#endif

#endif
