/* The in-match Multiplayer Options folder's rows (src/game/gevr_mpoptions.c), as built: driven by
 * test_mp_options_rows.py. */
#include <stdio.h>
#include <string.h>
#include "gevr_mpoptions.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

static unsigned char ids[GEVR_SETUP_ROW_COUNT], hostonly[GEVR_SETUP_ROW_COUNT];
static int n;

static int at(int id)
{
    for (int i = 0; i < n; i++) if (ids[i] == id) return i;
    return -1;
}
static int has(int id) { return at(id) >= 0; }

static GevrSetupState dm(int host)
{
    GevrSetupState s;
    memset(&s, 0, sizeof(s));
    s.mode = GEVR_SETUP_MODE_DEATHMATCH;
    s.host = (unsigned char)host;
    return s;
}

int main(void)
{
    GevrSetupState s;

    /* the host in a free-for-all warmup: the original folder's rows first, in its order */
    s = dm(1);
    n = gevrMpOptionsRowSet(&s, ids, hostonly);
    CHECK(n > 0 && ids[0] == GEVR_SETUP_PLAYERS && ids[1] == GEVR_SETUP_SCENARIO && ids[2] == GEVR_SETUP_LEVEL &&
          ids[3] == GEVR_SETUP_LENGTH && ids[4] == GEVR_SETUP_WEAPONS && ids[5] == GEVR_SETUP_CHARACTER &&
          ids[6] == GEVR_SETUP_HEALTH, "the folder's own order: Players, Scenario, Level, Game Length, Weapons, Characters, Health");
    CHECK(has(GEVR_SETUP_BOTS) && has(GEVR_SETUP_NEXT_ROUND) && has(GEVR_SETUP_DUAL_WIELD) && has(GEVR_SETUP_LOADOUTS) &&
          has(GEVR_SETUP_HANDICAPS) && has(GEVR_SETUP_FULL_MENU), "the host's rules and the full menu");
    CHECK(!has(GEVR_SETUP_TEAMS) && !has(GEVR_SETUP_FRIENDLY_FIRE) && !has(GEVR_SETUP_NEXT_MAP) && !has(GEVR_SETUP_NEXT_WEAPONS) &&
          !has(GEVR_SETUP_CUSTOM) && !has(GEVR_SETUP_LOADOUT) && !has(GEVR_SETUP_RETURN) && !has(GEVR_SETUP_DROP_OUT),
          "no teams, votes, custom guns, own loadout, return or drop out unless they apply");
    for (int i = 0; i < n; i++)
    {
        const int id = ids[i];
        const int own = id == GEVR_SETUP_CHARACTER || id == GEVR_SETUP_HEALTH || id == GEVR_SETUP_TEAMS ||
                        id == GEVR_SETUP_LOADOUT || id == GEVR_SETUP_NEXT_MAP || id == GEVR_SETUP_NEXT_WEAPONS ||
                        id == GEVR_SETUP_DROP_OUT || id == GEVR_SETUP_FULL_MENU;
        CHECK(hostonly[i] == !own, "row %d: %s", id, own ? "anyone's" : "the host's");
    }
    for (int i = 0; i < n; i++) for (int k = i + 1; k < n; k++) CHECK(ids[i] != ids[k], "row %d twice", ids[i]);

    /* a joiner sees the host's rows (dimmed: hostonly), no bots, no return */
    s = dm(0);
    n = gevrMpOptionsRowSet(&s, ids, hostonly);
    CHECK(has(GEVR_SETUP_PLAYERS) && hostonly[at(GEVR_SETUP_PLAYERS)] && has(GEVR_SETUP_SCENARIO) && hostonly[at(GEVR_SETUP_SCENARIO)],
          "a joiner sees the host's rows, read-only");
    CHECK(!has(GEVR_SETUP_BOTS) && has(GEVR_SETUP_CHARACTER) && !hostonly[at(GEVR_SETUP_CHARACTER)], "no bots; their own character");
    s.can_return = 1;
    n = gevrMpOptionsRowSet(&s, ids, hostonly);
    CHECK(!has(GEVR_SETUP_RETURN), "RETURN TO LOBBY is the host's alone");

    /* a team scenario: its own size, teams, friendly fire */
    s = dm(1);
    s.teams = 1;
    n = gevrMpOptionsRowSet(&s, ids, hostonly);
    CHECK(!has(GEVR_SETUP_PLAYERS) && has(GEVR_SETUP_TEAMS) && !hostonly[at(GEVR_SETUP_TEAMS)] && has(GEVR_SETUP_FRIENDLY_FIRE) &&
          hostonly[at(GEVR_SETUP_FRIENDLY_FIRE)], "teams: no player count, the Teams screen, friendly fire");
    CHECK(at(GEVR_SETUP_TEAMS) > at(GEVR_SETUP_HEALTH), "Teams after Health");

    /* the vote: everyone's map and weapons in place of the host's */
    s = dm(1);
    s.voting = 1;
    s.custom = 1;
    n = gevrMpOptionsRowSet(&s, ids, hostonly);
    CHECK(!has(GEVR_SETUP_LEVEL) && has(GEVR_SETUP_NEXT_MAP) && at(GEVR_SETUP_NEXT_MAP) == at(GEVR_SETUP_SCENARIO) + 1 &&
          !hostonly[at(GEVR_SETUP_NEXT_MAP)], "voting: NEXT MAP where LEVEL was, anyone's");
    CHECK(!has(GEVR_SETUP_WEAPONS) && !has(GEVR_SETUP_CUSTOM) && has(GEVR_SETUP_NEXT_WEAPONS), "voting: NEXT WEAPONS");

    /* scenario rules: the golden gun has its own weapons, YOLT no length; the custom set its sheet */
    s = dm(1);
    s.goldengun = 1;
    s.yolt = 1;
    n = gevrMpOptionsRowSet(&s, ids, hostonly);
    CHECK(!has(GEVR_SETUP_WEAPONS) && !has(GEVR_SETUP_NEXT_WEAPONS) && !has(GEVR_SETUP_LENGTH), "golden gun / YOLT");
    s = dm(1);
    s.custom = 1;
    s.loadouts = 1;
    n = gevrMpOptionsRowSet(&s, ids, hostonly);
    CHECK(at(GEVR_SETUP_CUSTOM) == at(GEVR_SETUP_WEAPONS) + 1 && has(GEVR_SETUP_LOADOUT) &&
          at(GEVR_SETUP_LOADOUT) == at(GEVR_SETUP_LOADOUTS) + 1, "the custom set's sheet after Weapons, the own loadout after Loadouts");

    /* a round in progress: no bots; the host may return to the lobby */
    s = dm(1);
    s.playing = 1;
    s.can_return = 1;
    n = gevrMpOptionsRowSet(&s, ids, hostonly);
    CHECK(!has(GEVR_SETUP_BOTS) && has(GEVR_SETUP_RETURN) && ids[n - 1] == GEVR_SETUP_FULL_MENU && ids[n - 2] == GEVR_SETUP_RETURN,
          "in a round: no bots, RETURN TO LOBBY before the full menu");

    /* split screen: the guest drops out (no full menu), the first seat keeps the full menu */
    s = dm(0);
    s.split = 1;
    s.guest = 1;
    n = gevrMpOptionsRowSet(&s, ids, hostonly);
    CHECK(has(GEVR_SETUP_DROP_OUT) && !has(GEVR_SETUP_FULL_MENU) && ids[n - 1] == GEVR_SETUP_DROP_OUT, "a guest: DROP OUT last");
    s.guest = 0;
    n = gevrMpOptionsRowSet(&s, ids, hostonly);
    CHECK(!has(GEVR_SETUP_DROP_OUT) && has(GEVR_SETUP_FULL_MENU), "the first seat: FULL MENU");

    /* a dedicated server's player is never the host: every host row read-only */
    s = dm(0);
    n = gevrMpOptionsRowSet(&s, ids, hostonly);
    for (int i = 0; i < n; i++) CHECK(ids[i] != GEVR_SETUP_BOTS && ids[i] != GEVR_SETUP_RETURN, "a dedicated server's player");

    /* no folder for co-op (its own menus) and, for now, Horde */
    s = dm(1);
    s.mode = GEVR_SETUP_MODE_COOP;
    CHECK(gevrMpOptionsRowSet(&s, ids, hostonly) == 0, "co-op: no folder");
    s.mode = GEVR_SETUP_MODE_HORDE;
    CHECK(gevrMpOptionsRowSet(&s, ids, hostonly) == 0, "Horde: its rows are to come");
    CHECK(gevrMpOptionsRowSet(NULL, ids, hostonly) == 0, "no state");

    /* every combination stays within the table and lists no row twice */
    for (int bits = 0; bits < (1 << 10); bits++)
    {
        s = dm(bits & 1);
        s.playing = (bits >> 1) & 1; s.teams = (bits >> 2) & 1; s.voting = (bits >> 3) & 1; s.goldengun = (bits >> 4) & 1;
        s.yolt = (bits >> 5) & 1; s.custom = (bits >> 6) & 1; s.loadouts = (bits >> 7) & 1; s.can_return = (bits >> 8) & 1;
        s.split = s.guest = (bits >> 9) & 1;
        n = gevrMpOptionsRowSet(&s, ids, hostonly);
        CHECK(n > 0 && n <= GEVR_SETUP_ROW_COUNT, "combination %d: %d rows", bits, n);
        for (int i = 0; i < n; i++) for (int k = i + 1; k < n; k++) CHECK(ids[i] != ids[k], "combination %d: row %d twice", bits, ids[i]);
        CHECK(has(GEVR_SETUP_CHARACTER) && has(GEVR_SETUP_HEALTH) && has(GEVR_SETUP_SCENARIO), "combination %d: the folder's core", bits);
    }

    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("PASS: the Multiplayer Options folder's rows per mode, seat, phase, scenario and vote\n");
    return 0;
}
