/* Endless Archives waves, using the game's ordinary armed guard AI. */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include <math.h>
#include <string.h>
#include "chr.h"
#include "chraction.h"
#include "chrobjdata.h"
#include "deb_loadallmodels.h"
#include "loadobjectmodel.h"
#include "objecthandler.h"
#include "player.h"
#include "bondinv.h"
#include "gun.h"
#include "bondview.h"
#include "bondhead.h"
#include "stan.h"
#include "net_game.h"
#include "net_coop.h"
#include "net_horde.h"
#include "system.h"
#include "gevr_ally.h"
#include "propobj.h"
#include "lv.h"

extern s32 netGetLocalSlot(void);
extern int netMpPlayerCount(int fallback);
extern s32 chrGetNumFree(void);
extern void chrStopFiring(ChrRecord *chr);
extern waypoint *chrlvStanPathRelated(coord3d *pos, StandTile *stan);
extern s32 netIsHost(void);
extern unsigned netGetRandomSeed(void);
extern AIRecord *ailistFindById(s32 id);
extern void gevrHudmsgBottomLive(char *mess);
extern ObjectRecord *create_new_item_instance_of_model(PROP modelnum, s32 weaponid);
extern void setupUpdateObjectRoomPosition(ObjectRecord *obj);
extern void chrpropActivate(PropRecord *prop);
extern void chrpropEnable(PropRecord *prop);
extern void chrpropDisable(PropRecord *prop);
extern void modelSetScale(Model *model, f32 scale);
extern void matrix_4x4_set_position_and_rotation_around_y(f32 *position, f32 angle, Mtxf *matrix);
extern void matrix_scalar_multiply(f32 scalar, f32 *matrix);
extern PropRecord *chrSpawnAtCoord(s32 body,s32 head,coord3d *pos,StandTile *stan,f32 angle,AIRecord *ai,s32 flags);
extern s32 check_cur_player_ammo_amount_in_inventory(AMMOTYPE ammotype);
extern void gunRequestHandWeaponChange(enum GUNHAND hand, s32 nextWeapon, s32 cycleDirection);
extern void doorUpdateBbox(DoorRecord *door);
extern void doorActivatePortal(DoorRecord *door);
extern void door7F053B10(DoorRecord *door);
extern void netSendHostDoorState(ObjectRecord *door, s32 state);
extern bool doorIsPadlockFree(DoorRecord *door);
extern unsigned netHordeStartWave(void);

#define HORDE_INTERACT_RANGE 220.0f
/* a station's pad at least this far from a door's, past the reach of B at the door */
#define HORDE_STATION_DOOR_CLEAR 320.0f

typedef struct {
    s32 item, ammo;
    const char *name;
} HordeWeaponDef;

static const HordeWeaponDef s_box_pool[HORDE_BOX_POOL_SIZE] = {
    { ITEM_RUGER,          AMMO_MAGNUM, "COUGAR MAGNUM" },
    { ITEM_UZI,            AMMO_9MM, "ZMG 9MM" },
    { ITEM_SPECTRE,        AMMO_9MM, "PHANTOM" },
    { ITEM_FNP90,          AMMO_9MM, "RC-P90" },
    { ITEM_AUTOSHOT,       AMMO_SHOTGUN, "AUTO SHOTGUN" },
    { ITEM_SNIPERRIFLE,    AMMO_RIFLE, "SNIPER RIFLE" },
    { ITEM_SILVERWPPK,     AMMO_9MM, "SILVER PP7" },
    { ITEM_GOLDWPPK,       AMMO_9MM, "GOLD PP7" },
    { ITEM_LASER,          AMMO_NONE, "MOONRAKER LASER" },
    { ITEM_GOLDENGUN,      AMMO_GGUN, "GOLDEN GUN" },
    { ITEM_GRENADELAUNCH,  AMMO_GRENADEROUND, "GRENADE LAUNCHER" },
    { ITEM_ROCKETLAUNCH,   AMMO_ROCKETS, "ROCKET LAUNCHER" },
};

static const HordeWeaponDef s_wall_items[HORDE_WALL_COUNT] = {
    { ITEM_SKORPION, AMMO_9MM_2, "KLOBB" },
    { ITEM_SHOTGUN,  AMMO_SHOTGUN, "SHOTGUN" },
    { ITEM_AK47,     AMMO_RIFLE, "KF7 SOVIET" },
    { ITEM_MP5K,     AMMO_9MM, "D5K DEUTSCHE" },
    { ITEM_M16,      AMMO_RIFLE, "AR33 ASSAULT RIFLE" },
    { ITEM_TOKEN,    AMMO_NONE, "BODY ARMOR" },
};

/* Bodies by enemy kind (user, 2026-10-17): Janus marines in armour, Janus
 * special forces, the story characters as bosses, Oddjob for the swarm. */
static const s32 s_boss_bodies[]={BODY_Jaws,BODY_Xenia,BODY_Oddjob,BODY_Trevelyan_Janus,BODY_Ourumov};
#define HORDE_SWARM_SCALE 0.6f
/* The Moonraker Laser's pool in Horde (user: "Laser shouldn't have unlimited
 * ammo"): a magazine gun on the watch laser's otherwise unused ammo type. */
#define HORDE_LASER_AMMO AMMO_WATCH_LASER
#define HORDE_LASER_MAG 20
#define HORDE_LASER_CRATE 10

static HordeState s_state;
static unsigned s_id[HORDE_GUARD_SLOTS];
static s32 s_counted[HORDE_GUARD_SLOTS], s_dead_ticks[HORDE_GUARD_SLOTS], s_dropped[HORDE_GUARD_SLOTS];
static s32 s_last_attacker[HORDE_GUARD_SLOTS], s_last_hitpart[HORDE_GUARD_SLOTS];
static coord3d s_last_pos[HORDE_GUARD_SLOTS];
static s32 s_stall_ticks[HORDE_GUARD_SLOTS], s_door_ticks[HORDE_GUARD_SLOTS];
static s32 s_pads[128], s_pad_count, s_body;
static s32 s_box_pads[HORDE_BOX_SPOTS], s_wall_pads[HORDE_WALL_COUNT];
static uint32_t s_applied_grant[4];
static unsigned s_shown_phase, s_shown_wave, s_shown_secs;
static uint32_t s_applied_ammo[4], s_ammo_marker_id[HORDE_AMMO_DROPS];
static ObjectRecord *s_ammo_marker[HORDE_AMMO_DROPS];
static s32 s_pickup_ticks;
static f32 s_speed[HORDE_GUARD_SLOTS];   /* a guard's run-speed factor (host) */
static s32 s_doors_opened;
#define HORDE_HELD_DOORS 96
static DoorRecord *s_held_doors[HORDE_HELD_DOORS];
static s32 s_held_count, s_hold_ticks;
/* The laser's own stats, restored outside Horde (it gets a finite pool there). */
static s32 s_laser_saved, s_laser_ammo, s_laser_mag;
static char s_prompt[100];
static s32 s_prompt_ticks;
/* The stations in the world, so players can find them: an uncollectable
 * model on each pad, placed on every headset (the pads come from the setup,
 * the box's spot from the snapshot). The box is an ammo crate that follows
 * its relocations; each wall buy is its gun, larger and turning; armour a vest. */
static ObjectRecord *s_box_marker, *s_wall_marker[HORDE_WALL_COUNT];
static s32 s_markers_placed, s_marker_spot;
static f32 s_marker_yaw;
static const s32 s_wall_models[HORDE_WALL_COUNT] = {
    PROP_CHRSKORPION, PROP_CHRSHOTGUN, PROP_CHRKALASH, PROP_CHRMP5K, PROP_CHRM16, PROP_BODYARMOUR };
