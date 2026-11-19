/*
 * The race's ghost and GoldenEye's active prop list (#263), production code
 * only: ghostPose and its helpers (gevr_ghost.c), the list functions and
 * propsTick (chrprop.c), pasted in by test_ghost_prop_list.py. The per-prop
 * ticks are stubs; the falling object's follows objTick's own rule.
 */
#include <ultra64.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "bondview.h"
#include "player.h"
#include "chr.h"
#include "gun.h"

#define NPROPS 6

PropRecord *g_ActivePropsTail;
PropRecord *g_ActivePropsHead;

static PropRecord s_props[NPROPS];
static s32 s_ticks[NPROPS];
static StandTile s_tile;

/* what ghostPose calls, outside the list */
void chrSetWeaponFlag4(ChrRecord *chr, GUNHAND hand) { (void)chr; (void)hand; }
void objFreePermanently(struct ObjectRecord *obj, bool freeprop) { (void)obj; (void)freeprop; }
PropRecord *chrGiveWeapon(ChrRecord *self, s32 propid, ITEM_IDS item, s32 flags)
{
    (void)self; (void)propid; (void)item; (void)flags;
    return NULL;
}
enum PROP getPropForHeldItem(ITEM_IDS item) { (void)item; return (enum PROP)-1; }
StandTile *stanFindGroundAtCyl(coord3d *pos, f32 radius, u8 *rooms, StandTile *prefer, f32 *groundy, s32 *incentre)
{
    (void)pos; (void)radius; (void)rooms; (void)prefer;
    *groundy = 0.0f;
    *incentre = TRUE;
    return &s_tile;
}
struct weapon_firing_animation_table *gevrPlayerBodyChooseAnim(struct player *pl, ChrRecord *chr, s32 crouchpos)
{
    (void)pl; (void)chr; (void)crouchpos;
    return NULL;
}
void getsuboffset(Model *model, coord3d *offset) { (void)model; offset->x = offset->y = offset->z = 0.0f; }
void setsuboffset(Model *model, coord3d *offset) { (void)model; (void)offset; }
void setsubroty(Model *model, f32 angle) { (void)model; (void)angle; }
void chrSetFiring(ChrRecord *self, s32 hand, s32 firing) { (void)self; (void)hand; (void)firing; }

/* what propsTick calls for each prop */
s32 chrTick(PropRecord *prop) { s_ticks[prop - s_props]++; return TICKOP_NONE; }
s32 playerTick(PropRecord *prop) { s_ticks[prop - s_props]++; return TICKOP_NONE; }
u8 explosionChrpropExplosionTick(PropRecord *prop) { s_ticks[prop - s_props]++; return TICKOP_NONE; }
u8 explosionChrpropSmokeTick(PropRecord *prop) { s_ticks[prop - s_props]++; return TICKOP_NONE; }
void propExecuteTickOperation(PropRecord *prop, TICKOP op) { (void)prop; (void)op; }
s32 get_cur_playernum(void) { return 0; }
s32 get_player_position_in_shuffled(s32 playernum) { (void)playernum; return 0; }
void handle_alarm_gas_timer_calldamage(void) {}
void loop_set_sound_effect_all_slots(void) {}
void propsDefragRoomProps(void) {}

/* objTick (propobj.c): an object with a projectile, falling, is ticked again
 * at the list's head, after everything else this frame (TICKOP_RETICK) */
s32 objTick(PropRecord *prop)
{
    ObjectRecord *obj = prop->obj;

    s_ticks[prop - s_props]++;
    if (obj->runtime_bitflags & RUNTIMEBITFLAG_ISRETICK)
    {
        obj->runtime_bitflags &= ~RUNTIMEBITFLAG_ISRETICK;
    }
    else if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
    {
        obj->runtime_bitflags |= RUNTIMEBITFLAG_ISRETICK;
        return TICKOP_RETICK;
    }
    return TICKOP_NONE;
}

/* INSERT_PRODUCTION_FUNCTIONS */

/* Every listed prop once from the tail, the head last, and back again from the head */
static s32 listIsWhole(s32 listed)
{
    PropRecord *p;
    s32 seen[NPROPS] = { 0 };
    s32 n = 0;

    for (p = g_ActivePropsTail; p != NULL; p = p->prev)
    {
        if (++n > listed || seen[p - s_props]++)
        {
            return FALSE;
        }
        if (p->prev == NULL && p != g_ActivePropsHead)
        {
            return FALSE;
        }
    }
    if (n != listed)
    {
        return FALSE;
    }
    n = 0;
    for (p = g_ActivePropsHead; p != NULL; p = p->next)
    {
        if (++n > listed)
        {
            return FALSE;
        }
    }
    return n == listed;
}

int main(void)
{
    static ChrRecord guard;
    static ChrRecord ghost;
    static ObjectRecord door;
    static ObjectRecord gun;
    static Model ghostmodel;
    struct gevrghostsample sample;
    PropRecord *player = &s_props[0];
    PropRecord *guardprop = &s_props[1];
    PropRecord *doorprop = &s_props[2];
    PropRecord *ghostprop = &s_props[3];
    PropRecord *gunprop = &s_props[4];
    s32 frame;
    s32 i;

    /* the level as setup lists it: Bond, a guard, a door */
    player->type = PROP_TYPE_VIEWER;
    guardprop->type = PROP_TYPE_CHR;
    guardprop->chr = &guard;
    guard.prop = guardprop;
    doorprop->type = PROP_TYPE_DOOR;
    doorprop->obj = &door;
    chrpropActivate(player);
    chrpropActivate(guardprop);
    chrpropActivate(doorprop);

    /* ghostBuild: the body is listed once, at the head */
    ghostprop->type = PROP_TYPE_CHR;
    ghostprop->chr = &ghost;
    ghostprop->rooms[0] = 0xff;
    ghost.prop = ghostprop;
    ghost.model = &ghostmodel;
    chrpropActivateThisFrame(ghostprop);
    s_race.chr = &ghost;
    s_race.held[0] = s_race.held[1] = -1;

    memset(&sample, 0, sizeof(sample));
    sample.x = 100.0f;
    sample.z = 200.0f;

    /* the guard dies: objDrop lists his gun, falling */
    gunprop->type = PROP_TYPE_WEAPON;
    gunprop->obj = &gun;
    gun.runtime_bitflags = RUNTIMEBITFLAG_HASPROJECTILE;
    chrpropActivate(gunprop);

    /* race frames: lvTick poses the ghost (gevrGhostTick), the render ticks the props */
    for (frame = 0; frame < 6; frame++)
    {
        memset(s_ticks, 0, sizeof(s_ticks));
        sample.x += 10.0f;
        ghostPose(&sample);
        propsTick();
        for (i = 0; i < 5; i++)
        {
            if (s_ticks[i] == 0)
            {
                fprintf(stderr, "frame %d: prop %d (type %d%s) was not ticked\n", frame, i, s_props[i].type,
                        i == 4 ? ", the falling gun" : "");
                return 3;
            }
        }
        if (s_ticks[4] != 2)
        {
            fprintf(stderr, "frame %d: the falling gun was ticked %d times, not twice (objTick's retick)\n", frame, s_ticks[4]);
            return 4;
        }
        if (!listIsWhole(5))
        {
            fprintf(stderr, "frame %d: the active prop list lost a prop or looped\n", frame);
            return 2;
        }
    }
    printf("ghost race: the falling gun ticked twice a frame for %d frames, every prop listed once\n", frame);
    return 0;
}
