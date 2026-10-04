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
 * vertices and display lists. NULL when the ROM is missing or the model has
 * something not handled; the caller frees the result.
 */
u8 *gevrGexBuildModel(const char *pdname, u32 numSwitches, const s32 *switchParts,
		u32 *outLen, u16 *outMatrices, u16 *outTextures);

/* A mapped id's compressed GE-X texture, or NULL for any other id. */
const u8 *gevrGexTextureForId(s32 id, u32 *len);

/* Set around a load_object_fill_header call: ob.c load_resource takes this
 * file in place of the cartridge's one it just inflated. */
extern u8 *gevrGexPendingFile;
extern u32 gevrGexPendingLen;
extern u32 gevrGexPendingGeTextures;   /* the cartridge model's own texture count */

/* The built file (gex) takes the cartridge's texture ids (ge, inflated)
 * where a texture of the same size sits at the same place in both tables. */
void gevrGexAdoptGeTextures(u8 *gex, u32 gexLen, const u8 *ge, u32 geLen, u32 numSwitches, u32 geTextures);

#ifdef __cplusplus
}
#endif

#endif
