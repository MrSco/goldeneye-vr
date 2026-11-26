/* gevr_modgame.c gevrModGameGunModel with input.c's gevrIsThrowable, both
 * the game's own: a muzzle on a thrown weapon is refused before anything
 * loads; a gun takes it; a legacy file refuses a fit or a muzzle. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* the item ids come with the production text */

typedef struct GevrModMesh { int unused; } GevrModMesh;
static int s_loads, s_sets, s_setMuzzle, s_setSupport;

/* assets: a pack mesh (native 1) and a cartridge-format file (native 0) */
static int gevrModAssetHasModel(const char *name) { return !strcmp(name, "pack.bin") || !strcmp(name, "GlegacyZ"); }
static int gevrModMeshLoad(const char *name, const GevrModMesh **out)
{
    static const GevrModMesh mesh;
    ++s_loads;
    *out = &mesh;
    return !strcmp(name, "pack.bin") ? 1 : 0;
}
static void gevrModGunModelSet(int hand, int item, const char *name, const float frame[4][4], const float muzzle[3], const float support[3])
{
    (void)hand, (void)item, (void)name, (void)frame;
    ++s_sets;
    s_setMuzzle = muzzle != NULL;
    s_setSupport = support != NULL;
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
    static const int thrown[] = {ITEM_THROWKNIFE, ITEM_GRENADE, ITEM_TIMEDMINE, ITEM_PROXIMITYMINE, ITEM_REMOTEMINE};

    for (unsigned i = 0; i < sizeof(thrown) / sizeof(thrown[0]); ++i) {
        s_loads = s_sets = 0;
        expect(gevrModGameGunModel(0, thrown[i], "pack.bin", NULL, muzzle, NULL), 0, "a thrown weapon refuses a muzzle");
        expect(gevrModGameGunModel(0, thrown[i], "pack.bin", NULL, NULL, muzzle), 0, "a thrown weapon refuses a support point");
        expect(s_loads + s_sets, 0, "nothing loads or is set for a refused muzzle");
        expect(gevrModGameGunModel(0, thrown[i], "pack.bin", frame, NULL, NULL), 1, "a thrown weapon takes a fit");
    }
    s_sets = 0;
    expect(gevrModGameGunModel(0, ITEM_AK47, "pack.bin", frame, muzzle, muzzle), 1, "a gun takes a fit, muzzle and support point");
    expect(s_sets == 1 && s_setMuzzle && s_setSupport, 1, "both authored points reach gun.c");
    expect(gevrModGameGunModel(0, ITEM_AK47, "GlegacyZ", NULL, NULL, muzzle), 0, "a legacy file refuses a support point");
    expect(gevrModGameGunModel(0, ITEM_AK47, "GlegacyZ", NULL, muzzle, NULL), 0, "a legacy file refuses a muzzle");
    expect(gevrModGameGunModel(0, ITEM_AK47, "GlegacyZ", NULL, NULL, NULL), 1, "a legacy file without options");
    expect(gevrModGameGunModel(0, ITEM_AK47, "missing.bin", NULL, NULL, NULL), 0, "a missing file");
    if (failures) return EXIT_FAILURE;
    puts("gun model: thrown weapons refuse a muzzle in gevr_modgame.c's own check");
    return EXIT_SUCCESS;
}
