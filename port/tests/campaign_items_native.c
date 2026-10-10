#include <ultra64.h>
#include <bondtypes.h>
#include "game/player.h"
#include "game/objecthandler.h"
#include "game/propobj.h"
#include "game/gun.h"
#include "aicommands2.h"
#include "net/netbuf.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define NET_COOP_HELD_MAX 8
#define LOG_NOTE 1
#define LOG_ERROR 3
static struct player players[4];
struct player *g_CurrentPlayer;
static s32 current, host = 1, coop = 1, objective_count;
struct stagesetup g_CurrentSetup;
struct objective_entry *objective_ptrs[10];
static ObjectRecord tagged[10];
static PropRecord props[10];
static InvItem inventory[4][10];
static s32 s_held[4][8];
static u8 s_held_count[4];
static u64 s_mission_check_us;
static s8 s_copy_weapon[8][2];
static WeaponObjRecord held[2];
static PropRecord handprops[2];
static Model models[2];
static WeaponStats stats;
static int created, jumped, overflow;
static s32 Offset;
static AIRecord *AiListp;
s32 get_cur_playernum(void) { return current; }
void set_cur_player(s32 slot) { current=slot; g_CurrentPlayer=&players[slot]; }
int netGetLocalSlot(void) { return 0; }
bool netIsHost(void) { return host; }
int netCoopActive(void) { return coop; }
bool netSlotOccupied(int slot) { return slot < 4; }
void sysLogPrintf(s32 level, const char *fmt, ...) { if (level==LOG_ERROR) overflow++; }
ObjectRecord *objFindByTagId(s32 tag) { return tag>=0 && tag<10 ? &tagged[tag] : NULL; }
s32 objectiveGetCount(void) { return objective_count; }
s32 sizepropdef(PropDefHeaderRecord *def) { return sizeof(MissionObjectiveRecord)/4; }
s32 chraiGoToLabel(AIRecord *list, s32 offset, u8 label) { jumped=1; return offset; }
enum PROP getPropForHeldItem(ITEM_IDS item) { return item==ITEM_UNARMED ? -1 : 1; }
void chrSetWeaponFlag4(ChrRecord *chr, GUNHAND hand) {}
void objFreePermanently(ObjectRecord *obj, bool freeprop) {
    for (int hand=0;hand<2;hand++) if (obj==(ObjectRecord*)&held[hand])
        players[1].prop->chr->weapons_held[hand]=NULL;
}
PropRecord *chrGiveWeapon(ChrRecord *chr, s32 model, ITEM_IDS item, s32 flags) {
    int hand=(flags & PROPFLAG_WEAPON_LEFTHANDED) ? GUNLEFT : GUNRIGHT;
    held[hand].weaponnum=item; held[hand].model=&models[hand]; models[hand].scale=1;
    handprops[hand].weapon=&held[hand]; chr->weapons_held[hand]=&handprops[hand];
    created++; return &handprops[hand];
}
float netGunSizeFactor(int mode) { return 1; }
int netActiveGunSize(void) { return 0; }
WeaponStats *get_ptr_item_statistics(ITEM_IDS item) { return &stats; }
/* INSERT_ITEMS */