static struct {
    SetupIntroSpawn spawn;
    struct SetupIntroItem item;
    struct SetupIntroAmmo ammo;
    SetupIntroEmpty end;
} s_intro;

const HordeState *gevrHordeState(void) { return &s_state; }

/* give_cur_player_ammo sets the pool; Horde's grants add to it, as the
 * game's own pickups do (propobj.c). */
static void hordeGiveAmmo(s32 ammo, s32 amount)
{
    if (ammo<=AMMO_NONE || ammo>=AMMOTYPE_MAX || amount<=0) return;
    give_cur_player_ammo(ammo,check_cur_player_ammo_amount_in_inventory(ammo)+amount);
}

static const char *hordeGunName(unsigned item)
{
    if (item==ITEM_WPPK) return "PP7";
    for (unsigned i=0;i<HORDE_BOX_POOL_SIZE;i++) if ((unsigned)s_box_pool[i].item==item) return s_box_pool[i].name;
    for (unsigned i=0;i<5;i++) if ((unsigned)s_wall_items[i].item==item) return s_wall_items[i].name;
    return "GUN";
}

/* The gun a new one would replace: the one in hand if it is one of the two. */
static unsigned hordeReplaceChoice(s32 me)
{
    if (me<0 || me>=4 || !s_state.guns[me][HORDE_GUN_SLOTS-1]) return 0;
    s32 prev=get_cur_playernum();set_cur_player(me);
    unsigned held=(unsigned)getCurrentPlayerWeaponId(GUNRIGHT);
    set_cur_player(prev);
    return hordeGunOwned(&s_state,(unsigned)me,held)?held:s_state.guns[me][0];
}
void gevrHordeText(char *text,unsigned size)
{
    s32 me=netGetLocalSlot();
    hordePlayerStatus(&s_state,(me>=0 && me<4)?(unsigned)me:0,text,size);
}
int gevrHordeDefeated(void) { return netHordeActive() && s_state.phase==HORDE_DEFEAT; }
unsigned gevrHordeGuardId(const ChrRecord *chr)
{
    if (!netHordeActive() || !chr || !g_ChrSlots || chr<g_ChrSlots || chr>=g_ChrSlots+g_NumChrSlots) return 0;
    s32 slot=chr-g_ChrSlots;
    return slot<HORDE_GUARD_SLOTS?s_id[slot]:0;
}
void gevrHordeSetGuardId(ChrRecord *chr,unsigned id)
{
    if (!netHordeActive() || !chr || !g_ChrSlots || chr<g_ChrSlots || chr>=g_ChrSlots+g_NumChrSlots) return;
    /* Companion generation is networked but never belongs to a wave. */
    if (id == GEVR_ALLY_HORDE_ID) id = 0;
    s32 slot=chr-g_ChrSlots;
    if (slot<HORDE_GUARD_SLOTS) {
        s_id[slot]=id;s_counted[slot]=0;s_dropped[slot]=0;s_dead_ticks[slot]=0;s_stall_ticks[slot]=0;s_door_ticks[slot]=0;
        s_last_attacker[slot]=-1;s_last_hitpart[slot]=0;
        if (chr->prop) s_last_pos[slot]=chr->prop->pos;
    }
}

/* Record the ROM's existing guard pads before suppressing mission entities.
 * The most common guard body avoids spawning Archives' story characters. */
void gevrHordePrepareSetup(void)
{
    s_pad_count=0; s_body=0;
    if (!netHordeActive()) return;
    s32 pads=0, bodies[128]={0}, most=0;
    if (g_CurrentSetup.pads) while (g_CurrentSetup.pads[pads].plink) pads++;
    for (PropDefHeaderRecord *p=g_CurrentSetup.propDefs;p && p->type!=PROPDEF_END;
         p=(PropDefHeaderRecord *)((u32 *)p+sizepropdef(p))) {
        if (p->type==PROPDEF_GUARD) {
            GuardRecord *guard=(GuardRecord *)p;
            s32 pad=guard->PadID;
            if (pad>=0 && pad<pads && g_CurrentSetup.pads[pad].stan && s_pad_count<128) {
                s32 duplicate=FALSE;
                for (s32 i=0;i<s_pad_count;i++) if (s_pads[i]==pad) duplicate=TRUE;
                if (!duplicate) s_pads[s_pad_count++]=pad;
            }
            if (guard->BodyID<128 && ++bodies[guard->BodyID]>most) {
                s_body=guard->BodyID; most=bodies[s_body];
            }
        }
    }
    /* Preserve the campaign's starting pad, but none of its inventory/cameras. */
    s32 start=s_pad_count?s_pads[0]:0;
    const unsigned sizes[INTROTYPE_MAX]={sizeof(SetupIntroSpawn),sizeof(struct SetupIntroItem),
        sizeof(struct SetupIntroAmmo),sizeof(struct SetupIntroSwirl),sizeof(struct SetupIntroAnim),
        sizeof(struct SetupIntroCuff),sizeof(SetupIntroCamera),sizeof(struct SetupIntroWatch),
        sizeof(struct SetupIntroCredits),sizeof(SetupIntroEmpty)};
    SetupIntroEmpty *intro=(SetupIntroEmpty *)g_CurrentSetup.intro;
    for (s32 i=0;intro && i<128 && intro->type>=0 && intro->type<INTROTYPE_END;i++) {
        if (intro->type==INTROTYPE_SPAWN) {
            s32 index=((SetupIntroSpawn *)intro)->index;
            if (index>=0 && index<pads) {start=index;break;}
        }
        intro=(SetupIntroEmpty *)((u8 *)intro+sizes[intro->type]);
    }
    /* Distribute Mystery Box relocation spots and Wall Buy stations across
     * non-spawn pads so rooms around the arena host distinct stations. A
     * station by a door took the B/grip meant for the door (user, 2026-10-17:
     * Archives' body armour in front of a door, the KF7 over the
     * interrogation room's), so pads near any door are left to the guards. */
    coord3d doors[64];
    s32 ndoors=0;
    for (PropDefHeaderRecord *p=g_CurrentSetup.propDefs;p && p->type!=PROPDEF_END && ndoors<64;
         p=(PropDefHeaderRecord *)((u32 *)p+sizepropdef(p))) {
        s32 pad=p->type==PROPDEF_DOOR?((ObjectRecord *)p)->pad:-1;
        if (pad<0) continue;
        if (isNotBoundPad(pad)) { if (pad<pads) doors[ndoors++]=g_CurrentSetup.pads[pad].pos; }
        else doors[ndoors++]=((PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(pad)])->pos;
    }
    s32 open[128], nopen=0;
    for (s32 i=1;i<s_pad_count;i++) {
        coord3d *at=&g_CurrentSetup.pads[s_pads[i]].pos;
        s32 clear=TRUE;
        for (s32 d=0;d<ndoors && clear;d++) {
            f32 dx=at->x-doors[d].x, dy=at->y-doors[d].y, dz=at->z-doors[d].z;
            if (dx*dx+dz*dz<HORDE_STATION_DOOR_CLEAR*HORDE_STATION_DOOR_CLEAR && dy*dy<250.0f*250.0f) clear=FALSE;
        }
        if (clear) open[nopen++]=s_pads[i];
    }
    if (nopen<(s32)(HORDE_BOX_SPOTS+HORDE_WALL_COUNT)) {   /* too few: any pad but the start */
        nopen=0;
        for (s32 i=s_pad_count>1?1:0;i<s_pad_count;i++) open[nopen++]=s_pads[i];
    }
    s32 pool=nopen>0?nopen:1;
    for (unsigned b=0;b<HORDE_BOX_SPOTS;b++)
        s_box_pads[b]=nopen?open[(b*pool)/HORDE_BOX_SPOTS]:0;
    for (unsigned w=0;w<HORDE_WALL_COUNT;w++) {
        s32 idx=((w+1)*pool)/(HORDE_WALL_COUNT+1);
        if (nopen>1 && open[idx%pool]==s_box_pads[0]) idx++;
        s_wall_pads[w]=nopen?open[idx%pool]:0;
    }
    memset(&s_intro,0,sizeof(s_intro));
    s_intro.spawn.type=INTROTYPE_SPAWN;s_intro.spawn.index=start;
    s_intro.item.type=INTROTYPE_ITEM;s_intro.item.item_right=ITEM_WPPK;
    s_intro.ammo.type=INTROTYPE_AMMO;s_intro.ammo.ammo_type=AMMO_9MM;s_intro.ammo.ammo_amount=42;
    s_intro.end.type=INTROTYPE_END;
    g_CurrentSetup.intro=(void *)&s_intro;
}

