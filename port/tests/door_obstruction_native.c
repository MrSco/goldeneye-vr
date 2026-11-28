#include <stdint.h>
#include <stdio.h>
#include "bondtypes.h"
#include "bondconstants.h"
#include "game/chrai.h"
#include "game/chraction.h"
#include "game/propobj.h"
#include "gevr_ragdoll.h"

PropRecord g_Props[2];
static s16 roomProps[] = {0, 1, -1};
s16 *ptr_list_object_lookup_indices = roomProps;
static struct rect4f overlap[4];

void chraiGetCollisionBounds(PropRecord *prop, struct rect4f **polygon, s32 *edges, f32 *top, f32 *bottom)
{
    (void)prop;
    *polygon = overlap;
    *edges = 4;
    *top = 100.f;
    *bottom = 0.f;
}

void chraiGetPropRoomIds(PropRecord *prop, s32 *roomids)
{
    (void)prop;
    roomids[0] = -1;
}

void roomGetProps(s32 *roomids) { (void)roomids; }

void chrpropGetCollisionBounds(PropRecord *prop, f32 *radius, f32 *top, f32 *bottom)
{
    (void)prop;
    *radius = 24.f;
    *top = 100.f;
    *bottom = 0.f;
}

f32 sub_GAME_7F03CFE8(PropRecord *prop) { (void)prop; return 0.f; }

static ChrRecord *doorHit;
static int doorHits;
static s32 doorPast;   /* chraction.c: TRUE for a body past the original lifetime */
s32 gevrBodyDoorHit(ChrRecord *chr) { doorHit = chr; doorHits++; return doorPast; }

static ChrRecord *ghost;   /* gevr_ghost.c: the race's see-through Bond */
s32 gevrGhostIsChr(ChrRecord *chr) { return chr != NULL && chr == ghost; }

/* The door's outline: everything with x below 50 (an occupant stands at 0) */
s32 chrpropTestPointInPolygon(coord3d *point, struct rect4f *polygon, s32 edges)
{
    (void)polygon;
    (void)edges;
    return point->f[0] < 50.f;
}

s32 chrobjTestPointPolygonCollision(coord3d *point, f32 radius, struct rect4f *polygon, s32 edges)
{
    (void)polygon;
    (void)edges;
    return point->f[0] < 50.f + radius;
}

/* gevr_ragdoll.c: body 0 lies in the doorway, body 1 away from it, body 2 above the door */
static coord3d ragdollParts[3][2] = {{{{{10, 5, 0}}}, {{{30, 8, 0}}}}, {{{{400, 5, 0}}}, {{{420, 5, 0}}}},
                                     {{{{10, 160, 0}}}, {{{12, 170, 0}}}}};
static int ragdolls, ragdollFaded[3];
int gevrRagdollBodies(void) { return ragdolls; }
int gevrRagdollBodyParts(int body, coord3d *pos, int max)
{
    int i;
    for (i = 0; i < 2 && i < max; i++) pos[i] = ragdollParts[body][i];
    return i;
}
void gevrRagdollDoorFade(int body) { ragdollFaded[body]++; }
void gevrRagdollDoorFadeLog(int body, int part, const float at[3], int truck, const float by[3],
                            const float *outline, int edges, float bottom, float top)
{
    (void)body; (void)part; (void)at; (void)truck; (void)by; (void)outline; (void)edges; (void)bottom; (void)top;
}

s32 chrobjTestPolygonsTouchingOrOverlap2D(struct rect4f *a, s32 na, struct rect4f *b, s32 nb)
{
    (void)a;
    (void)na;
    (void)b;
    (void)nb;
    return 1;
}

/* INSERT_PRODUCTION_CODE */

