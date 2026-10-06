#include "gevr_frame_timing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef int s32;
struct coord3d { float f[3]; };
static int g_gevrStereo = 1, network, localSlot, playerSlot;
static int netIsActive(void) { return network; }
static int netGetLocalSlot(void) { return localSlot; }
static int get_cur_playernum(void) { return playerSlot; }
/* INSERT_WRAPPER */

static int responses[9], calls[9], count;
static int attempt(int kind) { calls[count] = kind; return responses[count++]; }
static int bondviewTrySimpleMovePlayerCollision(struct coord3d *pos, struct coord3d *a, struct coord3d *b)
{
    (void)pos;
    const int result = attempt(GEVR_MOVE_SIMPLE);
    if (!result) { a->f[0]=0; a->f[2]=0; b->f[0]=0; b->f[2]=10; }
    return result;
}
static int bondviewTryFractionMovePlayerCollision(struct coord3d *p, struct coord3d *a, struct coord3d *b,
    struct coord3d *c, struct coord3d *d)
{
    (void)p; (void)a; (void)b;
    *c = *a; *d = *b;
    return attempt(GEVR_MOVE_FRACTION);
}
static int bondviewTryEdgeMovePlayerCollision(struct coord3d *p, struct coord3d *a, struct coord3d *b)
{ (void)p; (void)a; (void)b; return attempt(GEVR_MOVE_EDGE); }
static int bondviewTryEndHopPlayerCollision(struct coord3d *p, struct coord3d *a, struct coord3d *b)
{ (void)p; (void)a; (void)b; return attempt(GEVR_MOVE_END); }

#define DECLARE_LOCALS \
    struct coord3d next_pos={{0}}, collision1_pt0={{0}}, collision1_pt1={{0}}, \
        collision2_pt0={{0}}, collision2_pt1={{0}}, collision3_pt0={{0}}, collision3_pt1={{0}}; \
    int temp_v0_7
#define GEVR_MOVE_RESULT(kind, result, edge0, edge1) (result)
static void baseline(int allow_scoot) {
    DECLARE_LOCALS;
    /* INSERT_PATH */
}
#undef GEVR_MOVE_RESULT
#define GEVR_MOVE_RESULT(kind, result, edge0, edge1) gevrBondMoveResult(kind, result, edge0, edge1)
static void diagnostic(int allow_scoot) {
    DECLARE_LOCALS;
    /* INSERT_PATH */
}

int main(void)
{
    gevrFrameTimingEnable(1);
    for (int scoot=0; scoot<2; scoot++) for (int simple=0; simple<2; simple++) {
        for (int pattern=0; pattern<6561; pattern++) { /* every ternary fallback result */
            int digits=pattern;
            responses[0]=simple;
            for (int i=1; i<9; i++) { responses[i]=digits%3-1; digits/=3; }
            count=0; baseline(scoot);
            int expected[9], n=count; memcpy(expected,calls,sizeof(expected));
            count=0;
            gevrFrameTimingBegin(1000000000);
            gevrFrameTimingMoveBegin(scoot);
            diagnostic(scoot);
            assert(n == count && memcmp(expected,calls,n*sizeof(int)) == 0);
            unsigned tried=0, accepted=0;
            for (int i=0; i<n; i++) {
                tried |= 1u<<calls[i];
                if (responses[i]>0) accepted |= 1u<<calls[i];
            }
            const float zero[3]={0};
            gevrFrameTimingCollision(zero,zero,zero,0);
            gevrFrameTimingSubmit(1001000000,1);
            GevrFrameTimingWindow w; gevrFrameTimingTake(&w);
            assert(w.collisionCount == 1 && w.collisionTicks[0].attempted == tried);
            assert(w.collisionTicks[0].accepted == accepted);
        }
    }
    network=1; playerSlot=1; localSlot=0;
    count=0; responses[0]=1;
    gevrFrameTimingBegin(2000000000); diagnostic(1);
    gevrFrameTimingDuration(GEVR_TIME_FRESH,100);
    gevrFrameTimingSubmit(2001000000,1);
    GevrFrameTimingWindow w; gevrFrameTimingTake(&w);
    assert(!w.workFrame.moveAttempted); /* a remote player must not contaminate local evidence */
    puts("PASS: collision fallback calls, results and short-circuit order unchanged across 26244 scenarios; remote guard");
}
