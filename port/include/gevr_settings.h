/*
 * The settings table (docs/launcher-unify.md): every VR setting defined once,
 * read by the in-game screen (port/vr/vr_pause_menu.cpp, gevr_panel_ui.h) and,
 * from phase 2, by the launcher. Plain C, so the game's C files, the port's
 * and the C++ launcher share it. port/src/gevr_settings.c holds the rows.
 *
 * A row has positions 0..n-1: a toggle's 0 off and 1 on, a choice's values in
 * order, a range's steps from its low end. Text, setting and stepping go
 * through the position, so a stepper, a checkbox, a dropdown and a slider all
 * read and write the same thing. Setting a row applies it and saves
 * goldeneye-vr.ini at once.
 */
#ifndef GEVR_SETTINGS_H
#define GEVR_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GEVR_SET_TOGGLE,   /* a checkbox: 0 off, 1 on */
    GEVR_SET_CHOICE,   /* a few named values: a dropdown */
    GEVR_SET_RANGE     /* ordered steps: a slider */
};

/* The sections, in the in-game screen's tab order */
enum {
    GEVR_SEC_COMFORT,
    GEVR_SEC_CONTROLS,
    GEVR_SEC_GESTURES,
    GEVR_SEC_WEAPONS,
    GEVR_SEC_DISPLAY,
    GEVR_SEC_RULES,
    GEVR_SEC_MODS,
    GEVR_SEC_COUNT
};

/* The launcher's places for them: its tabs and sub-tabs (docs/launcher-unify.md) */
enum {
    GEVR_LSEC_PLAY,
    GEVR_LSEC_HANDS,       /* Controls > Hands */
    GEVR_LSEC_GESTURES,    /* Controls > Gestures */
    GEVR_LSEC_THROWING,    /* Controls > Throwing */
    GEVR_LSEC_COMFORT,
    GEVR_LSEC_SCREEN,
    GEVR_LSEC_RULES,       /* Rules > Game rules */
    GEVR_LSEC_ALLY,        /* Rules > AI ally */
    GEVR_LSEC_CHEATS,      /* Rules > Cheats */
    GEVR_LSEC_MODS,
    GEVR_LSEC_COUNT
};

/* Where a row shows */
#define GEVR_SET_INGAME   1u   /* the in-game screen */
#define GEVR_SET_LAUNCHER 2u   /* the launcher (phase 2) */
#define GEVR_SET_BOTH     (GEVR_SET_INGAME | GEVR_SET_LAUNCHER)

typedef struct GevrSettingInfo {
    const char *label;
    const char *tooltip;   /* may be NULL */
    int kind;              /* GEVR_SET_* */
    int section;           /* GEVR_SEC_*: the in-game screen's tab */
    int launcherSection;   /* GEVR_LSEC_*: the launcher's tab or sub-tab */
    unsigned where;        /* GEVR_SET_INGAME, GEVR_SET_LAUNCHER */
} GevrSettingInfo;

int gevrSettingCount(void);
int gevrSettingInfo(int id, GevrSettingInfo *out);
/* Shown now: rows that only matter while another is on (third person's
 * camera, dismemberment's thresholds) hide while it is off. */
int gevrSettingShown(int id);
int gevrSettingPositions(int id);
int gevrSettingIndex(int id);
/* The default position (Reset), or -1 when the row has none. */
int gevrSettingDefault(int id);
/* Clamped; applies (the display rate, the screen, the host's rule online) and saves. */
void gevrSettingSet(int id, int index);
/* dir -1 or +1 one position (clamped), 2 the next (wrapping): the stepper's. */
void gevrSettingStep(int id, int dir);
/* index < 0: the current value as it should read (online: the host's). */
void gevrSettingText(int id, int index, char *buf, int size);
/* Why it can't be changed now ("Needs GoldenEye X", "Host's choice"), or NULL. */
const char *gevrSettingBlocked(int id);

int gevrSettingSectionCount(void);
const char *gevrSettingSectionName(int section);
/* The section's rows shown now in `where`, in order: their ids into ids[max].
 * GEVR_SET_LAUNCHER takes a GEVR_LSEC_* section, GEVR_SET_INGAME a GEVR_SEC_*. */
int gevrSettingSectionRows(int section, unsigned where, int *ids, int max);

#ifdef __cplusplus
}
#endif

#endif
