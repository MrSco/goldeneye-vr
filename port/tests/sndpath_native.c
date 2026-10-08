/* Other players' sounds through the portals (src/game/gevr_sndpath.c), over a synthetic row of rooms. */
#include "../../src/game/gevr_sndpath.c"
#define EXPORT __declspec(dllexport)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

bg_portal_data_entry *g_BgPortals;
f32 room_data_float1 = 1.0f;
struct player *g_playerPointers[MAX_PLAYER_COUNT];
s32 sub_GAME_7F0537B8(f32 distance, f32 min, f32 max) { (void)min; (void)max; return (s32)distance; }
bool netIsActive(void) { return TRUE; }
s32 getPlayerCount(void) { return 1; }
enum CAMERAMODE g_CameraMode = CAMERAMODE_FP;
int netGetLocalSlot(void) { return 0; }
void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; (void)fmt; }
static s32 s_volume;
void sndCreatePostEvent(ALSoundState *state, s16 type, s32 value) { (void)state; (void)type; s_volume = value; }
/* the floor's room: rooms 1, 2, 3 along x, a thousand wide */
static StandTile s_floor;
StandTile *stanFindTileBelowPos(coord3d *pos, u8 *rooms, f32 *y) { (void)rooms; (void)y; s_floor.room = (u8)(1 + (s32)(pos->x / 1000.0f)); return &s_floor; }
static int s_spatialInits;
int netSpatialInit(void) { return ++s_spatialInits; }
void netSpatialResetSlot(unsigned slot) { (void)slot; }
/* looking down -z, as the head does at no turn */
void gevrVoiceListenerBasis(float forward[3], float up[3]) { forward[0] = 0; forward[1] = 0; forward[2] = -1; up[0] = 0; up[1] = 1; up[2] = 0; }

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
    CHECK(s_count == 2 && !s_rowReady[0] && spRow(0)[1] > 999.0f && spRow(0)[1] < 1001.0f && s_rowReady[0]);
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
    /* a placed sound: measured from the local player, in the room under it */
    {
        static struct player me;
        static PropRecord body;
        static ALSoundState shot, other;
        coord3d left = { { -300, 100, 0 } };

        body.pos = here;
        body.rooms[0] = 1;
        me.prop = &body;
        g_playerPointers[0] = &me;
        CHECK(gevrSndPathVolume(&there, -1, 5000.0f, 6000.0f) == (s32)gevrSndPathDistance(1, &here, 3, &there));
        CHECK(gevrSndPathVolume(&nextdoor, 1, 5000.0f, 6000.0f) == (s32)spDist(&here, &nextdoor));
        /* heard from where it is: 3 m to the listener's left */
        left.x += here.x;
        gevrSndSpatialPlace(&shot, &left);
        CHECK(s_spatialInits == 1 && s_tagCount == 1);
        gevrSndSpatialVoice(&other.voice);
        CHECK(g_gevrSndSpatialSlot == -1);
        gevrSndSpatialVoice(&shot.voice);
        CHECK(g_gevrSndSpatialSlot == NET_SPATIAL_SFX_FIRST && g_gevrSndSpatialPositioned);
        CHECK(g_gevrSndSpatialDir[0] < -0.99f && g_gevrSndSpatialDir[2] > -0.01f && g_gevrSndSpatialDir[2] < 0.01f);
        /* freed: the next sound in that state is not placed */
        gevrSndSpatialForget(&shot);
        gevrSndSpatialVoice(&shot.voice);
        CHECK(g_gevrSndSpatialSlot == -1 && s_tagCount == 0);
        g_playerPointers[0] = NULL;
    }
    /* title: the stage bank was reset and the previous portals were cleared */
    g_BgPortals = NULL;
    gevrSndPathStageLoaded();
    CHECK(s_count == 0);
    CHECK(gevrSndPathDistance(1, &here, 3, &there) == spDist(&here, &there));
    /* the next stage still builds its paths */
    rooms();
    gevrSndPathStageLoaded();
    CHECK(s_count == 2);
    d = gevrSndPathDistance(1, &here, 3, &there);
    CHECK(d > 2.0f * 1000.0f + 900.0f && d < 3000.0f + 2.0f * 1000.0f);
    /* not online: no table, straight */
    spFree();
    CHECK(gevrSndPathDistance(1, &here, 3, &there) == spDist(&here, &there));
    return 0;
}
