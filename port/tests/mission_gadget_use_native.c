#include <ultra64.h>
#undef GAME_TICKRATE
#include <bondtypes.h>
#include <bondconstants.h>
#include "bondview.h"
#include "assets/obseg/text/LgunE.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef HUDMESSAGEBOTTOM
#define HUDMESSAGEBOTTOM(text) ((void)(text))
static struct player player;
struct player *g_CurrentPlayer=&player;
static int g_gevrStereo=1,hasKey,ammo[256],granted,removed,keyReports,useReports,switches[2];
static void *g_musicSfxBufferPtr;
static struct { int AmmoType,MagSize; } sniperrifle_stats={7,5};
static ObjectRecord target;
static PropRecord targetProp;
u8 *langGet(s32 id) { (void)id;return NULL; }
bool bondinvHasGEKey(void) { return hasKey; }
/* gevr_romlayout.c: GoldenEye's use-on-object items, the bomb defuser and datathief */
static int gevrRomGadgetUse(int which) { return which ? ITEM_DATATHIEF : ITEM_BOMBDEFUSER; }
static int gevrRomCaseAmmo(void) { return AMMO_KNIFE; }   /* the weapon case's first ammo */
static void gevrCoopReportKeyCopy(void) { keyReports++; }
ALSoundState *sndPlaySfx(struct ALBankAlt_s *bank,s16 id,ALSoundState *p) { (void)bank;(void)id;(void)p;return NULL; }
static void sub_GAME_7F05D690(void) {}
static void currentPlayerEquipWeaponWrapper(GUNHAND hand,s32 item) { switches[hand]++;player.hands[hand].weapon_next_weapon=item; }
static void add_ammo_to_inventory(int type,int count,int a,int b) { (void)a;(void)b;ammo[type]+=count; }
static void bondinvAddInvItem(int item) { granted=item; }
static void bondinvRemoveItemByID(int item) { removed=item; }
static void set_sound_effect_for_weapontype_collection(int item) { (void)item; }
static void display_text_for_weapon_in_lower_left_corner(int item) { (void)item; }
static int check_cur_player_ammo_amount_in_inventory(int type) { return ammo[type]; }
static void give_cur_player_ammo(int type,int count) { ammo[type]=count; }
static int finds;
PropRecord *propFindForInteract(void) { finds++;return &targetProp; }
void gevrCoopReportGadgetUse(ObjectRecord *obj) { assert(obj==&target);useReports++; }
s32 g_GlobalTimer;
static s32 cur_player;
s32 get_cur_playernum(void) { return cur_player; }
/* QUEUE */
/* PRODUCTION */
static void use(GUNHAND hand,int var_s1) {
    struct hand *handptr=&player.hands[hand];
    PropRecord *temp_v0_8;
    /* USE_ITEM */
}
static void reset(int stereo) {
    memset(&player,0,sizeof(player)); memset(ammo,0,sizeof(ammo));memset(switches,0,sizeof(switches));
    memset(&target,0,sizeof(target));memset(&targetProp,0,sizeof(targetProp));
    targetProp.obj=&target;g_gevrStereo=stereo;hasKey=0;granted=removed=keyReports=useReports=finds=0;
    player.hands[0].weapon_next_weapon=ITEM_AK47;player.hands[1].weapon_next_weapon=ITEM_WPPK;
    player.hands[0].weapon_action_state=player.hands[1].weapon_action_state=GUN_ANIM_STATE_USE_ITEM;
}
int main(void) {
    for(int hand=0;hand<2;hand++) {
        reset(1);hasKey=1;int other=player.hands[1-hand].weapon_next_weapon;
        use(hand,ITEM_KEYANALYSERCASE);
        assert(player.copiedgoldeneye && keyReports==1);
        assert(switches[hand]==1 && player.hands[hand].weapon_next_weapon==ITEM_GOLDENEYEKEY);
        assert(!switches[1-hand] && player.hands[1-hand].weapon_next_weapon==other);
        reset(1);use(hand,ITEM_KEYANALYSERCASE);assert(!player.copiedgoldeneye && !switches[0] && !switches[1]);
        reset(1);other=player.hands[1-hand].weapon_next_weapon;use(hand,ITEM_WEAPONCASE);
        assert(granted==ITEM_SNIPERRIFLE && removed==ITEM_WEAPONCASE);
        assert(ammo[AMMO_KNIFE]==2 && ammo[AMMO_GRENADE]==2 && ammo[sniperrifle_stats.AmmoType]==5);
        assert(switches[hand]==1 && player.hands[hand].weapon_next_weapon==ITEM_SNIPERRIFLE);
        assert(!switches[1-hand] && player.hands[1-hand].weapon_next_weapon==other);
        const int devices[]={ITEM_BOMBDEFUSER,ITEM_DATATHIEF,ITEM_DOORDECODER,ITEM_EXPLOSIVEFLOPPY,ITEM_DATTAPE};
        for(unsigned i=0;i<sizeof(devices)/sizeof(devices[0]);i++) {
            /* #357: the use only queues; the search waits for the frame's own view */
            reset(1);use(hand,devices[i]);assert(!finds && !(target.state&0x40) && !useReports);
            gevrGadgetUseFlush();assert(finds==1 && (target.state&0x40) && useReports==1);
            gevrGadgetUseFlush();assert(finds==1 && useReports==1);   /* once per use */
            player.hands[hand].field_88C=1;use(hand,devices[i]);gevrGadgetUseFlush();assert(useReports==1);
            /* a use left from before a pause or a stage load is dropped */
            player.hands[hand].field_88C=0;target.state=0;use(hand,devices[i]);g_GlobalTimer+=11;
            gevrGadgetUseFlush();assert(useReports==1 && !(target.state&0x40));
            /* ... and one from the last level: the clock starts again near zero */
            g_GlobalTimer=500;use(hand,devices[i]);g_GlobalTimer=3;
            gevrGadgetUseFlush();assert(useReports==1 && !(target.state&0x40));
            /* another player's view does not take this one's use */
            use(hand,devices[i]);cur_player=1;gevrGadgetUseFlush();assert(useReports==1);
            cur_player=0;gevrGadgetUseFlush();assert(useReports==2);
            player.hands[hand].field_888=1;use(hand,devices[i]);
            assert(player.hands[hand].weapon_action_state==GUN_ANIM_STATE_IDLE);
        }
    }
    reset(0);hasKey=1;use(GUNLEFT,ITEM_KEYANALYSERCASE);
    assert(player.hands[GUNRIGHT].weapon_next_weapon==ITEM_GOLDENEYEKEY && player.hands[GUNLEFT].weapon_next_weapon==ITEM_UNARMED);
    reset(0);use(GUNLEFT,ITEM_WEAPONCASE);
    assert(player.hands[GUNRIGHT].weapon_next_weapon==ITEM_SNIPERRIFLE && player.hands[GUNLEFT].weapon_next_weapon==ITEM_UNARMED);
    puts("PASS: per-hand key/case results, mission device activation in the frame's view (#357), co-op reports, repeat/release and original flat behavior");
}