int gevrHordeSkipProp(int type)
{
    if (!netHordeActive()) return FALSE;
    switch (type) {
    case PROPDEF_GUARD:case PROPDEF_GUARD_ATTRIBUTE:case PROPDEF_KEY:case PROPDEF_HAT:
    case PROPDEF_COLLECTABLE:case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:case PROPDEF_CAMERAPOS:
    case PROPDEF_OBJECTIVE_START:case PROPDEF_OBJECTIVE_ENTER_ROOM:
    case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM:case PROPDEF_OBJECTIVE_PHOTOGRAPH:
    case PROPDEF_CCTV:case PROPDEF_AUTOGUN:case PROPDEF_VEHICHLE:case PROPDEF_AIRCRAFT:
    case PROPDEF_TANK:case PROPDEF_GAS_RELEASING:return TRUE;
    default:return FALSE;
    }
}

/* Horde's laser stats while in Horde, the game's own anywhere else. */
static void hordeLaserStats(s32 horde)
{
    WeaponStats *laser=get_ptr_item_statistics(ITEM_LASER);
    if (!laser) return;
    if (!s_laser_saved) {
        s_laser_saved=TRUE;s_laser_ammo=laser->AmmoType;s_laser_mag=laser->MagSize;
    }
    laser->AmmoType=horde?HORDE_LASER_AMMO:s_laser_ammo;
    laser->MagSize=horde?HORDE_LASER_MAG:s_laser_mag;
}

void gevrHordeStageLoaded(void)
{
    hordeLaserStats(netHordeActive());
    hordeInitAt(&s_state,netGetRandomSeed(),netHordeActive()?netHordeStartWave():1);
    for (unsigned p=0;p<4;p++) s_state.guns[p][0]=ITEM_WPPK;   /* the intro's PP7 */
    memset(s_speed,0,sizeof(s_speed));
    s_doors_opened=FALSE;s_held_count=0;s_hold_ticks=0;
    memset(s_id,0,sizeof(s_id));memset(s_counted,0,sizeof(s_counted));
    memset(s_dead_ticks,0,sizeof(s_dead_ticks));
    memset(s_last_attacker,0xff,sizeof(s_last_attacker));   /* -1: no player hit it */
    memset(s_last_hitpart,0,sizeof(s_last_hitpart));
    memset(s_applied_grant,0,sizeof(s_applied_grant));
    memset(s_dropped,0,sizeof(s_dropped));memset(s_applied_ammo,0,sizeof(s_applied_ammo));
    memset(s_ammo_marker,0,sizeof(s_ammo_marker));memset(s_ammo_marker_id,0,sizeof(s_ammo_marker_id));
    s_pickup_ticks=0;
    s_shown_phase=~0u;s_shown_wave=~0u;s_shown_secs=~0u;
    s_prompt[0]=0;s_prompt_ticks=0;
    s_box_marker=NULL;memset(s_wall_marker,0,sizeof(s_wall_marker));   /* the stage's objects went with it */
    s_markers_placed=FALSE;s_marker_spot=-1;s_marker_yaw=0.0f;
}
void gevrHordeApplyState(const HordeState *state)
{
    if (!netHordeActive() || (s32)(state->revision-s_state.revision)<0) return;
    s_state=*state;
}
void gevrHordeDefeat(void)
{
    if (!netHordeActive() || !netIsHost() || s_state.phase==HORDE_DEFEAT) return;
    s_state.phase=HORDE_DEFEAT;s_state.revision++;
    for (s32 i=0;i<g_NumChrSlots && i<HORDE_GUARD_SLOTS;i++) if(s_id[i] && g_ChrSlots[i].model)
        chrStopFiring(&g_ChrSlots[i]);
    gevrAllyStop(); /* defeat pauses action ticks; clear companion fire now */
    netHordeSend(-1);
}
void gevrHordeRevived(void)
{
    if (!netHordeActive() || !netIsHost() || s_state.phase==HORDE_DEFEAT) return;
    if (s_state.revives<UINT32_MAX) s_state.revives++;
    s_state.revision++;netHordeSend(-1);
}
void gevrHordeBecameHost(void)
{
    if (!netHordeActive()) return;
    unsigned expected=s_state.alive, next=s_state.next_id;
    s_state.alive=0;
    for (s32 i=0;i<g_NumChrSlots && i<HORDE_GUARD_SLOTS;i++) {
        ChrRecord *chr=&g_ChrSlots[i];
        s_counted[i]=!chr->model || chr->actiontype==ACT_DIE || chr->actiontype==ACT_DEAD;
        s_dropped[i]=s_counted[i];
        if (s_id[i] && !s_counted[i]) s_state.alive++;
        /* State may lag the last reliable spawn; never reuse an adopted ID. */
        if (s_id[i]>=s_state.next_id) s_state.next_id=s_id[i]+1;
    }
    /* A reliable spawn may arrive just before its director update. Account
     * for it once, and replace missing actors rather than skipping enemies. */
    unsigned extra=s_state.next_id>=next?s_state.next_id-next:0;
    if (extra>s_state.remaining) extra=s_state.remaining;
    s_state.remaining-=extra;
    if (expected+extra>s_state.alive) {
        unsigned missing=expected+extra-s_state.alive;
        s_state.remaining=s_state.remaining+missing>HORDE_MAX_QUOTA?HORDE_MAX_QUOTA:s_state.remaining+missing;
    }
    /* Another client may hold newer revisions from the old host than this one
     * did; jump past them so the snapshots are not dropped as stale. */
    s_state.revision+=0x10000;netHordeSend(-1);
}

void gevrHordeNotifyGuardHit(ChrRecord *chr, int attacker, int hitpart)
{
    if (!netHordeActive() || !netIsHost() || s_state.phase==HORDE_DEFEAT || !chr || !g_ChrSlots) return;
    s32 slot=chr-g_ChrSlots;
    if (slot<0 || slot>=g_NumChrSlots || slot>=HORDE_GUARD_SLOTS || !s_id[slot] || s_counted[slot]) return;
    /* Only a known player's hit pays; anything else credits nobody (not slot 0, the host). */
    unsigned who=(attacker>=0 && attacker<4)?(unsigned)attacker:4;
    s_last_attacker[slot]=who<4?(s32)who:-1;
    s_last_hitpart[slot]=hitpart;
    s32 lethal=(chr->maxdamage>0.0f && chr->damage>=chr->maxdamage)
        || chr->actiontype==ACT_DIE || chr->actiontype==ACT_DEAD;
    if (lethal) {
        s_counted[slot]=TRUE;
        hordeEnemyKilled(&s_state,TRUE,who,hitpart==HIT_HEAD);
        netHordeSend(-1);
    } else if (who<4) {
        hordeRewardHit(&s_state,who);
    }
}

