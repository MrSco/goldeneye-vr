#ifndef GEVR_MPOPTIONS_H
#define GEVR_MPOPTIONS_H
/*
 * The in-match Multiplayer Options folder's rows (online, mpmenu.c MENU_SETUP):
 * which rows a seat sees, in what order, and which are the host's. No game
 * headers, so port/tests compiles it as written (mpoptions_rows_native.c).
 */
enum {
    GEVR_SETUP_PLAYERS,        /* the match's player count (not local pads) */
    GEVR_SETUP_SCENARIO,       /* opens the Scenario list */
    GEVR_SETUP_LEVEL,          /* opens the stage photos */
    GEVR_SETUP_NEXT_MAP,       /* the vote's map: everyone, opens the photos to vote */
    GEVR_SETUP_LENGTH,
    GEVR_SETUP_WEAPONS,
    GEVR_SETUP_CUSTOM,         /* the custom set's four guns: a sheet */
    GEVR_SETUP_NEXT_WEAPONS,   /* the vote's weapons */
    GEVR_SETUP_CHARACTER,      /* opens Select Character */
    GEVR_SETUP_HEALTH,         /* opens the Handicap screen; the host's value is the match's health */
    GEVR_SETUP_HANDICAPS,      /* the host's rule: players pick their own health */
    GEVR_SETUP_TEAMS,          /* opens the Teams screen */
    GEVR_SETUP_DUAL_WIELD,
    GEVR_SETUP_LOADOUTS,       /* the host's rule: players spawn with their own four guns */
    GEVR_SETUP_LOADOUT,        /* this seat's four guns: a sheet */
    GEVR_SETUP_BOTS,           /* bots, their count and difficulty: a sheet */
    GEVR_SETUP_NEXT_ROUND,
    GEVR_SETUP_FRIENDLY_FIRE,
    GEVR_SETUP_RETURN,         /* RETURN TO LOBBY: the host, during a countdown or a round */
    GEVR_SETUP_DROP_OUT,       /* a split screen's guest leaves the match */
    GEVR_SETUP_FULL_MENU,      /* the pause window (one player) or the full panel (split screen) */
    /* Horde's own (the host's, pending until START WAVE or Retry Horde: docs/horde-mode.md) */
    GEVR_SETUP_HORDE_MAP,      /* the run's map: opens the stage photos filtered to Horde's maps */
    GEVR_SETUP_STARTING_WAVE,  /* the run's first wave, 1..50 */
    GEVR_SETUP_AI_ALLY,        /* the host's AI ally (GoldenEye's campaign only) */
    /* the pre-match lobby (docs/match-lobby.md) */
    GEVR_SETUP_LOBBY,          /* the host's Lobby rule: the lobby before each match */
    GEVR_SETUP_WARMUP,         /* the host's Warmup rule: the practice after the lobby's START */
    GEVR_SETUP_LEAVE,          /* in the lobby: this machine leaves the match (a split screen's guest: Drop Out) */
    GEVR_SETUP_ROW_COUNT
};
enum { GEVR_SETUP_MODE_DEATHMATCH, GEVR_SETUP_MODE_COOP, GEVR_SETUP_MODE_HORDE };

typedef struct {
    unsigned char mode;        /* GEVR_SETUP_MODE_* */
    unsigned char host;        /* this seat is the host's own (never on a dedicated server) */
    unsigned char playing;     /* a round in progress (else the warmup, or between rounds) */
    unsigned char teams;       /* a team scenario */
    unsigned char voting;      /* the next round is voted */
    unsigned char goldengun;   /* The Man with the Golden Gun: its own weapons */
    unsigned char yolt;        /* You Only Live Twice: no game length */
    unsigned char custom;      /* the custom weapon set */
    unsigned char loadouts;    /* the host's LOADOUTS rule is on */
    unsigned char can_return;  /* the host may RETURN TO LOBBY now */
    unsigned char split;       /* more than one player at this machine */
    unsigned char guest;       /* this seat is a split screen's guest (not the machine's first) */
    unsigned char ally;        /* Horde: the AI ally is on offer (GoldenEye's campaign) */
    unsigned char lobby;       /* the pre-match lobby, on the title stage (no pause window there) */
    unsigned char lobby_rule;  /* the host's Lobby rule is on */
} GevrSetupState;

/* The rows for s, top to bottom, into ids (room for GEVR_SETUP_ROW_COUNT);
 * hostonly[i]: the host's row, read-only (dimmed) at any other seat. The
 * count: 0 is no folder (co-op plays its own menus). */
int gevrMpOptionsRowSet(const GevrSetupState *s, unsigned char *ids, unsigned char *hostonly);
#endif
