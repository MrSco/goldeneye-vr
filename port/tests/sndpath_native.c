/* Other players' sounds through the portals (src/game/gevr_sndpath.c), over a synthetic row of rooms. */
#include "../../src/game/gevr_sndpath.c"
#define EXPORT __declspec(dllexport)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

bg_portal_data_entry *g_BgPortals;
f32 room_data_float1 = 1.0f;
struct player *g_playerPointers[MAX_PLAYER_COUNT];
s32 sub_GAME_7F0537B8(f32 distance, f32 min, f32 max) { (void)min; (void)max; return (s32)distance; }
bool netIsActive(void) { return TRUE; }
int netGetLocalSlot(void) { return 0; }
void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; (void)fmt; }
static s32 s_volume;
void sndCreatePostEvent(ALSoundState *state, s16 type, s32 value) { (void)state; (void)type; s_volume = value; }

/*
 * Rooms 1, 2, 3 in a row along x, 1000 wide, each joined to the next by a
 * doorway at z = 900 (the far side): from room 1 to room 3 a sound goes up
 * to the doorways, across, and back down.
 */
static struct { bg_portal_entry portal; coord3d more[3]; } s_doors[2];
static bg_portal_data_entry s_table[3];

static void rooms(void)
{
    for (s32 d = 0; d < 2; d++) {
        f32 x = 1000.0f * (d + 1);
        coord3d *p = &s_doors[d].portal.point;
        s_doors[d].portal.numPoints = 4;
        p[0] = (coord3d){ { x, 0, 800 } };  p[1] = (coord3d){ { x, 200, 800 } };
        p[2] = (coord3d){ { x, 200, 1000 } }; p[3] = (coord3d){ { x, 0, 1000 } };
        s_table[d].offset_portal = &s_doors[d].portal;
        s_table[d].connectedRoom1 = (u8)(d + 1);
        s_table[d].connectedRoom2 = (u8)(d + 2);
    }
    s_table[2].offset_portal = NULL;
    g_BgPortals = s_table;
}

EXPORT int test_sndpath(void)
{
    coord3d here = { { 500, 100, 0 } }, there = { { 2500, 100, 0 } }, nextdoor = { { 1500, 100, 0 } };
    f32 d;

    rooms();
    gevrSndPathStageLoaded();
    CHECK(s_count == 2 && s_path[1] > 999.0f && s_path[1] < 1001.0f);
    /* the same room: a straight line */
    CHECK(gevrSndPathDistance(1, &here, 1, &nextdoor) == spDist(&here, &nextdoor));
    /* the next room behind the wall: round by its doorway, never nearer than straight */
    d = gevrSndPathDistance(1, &here, 2, &nextdoor);
    CHECK(d > spDist(&here, &nextdoor) + 500.0f);
    /* two rooms on: through both doorways */
    d = gevrSndPathDistance(1, &here, 3, &there);
    CHECK(d > 2.0f * 1000.0f + 900.0f && d < 3000.0f + 2.0f * 1000.0f);
    /* a room unknown: straight */
    CHECK(gevrSndPathDistance(-1, &here, 3, &there) == spDist(&here, &there));
    /* not online: no table, straight */
    spFree();
    CHECK(gevrSndPathDistance(1, &here, 3, &there) == spDist(&here, &there));
    return 0;
}