static int check(const char *name, s32 expected, int hits)
{
    doorHit = NULL;
    doorHits = 0;
    const s32 result = sub_GAME_7F0448A8(&g_Props[0]);
    if (result != expected || doorHits != hits
        || (hits && doorHit != g_Props[1].chr)) {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

int main(void)
{
    ChrRecord guard = {0};
    ObjectRecord crate = {.type = PROPDEF_PROP};
    int failures = 0;
    g_Props[0].type = PROP_TYPE_DOOR;
    roomProps[1] = -1;
    failures += check("clear door", 1, 0);
    roomProps[1] = 1;

    /* A falling guard blocks as in the original; a body on the floor also
     * blocks, but the door touching it starts its fade. */
    g_Props[1] = (PropRecord){.type = PROP_TYPE_CHR, .chr = &guard};
    guard.actiontype = ACT_DIE;
    failures += check("dying guard blocks door without fading", 0, 0);
    guard.actiontype = ACT_DEAD;
    failures += check("a body within the original lifetime blocks the door", 0, 1);
    doorPast = TRUE;
    failures += check("a body past the original lifetime fades and never holds the door", 1, 1);
    doorPast = FALSE;

    guard.actiontype = ACT_STAND;
    guard.hidden = 0;
    failures += check("living guard blocks door", 0, 0);
    failures += !(guard.hidden & CHRHIDDEN_OFFSCREEN_PATROL);
    /* 64-bit builds read the chr's prop pointer as its flags: an address with
     * bit 10 set let the guard through. Only CHRFLAG_HIDDEN does. */
    guard.prop = (PropRecord *)(uintptr_t)0x7400;
    failures += check("a guard blocks whatever its prop's address", 0, 0);
    guard.prop = NULL;
    guard.chrflags = CHRFLAG_HIDDEN;
    failures += check("a hidden guard never blocks", 1, 0);
    guard.chrflags = 0;
    ghost = &guard;
    failures += check("the race's ghost never blocks", 1, 0);
    ghost = NULL;
    g_Props[1] = (PropRecord){.type = PROP_TYPE_VIEWER};
    failures += check("player blocks door", 0, 0);
    g_Props[1] = (PropRecord){.type = PROP_TYPE_OBJ, .obj = &crate};
    failures += check("crate blocks door", 0, 0);

    /* A ragdoll body in the doorway fades and never holds the door; one beside the
     * door or above it is left alone */
    roomProps[1] = -1;
    ragdolls = 3;
    failures += check("ragdoll bodies never block a door", 1, 0);
    if (ragdollFaded[0] != 1 || ragdollFaded[1] || ragdollFaded[2]) {
        fprintf(stderr, "FAIL: a door fades only the ragdoll body it touches\n");
        failures++;
    }
    roomProps[1] = 1;

    g_Props[0].type = PROP_TYPE_OBJ;
    g_Props[1] = (PropRecord){.type = PROP_TYPE_CHR, .chr = &guard};
    guard.actiontype = ACT_DEAD;
    ragdollFaded[0] = 0;
    failures += check("corpse blocks non-door objects without fading", 0, 0);
    if (ragdollFaded[0]) {
        fprintf(stderr, "FAIL: only a door or the truck fades ragdoll bodies\n");
        failures++;
    }

    /* Dam's truck (owner, 2026-11-23): it stops for Bond, a guard, and a body
     * until it fades, as in the original; a body kept past that fades, and a
     * ragdoll it touches fades, as at a door */
    ObjectRecord truck = {.type = PROPDEF_VEHICHLE};
    g_Props[0] = (PropRecord){.type = PROP_TYPE_OBJ, .obj = &truck};
    g_Props[1] = (PropRecord){.type = PROP_TYPE_VIEWER};
    failures += check("the player stops the truck", 0, 0);
    g_Props[1] = (PropRecord){.type = PROP_TYPE_CHR, .chr = &guard};
    guard.actiontype = ACT_STAND;
    guard.hidden = 0;
    failures += check("a guard stops the truck", 0, 0);
    failures += (guard.hidden & CHRHIDDEN_OFFSCREEN_PATROL) != 0;   /* a door's mark only */
    ghost = &guard;
    failures += check("the race's ghost never stops the truck", 1, 0);
    ghost = NULL;
    guard.actiontype = ACT_DEAD;
    failures += check("a body within the original lifetime stops the truck", 0, 1);
    doorPast = TRUE;
    failures += check("a body past the original lifetime fades and never holds the truck", 1, 1);
    doorPast = FALSE;
    roomProps[1] = -1;
    ragdollFaded[0] = 0;
    failures += check("ragdoll bodies never stop the truck", 1, 0);
    if (ragdollFaded[0] != 1) {
        fprintf(stderr, "FAIL: the truck fades the ragdoll body it touches\n");
        failures++;
    }
    roomProps[1] = 1;
    if (!failures) puts("PASS: door obstruction");
    return failures != 0;
}
