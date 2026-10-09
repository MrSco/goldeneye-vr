/* Co-op menu follow: the screen being left keeps its mission until cleanup. */
#include <stdio.h>

typedef int s32;
typedef int MENU;

#define MENU_INVALID -1
#define MENU_MISSION_SELECT 7
#define MENU_BRIEFING 10
#define MENU_MISSION_COMPLETE 13
#define MENU_SWITCH_SCREENS 23

MENU current_menu;
MENU menu_update;
MENU maybe_prev_menu;

/* INSERT_TARGET */
/* INSERT_ACTION */

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "fail %s line %d\n", #x, __LINE__); return 1; } } while (0)

static void set_menus(int current, int update, int pending)
{
    current_menu = current;
    menu_update = update;
    maybe_prev_menu = pending;
}

int main(void)
{
    /* On the briefing, host moves to mission select. Change screen, do not
       apply: cleanup still needs this mission's folder entry. */
    set_menus(MENU_BRIEFING, MENU_INVALID, MENU_INVALID);
    CHECK(gevrCoopMenuMissionAction(MENU_MISSION_SELECT) == 0);

    /* Black switch toward mission select. The briefing's entry stays. */
    set_menus(MENU_SWITCH_SCREENS, MENU_MISSION_SELECT, MENU_INVALID);
    CHECK(gevrCoopMenuMissionAction(MENU_MISSION_SELECT) == 2);

    /* The frame mission select inits: apply the host, including page -1. */
    set_menus(MENU_SWITCH_SCREENS, MENU_INVALID, MENU_MISSION_SELECT);
    CHECK(gevrCoopMenuMissionAction(MENU_MISSION_SELECT) == 1);

    /* The frame the next briefing inits: apply its folder entry first. */
    set_menus(MENU_SWITCH_SCREENS, MENU_INVALID, MENU_BRIEFING);
    CHECK(gevrCoopMenuMissionAction(MENU_BRIEFING) == 1);

    /* Already on the host's screen: keep applying cursor and choices. */
    set_menus(MENU_BRIEFING, MENU_INVALID, MENU_INVALID);
    CHECK(gevrCoopMenuMissionAction(MENU_BRIEFING) == 1);

    /* Debrief to the next briefing has not started the switch yet. */
    set_menus(MENU_MISSION_COMPLETE, MENU_INVALID, MENU_INVALID);
    CHECK(gevrCoopMenuMissionAction(MENU_BRIEFING) == 0);

    printf("coop menu follow ok\n");
    return 0;
}
