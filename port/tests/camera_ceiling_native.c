#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include "game/bg.h"
#include "gevr_camera_collision.h"

bool intersectRayTriangle(Vertex *, Vertex *, Vertex *, coord3d *, coord3d *, coord3d *, coord3d *, HitThing *);

s32 g_MaxNumRooms = 4;
s_room_info g_BgRoomInfo[4];
static bg_portal_data_entry portals[1];
bg_portal_data_entry *g_BgPortals = portals;
static f32 room_data_float1 = 1;
static s32 loads, rays;
static struct { Vertex v[4]; coord3d offset; s32 exists; } meshes[4];

static s32 bgGetConnectedRooms(s32 room, s32 *rooms, s32 max)
{
    assert(max >= 2);
    rooms[0] = room == 1 ? 2 : 1;
    rooms[1] = 3;
    return 2;
}
static s32 bgCheckIfRoomModelNeedsLoad(s32 room)
{
    loads++;
    g_BgRoomInfo[room].vtx_batch_bounds = (void *)1;
    return 0;
}
/* Asset enumeration is mocked; all intersections use the production triangle
 * routine, including its infinite-ray semantics and both triangle windings. */
bool bgTestBulletHitBackground(coord3d *from, coord3d *to, s32 room, HitThing *hit)
{
    coord3d a, b, d;
    HitThing h;
    s32 found = 0;
    rays++;
    if (!meshes[room].exists) return FALSE;
    for (s32 i=0; i<3; i++) {
        a.f[i] = from->f[i] * room_data_float1;
        b.f[i] = to->f[i] * room_data_float1;
        d.f[i] = b.f[i] - a.f[i];
    }
    for (s32 i=0; i<2; i++) {
        if (intersectRayTriangle(&meshes[room].v[0], &meshes[room].v[i+1],
                                &meshes[room].v[i+2], &meshes[room].offset, &a, &b, &d, &h)) {
            if (!found || h.hitpos.y < hit->hitpos.y) *hit = h;
            found = 1;
        }
    }
    return found;
}

/* INSERT_BG */

static struct TestPlayer {
    s32 cameramode, bonddead;
    struct { StandTile *current_tile_ptr_for_portals; coord3d pos3; } field_488;
} player, *g_CurrentPlayer = &player;
static s32 g_gevrStereo, s_gevrCopyTrace, g_PlayerIsInTank, g_CameraMode = CAMERAMODE_FP;
static s32 online, playerSlot, localSlot;
static s32 netIsActive(void) { return online; }
static s32 get_cur_playernum(void) { return playerSlot; }
static s32 netGetLocalSlot(void) { return localSlot; }
static f32 s_gevrCameraFloor;
static s32 s_gevrCameraRoom;
static f32 s_gevrCameraCeilingDrop;

/* INSERT_GAME */

static void reset(void)
{
    memset(meshes, 0, sizeof(meshes));
    memset(g_BgRoomInfo, 0, sizeof(g_BgRoomInfo));
    memset(&player, 0, sizeof(player));
    for (s32 i=1; i<4; i++) {
        g_BgRoomInfo[i].minbounds = (coord3d){.f={-1000,0,-1000}};
        g_BgRoomInfo[i].maxbounds = (coord3d){.f={1000,1000,1000}};
    }
    room_data_float1 = 1;
    loads = rays = 0;
    online = playerSlot = localSlot = g_gevrStereo = s_gevrCopyTrace = g_PlayerIsInTank = 0;
    g_CameraMode = CAMERAMODE_FP;
    s_gevrCameraRoom = 0;
    g_BgPortals = portals;
}
static void ceiling(s32 room, s32 y, s32 xmin, s32 xmax, s32 zmin, s32 zmax)
{
    const s32 x[4] = {xmin,xmax,xmax,xmin}, z[4] = {zmin,zmin,zmax,zmax};
    for (s32 i=0; i<4; i++) {
        meshes[room].v[i].coord.x = x[i];
        meshes[room].v[i].coord.y = y;
        meshes[room].v[i].coord.z = z[i];
    }
    meshes[room].exists = 1;
}
static void closeTo(f32 actual, f32 expected) { assert(fabsf(actual-expected) < 0.001f); }