static s32 hordeNearPadAt(const f32 pos[3], s32 pad_id, f32 radius)
{
    if (!g_CurrentSetup.pads || pad_id<0) return FALSE;
    coord3d *at=&g_CurrentSetup.pads[pad_id].pos;
    f32 dx=pos[0]-at->x, dy=pos[1]-at->y, dz=pos[2]-at->z;
    return dx*dx+dz*dz<=radius*radius && dy*dy<=150.0f*150.0f;
}

static s32 hordeNearPad(s32 slot, s32 pad_id, f32 radius)
{
    if (slot<0 || slot>=4 || !g_playerPointers[slot] || !g_playerPointers[slot]->prop) return FALSE;
    if (gevrCoopDowned(slot)) return FALSE;
    return hordeNearPadAt(g_playerPointers[slot]->prop->pos.f,pad_id,radius);
}

int gevrHordeHandleAction(int slot, unsigned action, unsigned item)
{
    if (!netHordeRunning() || !netIsHost() || slot<0 || slot>=4 || gevrCoopDowned(slot)) return 0;
    if (netPlayerIsSpectator(slot) || !netLobbySlotConnected(slot)) return 0;
    if (action==HORDE_ACT_AMMO_PICKUP) {
        PropRecord *player=g_playerPointers[slot]?g_playerPointers[slot]->prop:NULL;
        if (!player) return 0;
        for (unsigned i=0;i<HORDE_AMMO_DROPS;i++) {
            HordeAmmoDrop *drop=&s_state.ammo[i];
            if (!item || drop->id!=item) continue;
            f32 dx=player->pos.x-drop->pos[0],dy=player->pos.y-drop->pos[1],dz=player->pos.z-drop->pos[2];
            StandTile *tile=player->stan;
            coord3d probe={.x=drop->pos[0],.y=drop->pos[1]+16,.z=drop->pos[2]};
            f32 floor=probe.y;
            StandTile *drop_tile=stanFindTileBelowPos(&probe,NULL,&floor);
            if (dx*dx+dz*dz>110.0f*110.0f || dy < -30.0f || dy>220.0f || !tile
                || !stanTestLineUnobstructed(&tile,player->pos.x,player->pos.z,drop->pos[0],drop->pos[2],
                    CDTYPE_OBJS|CDTYPE_DOORS|CDTYPE_PATHBLOCKER,40,40,0,1) || !drop_tile || tile!=drop_tile) return 0;
            drop->id=0;drop->ticks=0;
            s_state.ammo_grants[slot]++;s_state.revision++;
            return 1;
        }
        return 0;
    }
    /* item: a wall's index in the low byte; the gun to give up in the next */
    unsigned replace=(item>>8)&255;
    item&=255;
    if (action==HORDE_ACT_BOX) {
        s32 pad=s_box_pads[s_state.box_spot%HORDE_BOX_SPOTS];
        if (!hordeNearPad(slot,pad,HORDE_INTERACT_RANGE*1.25f)) return 0;
        return hordeUseBox(&s_state,(unsigned)slot,(unsigned)s_box_pool[s_state.box_weapon%HORDE_BOX_POOL_SIZE].item,replace)!=0;
    }
    if ((action==HORDE_ACT_WALL_BUY || action==HORDE_ACT_WALL_AMMO) && item<HORDE_WALL_COUNT) {
        if (!hordeNearPad(slot,s_wall_pads[item],HORDE_INTERACT_RANGE*1.25f)) return 0;
        return hordeBuyWall(&s_state,(unsigned)slot,item,action==HORDE_ACT_WALL_AMMO,
            item<5?(unsigned)s_wall_items[item].item:0,replace);
    }
    return 0;
}

/* B: the player standing at a station. The grip: the hand reaching it, so a
 * grip meant for a holster, a pickup or a door beside one buys nothing. */
#define HORDE_HAND_RANGE 110.0f
static int hordeUse(const f32 *hand)
{
    if (!netHordeRunning()) return 0;
    s32 me=netGetLocalSlot(), prev=get_cur_playernum();
    if (me<0 || me>=4 || !g_playerPointers[me] || !g_playerPointers[me]->prop
        || netPlayerIsSpectator(me) || gevrCoopDowned(me)) return 0;
    const f32 *at=hand?hand:g_playerPointers[me]->prop->pos.f;
    const f32 range=hand?HORDE_HAND_RANGE:HORDE_INTERACT_RANGE;
    s32 box_pad=s_box_pads[s_state.box_spot%HORDE_BOX_SPOTS];
    if (hordeNearPadAt(at,box_pad,range)) {
        if (s_state.box_state==HORDE_BOX_IDLE) {
            if (s_state.credits[me]<HORDE_BOX_COST) {
                set_cur_player(me);hudmsgBottomShow("NEED 950 PTS FOR MYSTERY BOX");set_cur_player(prev);
                return 1;
            }
            netHordeRequestAction(HORDE_ACT_BOX,0);
            return 1;
        }
        if (s_state.box_state==HORDE_BOX_READY && s_state.box_buyer==(unsigned)me) {
            netHordeRequestAction(HORDE_ACT_BOX,hordeReplaceChoice(me)<<8);
            return 1;
        }
        return 0;
    }
    for (unsigned w=0;w<HORDE_WALL_COUNT;w++) if (hordeNearPadAt(at,s_wall_pads[w],range)) {
        set_cur_player(me);
        s32 owned=w<5 && hordeGunOwned(&s_state,(unsigned)me,(unsigned)s_wall_items[w].item);
        if (w==5 && g_playerPointers[me]->bondarmour>=1.0f) {
            hudmsgBottomShow("BODY ARMOR ALREADY FULL");set_cur_player(prev);return 1;
        }
        if (owned) {
            hudmsgBottomShow("AMMO DROPS FROM ENEMIES");set_cur_player(prev);return 1;
        }
        uint32_t cost=hordeWallCost(w,FALSE);
        if (s_state.credits[me]<cost) {
            hudmsgBottomShow("NOT ENOUGH POINTS");set_cur_player(prev);return 1;
        }
        set_cur_player(prev);
        netHordeRequestAction(HORDE_ACT_WALL_BUY,w|(w<5?hordeReplaceChoice(me)<<8:0));
        return 1;
    }
    return 0;
}

int gevrHordeOnUse(void) { return hordeUse(NULL); }
int gevrHordeOnGrip(const f32 hand[3]) { return hordeUse(hand); }

static void hordeApplyLocalGrant(s32 me)
{
    if (me<0 || me>=4 || !g_playerPointers[me] || s_state.grant_seq[me]==s_applied_grant[me]) return;
    s_applied_grant[me]=s_state.grant_seq[me];
    s32 prev=get_cur_playernum();
    set_cur_player(me);
    char msg[80];
    uint8_t kind=s_state.grant_kind[me], idx=s_state.grant_item[me];
    if (kind==HORDE_GRANT_BOX_WEAPON || kind==HORDE_GRANT_WALL_WEAPON) {
        const HordeWeaponDef *def=kind==HORDE_GRANT_BOX_WEAPON?&s_box_pool[idx%HORDE_BOX_POOL_SIZE]
                                                             :&s_wall_items[idx%HORDE_WALL_COUNT];
        unsigned gone=s_state.grant_replace[me];
        s32 owned=bondinvHasInvItem(def->item);
        if (gone && gone!=(unsigned)def->item) {
            /* the third gun: the one given up leaves both hands and the inventory */
            InvItem *pair=bondinvGetDualWeapon(gone,gone);
            if (pair) bondinvRemoveItem(pair);
            bondinvRemoveItemByID(gone);
            if ((unsigned)getCurrentPlayerWeaponId(GUNLEFT)==gone) gunRequestHandWeaponChange(GUNLEFT,ITEM_UNARMED,1);
        }
        bondinvAddInvItem(def->item);
        WeaponStats *stats=get_ptr_item_statistics(def->item);
        if (!owned && stats && stats->AmmoType>AMMO_NONE && stats->MagSize>0)
            hordeGiveAmmo(stats->AmmoType,(s32)stats->MagSize*(def->item==ITEM_LASER?2:1));
        if (!owned) gunRequestHandWeaponChange(GUNRIGHT,def->item,1);
        if (gone && gone!=(unsigned)def->item)
            snprintf(msg,sizeof(msg),"%s: %s FOR %s",kind==HORDE_GRANT_BOX_WEAPON?"MYSTERY BOX":"PURCHASED",
                def->name,hordeGunName(gone));
        else snprintf(msg,sizeof(msg),kind==HORDE_GRANT_BOX_WEAPON?"MYSTERY BOX: %s":"PURCHASED %s",def->name);
        hudmsgTopShow(msg);
    } else if (kind==HORDE_GRANT_ARMOR) {
        g_playerPointers[me]->bondarmour=1.0f;
        if (g_playerPointers[me]->actual_armor<=0.0f) g_playerPointers[me]->actual_armor=1.0f;
        hudmsgTopShow("BODY ARMOR EQUIPPED");
    }
    set_cur_player(prev);
}

