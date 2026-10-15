#include <math.h>
#include <stdio.h>
#include <assert.h>
#include "gevr_speedboat.h"
typedef float f32;
typedef struct { f32 m[4][4]; } Mtxf;
struct coord3d { f32 x, y, z; };
struct Model { f32 scale; };
struct ObjectRecord { Mtxf mtx; struct coord3d runtime_pos; struct Model *model; };
struct PropRecord { struct ObjectRecord *obj; struct coord3d pos; };
static struct PropRecord *s_boat;
static struct GevrBoatRide s_ride;
static int room_updates;
static f32 s_hullY;   /* the hull on the water, apart from the ride's jetty height */
/* gevr_dam_boat.c's ride ends with its hull: test_dam_boat_lifecycle.py runs the
 * real check; here the hull is live while the prop has its object. */
static struct ObjectRecord *s_boatObj;
static int gevrDamBoatLive(void) { s_boatObj = s_boat != NULL ? s_boat->obj : NULL; return s_boatObj != NULL; }
static void setupUpdateObjectRoomPosition(struct ObjectRecord *obj) { (void)obj; room_updates++; }
/* INSERT_TRANSFORM */
/* INSERT_PUSH */
int main(void)
{
    struct Model model = { gevrDamBoatVisualScale(0.1f) };
    struct ObjectRecord obj = { .model = &model };
    struct PropRecord prop = { .obj = &obj };
    gevrDamBoatPush(); /* Missing boat during stage teardown is safe. */
    s_boat = &prop;
    prop.obj = NULL;
    gevrDamBoatPush();
    assert(room_updates == 0);
    prop.obj = &obj;
    for (int tick = 0; tick < 100; tick++)
    {
        s_ride.x = 1000 + tick;
        s_ride.y = -200 + tick;
        s_hullY = -500 + tick;
        s_ride.z = 3000 - tick;
        s_ride.yaw = tick * 0.1f;
        gevrDamBoatPush();
        /* The renderer copies this basis directly: metadata alone cannot size it. */
        for (int row = 0; row < 3; row++)
        {
            float length = 0;
            for (int col = 0; col < 3; col++) length += obj.mtx.m[row][col] * obj.mtx.m[row][col];
            assert(fabsf(sqrtf(length) - 0.175f) < 0.00001f);
        }
        assert(obj.mtx.m[3][0] == s_ride.x && obj.mtx.m[3][1] == s_hullY && obj.mtx.m[3][2] == s_ride.z);
        assert(obj.mtx.m[3][3] == 1);
        assert(obj.runtime_pos.x == s_ride.x && obj.runtime_pos.y == s_hullY && obj.runtime_pos.z == s_ride.z);
        assert(prop.pos.x == s_ride.x && prop.pos.y == s_hullY && prop.pos.z == s_ride.z);
    }
    assert(room_updates == 100);
    puts("PASS: production boat transform stays 1.75x retail across 100 turns without scaling position");
    return 0;
}
