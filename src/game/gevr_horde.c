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

extern s32 netGetLocalSlot(void);
extern int netMpPlayerCount(int fallback);
extern s32 chrGetNumFree(void);
extern void chrStopFiring(ChrRecord *chr);
extern waypoint *chrlvStanPathRelated(coord3d *pos, StandTile *stan);
extern s32 netIsHost(void);
extern unsigned netGetRandomSeed(void);
extern AIRecord *ailistFindById(s32 id);
extern void gevrHudmsgBottomLive(char *mess);
extern PropRecord *chrSpawnAtCoord(s32 body,s32 head,coord3d *pos,StandTile *stan,f32 angle,AIRecord *ai,s32 flags);

#define HORDE_INTERACT_RANGE 220.0f

typedef struct {
    s32 item, ammo, amount;
    const char *name;
} HordeWeaponDef;

static const HordeWeaponDef s_box_pool[HORDE_BOX_POOL_SIZE] = {
    { ITEM_RUGER,          AMMO_MAGNUM,       100, "COUGAR MAGNUM" },
    { ITEM_UZI,            AMMO_9MM,          240, "ZMG 9MM" },
    { ITEM_SPECTRE,        AMMO_9MM,          240, "PHANTOM" },
    { ITEM_FNP90,          AMMO_9MM,          320, "RC-P90" },
    { ITEM_AUTOSHOT,       AMMO_SHOTGUN,      100, "AUTO SHOTGUN" },
    { ITEM_SNIPERRIFLE,    AMMO_RIFLE,        100, "SNIPER RIFLE" },
    { ITEM_SILVERWPPK,     AMMO_9MM,          200, "SILVER PP7" },
    { ITEM_GOLDWPPK,       AMMO_9MM,          200, "GOLD PP7" },
    { ITEM_LASER,          AMMO_NONE,           0, "MOONRAKER LASER" },
    { ITEM_GOLDENGUN,      AMMO_GGUN,          50, "GOLDEN GUN" },
    { ITEM_GRENADELAUNCH,  AMMO_GRENADEROUND,  12, "GRENADE LAUNCHER" },
    { ITEM_ROCKETLAUNCH,   AMMO_ROCKETS,        6, "ROCKET LAUNCHER" },
};

static const HordeWeaponDef s_wall_items[HORDE_WALL_COUNT] = {
    { ITEM_SKORPION, AMMO_9MM_2,   160, "KLOBB" },
    { ITEM_SHOTGUN,  AMMO_SHOTGUN,  40, "SHOTGUN" },
    { ITEM_AK47,     AMMO_RIFLE,   200, "KF7 SOVIET" },
    { ITEM_MP5K,     AMMO_9MM,     200, "D5K DEUTSCHE" },
    { ITEM_M16,      AMMO_RIFLE,   240, "AR33 ASSAULT RIFLE" },
    { ITEM_TOKEN,    AMMO_NONE,      0, "BODY ARMOR" },
};

static HordeState s_state;
static unsigned s_id[HORDE_GUARD_SLOTS];
static s32 s_counted[HORDE_GUARD_SLOTS], s_dead_ticks[HORDE_GUARD_SLOTS];
static s32 s_last_attacker[HORDE_GUARD_SLOTS], s_last_hitpart[HORDE_GUARD_SLOTS];
static coord3d s_last_pos[HORDE_GUARD_SLOTS];
static s32 s_stall_ticks[HORDE_GUARD_SLOTS];
static s32 s_pads[128], s_pad_count, s_body;
static s32 s_box_pads[HORDE_BOX_SPOTS], s_wall_pads[HORDE_WALL_COUNT];
static uint32_t s_applied_grant[4];
static unsigned s_resupplied, s_shown_phase, s_shown_wave, s_shown_secs;
static unsigned s_shown_box_state, s_shown_box_weapon, s_shown_prompt;
static struct {
    SetupIntroSpawn spawn;
    struct SetupIntroItem item;
    struct SetupIntroAmmo ammo;
    SetupIntroEmpty end;
} s_intro;