static s32 hordeIsBossBody(s32 body);

/* One shared crate per genuine death. Old loot expires and slots are reused;
 * neither retained corpses nor repeated kill reports create more objects. */
static void hordeDropAmmo(s32 slot)
{
    ChrRecord *chr=&g_ChrSlots[slot];
    if (s_dropped[slot] || !chr->prop || !chr->model) return;
    s_dropped[slot]=TRUE;
    if (!hordeDropsAmmo((uint32_t)s_id[slot],hordeIsBossBody(chr->bodynum)
        && hordeWaveKind(s_state.wave)==HORDE_WAVE_BOSS)) return;
    unsigned best=0;
    for (unsigned i=0;i<HORDE_AMMO_DROPS;i++) {
        if (!s_state.ammo[i].id) {best=i;break;}
        if (s_state.ammo[i].ticks<s_state.ammo[best].ticks) best=i;
    }
    HordeAmmoDrop *drop=&s_state.ammo[best];
    drop->id=s_id[slot];drop->ticks=HORDE_AMMO_LIFETIME;
    memcpy(drop->pos,chr->prop->pos.f,sizeof(drop->pos));
    drop->pos[1]=chr->ground+8.0f;
    s_state.revision++;
}

static void hordeApplyAmmo(s32 me)
{
    if (me<0 || me>=4 || !g_playerPointers[me]) return;
    uint32_t previous=s_applied_ammo[me];
    uint32_t count=s_state.ammo_grants[me]-previous;
    if (!count) return;
    s_applied_ammo[me]=s_state.ammo_grants[me];
    /* Owned weapons share their ammo pool. Each crate gives each owned pool
     * once: 32 pistol/SMG, 24 rifle, 8 shells, 6 magnum (one death in three
     * drops one, hordeDropsAmmo). Special rounds arrive once per two crates
     * so explosive multi-kills cannot refill one rocket/grenade for every
     * enemy in the blast. */
    s32 prev=get_cur_playernum();set_cur_player(me);
    s32 amounts[AMMOTYPE_MAX]={0};
    for (s32 item=ITEM_WPPK;item<ITEM_IDS_MAX;item++) if (bondinvHasInvItem(item)) {
        WeaponStats *stats=get_ptr_item_statistics(item);
        s32 ammo=stats?stats->AmmoType:AMMO_NONE;
        if (ammo<=AMMO_NONE || ammo>=AMMOTYPE_MAX) continue;
        s32 amount=(ammo==AMMO_9MM || ammo==AMMO_9MM_2)?32:ammo==AMMO_RIFLE?24:
            ammo==AMMO_SHOTGUN?8:ammo==AMMO_MAGNUM?6:item==ITEM_LASER?HORDE_LASER_CRATE:1;
        amounts[ammo]=amount;
    }
    for (s32 ammo=1;ammo<AMMOTYPE_MAX;ammo++) if (amounts[ammo]) {
        uint32_t rounds=amounts[ammo]>1?count:s_state.ammo_grants[me]/2-previous/2;
        if (rounds) hordeGiveAmmo(ammo,amounts[ammo]*(s32)(rounds>1024?1024:rounds));
    }
    hudmsgTopShow("ENEMY AMMO COLLECTED");set_cur_player(prev);
}

static s32 hordeIsBossBody(s32 body)
{
    for (unsigned i=0;i<sizeof(s_boss_bodies)/sizeof(s_boss_bodies[0]);i++) if (s_boss_bodies[i]==body) return TRUE;
    return FALSE;
}

/* A spawned guard's kind from its body and the wave: the host decides the
 * body, every headset (net_coop.c coopMakeSpawn) derives the rest - the
 * swarm's small Oddjobs and each guard's run speed. */
void gevrHordeDecorateGuard(ChrRecord *chr, s32 body)
{
    if (!netHordeActive() || !chr || !g_ChrSlots || chr<g_ChrSlots || chr>=g_ChrSlots+g_NumChrSlots) return;
    s32 slot=chr-g_ChrSlots;
    unsigned wavekind=hordeWaveKind(s_state.wave);
    unsigned kind=wavekind==HORDE_WAVE_SWARM && body==BODY_Oddjob?HORDE_ENEMY_SWARM
        :wavekind==HORDE_WAVE_BOSS && hordeIsBossBody(body)?HORDE_ENEMY_BOSS:HORDE_ENEMY_GUARD;
    if (slot<HORDE_GUARD_SLOTS) s_speed[slot]=(f32)hordeEnemySpeedPercent(s_state.wave,kind)*0.01f;
    if (kind==HORDE_ENEMY_SWARM && chr->model) modelSetScale(chr->model,chr->model->scale*HORDE_SWARM_SCALE);
}

/* chraction.c: a Horde guard's run animation speed factor, 1 for anyone else. */
f32 gevrHordeGuardSpeed(const ChrRecord *chr)
{
    if (!netHordeActive() || !chr || !g_ChrSlots || chr<g_ChrSlots || chr>=g_ChrSlots+g_NumChrSlots) return 1.0f;
    s32 slot=chr-g_ChrSlots;
    return slot<HORDE_GUARD_SLOTS && s_id[slot] && s_speed[slot]>0.0f?s_speed[slot]:1.0f;
}

/* lv.c: guards' accuracy in Horde (user, 2026-10-17: "I should be getting hit
 * more often"). Agent's 0.6 made a wave of guards rarely land a shot. */
f32 gevrHordeAiAccuracy(f32 difficulty)
{
    if (!netHordeActive()) return difficulty;
    return (f32)hordeAiAccuracyPercent(s_state.wave)*0.01f;
}

/* A door open at once, as the AI's DoorOpenInstant has it (chrai.c). */
static void hordeDoorOpenNow(DoorRecord *door)
{
    door->speed=0;
    door->openPosition=door->maxFrac;
    door->openedTime=g_GlobalTimer;
    door->openstate=DOORSTATE_STATIONARY;
    door->flags|=PROPFLAG_DOOR_KEEPOPEN;
    doorUpdateBbox(door);
    doorActivatePortal(door);
    door7F053B10(door);
}

/* Every door open and held open (user: guards got stuck on them). A door the
 * level never lets open (locked for good, padlocked) stays shut: it may
 * close off space the level has not built. Every headset does the same, so
 * nothing is sent: a door message starts a door moving (doorStartOpen),
 * which clears its keep-open flag, and it then shut itself on its timer
 * (user, 2026-10-18: Archives doors still opened and closed). No one can
 * work a held door (PROPFLAG_CANNOT_ACTIVATE, chrprop.c), and hordeHoldDoors
 * opens again any the game moves. */
