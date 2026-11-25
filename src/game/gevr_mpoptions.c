/*
 * The Multiplayer Options folder's rows in an online match (mpmenu.c draws
 * and drives them, docs/in-game-match-setup.md): GoldenEye's own folder
 * (Players, Scenario, Level, Game Length, Weapons, Character, Health) and the
 * online match's rows after them. The host's rows show at every seat, dimmed
 * where they cannot be changed, as the original dimmed a locked row.
 */
#include "gevr_mpoptions.h"

static int add(unsigned char *ids, unsigned char *hostonly, int n, int id, int host)
{
    ids[n] = (unsigned char)id;
    hostonly[n] = (unsigned char)(host != 0);
    return n + 1;
}

int gevrMpOptionsRowSet(const GevrSetupState *s, unsigned char *ids, unsigned char *hostonly)
{
    int n = 0;

    if (s == 0) return 0;
    switch (s->mode)
    {
        case GEVR_SETUP_MODE_DEATHMATCH:
            break;
        case GEVR_SETUP_MODE_HORDE:
            /* to come: LEVEL (the Horde stages), STARTING WAVE, FRIENDLY FIRE, AI ALLY,
             * CHARACTER, then DROP OUT and FULL MENU (in-game-match-setup.md) */
            return 0;
        default:
            return 0;   /* co-op: the party's own menus (net_coop_menu.c) */
    }
    /* a team scenario takes its own size */
    if (!s->teams) n = add(ids, hostonly, n, GEVR_SETUP_PLAYERS, 1);
    n = add(ids, hostonly, n, GEVR_SETUP_SCENARIO, 1);
    /* while the next round is voted, the map is everyone's vote */
    n = add(ids, hostonly, n, s->voting ? GEVR_SETUP_NEXT_MAP : GEVR_SETUP_LEVEL, !s->voting);
    if (!s->yolt) n = add(ids, hostonly, n, GEVR_SETUP_LENGTH, 1);
    if (!s->goldengun)
    {
        if (s->voting) n = add(ids, hostonly, n, GEVR_SETUP_NEXT_WEAPONS, 0);
        else
        {
            n = add(ids, hostonly, n, GEVR_SETUP_WEAPONS, 1);
            if (s->custom) n = add(ids, hostonly, n, GEVR_SETUP_CUSTOM, 1);
        }
    }
    n = add(ids, hostonly, n, GEVR_SETUP_CHARACTER, 0);
    n = add(ids, hostonly, n, GEVR_SETUP_HEALTH, 0);
    n = add(ids, hostonly, n, GEVR_SETUP_HANDICAPS, 1);
    if (s->teams) n = add(ids, hostonly, n, GEVR_SETUP_TEAMS, 0);
    n = add(ids, hostonly, n, GEVR_SETUP_DUAL_WIELD, 1);
    n = add(ids, hostonly, n, GEVR_SETUP_LOADOUTS, 1);
    if (s->loadouts) n = add(ids, hostonly, n, GEVR_SETUP_LOADOUT, 0);
    /* bots change between rounds, and only the host sees them (net_core.c gevrNetBotRowsEditable) */
    if (s->host && !s->playing) n = add(ids, hostonly, n, GEVR_SETUP_BOTS, 1);
    n = add(ids, hostonly, n, GEVR_SETUP_NEXT_ROUND, 1);
    if (s->teams) n = add(ids, hostonly, n, GEVR_SETUP_FRIENDLY_FIRE, 1);
    if (s->host && s->can_return) n = add(ids, hostonly, n, GEVR_SETUP_RETURN, 1);
    if (s->split && s->guest) n = add(ids, hostonly, n, GEVR_SETUP_DROP_OUT, 0);
    if (!s->guest) n = add(ids, hostonly, n, GEVR_SETUP_FULL_MENU, 0);
    return n;
}