int main(void)
{
    reset();
    ceiling(1,170,-64,64,-64,64);
    coord3d eye = {.f={0,175,0}};
    gevrBgClampCameraCeiling(eye.f,0,1,TRUE);
    closeTo(eye.y,150); // recover an eye already above the lintel
    closeTo(eye.x,0); closeTo(eye.z,0);
    eye.y = 230; // tall character (Jaws) penetrates farther than an ordinary eye
    gevrBgClampCameraCeiling(eye.f,0,1,TRUE);
    closeTo(eye.y,150);
    eye.x = 100; eye.y = 230;
    gevrBgClampCameraCeiling(eye.f,0,1,TRUE);
    closeTo(eye.y,230); // retain the tall character's height outside the opening
    eye.x = 0;
    eye.y = 130;
    gevrBgClampCameraCeiling(eye.f,0,1,TRUE);
    closeTo(eye.y,130); // infinite ray's hit beyond the probe is ignored
    eye.y = 200;
    loads = 0;
    gevrBgClampCameraCeiling(eye.f,0,1,FALSE);
    closeTo(eye.y,150); assert(loads==0);

    reset();
    ceiling(2,165,4,16,-64,64); // only the eye footprint reaches the next room's lintel
    eye = (coord3d){.f={0,175,0}};
    gevrBgClampCameraCeiling(eye.f,0,1,TRUE);
    closeTo(eye.y,145);
    eye.x = 30; eye.y = 175;
    gevrBgClampCameraCeiling(eye.f,0,1,TRUE);
    closeTo(eye.y,175); // restores standing height on leaving the doorway

    reset();
    room_data_float1 = 0.25f;
    ceiling(1,42,-64,64,-64,64);
    meshes[1].offset.y = 100;
    eye = (coord3d){.f={0,575,0}};
    gevrBgClampCameraCeiling(eye.f,400,1,TRUE);
    closeTo(eye.y,548); // world = (42+100)/.25, independent of visibility
    Vertex tmp = meshes[1].v[1]; meshes[1].v[1] = meshes[1].v[3]; meshes[1].v[3] = tmp;
    eye.y = 575;
    gevrBgClampCameraCeiling(eye.f,400,1,TRUE);
    closeTo(eye.y,548);

    reset();
    eye = (coord3d){.f={0,175,0}};
    gevrBgClampCameraCeiling(eye.f,0,1,TRUE);
    closeTo(eye.y,175); // open sky
    g_BgRoomInfo[3].minbounds.x = 5000;
    loads = 0;
    gevrBgClampCameraCeiling(eye.f,0,1,TRUE);
    assert(loads==2); // never loads distant neighbours
    rays = 0; loads = 0;
    gevrBgClampCameraCeiling(eye.f,0,0,TRUE);
    assert(loads==0 && rays==0);
    g_BgPortals = NULL;
    gevrBgClampCameraCeiling(eye.f,0,1,TRUE);
    assert(loads==0 && rays==0);

    reset();
    StandTile tile = {0}; tile.room = 1;
    player.field_488.current_tile_ptr_for_portals = &tile;
    ceiling(1,170,-64,64,-64,64);
    for (g_gevrStereo=0; g_gevrStereo<=1; g_gevrStereo++) {
        eye = (coord3d){.f={0,205,0}};
        gevrGameCameraCeiling(&eye);
        closeTo(eye.y,150); // flat animation height and stereo physical rise
    }
    eye.y = 205;
    online = 1; playerSlot = 1;
    player.field_488.pos3.y = 400;
    gevrGameCameraCeiling(&eye); // another player's render must not replace the local context
    eye.y = 205; loads = 0;
    gevrClampCameraCeiling(eye.f);
    closeTo(eye.y,150); assert(loads==0);
    closeTo(s_gevrCameraCeilingDrop,55); // another player's render keeps the local drop too
    online = playerSlot = 0; player.field_488.pos3.y = 0;
    player.cameramode = 1; eye.y = 205;
    gevrGameCameraCeiling(&eye);
    closeTo(eye.y,205); assert(s_gevrCameraRoom==0);
    player.cameramode = 0; player.bonddead = 1;
    gevrGameCameraCeiling(&eye); closeTo(eye.y,205);
    player.bonddead = 0; g_PlayerIsInTank = 1;
    gevrGameCameraCeiling(&eye); closeTo(eye.y,205);
    g_PlayerIsInTank = 0; g_CameraMode = CAMERAMODE_SWIRL;
    gevrGameCameraCeiling(&eye); closeTo(eye.y,205);
    puts("PASS: production lintel clearance, recovery, ducking, neighbouring rooms, footprint, asset scale, flat/stereo and local redraw context");
}