static void hordeOpenDoors(void)
{
    s_doors_opened=TRUE;
    s_held_count=0;
    for (PropDefHeaderRecord *p=g_CurrentSetup.propDefs;p && p->type!=PROPDEF_END;
         p=(PropDefHeaderRecord *)((u32 *)p+sizepropdef(p))) {
        if (p->type!=PROPDEF_DOOR) continue;
        DoorRecord *door=(DoorRecord *)p;
        if (!door->prop || (door->flags & PROPFLAG_CANNOT_ACTIVATE) || !doorIsPadlockFree(door)
            || door->maxFrac<=0.0f) continue;
        hordeDoorOpenNow(door);
        door->flags|=PROPFLAG_CANNOT_ACTIVATE;
        if (s_held_count<HORDE_HELD_DOORS) s_held_doors[s_held_count++]=door;
    }
    sysLogPrintf(LOG_NOTE,"horde: %d doors held open",s_held_count);
}

static void hordeHoldDoors(s32 ticks)
{
    if ((s_hold_ticks-=ticks)>0) return;
    s_hold_ticks=15;
    s32 moved=0;
    for (s32 i=0;i<s_held_count;i++) {
        DoorRecord *door=s_held_doors[i];
        if (door->openstate==DOORSTATE_STATIONARY && door->openPosition>=door->maxFrac
            && (door->flags & PROPFLAG_DOOR_KEEPOPEN)) continue;
        hordeDoorOpenNow(door);
        moved++;
    }
    if (moved) sysLogPrintf(LOG_NOTE,"horde: %d held doors opened again",moved);
}

static s32 hordeSpawn(void)
{
    if (!s_pad_count || chrGetNumFree()<4 || !s_state.next_id) return FALSE;
    unsigned first=hordeRandom(&s_state)%s_pad_count;
    for (s32 n=0;n<s_pad_count;n++) {
        PadRecord *pad=&g_CurrentSetup.pads[s_pads[(first+n)%s_pad_count]];
        s32 safe=TRUE, reachable=FALSE;
        waypoint *from=chrlvStanPathRelated(&pad->pos,pad->stan);
        for (s32 p=0;p<4 && safe;p++) if(netLobbySlotConnected(p) && !netPlayerIsSpectator(p)
            && g_playerPointers[p] && g_playerPointers[p]->prop && !gevrCoopDowned(p)) {
            coord3d *pos=&g_playerPointers[p]->prop->pos;
            f32 dx=pos->x-pad->pos.x,dy=pos->y-pad->pos.y,dz=pos->z-pad->pos.z;
            /* Leave space around every player, including on adjacent floors. */
            if (dx*dx+dz*dz<900.0f*900.0f && dy*dy<300.0f*300.0f) safe=FALSE;
            StandTile *tile=g_playerPointers[p]->prop->stan;
            if (tile && dy*dy<150.0f*150.0f && stanTestLineUnobstructed(&tile,pos->x,pos->z,pad->pos.x,pad->pos.z,
                CDTYPE_OBJS|CDTYPE_DOORS|CDTYPE_PATHBLOCKER|CDTYPE_AIOPAQUE,140,140,0,1) && tile==pad->stan) safe=FALSE;
            /* Route searches are the expensive part: skip them once the pad is ruled out or proven. */
            if (!safe || reachable) continue;
            waypoint *to=chrlvStanPathRelated(pos,g_playerPointers[p]->prop->stan);
            waypoint *route[MAX_CHRWAYPOINTS];
            if (from && to && (from==to || waypointFindRoute(from,to,route,MAX_CHRWAYPOINTS)>=2)) reachable=TRUE;
        }
        if (!safe || !reachable) continue;
        AIRecord *ai=ailistFindById(GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND);
        if (!ai) return FALSE;
        unsigned players=s_state.players?s_state.players:1;
        unsigned kind=hordeEnemyKind(s_state.wave,s_state.next_id,hordeWaveSpawned(&s_state),players);
        s32 body=kind==HORDE_ENEMY_ARMORED?BODY_Janus_Marine:kind==HORDE_ENEMY_JANUS?BODY_Janus_Special_Forces
            :kind==HORDE_ENEMY_SWARM?BODY_Oddjob
            :kind==HORDE_ENEMY_BOSS?s_boss_bodies[s_state.next_id%(sizeof(s_boss_bodies)/sizeof(s_boss_bodies[0]))]
            :s_body;
        PropRecord *prop=chrSpawnAtCoord(body,-1,&pad->pos,pad->stan,
            atan2f(pad->look.x,pad->look.z),ai,0);
        if (!prop || !prop->chr) continue;
        ChrRecord *chr=prop->chr;
        /* Horde has no script character references. Keep a bounded unique
         * script ID while the full run/generation identity travels separately. */
        chr->chrnum=1000+(chr-g_ChrSlots);
        static const s32 tier_weapons[6]={ITEM_TT33,ITEM_SKORPION,ITEM_MP5K,ITEM_AK47,ITEM_M16,ITEM_FNP90};
        static const s32 tier_models[6]={PROP_CHRTT33,PROP_CHRSKORPION,PROP_CHRMP5K,PROP_CHRKALASH,PROP_CHRM16,PROP_CHRFNP90};
        unsigned tier=hordeEnemyWeaponTier(s_state.wave,s_state.next_id);
        if (kind==HORDE_ENEMY_ARMORED || kind==HORDE_ENEMY_JANUS) tier=tier<5?tier+1:5;
        if (kind==HORDE_ENEMY_BOSS) tier=4+(s_state.next_id&1);   /* AR33 / RC-P90 */
        if (kind==HORDE_ENEMY_SWARM) tier=0;                      /* a pistol each */
        s32 weapon=tier_weapons[tier%6], model=tier_models[tier%6];
        /* Ammo crates are separate from guns; keep wave weapons out of the economy. */
        if (!chrGiveWeapon(chr,model,weapon,PROPFLAG_AIUNDROPPABLE)) {
            chr->hidden|=CHRHIDDEN_REMOVE;return FALSE;
        }
        f32 base_dmg=chr->maxdamage>0.0f?chr->maxdamage:4.0f;
        chr->maxdamage=base_dmg*((f32)hordeEnemyHealthPercent(s_state.wave)*0.01f)
            *((f32)hordeEnemyKindHealthPercent(kind,players)*0.01f);
        chr->alertness=255;chr->grenadeprob=0;chr->aireturnlist=GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND;
        gevrHordeSetGuardId(chr,s_state.next_id++);
        gevrHordeDecorateGuard(chr,body);
        if (!s_state.next_id || s_state.next_id==GEVR_ALLY_HORDE_ID) s_state.next_id=1;
        gevrCoopChrSpawned(chr,ai,0); /* announce only the fully configured guard */
        return TRUE;
    }
    return FALSE;
}

