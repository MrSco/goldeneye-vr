/*
 * Online co-op (#94): the party's menus. A co-op party plays the solo
 * campaign through the game's own front end (front.c): each player picks
 * their own save folder on their own headset, then the host drives mission
 * select, difficulty, 007 options, the briefing, the debrief and the
 * statistics page, and every other headset shows the host's screen, cursor
 * and choices. Each headset draws them from its own save (its portraits,
 * ticks and times); the host's pick decides, and the host's Start loads the
 * mission everywhere (net_core.c netCoopHostStartMission).
 *
 * Ten times a second the host sends its screen, its cursor and the choices
 * the screens read (the mission's folder entry, the stage, the difficulty,
 * the briefing page, 007's sliders); a change of screen goes reliably. A
 * following headset sets the same globals before each frame's menu code
 * runs, takes no input of its own, and changes screen through the game's
 * own black switch (frontChangeMenu, reload).
 */
#include "net_core.h"   /* first, as in net_core.c: <stdbool.h> before the game's headers */
#include "net_coop.h"
#include "net_protocol.h"
#include "net/netbuf.h"
#include <string.h>
#include "system.h"
#include "bondconstants.h"
#include "boss.h"
#include "game/front.h"

#define COOP_MENU_SEND_US 100000ull     /* the host's cursor, ten times a second */

typedef struct {
    s8 menu;                /* MENU: the host's screen, settled */
    s8 briefingpage;        /* its mission's folder entry, -1 none */
    s8 page;                /* the briefing's page */
    u8 stage;               /* selected_stage, 0xFF none */
    u8 difficulty;          /* selected_difficulty */
    f32 cursor_h, cursor_v;
    f32 sliders[4];         /* 007: reaction, health, accuracy, damage */
} CoopMenuState;

#define COOP_MENU_BYTES (5 + 4 * 6)

static CoopMenuState s_sent, s_host;
static bool s_sent_valid, s_host_valid;
static u64 s_sent_us;

/*
 * Before each co-op stage load. A host can start while a teammate is still
 * choosing a folder, and a drop-in can bypass that screen entirely. Keep a
 * chosen local folder; otherwise use this headset's first folder, never a
 * folder supplied by the host. This also runs in the launcher before the
 * game initializes: save I/O belongs to boss.c and the folder screen.
 */
void gevrCoopPrepareSaveFolder(void)
{
    if (selected_folder_num < FOLDER1 || selected_folder_num >= MAX_FOLDER_COUNT)
    {
        selected_folder_num = FOLDER1;
        sysLogPrintf(LOG_NOTE, "coop: no local save folder selected; using folder %d", selected_folder_num + 1);
    }
}

/* The screens a following headset shows as the host's */
static bool coopMenuFollowable(int menu)
{
    return menu == MENU_MISSION_SELECT || menu == MENU_DIFFICULTY || menu == MENU_007_OPTIONS ||
           menu == MENU_BRIEFING || menu == MENU_MISSION_FAILED || menu == MENU_MISSION_COMPLETE;
}

/* The screen this headset is on, or the one a switch is taking it to */
static int coopMenuTarget(void)
{
    if (maybe_prev_menu > MENU_INVALID) return maybe_prev_menu;
    if (menu_update > MENU_INVALID) return menu_update;
    return current_menu;
}

static bool coopInMenus(void)
{
    return netCoopSession() && bossGetStageNum() == LEVELID_TITLE;
}

int gevrCoopIsHost(void)
{
    return netCoopSession() && netIsHost();
}

/* options.c: the solo watch's controller; in a co-op mission this headset's player's (input.c) */
int gevrWatchController(void)
{
    return netCoopActive() ? netGetLocalSlot() : 0;
}

/* This headset follows the host's menus: past its own folder screen, on one the host drives */
int gevrCoopMenuFollowing(void)
{
    return coopInMenus() && !netIsHost() && coopMenuFollowable(coopMenuTarget());
}

/* The host's choices, as the screens read them */
static void coopMenuCollect(CoopMenuState *st)
{
    memset(st, 0, sizeof(*st));
    st->menu = (s8)current_menu;
    st->briefingpage = (s8)(briefingpage >= -1 && briefingpage < 127 ? briefingpage : -1);
    st->page = (s8)current_menu_briefing_page;
    st->stage = (u8)(selected_stage >= 0 && selected_stage < 0xFF ? selected_stage : 0xFF);
    st->difficulty = (u8)(selected_difficulty >= 0 && selected_difficulty < 0xFF ? selected_difficulty : 0xFF);
    st->cursor_h = cursor_h_pos;
    st->cursor_v = cursor_v_pos;
    st->sliders[0] = slider_007_mode_reaction;
    st->sliders[1] = slider_007_mode_health;
    st->sliders[2] = slider_007_mode_accuracy;
    st->sliders[3] = slider_007_mode_damage;
}

