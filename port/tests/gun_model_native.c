/* gevr_modgame.c gevrModGameGunModel with input.c's gevrIsThrowable, both
 * the game's own: a muzzle on a thrown weapon is refused before anything
 * loads; a gun takes it; a legacy file refuses a fit or a muzzle. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* the item ids come with the production text */

typedef struct GevrModMesh { int unused; } GevrModMesh;
enum { GEVR_MODPART_ACTION, GEVR_MODPART_HANDLE, GEVR_MODPART_GROUPS };
typedef struct GevrModGunParts {
    unsigned pictures[GEVR_MODPART_GROUPS];
    float travel[GEVR_MODPART_GROUPS][3];
    int holdOpen;
} GevrModGunParts;
static int s_loads, s_sets, s_splits, s_setMuzzle, s_setSupport, s_setParts;

/* assets: a pack mesh (native 1) and a cartridge-format file (native 0) */
static int gevrModAssetHasModel(const char *name) { return !strcmp(name, "pack.bin") || !strcmp(name, "GlegacyZ"); }
static int gevrModMeshLoad(const char *name, const GevrModMesh **out)
{
    static const GevrModMesh mesh;
    ++s_loads;
    *out = &mesh;
    return !strcmp(name, "pack.bin") ? 1 : 0;
}
/* the mesh has pictures 0-3 (gevr_modmesh.c refuses a group past them) */
static int gevrModMeshSplit(const GevrModMesh *mesh, const unsigned pictures[GEVR_MODPART_GROUPS])
{
    (void)mesh;
    ++s_splits;
    return !((pictures[0] | pictures[1]) & ~15u);
}
static void gevrModGunModelSet(int hand, int item, const char *name, const float frame[4][4], const float muzzle[3], const float support[3],
                               const GevrModGunParts *parts)
{
    (void)hand, (void)item, (void)name, (void)frame;
    ++s_sets;
    s_setMuzzle = muzzle != NULL;
    s_setSupport = support != NULL;
    s_setParts = parts != NULL;
}

/* PRODUCTION */

static int failures;
static void expect(int got, int want, const char *what)
{
    if (got != want) {
        printf("FAIL %s: %d, wanted %d\n", what, got, want);
        ++failures;
    }
}

int main(void)
{
    static const float muzzle[3] = {0, 27, 630};
    static const float frame[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    static const GevrModGunParts bolt = {{2, 0}, {{0, 0, -44}}, 0};
    static const GevrModGunParts stray = {{1u << 5, 0}, {{0, 0, -44}}, 1};
    static const GevrModGunParts holdOnly = {{0, 0}, {{0}}, 0};
    static const int thrown[] = {ITEM_THROWKNIFE, ITEM_GRENADE, ITEM_TIMEDMINE, ITEM_PROXIMITYMINE, ITEM_REMOTEMINE};

    for (unsigned i = 0; i < sizeof(thrown) / sizeof(thrown[0]); ++i) {
        s_loads = s_sets = 0;
        expect(gevrModGameGunModel(0, thrown[i], "pack.bin", NULL, muzzle, NULL, NULL), 0, "a thrown weapon refuses a muzzle");
        expect(gevrModGameGunModel(0, thrown[i], "pack.bin", NULL, NULL, muzzle, NULL), 0, "a thrown weapon refuses a support point");
        expect(s_loads + s_sets, 0, "nothing loads or is set for a refused muzzle");
        expect(gevrModGameGunModel(0, thrown[i], "pack.bin", frame, NULL, NULL, NULL), 1, "a thrown weapon takes a fit");
        expect(gevrModGameGunModel(0, thrown[i], "pack.bin", NULL, NULL, NULL, &bolt), 0, "a thrown weapon refuses an action");
    }
    s_sets = 0;
    expect(gevrModGameGunModel(0, ITEM_AK47, "pack.bin", frame, muzzle, muzzle, NULL), 1, "a gun takes a fit, muzzle and support point");
    expect(s_sets == 1 && s_setMuzzle && s_setSupport && !s_setParts, 1, "both authored points reach gun.c");
    s_sets = s_splits = 0;
    expect(gevrModGameGunModel(0, ITEM_SNIPERRIFLE, "pack.bin", NULL, NULL, NULL, &bolt), 1, "a gun takes moving pictures");
    expect(s_sets == 1 && s_splits == 1 && s_setParts, 1, "its lists are split before gun.c has the look");
    expect(gevrModGameGunModel(0, ITEM_SNIPERRIFLE, "pack.bin", NULL, NULL, NULL, &stray), 0, "a picture the file lacks refuses it");
    expect(s_sets, 1, "and gun.c keeps the last look");
    expect(gevrModGameGunModel(0, ITEM_SNIPERRIFLE, "pack.bin", NULL, NULL, NULL, &holdOnly), 1, "holdOpen alone needs no split");
    expect(s_splits, 2, "no split for a look without moving pictures");
    expect(gevrModGameGunModel(0, ITEM_AK47, "GlegacyZ", NULL, NULL, NULL, &bolt), 0, "a legacy file refuses an action");
    expect(gevrModGameGunModel(0, ITEM_AK47, "GlegacyZ", NULL, NULL, muzzle, NULL), 0, "a legacy file refuses a support point");
    expect(gevrModGameGunModel(0, ITEM_AK47, "GlegacyZ", NULL, muzzle, NULL, NULL), 0, "a legacy file refuses a muzzle");
    expect(gevrModGameGunModel(0, ITEM_AK47, "GlegacyZ", NULL, NULL, NULL, NULL), 1, "a legacy file without options");
    expect(gevrModGameGunModel(0, ITEM_AK47, "missing.bin", NULL, NULL, NULL, NULL), 0, "a missing file");
    if (failures) return EXIT_FAILURE;
    puts("gun model: thrown weapons refuse a muzzle in gevr_modgame.c's own check");
    return EXIT_SUCCESS;
}