static void hordeRecoverGuard(s32 slot,s32 ticks)
{
    ChrRecord *chr=&g_ChrSlots[slot];
    coord3d *pos=&chr->prop->pos, *last=&s_last_pos[slot];
    f32 dx=pos->x-last->x,dy=pos->y-last->y,dz=pos->z-last->z;
    if ((s_door_ticks[slot]-=ticks)<=0) {
        s_door_ticks[slot]=60;
        s32 target=gevrCoopGuardTarget(chr);
        if (target>=0 && target<4 && g_playerPointers[target] && g_playerPointers[target]->prop) {
            coord3d *to=&g_playerPointers[target]->prop->pos;
            PropRecord *block=sub_GAME_7F0B1410(chr->prop->stan,pos->x,pos->z,to->x,to->z,CDTYPE_DOORS);
            if (block && block->type==PROP_TYPE_DOOR && block->door) {
                f32 x=block->pos.x-pos->x,y=block->pos.y-pos->y,z=block->pos.z-pos->z;
                if (x*x+z*z<220.0f*220.0f && y*y<200.0f*200.0f
                    && block->door->openstate!=DOORSTATE_OPENING && block->door->openPosition<block->door->frac) {
                    doorsChooseSwingDirection(chr->prop,block->door);
                    doorActivate(block->door,DOORSTATE_OPENING);
                }
            }
        }
    }
    if (dx*dx+dy*dy+dz*dz>100.0f*100.0f) {s_stall_ticks[slot]=0;*last=*pos;return;}
    if ((s_stall_ticks[slot]+=ticks)<6*60) return;
    for (s32 p=0;p<4;p++) if (netLobbySlotConnected(p) && !netPlayerIsSpectator(p)
        && !gevrCoopDowned(p) && g_playerPointers[p] && g_playerPointers[p]->prop) {
        PropRecord *target=g_playerPointers[p]->prop;
        StandTile *tile=chr->prop->stan;
        f32 height=target->pos.y-pos->y;
        if (tile && height*height<150.0f*150.0f
            && stanTestLineUnobstructed(&tile,pos->x,pos->z,target->pos.x,target->pos.z,
                CDTYPE_OBJS|CDTYPE_DOORS|CDTYPE_PATHBLOCKER|CDTYPE_AIOPAQUE,140,140,0,1)
            && tile==target->stan) {s_stall_ticks[slot]=0;return;}
    }
    /* A guard fighting from cover stays. An unseen, immobile guard gets a
     * fresh path attempt, then a replacement under the same wave quota. */
    if (s_stall_ticks[slot]<18*60) {
        if (s_stall_ticks[slot]-ticks<6*60) {
            chrStopFiring(chr);chr->actiontype=ACT_STAND;
            chr->ailist=ailistFindById(GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND);chr->aioffset=0;
        }
    } else chr->hidden|=CHRHIDDEN_REMOVE;
}

/* The station the player stands at, on the live bottom line (refreshed so it
 * stays up). During a break the countdown rides on the same line, rather
 * than the two rewriting one live row in turn. 1 while a prompt shows. */
static s32 hordeUpdateStationPrompt(s32 me, s32 ticks, unsigned breakSecs)
{
    if (me<0 || me>=4 || !g_playerPointers[me] || netPlayerIsSpectator(me) || gevrCoopDowned(me)) {
        s_prompt[0]=0;
        return FALSE;
    }
    char prompt[100], line[100];
    prompt[0]=0;
    s32 box_pad=s_box_pads[s_state.box_spot%HORDE_BOX_SPOTS];
    s32 prev=get_cur_playernum();
    if (hordeNearPad(me,box_pad,HORDE_INTERACT_RANGE)) {
        const char *wname=s_box_pool[s_state.box_weapon%HORDE_BOX_POOL_SIZE].name;
        if (s_state.box_state==HORDE_BOX_IDLE)
            snprintf(prompt,sizeof(prompt),"MYSTERY BOX [950 PTS] (%u PTS) - PRESS B/GRIP",s_state.credits[me]);
        else if (s_state.box_state==HORDE_BOX_ROLLING)
            snprintf(prompt,sizeof(prompt),"MYSTERY BOX ROLLING: %s...",wname);
        else if (s_state.box_state==HORDE_BOX_READY) {
            unsigned item=(unsigned)s_box_pool[s_state.box_weapon%HORDE_BOX_POOL_SIZE].item;
            unsigned gone=hordeGunOwned(&s_state,(unsigned)me,item)?0:hordeReplaceChoice(me);
            if (s_state.box_buyer!=(unsigned)me)
                snprintf(prompt,sizeof(prompt),"MYSTERY BOX: %s (%us)",wname,(s_state.box_timer+59)/60);
            else if (gone)
                snprintf(prompt,sizeof(prompt),"TAKE %s FOR %s (%us) - PRESS B/GRIP",wname,hordeGunName(gone),
                    (s_state.box_timer+59)/60);
            else snprintf(prompt,sizeof(prompt),"TAKE %s (%us) - PRESS B/GRIP",wname,(s_state.box_timer+59)/60);
        }
        else
            snprintf(prompt,sizeof(prompt),"TEDDY BEAR! BOX MOVING - 950 PTS REFUNDED");
    } else {
        for (unsigned w=0;w<HORDE_WALL_COUNT;w++) if (hordeNearPad(me,s_wall_pads[w],HORDE_INTERACT_RANGE)) {
            s32 owned=w<5 && hordeGunOwned(&s_state,(unsigned)me,(unsigned)s_wall_items[w].item);
            unsigned gone=w<5 && !owned?hordeReplaceChoice(me):0;
            if (owned) snprintf(prompt,sizeof(prompt),"%s OWNED - AMMO DROPS FROM ENEMIES",s_wall_items[w].name);
            else if (gone) snprintf(prompt,sizeof(prompt),"WALL BUY: %s FOR %s [%u PTS] (%u PTS) - PRESS B/GRIP",
                s_wall_items[w].name,hordeGunName(gone),hordeWallCost(w,FALSE),s_state.credits[me]);
            else snprintf(prompt,sizeof(prompt),"WALL BUY: %s [%u PTS] (%u PTS) - PRESS B/GRIP",
                s_wall_items[w].name,hordeWallCost(w,FALSE),s_state.credits[me]);
            break;
        }
    }
    if (!prompt[0]) {
        s_prompt[0]=0;
        return FALSE;
    }
    if (breakSecs) snprintf(line,sizeof(line),"%s - WAVE IN %u",prompt,breakSecs);
    else snprintf(line,sizeof(line),"%s",prompt);
    s_prompt_ticks-=ticks;
    if (strcmp(line,s_prompt) || s_prompt_ticks<=0) {
        snprintf(s_prompt,sizeof(s_prompt),"%s",line);
        s_prompt_ticks=30;
        set_cur_player(me);gevrHudmsgBottomLive(s_prompt);set_cur_player(prev);
    }
    return TRUE;
}

/* An uncollectable model standing on a station's pad. */
static ObjectRecord *hordeMarkerCreate(s32 model, s32 item, f32 scale)
{
    ObjectRecord *obj=create_new_item_instance_of_model(model,item);
    if (!obj || !obj->prop || !obj->model) return NULL;
    obj->flags|=PROPFLAG_UNCOLLECTABLE|PROPFLAG_INVINCIBLE;
    if (scale!=1.0f) modelSetScale(obj->model,obj->model->scale*scale);
    return obj;
}

static void hordeMarkerPlace(ObjectRecord *obj, s32 pad_id, f32 lift, f32 yaw, s32 rooms)
{
    if (!obj || !obj->prop || !obj->model || !g_CurrentSetup.pads || pad_id<0) return;
    PadRecord *pad=&g_CurrentSetup.pads[pad_id];
    coord3d pos=obj->prop->pos;   /* turning in place: where it stands already */
    if (rooms) {
        coord3d probe=pad->pos;
        f32 floor=pad->pos.y;
        pos=pad->pos;
        if (stanFindTileBelowPos(&probe,NULL,&floor)) pos.y=floor;
        pos.y+=lift;
    }
    matrix_4x4_set_position_and_rotation_around_y(pos.f,yaw,&obj->mtx);
    matrix_scalar_multiply(obj->model->scale,obj->mtx.m[0]);
    obj->runtime_pos=pos;
    obj->prop->pos=pos;
    if (rooms) {
        if (pad->stan) obj->prop->stan=pad->stan;
        setupUpdateObjectRoomPosition(obj);
    }
}

