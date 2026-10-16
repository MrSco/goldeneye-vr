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
extern PropRecord *chrSpawnAtCoord(s32 body,s32 head,coord3d *pos,StandTile *stan,f32 angle,AIRecord *ai,s32 flags);

static HordeState s_state;
static unsigned s_id[HORDE_GUARD_SLOTS];
static s32 s_counted[HORDE_GUARD_SLOTS], s_dead_ticks[HORDE_GUARD_SLOTS];
static coord3d s_last_pos[HORDE_GUARD_SLOTS];
static s32 s_stall_ticks[HORDE_GUARD_SLOTS];
static s32 s_pads[128], s_pad_count, s_body;
static unsigned s_resupplied, s_shown_revision;
static s32 s_hud_ticks;
static struct {
    SetupIntroSpawn spawn;
    struct SetupIntroItem item;
    struct SetupIntroAmmo ammo;
    SetupIntroEmpty end;
} s_intro;

const HordeState *gevrHordeState(void) { return &s_state; }
void gevrHordeText(char *text,unsigned size) { hordeStatus(&s_state,text,size); }
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
    s_resupplied=~0u;s_shown_revision=~0u;s_hud_ticks=0;
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
    s_state.revision++;netHordeSend(-1);
}

static void hordeResupply(void)
{
    s32 me=netGetLocalSlot(),prev=get_cur_playernum();
    if (me<0 || me>=4 || !g_playerPointers[me] || !g_playerPointers[me]->prop) return;
    if (netPlayerIsSpectator(me)) return;
    set_cur_player(me);
    bondinvAddInvItem(ITEM_WPPK);
    if (s_state.completed>=2) bondinvAddInvItem(ITEM_SKORPION);
    if (s_state.completed>=4) bondinvAddInvItem(ITEM_AK47);
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
        for (s32 p=0;p<4;p++) if(netLobbySlotConnected(p) && !netPlayerIsSpectator(p)
            && g_playerPointers[p] && g_playerPointers[p]->prop && !gevrCoopDowned(p)) {
            coord3d *pos=&g_playerPointers[p]->prop->pos;
            f32 dx=pos->x-pad->pos.x,dy=pos->y-pad->pos.y,dz=pos->z-pad->pos.z;
            /* Leave space around every player, including on adjacent floors. */
            if (dx*dx+dz*dz<900.0f*900.0f && dy*dy<300.0f*300.0f) safe=FALSE;
            StandTile *tile=g_playerPointers[p]->prop->stan;
            if (tile && dy*dy<150.0f*150.0f && stanTestLineUnobstructed(&tile,pos->x,pos->z,pad->pos.x,pad->pos.z,
                CDTYPE_OBJS|CDTYPE_DOORS|CDTYPE_PATHBLOCKER|CDTYPE_AIOPAQUE,140,140,0,1) && tile==pad->stan) safe=FALSE;
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
        s32 weapon=s_state.wave<3?ITEM_TT33:s_state.wave<6?ITEM_SKORPION:ITEM_AK47;
        s32 model=weapon==ITEM_TT33?PROP_CHRTT33:weapon==ITEM_SKORPION?PROP_CHRSKORPION:PROP_CHRKALASH;
        /* Resupply provides ammunition; undroppable guns bound object growth. */
        if (!chrGiveWeapon(chr,model,weapon,PROPFLAG_AIUNDROPPABLE)) {
            chr->hidden|=CHRHIDDEN_REMOVE;return FALSE;
        }
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
                hordeEnemyGone(&s_state,chr->model!=NULL);
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
    if ((s_state.phase==HORDE_PREPARE || s_state.phase==HORDE_INTERMISSION)
        && s_resupplied!=s_state.completed) hordeResupply();
    s_hud_ticks-=ticks;
    if (s_hud_ticks<=0 || s_shown_revision!=s_state.revision) {
        s32 me=netGetLocalSlot(),prev=get_cur_playernum();
        char text[100];gevrHordeText(text,sizeof(text));
        if (me>=0 && me<4 && g_playerPointers[me]) {set_cur_player(me);hudmsgBottomShow(text);set_cur_player(prev);}
        s_hud_ticks=60;s_shown_revision=s_state.revision;
    }
}
