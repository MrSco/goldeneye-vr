/* Production playerTick cinematic branch, camera advance and intro finish.
 * Only animation, renderer and room services are fixtures. */
#include <ultra64.h>
#include <bondtypes.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define LOG_NOTE 1
#define S7F081478_FACTOR_2 0.04549998f
/* INSERT_COLLISION_TYPE */
struct player {
    PropRecord *prop;
    Model *bodyModel;
    struct collision434 field_488, previous_collision_info;
    coord3d bondprevpos, field_3B8;
    rect4f collision_bounds;
    f32 field_3C4, field_3C8, field_3CC, vv_theta;
    s32 cameramode, field_AC;
};
static struct player players[4], *g_CurrentPlayer, *g_playerPointers[4];
static PropRecord props[4];
static ChrRecord chrs[4];
static Model models[4];
static StandTile tiles[4];
static coord3d modelpos[4];
static s32 coop = 1, count = 4, slot, g_CameraMode, g_CameraAfterCinema;
static s32 camera_mode, g_bondviewForceDisarm, g_gevrExtraPass;
static u32 g_gevrTeleportEpoch;
static int ticks, rooms, teleports, removals;
static s32 g_PlayerIsInTank;
static s32 gevrCoopActive(void) { return coop; }
static s32 getPlayerCount(void) { return count; }
static s32 get_cur_playernum(void) { return slot; }
static s32 getPlayerPointerIndex(PropRecord *p) { return (s32)(p - props); }
static s32 gevrPlayerModelTickOwner(void) { return !g_gevrExtraPass; }
static s32 get_debug_render_raster(void) { return 2; }
static void currentPlayerSetCameraMode(s32 mode) { g_CurrentPlayer->cameramode = mode; }
static void sysLogPrintf(s32 level, const char *fmt, ...) {}
static void bondviewUpdatePlayerRoom(struct player *p) { rooms++; }
static void gevrNotifyTeleport(void) { teleports++; }
static int gevrRagdollNeedsPose(struct ChrRecord *chr) { return 0; } /* ragdolls off */
static f32 bondviewGetPlayerYawRadians(void) { return (360 - g_CurrentPlayer->vv_theta) * 6.2831853f / 360; }
static void setsuboffset(Model *m, coord3d *p) { modelpos[m - models] = *p; }
static void setsubroty(Model *m, f32 yaw) {}
static void bondviewGetTankCollisionBounds(rect4f *r, coord3d *p, f32 yaw) { assert(0); }
static f32 g_TankOrientationAngle;
static void chrpropCleanupForRemoval(PropRecord *p) { removals++; }
static void currentPlayerStartChrFade(f32 time, f32 frac) {}
static void bondviewResetIntroCameraMessageDialogs(void) {}
static void bondviewSetCameraMode(s32 mode) { g_CameraMode = mode; }
static s32 chrTick(PropRecord *p)
{
    ticks++;
    /* An intro frame's root motion moves the visual body into scenery and
     * can lose its floor tile. The gameplay spawn must remain usable. */
    p->pos.x += 1000;
    p->pos.z -= 500;
    p->stan = NULL;
    modelpos[p - props] = p->pos;
    return 23;
}
static Mtxf *currentPlayerGetViewToWorldMtxf(void) { static Mtxf m; return &m; }
static void matrix_4x4_multiply_homogeneous(Mtxf *a, Mtxf *b, Mtxf *c) { memset(c, 0, sizeof(*c)); }

/* INSERT_BOUNDS */
/* INSERT_REMOVE */
/* INSERT_HELPERS */
/* INSERT_ADVANCE */

