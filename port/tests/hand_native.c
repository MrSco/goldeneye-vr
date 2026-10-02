#include "net_game.h"
#include "net_match.h"
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "bondview.h"
#include "gun.h"
#include "bondinv.h"
#include <string.h>
#define false 0
#define true 1
int gevrVrTriggerDown[2];
#define EXPORT __declspec(dllexport)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
struct player *g_CurrentPlayer;
static struct player player;
static InvItem pair;
static int online, mode, spectator, remote;
s32 get_scenario(void) { return 0; }
bool bondinvIsAliveWithFlag(void) { return 0; }
static int owned[ITEM_IDS_MAX], reserve[ITEM_IDS_MAX];
static int inventoryItems[8], inventoryCount;
s32 g_gevrStereo = 1;
bool netIsActive(void) { return online; }
int netActiveDualWield(void) { return mode; }
int netGetLocalSlot(void) { return 0; }
s32 get_cur_playernum(void) { return remote; }
s32 getPlayerCount(void) { return online ? 3 : 1; }
s32 gevrSpectating(void) { return spectator; }
s32 bondinvCountTotalItemsInInv(void) { return inventoryCount; }
s32 bondinvGetTextbyInvIndex(s32 i) { return inventoryItems[i]; }
u16 *bondinvGetNameByIndex(s32 i) { return (u16 *) "Carried item\n"; }
u16 *get_ptr_short_watch_text_for_item(ITEM_IDS item) { return (u16 *) "Weapon\n"; }
bool bondinvItemAvailable(ITEM_IDS item) { return owned[item]; }
bool bondinvItemAvailableForHand(s32 right, s32 left) {
    return left == ITEM_UNARMED || (pair.type == INV_ITEM_DUAL
        && right == pair.type_inv_item.type_dual.weapon_right
        && left == pair.type_inv_item.type_dual.weapon_left);
}
ITEM_IDS getCurrentPlayerWeaponId(GUNHAND hand) { return player.hands[hand].weaponnum; }
/* INSERT_NEXT_WEAPON */
ITEM_IDS get_item_in_hand_or_watch_menu(GUNHAND hand) { return getCurrentPlayerWeaponId(hand); }
s32 get_ammo_type_for_weapon(ITEM_IDS item) { return item > ITEM_KNIFE && item < ITEM_BOMBCASE && item != ITEM_TRIGGER ? item : 0; }
s32 get_ammo_in_hands_magazine(GUNHAND hand) { return player.hands[hand].weapon_ammo_in_magazine; }
s32 get_ammo_in_hands_weapon(GUNHAND hand) { return reserve[player.hands[hand].weaponnum]; }
s32 bondwalkItemHasAmmo(ITEM_IDS item) { return item == ITEM_FIST || item == ITEM_KNIFE || reserve[item] > 0; }
/* INSERT_HAND_REQUEST */
/* INSERT_HAND_INVENTORY */
static void settle(void) {
    for (int h = 0; h < 2; h++) {
        player.hands[h].weaponnum = gevrHandSelected(h);
        player.hands[h].weapon_animation_trigger = 0;
        player.hands[h].weapon_current_animation = 0;
        player.hands[h].weapon_action_state = GUN_ANIM_STATE_IDLE;
    }
}
static void reset(void) {
    memset(&player, 0, sizeof(player)); memset(&pair, 0, sizeof(pair));
    memset(owned, 0, sizeof(owned)); memset(reserve, 0, sizeof(reserve));
    gevrVrTriggerDown[0] = gevrVrTriggerDown[1] = 0;
    g_CurrentPlayer = &player; g_gevrStereo = 1; online = mode = spectator = remote = 0;
    player.ptr_hand_weapon_buffer[GUNLEFT] = (void *) 1;
    inventoryCount = 5;
    inventoryItems[0] = ITEM_WPPK; inventoryItems[1] = ITEM_AK47;
    inventoryItems[2] = ITEM_GRENADE; inventoryItems[3] = ITEM_CAMERA;
    inventoryItems[4] = ITEM_FIST;
    for (int i = 0; i < inventoryCount; i++) owned[inventoryItems[i]] = 1;
    player.hands[GUNRIGHT].weaponnum = ITEM_WPPK;
    player.hands[GUNLEFT].weaponnum = ITEM_GRENADE;
}
EXPORT int test_hand_cycles(void) {
    reset();
    int count = gevrWeaponPanelBuild(); CHECK(count == 5);
    for (int i = 0; i < count; i++) CHECK(s_gevrWpList[i].left == ITEM_UNARMED && s_gevrWpList[i].right != ITEM_UNARMED);
    count = gevrWeaponPanelBuildLeft(); CHECK(count == 3);
    for (int i = 0; i < count; i++) CHECK(s_gevrWpList[i].left != ITEM_CAMERA && s_gevrWpList[i].left != ITEM_FIST);
    gevrCycleHandWeapon(GUNRIGHT, 1);
    CHECK(gevrHandSelected(GUNRIGHT) == ITEM_AK47);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_GRENADE);
    // Repeated taps use the pending gun, even before its animation completes.
    gevrCycleHandWeapon(GUNRIGHT, 1); CHECK(player.hands[GUNRIGHT].weapon_next_weapon == ITEM_GRENADE);
    gevrCycleHandWeapon(GUNRIGHT, -1); CHECK(player.hands[GUNRIGHT].weapon_next_weapon == ITEM_AK47);
    gevrCycleHandWeapon(GUNRIGHT, -1); CHECK(player.hands[GUNRIGHT].weapon_next_weapon == ITEM_WPPK);
    gevrCycleHandWeapon(GUNRIGHT, 1); CHECK(player.hands[GUNRIGHT].weapon_next_weapon == ITEM_AK47);
    settle(); gevrCycleHandWeapon(GUNLEFT, 1);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_UNARMED);
    settle(); gevrCycleHandWeapon(GUNLEFT, 1);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_WPPK);
    CHECK(gevrHandSelected(GUNRIGHT) == ITEM_AK47);
    settle(); gevrCycleHandWeapon(GUNLEFT, -1);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_UNARMED);
    settle(); gevrCycleHandWeapon(GUNLEFT, -1);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_GRENADE);
    CHECK(player.ptr_inventory_first_in_cycle == NULL);
    // Pair-only weapons become distinct single entries, with no allocation.
    pair.type = INV_ITEM_DUAL; pair.next = &pair;
    pair.type_inv_item.type_dual.weapon_right = ITEM_SNIPERRIFLE;
    pair.type_inv_item.type_dual.weapon_left = ITEM_SNIPERRIFLE;
    player.ptr_inventory_first_in_cycle = &pair;
    CHECK(gevrWeaponPanelBuild() == 6); CHECK(gevrWeaponPanelBuildLeft() == 4);
    CHECK(pair.next == &pair); CHECK(pair.type == INV_ITEM_DUAL);
    // Actual selector equip helper only requests the selected hand.
    GevrWpEntry e = {.right = ITEM_CAMERA, .left = ITEM_SNIPERRIFLE};
    settle(); gevrWeaponPanelEquip(GUNLEFT, &e, 1);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_SNIPERRIFLE);
    CHECK(gevrHandSelected(GUNRIGHT) == ITEM_AK47);
    settle(); gevrWeaponPanelEquip(GUNRIGHT, &e, 1);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_SNIPERRIFLE);
    // Multiplayer host rules and remote copies retain their restrictions.
    reset(); online = 1; mode = NET_DUAL_OFF;
    CHECK(!gevrLeftPanelAvailable()); gevrCycleHandWeapon(GUNLEFT, 1);
    CHECK(!player.hands[GUNLEFT].weapon_animation_trigger);
    mode = NET_DUAL_ANY; CHECK(gevrLeftPanelAvailable()); CHECK(gevrWeaponPanelBuildLeft() == 3);
    mode = NET_DUAL_DOUBLES; CHECK(gevrWeaponPanelBuildLeft() == 1);
    pair.type = INV_ITEM_DUAL; pair.next = &pair;
    pair.type_inv_item.type_dual.weapon_right = ITEM_WPPK;
    pair.type_inv_item.type_dual.weapon_left = ITEM_WPPK;
    player.ptr_inventory_first_in_cycle = &pair;
    CHECK(gevrWeaponPanelBuildLeft() == 2);
    remote = 1; CHECK(!gevrLeftPanelAvailable()); gevrCycleHandWeapon(GUNRIGHT, 1);
    CHECK(!player.hands[GUNRIGHT].weapon_animation_trigger);
    remote = 0; spectator = 1; gevrCycleHandWeapon(GUNRIGHT, 1);
    CHECK(!player.hands[GUNRIGHT].weapon_animation_trigger);
    return 0;
}
s32 currentPlayerEquipWeaponWrapper(GUNHAND hand, s32 item) { player.hands[hand].weapon_next_weapon = item; return 0; }
static void validateNativePair(GUNHAND hand) {
    struct hand *handptr = &player.hands[hand];
    struct hand *temp_v1_5 = &player.hands[1 - hand];
    /* INSERT_PAIR_VALIDATION */
}
static s32 lvlGetControlsLockedFlag(void) { return 0; }
static int D_80032458;
void autoadvance_on_deplete_all_ammo(void) { gevrAutoAdvanceHand(0); gevrAutoAdvanceHand(1); }
static void tickDepletion(GUNHAND hand) {
    struct hand *handptr = &player.hands[hand], *sp1BC;
    int sp1C4 = get_ammo_type_for_weapon(getCurrentPlayerWeaponId(hand));
    int temp_v0_3;
    /* INSERT_DEPLETION */
}
EXPORT int test_hand_depletion(void) {
    reset(); reserve[ITEM_AK47] = 50;
    player.hands[GUNLEFT].weapon_ammo_in_magazine = 1;
    player.trigger_released = 1;
    tickDepletion(GUNRIGHT);
    CHECK(gevrHandSelected(GUNRIGHT) == ITEM_AK47);
    CHECK(player.hands[GUNLEFT].weaponnum == ITEM_GRENADE);
    CHECK(!player.hands[GUNLEFT].weapon_animation_trigger);
    CHECK(player.hands[GUNLEFT].weapon_ammo_in_magazine == 1);
    reset(); reserve[ITEM_WPPK] = 7; player.hands[GUNRIGHT].weapon_ammo_in_magazine = 3;
    pair.type=INV_ITEM_DUAL;pair.type_inv_item.type_dual.weapon_right=pair.type_inv_item.type_dual.weapon_left=ITEM_WPPK;
    player.trigger_released = 0; gevrVrTriggerDown[GUNRIGHT] = 1; tickDepletion(GUNLEFT);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_WPPK);
    CHECK(!player.hands[GUNRIGHT].weapon_animation_trigger);
    reset(); gevrAutoAdvanceHand(GUNLEFT);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_UNARMED);
    reset(); player.hands[GUNLEFT].weapon_ammo_in_magazine = 1;
    gevrAutoAdvanceHand(GUNLEFT); CHECK(!player.hands[GUNLEFT].weapon_animation_trigger);
    player.hands[GUNLEFT].weapon_ammo_in_magazine = 0; reserve[ITEM_GRENADE] = 2;
    gevrAutoAdvanceHand(GUNLEFT); CHECK(!player.hands[GUNLEFT].weapon_animation_trigger);
    reset(); player.hands[GUNRIGHT].weaponnum = ITEM_TANKSHELLS;
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 5; reserve[ITEM_WPPK] = 7;
    gevrAutoAdvanceHand(GUNRIGHT); CHECK(player.hands[GUNRIGHT].weapon_next_weapon == ITEM_WPPK);
    CHECK(!player.hands[GUNLEFT].weapon_animation_trigger);
    // Native swap validation must not reject a mix or holster its other hand.
    reset(); player.hands[GUNRIGHT].weapon_next_weapon = ITEM_AK47;
    validateNativePair(GUNRIGHT); CHECK(!player.hands[GUNLEFT].weapon_next_weapon);
    player.hands[GUNLEFT].weapon_next_weapon = ITEM_SNIPERRIFLE;
    validateNativePair(GUNLEFT); CHECK(player.hands[GUNLEFT].weapon_next_weapon == ITEM_SNIPERRIFLE);
    g_gevrStereo = 0; validateNativePair(GUNLEFT);
    CHECK(player.hands[GUNLEFT].weapon_next_weapon == ITEM_UNARMED);
    reset(); owned[ITEM_TRIGGER] = 1; player.hands[GUNRIGHT].weaponnum = ITEM_REMOTEMINE;
    gevrAutoAdvanceHand(GUNRIGHT); CHECK(player.hands[GUNRIGHT].weapon_next_weapon == ITEM_TRIGGER);
    CHECK(!player.hands[GUNLEFT].weapon_animation_trigger);
    return 0;
}