static void coopMenuSend(void)
{
    CoopMenuState st;
    u8 raw[16 + COOP_MENU_BYTES];
    struct netbuf buf = { .data = raw, .size = sizeof(raw) };
    u64 now = sysGetMicroseconds();
    bool changed;

    /* a switch between screens, or the mission's load: the last settled screen stands */
    if (!coopMenuFollowable(current_menu)) return;
    coopMenuCollect(&st);
    changed = !s_sent_valid || st.menu != s_sent.menu || st.briefingpage != s_sent.briefingpage ||
              st.page != s_sent.page || st.stage != s_sent.stage || st.difficulty != s_sent.difficulty;
    if (!changed && now - s_sent_us < COOP_MENU_SEND_US) return;

    netbufStartWrite(&buf);
    netbufWriteU32(&buf, GEVR_NET_MAGIC);
    netbufWriteU16(&buf, GEVR_NET_VERSION);
    netbufWriteU8(&buf, NET_MSG_COOP_MENU);
    netbufWriteU8(&buf, (uint8_t)netGetLocalSlot());
    netbufWriteU8(&buf, (u8)st.menu);
    netbufWriteU8(&buf, (u8)st.briefingpage);
    netbufWriteU8(&buf, (u8)st.page);
    netbufWriteU8(&buf, st.stage);
    netbufWriteU8(&buf, st.difficulty);
    netbufWriteF32(&buf, st.cursor_h);
    netbufWriteF32(&buf, st.cursor_v);
    for (int i = 0; i < 4; i++) netbufWriteF32(&buf, st.sliders[i]);
    if (buf.error) return;
    netCoopBroadcast(raw, buf.wp, changed);
    s_sent = st;
    s_sent_valid = true;
    s_sent_us = now;
}

/* net_core.c: NET_MSG_COOP_MENU from the host */
void netCoopReceiveMenu(struct netbuf *b)
{
    CoopMenuState st;
    memset(&st, 0, sizeof(st));
    st.menu = (s8)netbufReadU8(b);
    st.briefingpage = (s8)netbufReadU8(b);
    st.page = (s8)netbufReadU8(b);
    st.stage = netbufReadU8(b);
    st.difficulty = netbufReadU8(b);
    st.cursor_h = netbufReadF32(b);
    st.cursor_v = netbufReadF32(b);
    for (int i = 0; i < 4; i++) st.sliders[i] = netbufReadF32(b);
    if (b->error || netbufReadLeft(b) || !coopMenuFollowable(st.menu) || st.briefingpage < -1 ||
        st.page < 0 || st.page > BRIEFING_M || st.cursor_h != st.cursor_h || st.cursor_v != st.cursor_v) return;
    for (int i = 0; i < 4; i++)
        if (st.sliders[i] != st.sliders[i] || st.sliders[i] < 0.0f || st.sliders[i] > 10.0f) return;
    s_host = st;
    s_host_valid = true;
}

/* The host's choices on this headset, before its menu code runs this frame */
static void coopMenuApply(const CoopMenuState *st)
{
    cursor_h_pos = st->cursor_h;
    cursor_v_pos = st->cursor_v;
    briefingpage = st->briefingpage;
    selected_stage = st->stage == 0xFF ? LEVELID_NONE : st->stage;
    selected_difficulty = (DIFFICULTY)(st->difficulty == 0xFF ? DIFFICULTY_MULTI : st->difficulty);
    slider_007_mode_reaction = st->sliders[0];
    slider_007_mode_health = st->sliders[1];
    slider_007_mode_accuracy = st->sliders[2];
    slider_007_mode_damage = st->sliders[3];
    if (current_menu == MENU_BRIEFING) current_menu_briefing_page = st->page;
}

static void coopMenuFollow(void)
{
    int target;

    if (!gevrCoopMenuFollowing() || !s_host_valid) return;
    target = s_host.menu;
    if (target != coopMenuTarget() && current_menu != MENU_SWITCH_SCREENS &&
        menu_update == MENU_INVALID && maybe_prev_menu == MENU_INVALID) {
        sysLogPrintf(LOG_NOTE, "coop: menus: following the host to screen %d", target);
        coopMenuApply(&s_host);
        frontChangeMenu((MENU)target, TRUE);
        return;
    }
    if (current_menu == target) coopMenuApply(&s_host);
}

/* front.c menu_init, every frame of the title stage: the host's screen out, the others' in */
void gevrCoopMenuTick(void)
{
    if (!coopInMenus()) {
        s_sent_valid = false;
        return;
    }
    if (netIsHost()) coopMenuSend();
    else coopMenuFollow();
}

/* The menus begin again (a new party, the title stage's load): nothing the old host said stands */
void netCoopMenuReset(void)
{
    s_host_valid = false;
    s_sent_valid = false;
}
