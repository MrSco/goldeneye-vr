#include <cassert>
#include <cstdio>
#include <vector>
using s32 = int;
using f32 = float;
using ITEM_IDS = int;
enum GUNHAND { GUNRIGHT, GUNLEFT };
enum { ITEM_UNARMED, GUN_ANIM_STATE_SWITCH_SWAP = 20, GUN_ANIM_STATE_SWITCH_HOLD,
       WEAPONSTATBITFLAG_HIDE_AMMO_DISPLAY = 1, WEAPONSTATBITFLAG_NO_CLIP_RELOADS = 2 };
#define GEVR
struct Gfx {};
struct sImageTableEntry { int width, id; } icons[4] = {{0,0},{10,1},{14,2},{18,3}};
struct Hand { int weapon, weapon_action_state, weapon_ammo_in_magazine; };
struct Player { int gunammooff, mpmenuon; Hand hands[2]; int ammoheldarr[4]; } player;
Player *g_CurrentPlayer = &player;
struct { int IconImage, IconYOffset; } ammo_related[4] = {{0,0},{1,0},{2,0},{3,0}};
int VrLeftHandedMode, flags[4], count = 1, slot;
struct Number { int value, x, align; };
struct Icon { int id, x; };
std::vector<Number> numbers;
std::vector<Icon> images;
int getCurrentPlayerWeaponId(int hand) { return player.hands[hand].weapon; }
int get_ammo_type_for_weapon(int weapon) { return weapon; }
int getPlayerCount() { return count; }
int get_cur_playernum() { return slot; }
int gevrCoopActive() { return 0; }
int getPlayer_c_screenleft() { return 0; }
int getPlayer_c_screenwidth() { return 320; }
int viGetViewLeft() { return 0; }
int viGetViewWidth() { return 320; }
int viGetViewTop() { return 0; }
int viGetViewHeight() { return 240; }
int bondwalkItemCheckBitflags(int weapon, int flag) { return flags[weapon] & flag; }
sImageTableEntry *texGetAmmoIcon(int id) { return &icons[id]; }
Gfx *set_rgba_redirect_generate_microcode(Gfx *g, sImageTableEntry *icon, float x,
                                        float, int, int, int, int) {
    images.push_back({icon->id, (int)x}); return g;
}
Gfx *gunDrawHudInteger(Gfx *g, int value, int x, int align, int, int, int) {
    numbers.push_back({value, x, align}); return g;
}
Gfx *microcode_constructor(Gfx *g) { return g; }
Gfx *combiner_bayer_lod_perspective(Gfx *g) { return g; }
/* INSERT_AMMO */
void draw() { numbers.clear(); images.clear(); Gfx g; generate_ammo_total_microcode(&g); }
int main() {
    player.hands[0] = {1, 0, 7}; player.hands[1] = {2, 0, 19};
    player.ammoheldarr[1] = 60; player.ammoheldarr[2] = 90;
    for (int lefty = 0; lefty < 2; lefty++) {
        VrLeftHandedMode = lefty; draw();
        assert(images.size() == 2 && numbers.size() == 4);
        assert(images[0].id == (lefty ? 2 : 1) && images[0].x > 160);
        assert(images[1].id == (lefty ? 1 : 2) && images[1].x < 160);
        assert(numbers[0].value == (lefty ? 19 : 7) && numbers[0].x > 160);
        assert(numbers[1].value == (lefty ? 90 : 60) && numbers[1].x > numbers[0].x);
        assert(numbers[2].value == (lefty ? 7 : 19) && numbers[2].x < 160);
        assert(numbers[3].value == (lefty ? 60 : 90) && numbers[3].x < numbers[2].x);
        player.hands[0].weapon_action_state = GUN_ANIM_STATE_SWITCH_SWAP; draw();
        assert(images.size() == 1 && images[0].id == 2);
        player.hands[0].weapon_action_state = 0;
        player.hands[1].weapon = ITEM_UNARMED; draw();
        assert(images.size() == 1 && (images[0].x < 160) == (lefty != 0));
        player.hands[1].weapon = 2;
    }
    player.hands[1].weapon = 1; flags[1] = WEAPONSTATBITFLAG_NO_CLIP_RELOADS;
    draw(); assert(numbers.size() == 2 && numbers[0].value == 86 && numbers[1].value == 86);
    flags[1] = WEAPONSTATBITFLAG_HIDE_AMMO_DISPLAY;
    draw(); assert(numbers.empty() && images.empty());
    flags[1] = 0; player.gunammooff = 1; draw(); assert(numbers.empty());
    player.gunammooff = 0; player.mpmenuon = 1; draw(); assert(numbers.empty());
    puts("PASS: production ammo groups, handedness, mixed weapons, reserves, switches and visibility");
}