static s32 cinematic_tick(PropRecord *prop)
{
    s32 index, ret;
    ChrRecord *chr;
    f32 mtx[16];
    /* INSERT_TICK */
    return -1;
}
static void without_swirl(void)
{
    /* Production fallback from bondviewSetCameraMode(CAMERAMODE_SWIRL). */
    if (0) {}
    /* INSERT_NO_SWIRL */
}
static void select_slot(int s)
{
    slot = s;
    g_CurrentPlayer = &players[s];
}
static void start(int s, int reload)
{
    select_slot(s);
    memset(g_CurrentPlayer, 0, sizeof(*g_CurrentPlayer));
    memset(&props[s], 0, sizeof(props[s]));
    memset(&chrs[s], 0, sizeof(chrs[s]));
    g_playerPointers[s] = g_CurrentPlayer;
    g_CurrentPlayer->prop = &props[s];
    g_CurrentPlayer->bodyModel = &models[s];
    g_CurrentPlayer->cameramode = 1;
    g_CurrentPlayer->vv_theta = 45;
    props[s].chr = &chrs[s];
    props[s].pos = (coord3d){{70.0f*s + reload*10, 170, reload*20.0f}};
    props[s].stan = &tiles[s];
    chrs[s].model = &models[s];
    chrs[s].actiontype = ACT_BONDINTRO;
    g_CurrentPlayer->field_488.collision_position = props[s].pos;
    g_CurrentPlayer->field_488.current_tile_ptr = props[s].stan;
    g_CurrentPlayer->field_488.collision_radius = 30;
    g_CurrentPlayer->field_488.theta_transform.x = 0.7f;
    modelpos[s] = props[s].pos;
}
static void equal_pos(coord3d a, coord3d b)
{
    assert(a.x == b.x && a.y == b.y && a.z == b.z);
}
static void finish_check(coord3d expected)
{
    assert(g_CameraMode == CAMERAMODE_FP);
    assert(g_CurrentPlayer->cameramode == 0);
    equal_pos(props[slot].pos, expected);
    equal_pos(modelpos[slot], expected);
    equal_pos(g_CurrentPlayer->field_488.pos, expected);
    equal_pos(g_CurrentPlayer->field_488.pos3, expected);
    equal_pos(g_CurrentPlayer->bondprevpos, expected);
    equal_pos(g_CurrentPlayer->previous_collision_info.collision_position, expected);
    assert(props[slot].stan == &tiles[slot]);
    assert(g_CurrentPlayer->field_488.current_tile_ptr_for_portals == &tiles[slot]);
    assert(g_CurrentPlayer->collision_bounds.f[0] == expected.x + 30);
    assert(g_CurrentPlayer->field_488.theta_transform.x == 0.7f);
    assert(chrs[slot].actiontype == ACT_BONDMULTI);
    assert(chrs[slot].chrflags & CHRFLAG_INIT);
    assert(g_CurrentPlayer->bodyModel == &models[slot] && props[slot].chr == &chrs[slot]);
}
int main(void)
{
    /* Every owner slot, normal completion / skipped swirl, extra rendering
     * passes, and repeated stage loads with different positions. */
    for (int reload = 0; reload < 3; reload++)
    for (int s = 0; s < 4; s++)
    {
        start(s, reload);
        coord3d expected = props[s].pos;
        const int modes[] = {CAMERAMODE_INTRO, CAMERAMODE_FADESWIRL, CAMERAMODE_SWIRL};
        for (int i = 0; i < 3; i++)
        {
            g_CameraMode = modes[i];
            g_gevrExtraPass = i == 1;
            assert(cinematic_tick(&props[s]) == 23);
            equal_pos(g_CurrentPlayer->field_488.collision_position, expected);
            assert(g_CurrentPlayer->field_488.current_tile_ptr == &tiles[s]);
        }
        g_gevrExtraPass = 0;
        bondviewAdvanceCameraMode();
        finish_check(expected);
    }
    /* Late-join / owner corrections received during an intro must survive. */
    start(2, 0);
    g_CameraMode = CAMERAMODE_SWIRL;
    assert(cinematic_tick(&props[2]) == 23);
    g_CurrentPlayer->field_488.collision_position = (coord3d){{900, 170, 800}};
    coord3d joined = g_CurrentPlayer->field_488.collision_position;
    assert(cinematic_tick(&props[2]) == 23);
    bondviewAdvanceCameraMode();
    finish_check(joined);
    /* Missions without a swirl take the same handoff. */
    start(1, 2);
    coord3d expected = props[1].pos;
    g_CameraMode = CAMERAMODE_SWIRL;
    without_swirl();
    finish_check(expected);
    /* Solo and scripted / ending cinematics still follow the actor. */
    for (int control = 0; control < 3; control++)
    {
        start(0, 0);
        coop = control != 0;
        g_CameraMode = control == 0 ? CAMERAMODE_SWIRL :
            control == 1 ? CAMERAMODE_POSEND : CAMERAMODE_DEATH_CAM_SP;
        assert(cinematic_tick(&props[0]) == 23);
        equal_pos(g_CurrentPlayer->field_488.collision_position, props[0].pos);
        assert(g_CurrentPlayer->field_488.current_tile_ptr == NULL);
    }
    coop = 0; count = 1;
    start(0, 0);
    g_CameraMode = CAMERAMODE_SWIRL;
    bondviewAdvanceCameraMode();
    assert(removals == 1 && !g_CurrentPlayer->bodyModel && !props[0].chr);
    assert(teleports == 14);
    puts("PASS: co-op intro preserves all four gameplay starts through animation, skips, extra passes, three loads, late join and no-swirl exit; solo/scripted/death cinematics unchanged");
    return 0;
}
