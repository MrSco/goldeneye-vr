#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "front.h"
#include "chrobjdata.h"
#include "gevr_romlayout.h"
#include "gevr_romcheck.h"
#include "macro.h"
#include "fr.h"
#include <memp.h>
#include "tex.h"
#include <music.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>

#define GEVR_CAST_AT 0x8002b600u
#define GEVR_CAST_CAP 128
#define INTRO_RANDOM_RIFLE_LEN 6
#define INTRO_RANDOM_PISTO_LEN 10
static struct intro_char g_Cast[GEVR_CAST_CAP];
static int g_CastCount;
static struct gevrDataWindow g_Window;


const uint8_t *gevrRomDataBytes(const struct gevrDataWindow *w, uint32_t address, uint32_t size);
/* Use the real data-window lookup by mapping the synthetic table at its VRAM. */
static unsigned char data_window[GEVR_CAST_AT - GEVR_DATA_VRAM + (20 * GEVR_CAST_CAP)];
void sysLogPrintf(int level, const char *fmt, ...) { (void)level; (void)fmt; }
#define LOG_ERROR 2
/* INSERT_BINDER */

static void put32(unsigned char *p, uint32_t value) {
    p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}
static void row(int i, int body, int head, int flag) {
    unsigned char *p = data_window + GEVR_CAST_AT - GEVR_DATA_VRAM + 20 * i;
    memset(p, 0, 20);
    put32(p, body); put32(p + 4, head); put32(p + 16, flag);
    p[8] = 0x12; p[9] = 0x34; p[10] = 0x23; p[11] = 0x45; p[12] = 0x34; p[13] = 0x56;
}
static struct intro_char intro_char_table[] = {{37, HEAD_FIXED}, {BODY_Siberian_Guard_1_Mishkin, HEAD_Male_Mishkin}, {-1}};
/* INSERT_CAST_HELPER */
/* INSERT_GLOBALS */

static ModelFileHeader body_headers[126], prop_headers[416];
static struct ChrModelFileRecord characters[126];
static struct ItemModelFileRecord props[416];
struct ChrModelFileRecord *c_item_entries = characters;
struct ItemModelFileRecord *PitemZ_entries = props;
static struct Model model, weapon_model;
static struct ModelAnimation animation;
static struct ModelAnimation *animation_table_ptrs1[] = { &animation };
static struct intro_animation intro_animation_table[] = {{0, 0, 1, INTRO_WEAPON_TYPE_RIFLE}, {-1}};
static struct coord3d zeroCoord;
static unsigned char *allocation;
u8 *ptr_logo_and_walletbond_DL;
uintptr_t ptr_menu_videobuffer;
struct Model *walletinst[4];
s32 current_menu, maybe_prev_menu, screen_size, spectrum_related_flag, is_emulating_spectrum;
s32 folder_selection_screen_option_icon, folder_selected_for_deletion, folder_selected_for_deletion_choice;
s32 tab_start_highlight, tab_prev_highlight, tab_next_highlight, maybe_is_in_menu, prev_keypresses, menu_update;
s32 intro_character_index;
u32 randomly_selected_intro_animation, intro_animation_count, full_actor_intro;
struct Model *cast_model, *cast_model_weapon;
int g_musicXTrack1Fade;
static int loads, last_size, selected_body, selected_head;
f32 cast_camera_dist_start, cast_camera_dist_end, cast_camera_angle_start, cast_camera_angle_end;
f32 cast_camera_height_start, cast_camera_height_end;
f32 cast_camera_dist, cast_camera_angle, cast_camera_height;
bool cast_camera_reset;
struct coord3d cast_rootpos_smoothed;
static uintptr_t z_buffer;
static s32 z_buffer_width, z_buffer_height;
s32 g_MenuTimer, g_ClockTimer;
static int changed_menu;

void *mempAllocBytesInBank(u32 size, u8 bank) {
    (void)bank;
    unsigned char *p = malloc(size + 128);
    assert(p); memset(p, 0xa5, size + 128);
    if (!allocation) allocation = p;
    return p;
}
void alloc_intro_matrices(void) {}
/* INSERT_MENU_INIT */
/* INSERT_DEPTH_SET */
static int player_count = 1, current_player;
s32 getPlayerCount(void) { return player_count; }
s32 get_cur_playernum(void) { return current_player; }
void zbufAllocate(void) { abort(); }
/* INSERT_DEPTH_INIT */

