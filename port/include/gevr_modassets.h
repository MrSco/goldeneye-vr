/*
 * A mod's assets (docs/mod-system.md, Assets): the files in its pack that
 * stand in for the cartridge's, read by the loaders that already exist.
 *
 *   assets/models/<CartridgeName>   a model file (a gun, prop or character:
 *       GpistolZ, PcartonZ, CdjbondZ...) in place of the cartridge's, as the
 *       cartridge stores it (Rare's 1172 compression) or decompressed. It
 *       keeps the cartridge's header (its switch and texture counts and its
 *       skeleton), as SubDrag's editors' imports do. ob.c load_resource.
 *   assets/textures/                 PNGs named as GLideN64 ("Rice") packs
 *       name them, over the player's HD pack (gevr_texpack.cpp).
 *
 * The run's mod campaign's (gevrModActive), on every machine of a party
 * alike: the content check makes it the same build everywhere.
 */
#ifndef GEVR_MODASSETS_H
#define GEVR_MODASSETS_H

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_MODASSET_MODEL_MAX (4u << 20)   /* a model file's bytes, as stored */

/* ob.c load_resource, for a model file (a pending header): the mod's file
 * for it rebuilt in host layout at dst (capacity bytes, the room the game
 * made for the cartridge's), returning its size; 0 keeps the cartridge's
 * (no such file, too big, not a model the header fits) and dst untouched. */
unsigned int gevrModAssetModel(const char *name, unsigned char *dst, unsigned int capacity, int numSwitches,
                               int numTextures);
/* The last model file loaded was the mod's (objecthandler_2.c): the patches
 * made for the cartridge's meshes (VR hands, magazines, the hat, Bean) leave
 * it alone. Cleared as each load starts. */
extern int gevrModAssetLast;
/* The mod's textures folder for the HD-pack layer (gfx_pc.cpp, as the game
 * starts: the launcher's mod campaign, gevrModPlay), or NULL: none, or no
 * such folder. */
const char *gevrModAssetTextureDir(void);

#ifdef __cplusplus
}
#endif
#endif