static u32 inputTime;
static int aDown, xDown, rightGrip, leftGrip;
s32 gevrWeaponPanelOpen, gevrWeaponPanelLeft, gevrWeaponPanelRelease;
static int gevrReturnPrompt, gevrSwallowX;
#define GEVR_WEAPON_PANEL_HOLD_MS 350
#define LOGI(...) ((void) 0)
static u32 SDL_GetTicks(void) { return inputTime; }
static int get_button_state(int hand, const char *name) {
    if (!strcmp(name, "a")) return aDown;
    if (!strcmp(name, "x")) return xDown;
    return hand ? rightGrip : leftGrip;
}
static unsigned tickInput(int stereoplay, int fitting) {
    struct { unsigned button; } pad = {.button = A_BUTTON}, *npad = &pad;
    /* INSERT_CYCLING_INPUT */
    return pad.button;
}
EXPORT int test_hand_input(void) {
    reset(); inputTime = 1000;
    aDown = xDown = rightGrip = leftGrip = 0; tickInput(0, 0);
    aDown = 1; CHECK(!(tickInput(1, 0) & A_BUTTON));
    inputTime += 80; aDown = 0; CHECK(!(tickInput(1, 0) & A_BUTTON));
    CHECK(gevrHandSelected(GUNRIGHT) == ITEM_AK47);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_GRENADE);
    settle(); rightGrip = 1; inputTime += 80; aDown = 1; tickInput(1, 0);
    inputTime += 80; aDown = 0;
    CHECK(!(tickInput(1, 0) & (A_BUTTON | Z_TRIG)));
    CHECK(gevrHandSelected(GUNRIGHT) == ITEM_WPPK);
    settle(); rightGrip = 0;
    xDown = 1; inputTime += 80; tickInput(1, 0);
    inputTime += 80; xDown = 0; tickInput(1, 0);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_UNARMED);
    CHECK(gevrHandSelected(GUNRIGHT) == ITEM_WPPK);
    settle(); leftGrip = 1; xDown = 1; inputTime += 80; tickInput(1, 0);
    inputTime += 80; xDown = 0; tickInput(1, 0);
    CHECK(gevrHandSelected(GUNLEFT) == ITEM_GRENADE);
    settle(); leftGrip = 0;
    aDown = 1; inputTime += 80; tickInput(1, 0);
    inputTime += 351; tickInput(1, 0);
    CHECK(gevrWeaponPanelOpen && !gevrWeaponPanelLeft);
    aDown = 0; tickInput(1, 0);
    CHECK(gevrWeaponPanelRelease && !gevrWeaponPanelOpen);
    CHECK(!player.hands[GUNRIGHT].weapon_animation_trigger);
    gevrWeaponPanelRelease = 0;
    xDown = 1; inputTime += 80; tickInput(1, 0);
    inputTime += 351; tickInput(1, 0);
    CHECK(gevrWeaponPanelOpen && gevrWeaponPanelLeft);
    xDown = 0; tickInput(1, 0);
    CHECK(gevrWeaponPanelRelease && !gevrWeaponPanelOpen);
    CHECK(!player.hands[GUNLEFT].weapon_animation_trigger);
    // Disabled off hand never falls back to native A or changes the right hand.
    reset(); online = 1; mode = NET_DUAL_OFF;
    xDown = 1; inputTime += 80; tickInput(1, 0);
    inputTime += 80; xDown = 0; CHECK(!(tickInput(1, 0) & A_BUTTON));
    CHECK(!player.hands[GUNRIGHT].weapon_animation_trigger);
    CHECK(!player.hands[GUNLEFT].weapon_animation_trigger);
    CHECK(tickInput(0, 0) & A_BUTTON); // Menus/flat play retain the native button.
    CHECK(tickInput(1, 1) & A_BUTTON); // Gun fitting retains its controls.
    return 0;
}

