#include <stdbool.h>
#include <wchar.h>
#include "bondconstants.h"
#include "gevr_tactical_gear.h"
#include "gevr_tactical_fit.h"
#include "gevr_tactical_stats.h"
#include "gevr_tactical_mesh.h"
#include "game/gevr_tactical_loadouts.h"
#include "system.h"

#define EXPORT __declspec(dllexport)

/* The desktop Workshop runs the same catalogue and assembly code without a game session. */
s32 sysArgCheck(const char *arg)
{
    return !strcmp(arg, "--tactical-gear") || !strcmp(arg, "--tactical-physical")
        || !strcmp(arg, "--tactical-mesh") || !strcmp(arg, "--tactical-expanded");
}

void sysLogPrintf(s32 level, const char *format, ...)
{
    (void)level;
    (void)format;
}

bool netIsActive(void) { return false; }
int gevrTacticalCampaignRules(void) { return 1; }

EXPORT const char *EditorGunName(int key)
{
    if (!gevrTacticalGearBuildAvailable(key)) return NULL;
    switch (key) {
#define LOADOUT_GUN(key, side, item, prop, label) case key: return label;
#include "gevr_tactical_loadout_guns.inc.h"
#undef LOADOUT_GUN
    default: return NULL;
    }
}

EXPORT int EditorIsSecondary(int key)
{
    if (key == GEVR_GEAR_PPK_SUPPRESSED) return 1;
    return tacticalGunIsPistol(key > GEVR_GEAR_PPK_SUPPRESSED ? key - 1 : key);
}

EXPORT int EditorGunCount(void) { return GEVR_GEAR_BUILD_COUNT; }

EXPORT int EditorCapacity(void) { return GEVR_GEAR_PARTS + 1; }
EXPORT int EditorWeaponStats(int key, GevrWeaponStats *out)
{
    return gevrWeaponStats(key, gevrTacticalGearAssembly(key), out);
}


EXPORT int EditorMountCount(int key)
{
    const GevrGearAssembly *assembly = gevrTacticalGearAssembly(key);
    return assembly ? assembly->count : 0;
}

static const GevrGearMount *getMount(int key, int index)
{
    const GevrGearAssembly *assembly = gevrTacticalGearAssembly(key);
    return assembly && index > 0 && index < assembly->count ? &assembly->mounts[index] : NULL;
}

EXPORT const char *EditorMountLabel(int key, int index)
{
    const GevrGearMount *mount = getMount(key, index);
    return mount ? mount->socket->label : NULL;
}

EXPORT const char *EditorMountPath(int key, int index)
{
    const GevrGearMount *mount = getMount(key, index);
    return mount ? mount->path : NULL;
}

EXPORT int EditorMountedPart(int key, int index) { return gevrTacticalGearMountPart(key, index); }
EXPORT int EditorOptions(int key, int index, int *parts) { return gevrTacticalGearOptions(key, index, parts); }
EXPORT int EditorReset(int key) { gevrTacticalGearReset(key); return 1; }

EXPORT int EditorFit(int key, int index, int part, int *conflict)
{
    return gevrTacticalGearCanInstall(key, index, part, conflict);
}

EXPORT int EditorInstall(int key, int index, int part, int *conflict)
{
    return gevrTacticalGearInstall(key, index, part, conflict);
}

EXPORT const char *EditorPartName(int id)
{
    const GevrGearPart *part = gevrTacticalGearPart(id);
    return part ? part->name : (id < 0 ? "Empty" : NULL);
}

EXPORT int EditorPartCapacity(int id)
{
    const GevrGearPart *part = gevrTacticalGearPart(id);
    return part ? part->capacity : 0;
}

EXPORT const char *EditorPartId(int part)
{
    const GevrGearPart *info = gevrTacticalGearPart(part);
    return info && (info->art & GEVR_GEAR_ART_MESH) ? info->id : NULL;
}

EXPORT int EditorGuardVest(int id) { return gevrTacticalGuardVest(id); }
EXPORT int EditorMissionAllowsStartingKit(int stage) { return gevrTacticalMissionAllowsStartingKit(stage); }