static void hordeUpdateMarkers(s32 ticks)
{
    if (!s_pad_count || !g_CurrentSetup.pads) return;
    if (!s_markers_placed) {
        s_markers_placed=TRUE;
        s_box_marker=hordeMarkerCreate(PROP_AMMO_CRATE5,ITEM_UNARMED,1.0f);
        for (unsigned w=0;w<HORDE_WALL_COUNT;w++)
            s_wall_marker[w]=hordeMarkerCreate(s_wall_models[w],s_wall_items[w].item,w<5?2.5f:1.5f);
        for (unsigned w=0;w<HORDE_WALL_COUNT;w++) if (s_wall_marker[w]) {
            hordeMarkerPlace(s_wall_marker[w],s_wall_pads[w],60.0f,0.0f,TRUE);
            chrpropActivate(s_wall_marker[w]->prop);chrpropEnable(s_wall_marker[w]->prop);
        }
        if (s_box_marker) {
            chrpropActivate(s_box_marker->prop);chrpropEnable(s_box_marker->prop);
        }
        sysLogPrintf(LOG_NOTE,"horde: station markers placed (box %s, wall buys at pads %d %d %d %d %d %d)",
            s_box_marker?"yes":"no",s_wall_pads[0],s_wall_pads[1],s_wall_pads[2],s_wall_pads[3],s_wall_pads[4],s_wall_pads[5]);
    }
    if (s_box_marker && s_marker_spot!=(s32)s_state.box_spot) {
        s_marker_spot=s_state.box_spot;
        hordeMarkerPlace(s_box_marker,s_box_pads[s_marker_spot%HORDE_BOX_SPOTS],0.0f,0.0f,TRUE);
    }
    /* the wall guns turn slowly where they stand, to catch the eye */
    s_marker_yaw+=0.02f*(f32)(ticks>0?ticks:0);
    if (s_marker_yaw>6.2831855f) s_marker_yaw-=6.2831855f;
    for (unsigned w=0;w<5;w++) if (s_wall_marker[w])
        hordeMarkerPlace(s_wall_marker[w],s_wall_pads[w],60.0f,s_marker_yaw,FALSE);
}

static void hordeUpdateAmmo(s32 ticks,s32 me)
{
    s_pickup_ticks-=ticks;
    for (unsigned i=0;i<HORDE_AMMO_DROPS;i++) {
        HordeAmmoDrop *drop=&s_state.ammo[i];
        ObjectRecord *obj=s_ammo_marker[i];
        if (!drop->id) {
            if (obj && s_ammo_marker_id[i]) chrpropDisable(obj->prop);
            s_ammo_marker_id[i]=0;continue;
        }
        if (!obj) {
            obj=s_ammo_marker[i]=hordeMarkerCreate(PROP_AMMO_CRATE5,ITEM_UNARMED,0.6f);
            /* walked over, not into: without its collision the host's reach
             * line (gevrHordeHandleAction) no longer stops at the crate */
            if (obj) {obj->flags&=~PROPFLAG_00000100;chrpropActivate(obj->prop);}
        }
        if (obj && s_ammo_marker_id[i]!=drop->id) {
            memcpy(obj->prop->pos.f,drop->pos,sizeof(drop->pos));obj->runtime_pos=obj->prop->pos;
            matrix_4x4_set_position_and_rotation_around_y(drop->pos,0,&obj->mtx);
            matrix_scalar_multiply(obj->model->scale,obj->mtx.m[0]);
            coord3d probe=obj->prop->pos;f32 floor=probe.y;
            obj->prop->stan=stanFindTileBelowPos(&probe,NULL,&floor);
            setupUpdateObjectRoomPosition(obj);chrpropEnable(obj->prop);
            s_ammo_marker_id[i]=drop->id;
        }
        if (s_pickup_ticks>0 || me<0 || me>=4 || !g_playerPointers[me] || !g_playerPointers[me]->prop
            || netPlayerIsSpectator(me) || gevrCoopDowned(me) || s_state.phase==HORDE_DEFEAT) continue;
        coord3d *at=&g_playerPointers[me]->prop->pos;
        f32 dx=at->x-drop->pos[0],dy=at->y-drop->pos[1],dz=at->z-drop->pos[2];
        if (dx*dx+dz*dz<=90.0f*90.0f && dy>=-20.0f && dy<=210.0f) {
            s_pickup_ticks=15;netHordeRequestAction(HORDE_ACT_AMMO_PICKUP,drop->id);
        }
    }
}

void gevrHordeTick(int ticks)
{
    if (!netHordeRunning()) return;
    if (!s_doors_opened) hordeOpenDoors();
    hordeHoldDoors(ticks);
    if (ticks<0) ticks=0;
    if (ticks>15) ticks=15;
    if (netIsHost() && ticks>0 && s_state.phase!=HORDE_DEFEAT) {
        unsigned revision=s_state.revision;
        for (s32 i=0;i<g_NumChrSlots && i<HORDE_GUARD_SLOTS;i++) if (s_id[i]) {
            ChrRecord *chr=&g_ChrSlots[i];
            s32 dead=!chr->model || chr->actiontype==ACT_DIE || chr->actiontype==ACT_DEAD;
            if (dead && !s_counted[i]) {
                s_counted[i]=TRUE;
                hordeEnemyKilled(&s_state,chr->model!=NULL,(unsigned)s_last_attacker[i],s_last_hitpart[i]==HIT_HEAD);
            }
            if (!dead && chr->prop) hordeRecoverGuard(i,ticks);
            if (dead && chr->model) {
                hordeDropAmmo(i);
                s_dead_ticks[i]+=ticks;
                if (s_dead_ticks[i]>5*60) chr->hidden|=CHRHIDDEN_REMOVE;
            }
            if (!chr->model) s_id[i]=0;
        }
        for (unsigned i=0;i<HORDE_AMMO_DROPS;i++) if (s_state.ammo[i].id) {
            if (s_state.ammo[i].ticks<=(unsigned)ticks) {
                s_state.ammo[i].id=0;s_state.ammo[i].ticks=0;s_state.revision++;
            } else s_state.ammo[i].ticks-=ticks;
        }
        s32 changed=hordeAdvance(&s_state,ticks,netMpPlayerCount(1));
        if (changed && s_state.phase==HORDE_INTERMISSION) netHordeAdmitPlayers();
        if (hordeCanSpawn(&s_state)) {
            if (hordeSpawn()) hordeSpawned(&s_state);
            else s_state.spawn_timer=30;
        }
        if (revision!=s_state.revision) netHordeSend(-1);
    }
    s32 me=netGetLocalSlot();
    hordeApplyLocalGrant(me);
    hordeApplyAmmo(me);
    hordeUpdateAmmo(ticks,me);
    /* Announce each wave and the defeat once, and keep a break's countdown
     * on screen while it runs; the pause menu keeps the running stats. A wave
     * message every second sat mid-view in VR all game. */
    s32 counting=s_state.phase==HORDE_PREPARE || s_state.phase==HORDE_INTERMISSION;
    unsigned secs=counting?(s_state.timer+59)/60:0;
    hordeUpdateMarkers(ticks);
    /* A station's prompt owns the live line (with the countdown on it). */
    s32 prompting=hordeUpdateStationPrompt(me,ticks,counting?secs:0);
    if (s_shown_phase!=s_state.phase || s_shown_wave!=s_state.wave || (counting && s_shown_secs!=secs)) {
        s32 prev=get_cur_playernum();
        char text[100];gevrHordeText(text,sizeof(text));
        if (me>=0 && me<4 && g_playerPointers[me] && !(counting && prompting)) {
            set_cur_player(me);
            if (counting) gevrHudmsgBottomLive(text); else hudmsgBottomShow(text);
            set_cur_player(prev);
        }
        s_shown_phase=s_state.phase;s_shown_wave=s_state.wave;s_shown_secs=secs;
    }
    /* leaving a station mid-break: the countdown comes back at once */
    if (counting && prompting) s_shown_secs=~0u;
}