static unsigned randomGetNext(void) { return 0; }
void musicTrack1ApplySeqpVol(u16 value) { (void)value; }
void musicTrack1Play(int value) { (void)value; }
static int get_random_head(int body) { (void)body; return 110; }
int fileIsCradleCompletedAnyFolder(void) { return 1; }
static int check_aztec_completed_any_folder_secret_00(void) { return 1; }
static int fileIsEgyptCompletedOn00AnyFolder(void) { return 1; }
void texInitPool(struct texpool *pool, u8 *data, s32 size) {
    (void)pool; assert(data == allocation && size == GEVR_CAST_TEXTURE_BYTES);
}
/* Conversion/rewrite are stand-ins; the bounded inflater is production code. */
static void load_object_fill_header(ModelFileHeader *header, u8 *name, u8 *dst, s32 size, struct texpool *pool) {
    (void)name; (void)pool;
    unsigned char payload[4096], compressed[5000];
    const int final_sizes[] = {46720, 50464, 8192};
    unsigned written = 0;
    z_stream z = {0};
    memset(payload, loads + 1, sizeof(payload));
    compressed[0] = 0x11; compressed[1] = 0x72;
    assert(deflateInit2(&z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    z.next_in = payload; z.avail_in = sizeof(payload);
    z.next_out = compressed + 2; z.avail_out = sizeof(compressed) - 2;
    assert(deflate(&z, Z_FINISH) == Z_STREAM_END);
    unsigned compressed_size = z.total_out + 2;
    deflateEnd(&z);
    if (gevrInflate1172(compressed, compressed_size, dst, size, &written) || size < final_sizes[loads]) {
        fputs("cast model capacity exhausted\n", stderr); exit(2);
    }
    assert(dst >= allocation + GEVR_CAST_TEXTURE_BYTES);
    assert((uintptr_t)dst + final_sizes[loads] <= (uintptr_t)allocation + GEVR_CAST_TEXTURE_BYTES + GEVR_CAST_MODEL_BYTES);
    memset(dst, loads + 1, final_sizes[loads]);
    header->Switches = (void *)dst;
    last_size = final_sizes[loads++];
}
static int get_pc_buffer_remaining_value(u8 *name) { (void)name; return last_size; }
static struct Model *setup_chr_instance(int body, int head, ModelFileHeader *b, ModelFileHeader *h, int flag) {
    (void)h; (void)flag; selected_body = body; selected_head = head; model.obj = b; return &model;
}
static void modelSetScale(Model *m, f32 scale) { (void)m; (void)scale; }
static void modelCalculateRwDataLen(ModelFileHeader *h) { (void)h; }
static Model *modelmgrInstantiateModel(ModelFileHeader *h) { weapon_model.obj = h; return &weapon_model; }
static void modelSetAnimTranslationScale(Model *m, f32 value) { (void)m; (void)value; }
static void setsuboffset(Model *m, struct coord3d *value) { (void)m; (void)value; }
static void setsubroty(Model *m, f32 value) { (void)m; (void)value; }
static void modelSetAnimPlaySpeed(Model *m, f32 value, f32 flag) { (void)m; (void)value; (void)flag; }
static void modelSetAnimation(Model *m, struct ModelAnimation *a, int flip, f32 start, f32 speed, f32 merge) {
    (void)m; (void)a; (void)flip; (void)start; (void)speed; (void)merge;
}
/* INSERT_INIT */

void viSetFovY(f32 value) { (void)value; }
void viSetZRange(f32 a, f32 b) { (void)a; (void)b; }
void viSetUseZBuf(int value) { (void)value; }
void viSetAspect(f32 value) { (void)value; }
static void set_cur_player_screen_size(int w, int h) { (void)w; (void)h; }
void viSetViewSize(s16 w, s16 h) { (void)w; (void)h; }
static void set_cur_player_viewport_size(int w, int h) { (void)w; (void)h; }
void viSetViewPosition(s16 w, s16 h) { (void)w; (void)h; }
void frontChangeMenu(MENU menu, s32 reload) { (void)reload; changed_menu = menu; }
static void set_cursor_to_stage_solo(int stage) { (void)stage; }
static void select_ramrom_to_play(void) { changed_menu = -2; }
u16 joyGetButtonsPressedThisFrame(s8 player, u16 buttons) { (void)player; (void)buttons; return 0; }
/* INSERT_ADVANCE */

int main(void) {
    const struct gevrRomLayout *layout;
    g_Window.bytes = data_window; g_Window.len = sizeof(data_window);
    gevrRomCampaignSet(GEVR_CAMPAIGN_GF64); layout = gevrRomActiveLayout();
    row(0, 37, 110, 0); row(1, BODY_Special_Operations_Uniform, 110, 0); row(2, -1, -1, 0);
    assert(readCastTable(layout));
    assert(gevrRomCastTable()[1].body == BODY_Special_Operations_Uniform);
    assert(gevrRomCastTable()[1].head == 110 && gevrRomCastTable()[1].text3 == 0x3456);
    assert(gevrRomCastTable()[2].body == -1);
    /* Malformed index, truncated/endless table, and absent automatic entry. */
    row(1, 126, 0, 0); assert(!readCastTable(layout) && !gevrRomCastTable());
    row(1, 3, 126, 0); assert(!readCastTable(layout));
    row(1, 3, -3, 0); assert(!readCastTable(layout));
    row(1, -1, -1, 0); assert(!readCastTable(layout));
    for (int i = 0; i < GEVR_CAST_CAP; i++) row(i, 3, HEAD_FIXED, 0);
    assert(!readCastTable(layout));
    g_Window.len = GEVR_CAST_AT - GEVR_DATA_VRAM + 21; assert(!readCastTable(layout));
    g_Window.len = sizeof(data_window);
    row(0, 37, HEAD_RANDOM, 0); row(1, BODY_Special_Operations_Uniform, 110, 0); row(2, -1, -1, 0);
    assert(readCastTable(layout));
    /* Other campaigns must never see Goldfinger's records. */
    gevrRomCampaignSet(GEVR_CAMPAIGN_TND64); assert(!gevrRomCastTable());
    gevrRomCampaignSet(GEVR_CAMPAIGN_GE); assert(gevrCastTable() == intro_char_table);
    gevrRomCampaignSet(GEVR_CAMPAIGN_GF64);
    for (int i = 0; i < 126; i++) { characters[i].header = &body_headers[i]; characters[i].filename = (void *)"body"; }
    for (int i = 0; i < 416; i++) { props[i].header = &prop_headers[i]; props[i].filename = (void *)"weapon"; }
    init_menus_or_reset();
    intro_character_index = 1;
    for (int i = 0; i < 200; i++) {
        loads = 0; init_menu18_displaycast(); assert(loads == 3);
        assert(selected_body == BODY_Special_Operations_Uniform && selected_head == 110);
        assert(z_buffer == ALIGN64_V1((uintptr_t)allocation + GEVR_CAST_TEXTURE_BYTES + GEVR_CAST_MODEL_BYTES));
        assert(z_buffer_width == 440 && z_buffer_height == 330);
        assert(z_buffer + GEVR_CAST_DEPTH_BYTES <= (uintptr_t)allocation + GEVR_MENU_BUFFER_BYTES);
        for (int j = 0; j < 32; j++) assert(*(unsigned char *)(z_buffer + j) == 0xa5);
    }
    /* A retail bonus-body number is an ordinary Goldfinger record: keep it. */
    row(2, BODY_Moonraker_Elite_1_Male, HEAD_FIXED, 0);
    row(3, 30, HEAD_FIXED, 1); row(4, -1, -1, 0);
    assert(readCastTable(layout));
    Gfx depth_commands[2];
    zbufInit(depth_commands);
    assert(depth_commands[1].words.w1 == z_buffer);
    player_count = 2; current_player = 1;
    zbufInit(depth_commands);
    assert(depth_commands[1].words.w1 == ((z_buffer - SCREEN_WIDTH * SCREEN_HEIGHT) & ~(uintptr_t)63));
    player_count = 1; current_player = 0;
    g_MenuTimer = 181; interface_menu18_displaycast();
    assert(changed_menu == MENU_DISPLAY_CAST && intro_character_index == 2);
    g_MenuTimer = 181; interface_menu18_displaycast();
    assert(changed_menu == -2 && intro_character_index == 0);
    /* Extended casts include flagged rows and return to mission select at end. */
    full_actor_intro = 1; intro_character_index = 2; g_MenuTimer = 181;
    interface_menu18_displaycast(); assert(intro_character_index == 3);
    g_MenuTimer = 181; interface_menu18_displaycast();
    assert(changed_menu == MENU_MISSION_SELECT && !full_actor_intro);
    row(1, 30, HEAD_RANDOM, 0); assert(readCastTable(layout));
    intro_character_index = 1; loads = 0; init_menu18_displaycast();
    assert(selected_head == 110 && loads == 3);
    puts("Goldfinger cast: ROM rows, malformed tables, retail/TND isolation, 200 large-model loads, depth guards and cast end passed");
    return 0;
}
