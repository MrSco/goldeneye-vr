/* bondview2.c gevrMuzzleFitAvailable against a stub hand: Gun fit offers the
 * barrel tip for a stock or GE-X gun, and not while a pack body's authored
 * muzzle replaces the trim it would edit. */
#include <stdio.h>
#include <stdlib.h>

typedef int s32;
typedef union { struct { float x, y, z; }; float f[3]; } coord3d;
typedef struct { void **Switches; s32 numSwitches; } ModelFileHeader;
typedef struct { int unused; } GexWeaponDef;
enum { FALSE, TRUE };
enum { GUNLEFT, GUNRIGHT };
enum { ITEM_UNARMED, ITEM_FIST, ITEM_PP7 = 3, GEVR_MAX_WEAPONS = 90 };

static int s_player = 1;
static int *g_CurrentPlayer = &s_player;
static s32 s_item = ITEM_PP7, s_gex, s_packMuzzle;
static void *s_switches[8];
static ModelFileHeader s_header = { s_switches, 8 };
static GexWeaponDef s_gexDef;

static s32 getCurrentPlayerWeaponId(s32 hand) { return hand == GUNRIGHT ? s_item : ITEM_UNARMED; }
static ModelFileHeader *get_ptr_itemheader_in_hand(s32 hand) { return hand == GUNRIGHT ? &s_header : NULL; }
static s32 gevrGexHeld(s32 hand) { return hand == GUNRIGHT && s_gex; }
static const GexWeaponDef *gevrGexWeaponForHand(s32 hand) { return gevrGexHeld(hand) ? &s_gexDef : NULL; }
s32 gevrModGunMuzzle(s32 hand, coord3d *out)
{
    if (hand != GUNRIGHT || !s_packMuzzle) return FALSE;
    out->x = 0, out->y = 3, out->z = 350;
    return TRUE;
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
    static int flashNode;
    s_switches[3] = &flashNode;
    expect(gevrMuzzleFitAvailable() != 0, 1, "stock gun with a flash node");
    s_packMuzzle = 1;
    expect(gevrMuzzleFitAvailable() != 0, 0, "pack body with an authored muzzle");
    s_gex = 1;
    expect(gevrMuzzleFitAvailable() != 0, 0, "pack muzzle over a GE-X gun");
    s_packMuzzle = 0;
    expect(gevrMuzzleFitAvailable() != 0, 1, "GE-X gun");
    s_gex = 0;
    s_switches[3] = NULL;
    expect(gevrMuzzleFitAvailable() != 0, 0, "stock gun without a flash node");
    s_switches[3] = &flashNode;
    s_item = ITEM_FIST;
    expect(gevrMuzzleFitAvailable() != 0, 0, "fist");
    s_item = ITEM_PP7;
    g_CurrentPlayer = NULL;
    expect(gevrMuzzleFitAvailable() != 0, 0, "no player");
    if (failures) return EXIT_FAILURE;
    puts("muzzle fit: stock and GE-X guns offer it, pack muzzles and guns without a tip do not");
    return EXIT_SUCCESS;
}