EXPORT const char *EditorArmourMesh(int id, int body)
{
#define TACTICAL_VEST_FIT(model, vest, path) if (body == model && id == GEVR_VEST_##vest) return "mods/tactical/" path;
#include "gevr_tactical_vest_fits.inc.h"
#undef TACTICAL_VEST_FIT
    static const char *const paths[] = {NULL,
#define TACTICAL_VEST(id, file, label) "mods/tactical/" file,
#include "gevr_tactical_vests.inc.h"
#undef TACTICAL_VEST
    };
    return id > 0 && id < (int)(sizeof(paths) / sizeof(paths[0])) ? paths[id] : NULL;
}

EXPORT const char *EditorHelmetMesh(int id)
{
    static const char *const paths[] = {
#define TACTICAL_HELMET(id, file, label) "mods/tactical/" file,
#include "gevr_tactical_helmets.inc.h"
#undef TACTICAL_HELMET
    };
    return id >= 0 && id < (int)(sizeof(paths) / sizeof(paths[0])) ? paths[id] : NULL;
}

static const GevrGearAssembly *previewAssembly(int key, const char *preset)
{
    if (!preset) return gevrTacticalGearAssembly(key);
    if (!strcmp(preset, "-")) return gevrTacticalGearStockAssembly(key);
    const int build = gevrTacticalGearNamedBuild(key, preset);
    return gevrTacticalGearDeployedAssembly(build);
}

EXPORT int EditorPreviewCount(int key, const char *preset)
{
    const GevrGearAssembly *assembly = previewAssembly(key, preset);
    return assembly ? assembly->count : 0;
}

EXPORT int EditorPreviewLateral(int key)
{
    if (key == GEVR_GEAR_PPK_SUPPRESSED) return gevrTacticalGearLateral(TACTICAL_GUN_USP);
    return gevrTacticalGearLateral(key > GEVR_GEAR_PPK_SUPPRESSED ? key - 1 : key);
}

EXPORT int EditorPreviewPiece(int key, const char *preset, int index, float transform[16])
{
    const GevrGearAssembly *assembly = previewAssembly(key, preset);
    if (!assembly || index < 0 || index >= assembly->count) return -1;
    const GevrGearMount *mount = &assembly->mounts[index];
    if (gevrTacticalGearIsRootChoice(mount->path)) return -1;
    memcpy(transform, mount->frame, 16 * sizeof(*transform));
    for (int axis = 0; axis < 3; ++axis) transform[12 + axis] *= .001f;
    return mount->part;
}

EXPORT int EditorIsSuppressed(int key)
{
    return gevrTacticalGearAssemblySuppressed(gevrTacticalGearAssembly(key));
}

EXPORT void EditorRefresh(void)
{
    gevrTacticalGearRefreshPresets();
    gevrTacticalGearResetIssuedBuilds();
}
EXPORT int EditorPresetCount(int key) { return gevrTacticalGearPresetCount(key); }
EXPORT const char *EditorPresetName(int key, int index) { return gevrTacticalGearPresetName(key, index); }
EXPORT int EditorLoad(int key, const char *name) { return gevrTacticalGearLoadNamedPreset(key, name); }
EXPORT uint64_t EditorPresetRevision(int key, const char *name) { return gevrTacticalGearPresetRevision(key, name); }
EXPORT int EditorSave(int key, const char *name) { return gevrTacticalGearSavePreset(key, name); }
EXPORT int EditorDelete(int key, const char *name, uint64_t revision)
{
    return gevrTacticalGearDeletePreset(key, name, revision);
}
EXPORT int EditorUpdate(int key, const char *name, uint64_t revision)
{
    return gevrTacticalGearUpdatePreset(key, name, revision);
}

EXPORT int EditorExport(int key, const char *name, const wchar_t *path)
{
    FILE *file = _wfopen(path, L"wx");
    if (!file) return 0;
    const int written = gevrTacticalGearExportPreset(key, name, file);
    const int closed = fclose(file);
    return written && !closed;
}

EXPORT int EditorImport(int key, const wchar_t *path)
{
    FILE *file = _wfopen(path, L"r");
    if (!file) return 0;
    const int imported = gevrTacticalGearImportPreset(key, file);
    fclose(file);
    return imported;
}
