/*
 * A mod's model files (port/src/gevr_modassets.c) with the real model
 * converter (gevr_model.c) and 1172 inflater (gevr_romcheck.c), over a fake
 * mods folder: test_modassets.py hands in the files, as the cartridge stores
 * them or decompressed, and the room the game makes for each.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gevr_modassets.h"
#include "gevr_modpack.h"
#include "gevr_model.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

/* gevr_modpack.c: the run's mod and the launcher's choice, and where mods live */
static gevrModInfo s_mod;
static int s_active, s_chosen;
static char s_root[1024];
const gevrModInfo *gevrModActive(void) { return s_active ? &s_mod : NULL; }
const gevrModInfo *gevrModPlay(void) { return s_chosen ? &s_mod : NULL; }
const char *gevrModsRoot(void) { return s_root; }

/* gevr_handpatch.c: the VR hands' models only, never these */
s32 gevrHandPatchWants(const char *name) { (void)name; return 0; }
void gevrHandPatchNoteNode(u32 src, u32 dst) { (void)src; (void)dst; }
void gevrHandPatchNoteMarker(u32 texnum, u32 w0) { (void)texnum; (void)w0; }
void gevrHandPatchNoteTexture(u32 slot, u32 texnum) { (void)slot; (void)texnum; }
s32 gevrHandPatchDropsTexture(const char *name, u32 texnum) { (void)name; (void)texnum; return 0; }
void sysFatalError(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    abort();
}

static char s_log[8192];
void sysLogPrintf(s32 level, const char *fmt, ...)
{
    va_list ap;
    size_t n = strlen(s_log);

    (void)level;
    va_start(ap, fmt);
    if (n + 2 < sizeof(s_log)) vsnprintf(s_log + n, sizeof(s_log) - n - 1, fmt, ap);
    va_end(ap);
    n = strlen(s_log);
    if (n + 1 < sizeof(s_log)) strcat(s_log, "\n");
}

EXPORT void test_setup(const char *root, const char *id, int active, int chosen)
{
    snprintf(s_root, sizeof(s_root), "%s", root);
    memset(&s_mod, 0, sizeof(s_mod));
    snprintf(s_mod.m.id, sizeof(s_mod.m.id), "%s", id);
    s_active = active;
    s_chosen = chosen;
}

/* load_resource's call: the mod's file for name into dst, or 0 */
EXPORT unsigned test_model(const char *name, unsigned char *dst, unsigned capacity, int switches, int textures)
{
    gevrModAssetLast = 0;
    return gevrModAssetModel(name, dst, capacity, switches, textures);
}

/* The cartridge's own, converted as load_resource converts it, to compare */
EXPORT unsigned test_convert(unsigned char *data, unsigned size, unsigned capacity, int switches, int textures)
{
    return gevrModelConvert(data, size, capacity, switches, textures, "cartridge");
}

/* mod.weapon.model's file: a name a pack's model can have, and the run's mod's having it */
EXPORT int test_name_ok(const char *name) { return gevrModAssetNameOk(name); }
EXPORT int test_has_model(const char *name) { return gevrModAssetHasModel(name); }

EXPORT const char *test_texture_dir(void)
{
    const char *dir = gevrModAssetTextureDir();

    return dir ? dir : "";
}

EXPORT const char *test_log(void) { return s_log; }
EXPORT void test_log_clear(void) { s_log[0] = '\0'; }