int bondinvHasInvItem(ITEM_IDS item) { return owned[item]; }
bool bondinvHasDualWeapon(ITEM_IDS right,ITEM_IDS left) { return bondinvItemAvailableForHand(right,left); }
int bondinvAddInvItem(ITEM_IDS item) { if(owned[item])return 0;owned[item]=1;inventoryItems[inventoryCount++]=item;return 1; }
int bondinvAddDoublesInvItem(ITEM_IDS right,ITEM_IDS left) {
    if(bondinvHasDualWeapon(right,left))return 0;
    pair.type=INV_ITEM_DUAL;pair.next=&pair;
    pair.type_inv_item.type_dual.weapon_right=right;pair.type_inv_item.type_dual.weapon_left=left;
    player.ptr_inventory_first_in_cycle=&pair;return 1;
}
u32 bondwalkItemCheckBitflags(ITEM_IDS item,u32 flags) { (void)flags;return item==ITEM_WPPK; }
/* INSERT_PICKUP */
static int pickup(int item) {
    WeaponObjRecord weapon={0};PropRecord prop={0};
    prop.type=PROP_TYPE_WEAPON;prop.obj=(ObjectRecord*)&weapon;
    weapon.type=PROPDEF_COLLECTABLE;weapon.weaponnum=item;weapon.LinkedWeaponType=-1;
    int before=gevrWeaponOwned(item),added=bondinvAddWeaponByProp(&prop);
    reserve[item]+=7;gevrWeaponPickedUp(item,before);return added;
}
EXPORT int test_pickup_ownership(void) {
    reset();CHECK(!gevrHandItemAllowed(GUNLEFT,ITEM_WPPK));
    CHECK(pickup(ITEM_WPPK));CHECK(gevrHandItemAllowed(GUNLEFT,ITEM_WPPK));
    CHECK(!player.hands[GUNRIGHT].weapon_animation_trigger && !player.hands[GUNLEFT].weapon_animation_trigger);
    CHECK(!pickup(ITEM_WPPK)); // Two is the maximum; extra copies only grant ammo.
    reset();player.hands[GUNRIGHT].weaponnum=ITEM_AK47;player.hands[GUNLEFT].weaponnum=ITEM_GRENADE;
    CHECK(!gevrHandItemAllowed(GUNLEFT,ITEM_AK47));CHECK(pickup(ITEM_AK47));
    CHECK(gevrHandItemAllowed(GUNLEFT,ITEM_AK47));CHECK(!player.hands[GUNRIGHT].weapon_animation_trigger);
    CHECK(!player.hands[GUNLEFT].weapon_animation_trigger);
    gunRequestHandWeaponChange(GUNRIGHT,ITEM_WPPK,1);CHECK(!pickup(ITEM_AK47));
    CHECK(gevrHandSelected(GUNRIGHT)==ITEM_WPPK); // Pending manual selection also survives a pickup.
    reset();memset(owned,0,sizeof(owned));owned[ITEM_FIST]=1;inventoryCount=1;inventoryItems[0]=ITEM_FIST;
    player.hands[GUNRIGHT].weaponnum=ITEM_FIST;
    CHECK(pickup(ITEM_WPPK));CHECK(gevrHandSelected(GUNRIGHT)==ITEM_WPPK);
    CHECK(!player.hands[GUNLEFT].weapon_animation_trigger);CHECK(!gevrHandItemAllowed(GUNLEFT,ITEM_WPPK));
    CHECK(pickup(ITEM_WPPK));CHECK(gevrHandItemAllowed(GUNLEFT,ITEM_WPPK));
    CHECK(gevrHandSelected(GUNLEFT)==ITEM_GRENADE);
    settle();gunRequestHandWeaponChange(GUNRIGHT,ITEM_FIST,1);settle();
    CHECK(pickup(ITEM_AK47));CHECK(gevrHandSelected(GUNRIGHT)==ITEM_FIST); // Deliberately selected fists.
    reset();player.hands[GUNRIGHT].weaponnum=ITEM_FIST;reserve[ITEM_WPPK]=1;
    CHECK(pickup(ITEM_AK47));CHECK(!player.hands[GUNRIGHT].weapon_animation_trigger);
    reset();owned[ITEM_SNIPERRIFLE]=0;CHECK(pickup(ITEM_SNIPERRIFLE));CHECK(!player.hands[GUNRIGHT].weapon_animation_trigger);
    reset();online=1;mode=NET_DUAL_OFF;CHECK(pickup(ITEM_WPPK));CHECK(!gevrLeftPanelAvailable());
    mode=NET_DUAL_DOUBLES;CHECK(gevrHandItemAllowed(GUNLEFT,ITEM_WPPK));CHECK(!gevrHandItemAllowed(GUNLEFT,ITEM_AK47));
    mode=NET_DUAL_ANY;CHECK(gevrHandItemAllowed(GUNLEFT,ITEM_AK47));
    CHECK(!gevrWeaponUsesCopies(ITEM_GRENADE));CHECK(!gevrWeaponUsesCopies(ITEM_CAMERA));
    CHECK(gevrWeaponUsesCopies(ITEM_AK47));CHECK(gevrWeaponUsesCopies(ITEM_WPPK));
    reset();player.hands[GUNRIGHT].weaponnum=ITEM_FIST;owned[ITEM_SNIPERRIFLE]=1;
    gevrWeaponPickedUp(ITEM_SNIPERRIFLE,0);CHECK(!player.hands[GUNRIGHT].weapon_animation_trigger); // Dry weapon.
    return 0;
}
static ModelNode cuff_nodes[6];
static union ModelRwData cuff_data[6];
static int character_body;
u16 get_player_mp_char_body(s32 slot) { (void)slot;return character_body; }
s32 fileGetBondForCurrentFolder(void) { return 0; }
union ModelRwData *modelGetNodeRwData(Model *model,ModelNode *node) {
    (void)model;return &cuff_data[node-cuff_nodes];
}
/* INSERT_CUFF */
/* INSERT_SELECT_CUFF */
EXPORT int test_character_sleeves(void) {
    CHECK(gevrMultiplayerCuff(BODY_Brosnan_Tuxedo)==CUFF_BROSNAN);
    CHECK(gevrMultiplayerCuff(BODY_Jungle_Commando)==CUFF_JUNGLE);
    CHECK(gevrMultiplayerCuff(BODY_Parka)==CUFF_SNOW);
    CHECK(gevrMultiplayerCuff(BODY_Scientist_1_Male)==CUFF_BOILER);
    CHECK(gevrMultiplayerCuff(BODY_Male_Mishkin)==CUFF_BLUE);
    reset();online=1;player.bondtype=CUFF_JUNGLE;
    Model model={0};ModelFileHeader header={0};ModelNode *switches[9]={0};
    for(int i=0;i<6;i++)switches[i+3]=&cuff_nodes[i];header.Switches=switches;
    int bodies[]={BODY_Brosnan_Tuxedo,BODY_Scientist_1_Male,BODY_Parka,BODY_Male_Mishkin,BODY_Jungle_Commando};
    int indices[]={1,0,5,3,4};
    for(int b=0;b<5;b++) {
        character_body=bodies[b];memset(cuff_data,0,sizeof(cuff_data));bondviewSelectCuff(&model,&header,3);
        for(int i=0;i<6;i++)CHECK(*(s32*)&cuff_data[i]==(i==indices[b]));
        CHECK(player.bondtype==CUFF_JUNGLE); // No mutation of single-player stage state.
    }
    online=0;character_body=BODY_Parka;bondviewSelectCuff(&model,&header,3);
    CHECK(*(s32*)&cuff_data[4] && !*(s32*)&cuff_data[5]);
    return 0;
}