static int collected(int tag) {
    AiIFBondCollectedObjectRecord cmd={0}; cmd.cmd=AI_IFBondCollectedObject; cmd.OBJECT_TAG=tag;
    AiListp=(AIRecord*)&cmd; Offset=0; jumped=0;
    switch (cmd.cmd) { /* INSERT_COLLECTED_CASE */ }
    return jumped;
}
static void receive_tags(int slot, s32 *tags, int n) {
    u8 raw[64]; struct netbuf buffer={.data=raw,.size=sizeof(raw)}, *b=&buffer;
    netbufStartWrite(b); netbufWriteU8(b,n);
    for (int i=0;i<n;i++) netbufWriteS32(b,tags[i]);
    b->size=b->wp; netbufStartRead(b);
    switch (5) { /* INSERT_HELD_CASE */ }
}
static void give(int player, int tag) {
    InvItem *item=&inventory[player][tag]; item->type=INV_ITEM_PROP;
    item->type_inv_item.type_prop.prop=&props[tag];
    InvItem *first=players[player].ptr_inventory_first_in_cycle;
    if (first) { item->next=first; item->prev=first->prev; first->prev->next=item; first->prev=item; }
    else { item->next=item->prev=item; players[player].ptr_inventory_first_in_cycle=item; }
}
static void documents(void) {
    /* A script-only list, duplicate references, a PRINT string and an EndList. */
    u8 list[]={AI_PRINT,'x',0,AI_IFBondCollectedObject,0,1,
               AI_IFBondCollectedObject,1,1,AI_IFBondCollectedObject,0,1,AI_EndList};
    AIListRecord lists[2]={{0}}; lists[0].ailist=(AIRecord*)list; g_CurrentSetup.ailists=lists;
    for(int i=0;i<10;i++) { tagged[i].prop=&props[i]; }
    s32 tags[8];
    for(int owner0=0;owner0<4;owner0++) for(int owner1=0;owner1<4;owner1++) {
        memset(inventory,0,sizeof(inventory)); memset(s_held_count,0,sizeof(s_held_count));
        for(int p=0;p<4;p++) players[p].ptr_inventory_first_in_cycle=NULL;
        give(owner0,0);
        for(int p=1;p<4;p++) receive_tags(p,tags,gevrCoopHeldObjectiveTags(tags,8,p));
        for(int context=0;context<4;context++) { set_cur_player(context); assert(!collected(1)); }
        give(owner1,1); give(owner1,9); /* unrelated gun does not consume the packet */
        for(int p=1;p<4;p++) {
            set_cur_player(3); int n=gevrCoopHeldObjectiveTags(tags,8,p);
            assert(current==3 && n<=2); receive_tags(p,tags,n);
        }
        for(int context=0;context<4;context++) {
            set_cur_player(context); assert(collected(0) && collected(1)); assert(current==context);
        }
    }
    /* Local solo semantics; inventory loss replaces a previous client report. */
    coop=0; set_cur_player(0); players[0].ptr_inventory_first_in_cycle=NULL;
    assert(!collected(0)); give(0,0); assert(collected(0)); assert(!collected(1)); coop=1;
    for(int p=1;p<4;p++) { players[p].ptr_inventory_first_in_cycle=NULL; receive_tags(p,tags,0); }
    assert(!collected(1));
    /* Direct collect/deposit objective criteria still report and deduplicate. */
    MissionObjectiveRecord criteria[3]={{0}};
    criteria[0].type=PROPDEF_OBJECTIVE_COLLECT_OBJECT; criteria[0].ObjRefID=0;
    criteria[1].type=PROPDEF_OBJECTIVE_DEPOSIT_OBJECT; criteria[1].ObjRefID=1;
    criteria[2].type=PROPDEF_OBJECTIVE_END;
    objective_ptrs[0]=(struct objective_entry*)criteria; objective_count=1;
    give(0,1); assert(gevrCoopHeldObjectiveTags(tags,8,0)==2 && !overflow);
    assert(gevrCoopHeldObjectiveTags(tags,1,0)==1 && overflow);
}
static void mines(void) {
    PropRecord playerprop={0}; ChrRecord chr={0}; playerprop.chr=&chr; players[1].prop=&playerprop;
    int items[]={ITEM_REMOTEMINE,ITEM_TIMEDMINE,ITEM_PROXIMITYMINE,ITEM_BOMBCASE,
                 ITEM_BUG,ITEM_MICROCAMERA,ITEM_PLASTIQUE};
    for(int hand=0;hand<2;hand++) for(int i=0;i<7;i++) {
        coop=1; int before=created;
        netSyncCopyHand(&players[1],1,hand,items[i],0);
        netSyncCopyHand(&players[1],1,hand,items[i],1);
        assert(created==before && !chr.weapons_held[hand]);
        assert(players[1].hands[hand].weaponnum==items[i]);
    }
    for(int hand=0;hand<2;hand++) {
        coop=1;
        netSyncCopyHand(&players[1],1,hand,ITEM_AK47,1); assert(chr.weapons_held[hand]);
        netSyncCopyHand(&players[1],1,hand,ITEM_REMOTEMINE,0); assert(!chr.weapons_held[hand]);
        coop=0; netSyncCopyHand(&players[1],1,hand,ITEM_TIMEDMINE,0); assert(chr.weapons_held[hand]);
        coop=1; netSyncCopyHand(&players[1],1,hand,ITEM_TIMEDMINE,0); assert(!chr.weapons_held[hand]);
        netSyncCopyHand(&players[1],1,hand,ITEM_UNARMED,0); assert(!chr.weapons_held[hand]);
    }
}
static ChrRecord guards[6];
ChrRecord *g_ChrSlots=guards;
s32 get_numguards(void) { return 6; }
bool bgRoomsSharePortal(s32 a,s32 b) { return 0; }
/* INSERT_SCAN */
static void natalya(void) {
    Model models[6]={{0}}; PropRecord bodies[6]={{0}}; StandTile tiles[6];
    for(int i=0;i<6;i++) {
        tiles[i].room=1; guards[i].prop=&bodies[i]; guards[i].model=&models[i];
        bodies[i].stan=&tiles[i]; bodies[i].type=PROP_TYPE_VIEWER; guards[i].chrnum=i;
    }
    /* The first four are live player bodies. With no enemy, continue scanning. */
    ChrRecord *self=&guards[4]; self->chrpreset1=-1;
    guards[5].model=NULL;
    assert(!sub_GAME_7F033B38(self,3000) && self->chrpreset1==-1);
    guards[5].model=&models[5]; bodies[5].type=PROP_TYPE_CHR;
    assert(sub_GAME_7F033B38(self,3000) && self->chrpreset1==5);
    guards[5].actiontype=ACT_DEAD; assert(!sub_GAME_7F033B38(self,3000));
    guards[5].actiontype=ACT_DIE; assert(!sub_GAME_7F033B38(self,3000));
    guards[5].actiontype=ACT_STAND; tiles[5].room=2; assert(!sub_GAME_7F033B38(self,3000));
    tiles[5].room=1; bodies[5].pos.x=4000; assert(!sub_GAME_7F033B38(self,3000));
}
int main(void) {
    set_cur_player(0); documents(); mines(); natalya();
    puts("PASS: both-hand mine equip, gun/DM controls, script/direct objective tags, four owners/contexts, NPC scan excludes player bodies");
}
