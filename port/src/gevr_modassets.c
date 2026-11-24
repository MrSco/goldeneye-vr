/* A mod's assets: see gevr_modassets.h. No game headers: the native test
 * builds this with the model converter and the 1172 inflater alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gevr_modassets.h"
#include "gevr_modpack.h"
#include "gevr_romcheck.h"
#include "gevr_model.h"
#include "system.h"

int gevrModAssetLast;
const char *gevrModAssetPendingName;
int gevrModAssetPendingUsed;

/* A flat asset name, never an absolute path or a directory. */
static int nameOk(const char *name)
{
    size_t i;

    size_t n;

    if (!name || (n = strlen(name)) == 0 || n > 63) return 0;
    if (n > 4 && strcmp(name + n - 4, ".bin") == 0) n -= 4;
    for (i = 0; i < n; i++) {
        const char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

/* <mods>/<id>/assets/<kind>[/<name>]: 0 when there's no mod or it doesn't fit */
static int assetPath(char *out, size_t cap, const gevrModInfo *mod, const char *kind, const char *name)
{
    const char *root = gevrModsRoot();
    int n;

    if (!mod || !root || !root[0]) return 0;
    n = name ? snprintf(out, cap, "%s/%s/assets/%s/%s", root, mod->m.id, kind, name)
             : snprintf(out, cap, "%s/%s/assets/%s", root, mod->m.id, kind);
    return n > 0 && (size_t)n < cap;
}

int gevrModAssetNameOk(const char *name)
{
    return nameOk(name);
}

FILE *gevrModAssetOpenModel(const char *name)
{
    char path[1024];
    if (!nameOk(name) || !assetPath(path, sizeof(path), gevrModActive(), "models", name)) return NULL;
    return fopen(path, "rb");
}

/* a file to read: a folder opens on some systems but gives no byte */
int gevrModAssetHasModel(const char *name)
{
    unsigned char first;
    FILE *f;
    int has;

    if ((f = gevrModAssetOpenModel(name)) == NULL) return 0;
    has = fread(&first, 1, 1, f) == 1;
    fclose(f);
    return has;
}

unsigned int gevrModAssetModel(const char *name, unsigned char *dst, unsigned int capacity, int numSwitches,
                               int numTextures)
{
    const gevrModInfo *mod = gevrModActive();
    char path[1024];
    FILE *f;
    long size = 0;
    unsigned char *stored = NULL, *work = NULL;
    unsigned int len = 0, converted = 0;

    if (!mod || !dst || capacity == 0 || !nameOk(name) || !assetPath(path, sizeof(path), mod, "models", name)) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;   /* the mod has none: the cartridge's */
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size <= 0 || (unsigned long)size > GEVR_MODASSET_MODEL_MAX || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        sysLogPrintf(LOG_WARNING, "mods: %s: assets/models/%s is empty or over 4 MB; the cartridge's is used", mod->m.id, name);
        return 0;
    }
    stored = malloc((size_t)size);
    work = malloc(capacity);   /* the room the game made for the cartridge's: tried here, dst only once it converts */
    if (!stored || !work || fread(stored, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(stored);
        free(work);
        sysLogPrintf(LOG_WARNING, "mods: %s: assets/models/%s can't be read; the cartridge's is used", mod->m.id, name);
        return 0;
    }
    fclose(f);
    if (size >= 2 && stored[0] == 0x11 && stored[1] == 0x72) {
        if (gevrInflate1172(stored, (unsigned int)size, work, capacity, &len) != 0) len = 0;   /* as the cartridge stores it */
    } else if ((unsigned long)size <= capacity) {
        memcpy(work, stored, (size_t)size);   /* decompressed already */
        len = (unsigned int)size;
    }
    if (len == 0) {
        sysLogPrintf(LOG_WARNING, "mods: %s: assets/models/%s won't decompress into the %u bytes the game makes for it; "
                     "the cartridge's is used", mod->m.id, name, capacity);
    } else {
        converted = gevrModelConvert(work, len, capacity, numSwitches, numTextures, name);
        if (!converted) {
            sysLogPrintf(LOG_WARNING, "mods: %s: assets/models/%s isn't a model the cartridge's header fits "
                         "(%d switches, %d textures); the cartridge's is used", mod->m.id, name, numSwitches, numTextures);
        }
    }
    if (converted) {
        memcpy(dst, work, converted);
        sysLogPrintf(LOG_NOTE, "mods: %s: %s is the mod's (%u bytes)", mod->m.id, name, converted);
    }
    free(stored);
    free(work);
    return converted;
}

const char *gevrModAssetTextureDir(void)
{
    static char dir[1024];
    struct stat st;

    if (!assetPath(dir, sizeof(dir), gevrModPlay(), "textures", NULL) || stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return NULL;
    }
    return dir;
}