const HordeState *gevrHordeState(void) { return &s_state; }
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
    s32 slot=chr-g_ChrSlots;
    if (slot<HORDE_GUARD_SLOTS) {
        s_id[slot]=id;s_counted[slot]=0;s_dead_ticks[slot]=0;s_stall_ticks[slot]=0;
        s_last_attacker[slot]=0;s_last_hitpart[slot]=0;
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
     * non-spawn pads so rooms around the arena host distinct stations. */
    s32 pool=s_pad_count>1?s_pad_count-1:1;
    for (unsigned b=0;b<HORDE_BOX_SPOTS;b++)
        s_box_pads[b]=s_pad_count?s_pads[(s_pad_count>1?1:0)+(b*pool)/HORDE_BOX_SPOTS]:0;
    for (unsigned w=0;w<HORDE_WALL_COUNT;w++) {
        s32 idx=(s_pad_count>1?1:0)+((w+1)*pool)/(HORDE_WALL_COUNT+1);
        if (s_pad_count>2 && s_pads[idx%s_pad_count]==s_box_pads[0]) idx++;
        s_wall_pads[w]=s_pad_count?s_pads[idx%s_pad_count]:0;
    }
    memset(&s_intro,0,sizeof(s_intro));
    s_intro.spawn.type=INTROTYPE_SPAWN;s_intro.spawn.index=start;
    s_intro.item.type=INTROTYPE_ITEM;s_intro.item.item_right=ITEM_WPPK;
    s_intro.ammo.type=INTROTYPE_AMMO;s_intro.ammo.ammo_type=AMMO_9MM;s_intro.ammo.ammo_amount=120;
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

void gevrHordeStageLoaded(void)
{
    hordeInit(&s_state,netGetRandomSeed());
    memset(s_id,0,sizeof(s_id));memset(s_counted,0,sizeof(s_counted));
    memset(s_dead_ticks,0,sizeof(s_dead_ticks));
    memset(s_last_attacker,0,sizeof(s_last_attacker));
    memset(s_last_hitpart,0,sizeof(s_last_hitpart));
    memset(s_applied_grant,0,sizeof(s_applied_grant));
    s_resupplied=~0u;s_shown_phase=~0u;s_shown_wave=~0u;s_shown_secs=~0u;
    s_shown_box_state=~0u;s_shown_box_weapon=~0u;s_shown_prompt=0;
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
    unsigned who=(attacker>=0 && attacker<4)?(unsigned)attacker:0;
    s_last_attacker[slot]=(s32)who;
    s_last_hitpart[slot]=hitpart;
    s32 lethal=(chr->maxdamage>0.0f && chr->damage>chr->maxdamage)
        || chr->actiontype==ACT_DIE || chr->actiontype==ACT_DEAD;
    if (lethal) {
        s_counted[slot]=TRUE;
        hordeEnemyKilled(&s_state,TRUE,who,hitpart==HIT_HEAD);
        netHordeSend(-1);
    } else {
        hordeRewardHit(&s_state,who);
    }
}

static s32 hordeNearPad(s32 slot, s32 pad_id, f32 radius)
{
    if (slot<0 || slot>=4 || !g_playerPointers[slot] || !g_playerPointers[slot]->prop) return FALSE;
    if (gevrCoopDowned(slot) || !g_CurrentSetup.pads || pad_id<0) return FALSE;
    coord3d *pos=&g_playerPointers[slot]->prop->pos;
    coord3d *at=&g_CurrentSetup.pads[pad_id].pos;
    f32 dx=pos->x-at->x, dy=pos->y-at->y, dz=pos->z-at->z;
    return dx*dx+dz*dz<=radius*radius && dy*dy<=150.0f*150.0f;
}

int gevrHordeHandleAction(int slot, unsigned action, unsigned item)
{
    if (!netHordeRunning() || !netIsHost() || slot<0 || slot>=4 || gevrCoopDowned(slot)) return 0;
    if (action==HORDE_ACT_BOX) {
        s32 pad=s_box_pads[s_state.box_spot%HORDE_BOX_SPOTS];
        if (!hordeNearPad(slot,pad,HORDE_INTERACT_RANGE*1.25f)) return 0;
        return hordeUseBox(&s_state,(unsigned)slot)!=0;
    }
    if ((action==HORDE_ACT_WALL_BUY || action==HORDE_ACT_WALL_AMMO) && item<HORDE_WALL_COUNT) {
        if (!hordeNearPad(slot,s_wall_pads[item],HORDE_INTERACT_RANGE*1.25f)) return 0;
        return hordeBuyWall(&s_state,(unsigned)slot,item,action==HORDE_ACT_WALL_AMMO);
    }
    return 0;
}

int gevrHordeOnUse(void)
{
    if (!netHordeRunning()) return 0;
    s32 me=netGetLocalSlot(), prev=get_cur_playernum();
    if (me<0 || me>=4 || !g_playerPointers[me] || netPlayerIsSpectator(me) || gevrCoopDowned(me)) return 0;
    s32 box_pad=s_box_pads[s_state.box_spot%HORDE_BOX_SPOTS];
    if (hordeNearPad(me,box_pad,HORDE_INTERACT_RANGE)) {
        if (s_state.box_state==HORDE_BOX_IDLE) {
            if (s_state.credits[me]<HORDE_BOX_COST) {
                set_cur_player(me);hudmsgBottomShow("NEED 950 PTS FOR MYSTERY BOX");set_cur_player(prev);
                return 1;
            }
            netHordeRequestAction(HORDE_ACT_BOX,0);
            return 1;
        }
        if (s_state.box_state==HORDE_BOX_READY && s_state.box_buyer==(unsigned)me) {
            netHordeRequestAction(HORDE_ACT_BOX,0);
            return 1;
        }
        return 0;
    }
    for (unsigned w=0;w<HORDE_WALL_COUNT;w++) if (hordeNearPad(me,s_wall_pads[w],HORDE_INTERACT_RANGE)) {
        set_cur_player(me);
        s32 owned=w<5 && bondinvHasInvItem(s_wall_items[w].item);
        if (w==5 && g_playerPointers[me]->bondarmour>=1.0f) {
            hudmsgBottomShow("BODY ARMOR ALREADY FULL");set_cur_player(prev);return 1;
        }
        uint32_t cost=hordeWallCost(w,owned);
        if (s_state.credits[me]<cost) {
            hudmsgBottomShow("NOT ENOUGH POINTS");set_cur_player(prev);return 1;
        }
        set_cur_player(prev);
        netHordeRequestAction(owned?HORDE_ACT_WALL_AMMO:HORDE_ACT_WALL_BUY,w);
        return 1;
    }
    return 0;
}

static void hordeApplyLocalGrant(s32 me)
{
    if (me<0 || me>=4 || !g_playerPointers[me] || s_state.grant_seq[me]==s_applied_grant[me]) return;
    s_applied_grant[me]=s_state.grant_seq[me];
    s32 prev=get_cur_playernum();
    set_cur_player(me);
    char msg[80];
    uint8_t kind=s_state.grant_kind[me], idx=s_state.grant_item[me];
    if (kind==HORDE_GRANT_BOX_WEAPON) {
        const HordeWeaponDef *def=&s_box_pool[idx%HORDE_BOX_POOL_SIZE];
        bondinvAddInvItem(def->item);
        if (def->ammo!=AMMO_NONE) give_cur_player_ammo(def->ammo,def->amount);
        snprintf(msg,sizeof(msg),"MYSTERY BOX: %s",def->name);
        hudmsgTopShow(msg);
    } else if (kind==HORDE_GRANT_WALL_WEAPON || kind==HORDE_GRANT_WALL_AMMO) {
        const HordeWeaponDef *def=&s_wall_items[idx%HORDE_WALL_COUNT];
        if (kind==HORDE_GRANT_WALL_WEAPON) bondinvAddInvItem(def->item);
        if (def->ammo!=AMMO_NONE) give_cur_player_ammo(def->ammo,def->amount);
        snprintf(msg,sizeof(msg),kind==HORDE_GRANT_WALL_AMMO?"AMMO REFILLED: %s":"PURCHASED %s",def->name);
        hudmsgTopShow(msg);
    } else if (kind==HORDE_GRANT_ARMOR) {
        g_playerPointers[me]->bondarmour=1.0f;
        if (g_playerPointers[me]->actual_armor<=0.0f) g_playerPointers[me]->actual_armor=1.0f;
        hudmsgTopShow("BODY ARMOR EQUIPPED");
    }
    set_cur_player(prev);
}

static void hordeResupply(void)
{
    s32 me=netGetLocalSlot(),prev=get_cur_playernum();
    if (me<0 || me>=4 || !g_playerPointers[me] || !g_playerPointers[me]->prop) return;
    if (netPlayerIsSpectator(me)) return;
    set_cur_player(me);
    /* Players start with PP7 and must earn points to buy wall weapons or roll
     * the Mystery Box; breaks still provide a baseline ammo top-up. */
    bondinvAddInvItem(ITEM_WPPK);
    give_cur_player_ammo(AMMO_9MM,120);
    give_cur_player_ammo(AMMO_9MM_2,120);
    give_cur_player_ammo(AMMO_RIFLE,120);
    hudmsgTopShow("HORDE RESUPPLY");
    set_cur_player(prev);s_resupplied=s_state.completed;
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
        PropRecord *prop=chrSpawnAtCoord(s_body,-1,&pad->pos,pad->stan,
            atan2f(pad->look.x,pad->look.z),ai,0);
        if (!prop || !prop->chr) continue;
        ChrRecord *chr=prop->chr;
        /* Horde has no script character references. Keep a bounded unique
         * script ID while the full run/generation identity travels separately. */
        chr->chrnum=1000+(chr-g_ChrSlots);
        static const s32 tier_weapons[6]={ITEM_TT33,ITEM_SKORPION,ITEM_MP5K,ITEM_AK47,ITEM_M16,ITEM_FNP90};
        static const s32 tier_models[6]={PROP_CHRTT33,PROP_CHRSKORPION,PROP_CHRMP5K,PROP_CHRKALASH,PROP_CHRM16,PROP_CHRFNP90};
        unsigned tier=hordeEnemyWeaponTier(s_state.wave,s_state.next_id);
        s32 weapon=tier_weapons[tier%6], model=tier_models[tier%6];
        /* Resupply provides ammunition; undroppable guns bound object growth. */
        if (!chrGiveWeapon(chr,model,weapon,PROPFLAG_AIUNDROPPABLE)) {
            chr->hidden|=CHRHIDDEN_REMOVE;return FALSE;
        }
        f32 base_dmg=chr->maxdamage>0.0f?chr->maxdamage:4.0f;
        chr->maxdamage=base_dmg*((f32)hordeEnemyHealthPercent(s_state.wave)*0.01f);
        chr->alertness=255;chr->grenadeprob=0;chr->aireturnlist=GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND;
        gevrHordeSetGuardId(chr,s_state.next_id++);
        if (!s_state.next_id) s_state.next_id=1;
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
    if (dx*dx+dy*dy+dz*dz>100.0f*100.0f) {s_stall_ticks[slot]=0;*last=*pos;return;}
    if ((s_stall_ticks[slot]+=ticks)<30*60) return;
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
    if (s_stall_ticks[slot]<45*60) {
        if (s_stall_ticks[slot]-ticks<30*60) {
            chrStopFiring(chr);chr->actiontype=ACT_STAND;
            chr->ailist=ailistFindById(GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND);chr->aioffset=0;
        }
    } else chr->hidden|=CHRHIDDEN_REMOVE;
}

static void hordeUpdateStationPrompt(s32 me)
{
    if (me<0 || me>=4 || !g_playerPointers[me] || netPlayerIsSpectator(me) || gevrCoopDowned(me)) return;
    char prompt[100];
    s32 box_pad=s_box_pads[s_state.box_spot%HORDE_BOX_SPOTS];
    if (hordeNearPad(me,box_pad,HORDE_INTERACT_RANGE)) {
        const char *wname=s_box_pool[s_state.box_weapon%HORDE_BOX_POOL_SIZE].name;
        if (s_state.box_state==HORDE_BOX_IDLE)
            snprintf(prompt,sizeof(prompt),"MYSTERY BOX [950 PTS] (%u PTS) - PRESS B/GRIP",s_state.credits[me]);
        else if (s_state.box_state==HORDE_BOX_ROLLING)
            snprintf(prompt,sizeof(prompt),"MYSTERY BOX ROLLING: %s...",wname);
        else if (s_state.box_state==HORDE_BOX_READY)
            snprintf(prompt,sizeof(prompt),s_state.box_buyer==(unsigned)me
                ?"TAKE %s (%us) - PRESS B/GRIP":"MYSTERY BOX: %s (%us)",wname,(s_state.box_timer+59)/60);
        else
            snprintf(prompt,sizeof(prompt),"TEDDY BEAR! BOX MOVING - 950 PTS REFUNDED");
        unsigned key=0x100u|(s_state.box_state<<4)|s_state.box_weapon|((s_state.box_timer+59)/60<<8);
        if (s_shown_prompt!=key) {
            s32 prev=get_cur_playernum();
            set_cur_player(me);gevrHudmsgBottomLive(prompt);set_cur_player(prev);
            s_shown_prompt=key;
        }
        return;
    }
    for (unsigned w=0;w<HORDE_WALL_COUNT;w++) if (hordeNearPad(me,s_wall_pads[w],HORDE_INTERACT_RANGE)) {
        s32 prev=get_cur_playernum();
        set_cur_player(me);
        s32 owned=w<5 && bondinvHasInvItem(s_wall_items[w].item);
        uint32_t cost=hordeWallCost(w,owned);
        snprintf(prompt,sizeof(prompt),owned?"AMMO REFILL: %s [%u PTS] (%u PTS) - PRESS B/GRIP"
            :"WALL BUY: %s [%u PTS] (%u PTS) - PRESS B/GRIP",
            s_wall_items[w].name,cost,s_state.credits[me]);
        unsigned key=0x200u|(w<<2)|(owned?1u:0u);
        if (s_shown_prompt!=key) {
            gevrHudmsgBottomLive(prompt);
            s_shown_prompt=key;
        }
        set_cur_player(prev);
        return;
    }
    s_shown_prompt=0;
}

void gevrHordeTick(int ticks)
{
    if (!netHordeRunning()) return;
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
                s_dead_ticks[i]+=ticks;
                if (s_dead_ticks[i]>5*60) chr->hidden|=CHRHIDDEN_REMOVE;
            }
            if (!chr->model) s_id[i]=0;
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
    if ((s_state.phase==HORDE_PREPARE || s_state.phase==HORDE_INTERMISSION)
        && s_resupplied!=s_state.completed) hordeResupply();
    /* Announce each wave and the defeat once, and keep a break's countdown
     * on screen while it runs; the pause menu keeps the running stats. A wave
     * message every second sat mid-view in VR all game. */
    s32 counting=s_state.phase==HORDE_PREPARE || s_state.phase==HORDE_INTERMISSION;
    unsigned secs=counting?(s_state.timer+59)/60:0;
    if (s_shown_phase!=s_state.phase || s_shown_wave!=s_state.wave || (counting && s_shown_secs!=secs)) {
        s32 prev=get_cur_playernum();
        char text[100];gevrHordeText(text,sizeof(text));
        if (me>=0 && me<4 && g_playerPointers[me]) {
            set_cur_player(me);
            if (counting) gevrHudmsgBottomLive(text); else hudmsgBottomShow(text);
            set_cur_player(prev);
        }
        s_shown_phase=s_state.phase;s_shown_wave=s_state.wave;s_shown_secs=secs;
    }
    hordeUpdateStationPrompt(me);
}
