#include "gevr_collision_slide.h"
#include "gevr_frame_timing.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define GEVR 1
typedef int s32;
typedef float f32;
typedef struct StandTile { int id; } StandTile;
struct coord3d { float f[3]; };
static struct {
    struct { struct coord3d collision_position; StandTile *current_tile_ptr; } field_488;
} player, *g_CurrentPlayer = &player;
static int g_gevrStereo=1, network, localSlot, playerSlot, calls, corner;
static int netIsActive(void) { return network; }
static int get_cur_playernum(void) { return playerSlot; }
static int netGetLocalSlot(void) { return localSlot; }
static StandTile tile={42};
static double nx,nz,minSide;
static int bondviewTryMoveToStan(struct coord3d *p, StandTile **out)
{
    calls++;
    /* Strict double geometry exposes float-quantized contact positions. A
     * second blocker must still reject the precision retry at real corners. */
    if (corner || p->f[0]*nx + p->f[2]*nz < minSide) return 0;
    *out=&tile;
    return 1;
}
/* INSERT_EDGE_MOVE */

int main(void)
{
    unsigned rescued=0, ordinary=0;
    const double directions[][2]={{.6,.8},{.595,-.804},{1,0},{0,1}};
    for (unsigned dir=0; dir<4; dir++) for (int side=-1; side<=1; side+=2)
    for (int reverse=-1; reverse<=1; reverse+=2) for (int i=0; i<200; i++) {
        const double len=hypot(directions[dir][0],directions[dir][1]);
        const double tx=directions[dir][0]/len, tz=directions[dir][1]/len;
        nx=-tz*side; nz=tx*side;
        struct coord3d start={{-30000.0f+i*.07f,175,17000.0f-i*.11f}};
        struct coord3d a={{(float)(start.f[0]-nx*30-tx*100),NAN,(float)(start.f[2]-nz*30-tz*100)}};
        struct coord3d b={{(float)(start.f[0]-nx*30+tx*100),NAN,(float)(start.f[2]-nz*30+tz*100)}};
        struct coord3d target={{(float)(start.f[0]+tx*reverse*5-nx*7),NAN,
            (float)(start.f[2]+tz*reverse*5-nz*7)}};
        /* Use the exact tangent of the float edge seen by production. */
        const double edgeLen=hypot((double)b.f[0]-a.f[0],(double)b.f[2]-a.f[2]);
        nx=-((double)b.f[2]-a.f[2])/edgeLen*side;
        nz=((double)b.f[0]-a.f[0])/edgeLen*side;
        minSide=start.f[0]*nx+start.f[2]*nz;
        player.field_488.collision_position=start;
        player.field_488.current_tile_ptr=NULL;
        calls=0; corner=0;
        assert(bondviewTryEdgeMovePlayerCollision(&target,&a,&b)==1);
        assert(player.field_488.current_tile_ptr == &tile);
        assert(player.field_488.collision_position.f[0]*nx + player.field_488.collision_position.f[2]*nz >= minSide);
        if (calls==2) rescued++; else { assert(calls==1); ordinary++; }
        const double away=(player.field_488.collision_position.f[0]-start.f[0])*nx+
            (player.field_488.collision_position.f[2]-start.f[2])*nz;
        assert(away < .025);
        /* A real second wall is never bypassed. */
        player.field_488.collision_position=start;
        calls=0; corner=1;
        assert(bondviewTryEdgeMovePlayerCollision(&target,&a,&b)==0 && calls==2);
        assert(!memcmp(&player.field_488.collision_position,&start,sizeof(start)));
        /* Remote players and virtual-screen play retain the original path. */
        corner=0; network=1; playerSlot=1; localSlot=0; calls=0;
        bondviewTryEdgeMovePlayerCollision(&target,&a,&b); assert(calls==1);
        network=0; playerSlot=0; g_gevrStereo=0; calls=0;
        player.field_488.collision_position=start;
        bondviewTryEdgeMovePlayerCollision(&target,&a,&b); assert(calls==1);
        g_gevrStereo=1;
    }
    assert(rescued>0 && ordinary>0);
    float start[3]={0,175,30}, target[3]={0,NAN,23};
    float a[3]={-100,NAN,0}, b[3]={100,NAN,0}, out[3];
    assert(!gevrCollisionSlideRetry(start,target,a,b,out)); /* head-on stop */
    target[0]=5;
    assert(gevrCollisionSlideRetry(start,target,a,b,out));
    assert(fabs(out[0]-5)<1e-5 && fabs(out[2]-30.01)<1e-5 && out[1]==175);
    assert(!gevrCollisionSlideRetry(start,target,a,a,out)); /* degenerate */
    start[2]=0; assert(!gevrCollisionSlideRetry(start,target,a,b,out)); /* ambiguous side */
    start[0]=NAN; assert(!gevrCollisionSlideRetry(start,target,a,b,out));
    printf("PASS: real edge-move path: %u rounding rescues, %u original successes, both directions/sides, axis/oblique/large coordinates, checked corners, remote/screen exclusion and head-on stops\n",rescued,ordinary);
}
