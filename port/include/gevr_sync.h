#ifndef GEVR_SYNC_H
#define GEVR_SYNC_H
/*
 * Cross-platform sync of settings and saves (docs/cloud-sync.md): every
 * device writes its own record to a store the player owns (Google Drive; a
 * folder for tests and development) and reads the others'. Off until the
 * player links a store. Runs beside iCloud on the Apple builds: each carries
 * what changes in the files, so a device with both passes changes between them.
 *
 * port/pc/gevr_sync.cpp (the rules: port/pc/gevr_sync_core.cpp). A no-op on
 * the builds without the native services (Android phones, the server).
 */
#ifdef __cplusplus
extern "C" {
#endif

/* Google Drive's link, for the launcher (gevrSyncDrive) */
enum {
    GEVR_SYNC_DRIVE_NONE,       /* this build has no Google client: nothing to show */
    GEVR_SYNC_DRIVE_UNLINKED,
    GEVR_SYNC_DRIVE_STARTING,   /* asking Google for a code */
    GEVR_SYNC_DRIVE_WAITING,    /* the code is shown: the player enters it on a phone */
    GEVR_SYNC_DRIVE_FAILED,     /* note says why */
    GEVR_SYNC_DRIVE_LINKED
};

typedef struct GevrSyncDriveInfo {
    int state;           /* GEVR_SYNC_DRIVE_* */
    int syncing;         /* this launch synced with Drive (else: from the next start) */
    char code[32];       /* WAITING: "ABCD-EFGH" */
    char url[128];       /* WAITING: where to enter it */
    char account[128];   /* LINKED: the Google account, "" unknown */
    char note[256];      /* FAILED: why */
    char status[160];    /* syncing: what this launch's sync did */
} GevrSyncDriveInfo;

/* The save check at START (gevrSyncSaveCheck*) */
enum {
    GEVR_SAVECHECK_IDLE,
    GEVR_SAVECHECK_CHECKING,   /* reading the other devices' saves (about 5 s at most) */
    GEVR_SAVECHECK_NEWER,      /* another device's save would win: from, when */
    GEVR_SAVECHECK_NONE,       /* nothing newer: start */
    GEVR_SAVECHECK_FAILED      /* couldn't check: play anyway, or try again */
};

typedef struct GevrSyncSaveCheck {
    int state;           /* GEVR_SAVECHECK_* */
    char from[64];       /* NEWER: the device's name */
    char when[32];       /* NEWER: when it was written, "2026-11-25 20:14" */
} GevrSyncSaveCheck;

#if (defined(GEVR_PC) || defined(GEVR_NATIVE_SERVICES)) && !defined(GEVR_HEADLESS)

/* main: after the save folder is known (and after gevrCloudLaunch), before
 * goldeneye.ini and goldeneye-vr.ini are read. The other devices' settings and
 * saves into the files; this device's changes to its record. */
void gevrSyncLaunch(void);

/* A settings or save file was just written (its path as written, "$S/..."
 * allowed): its changes go to this device's record, in the background. */
void gevrSyncFileSaved(const char *path);

/* One line for the launcher ("Sync: 3 settings from your other devices"), "" for none. */
void gevrSyncStatus(char *buf, int size);

/* A message that happened once (a save conflict), taken. */
int gevrSyncTakeMessage(char *buf, int size);

/* 1 when this launch syncs (a store is linked). */
int gevrSyncActive(void);

/* Google Drive: the state, and starting, stopping and undoing the sign-in. A
 * device signed in syncs from its next start (the game has read its files). */
int gevrSyncDrive(GevrSyncDriveInfo *out);
void gevrSyncDriveLink(void);
void gevrSyncDriveCancel(void);
void gevrSyncDriveUnlink(void);

/* ROMs and packs (port/pc/gevr_sync_files.cpp): a ROM or pack was added or
 * replaced, so the files pass looks now. */
void gevrSyncKick(void);
/* a transfer under way: 1, its share (-1 unknown) and its line ("Sync: uploading ge.z64") */
int gevrSyncProgress(float *done, char *buf, int size);
/* 1 once after a ROM (or pack) came: the launcher scans its ROMs again */
int gevrSyncTakeRomArrived(void);
/* "Big packs too": the texture packs travel as well (this device's choice) */
int gevrSyncPacks(void);
void gevrSyncSetPacks(int on);

/* This device's name in the sync ("Den"), its kind's ("Apple TV") by default. */
void gevrSyncDeviceName(char *buf, int size);
void gevrSyncSetDeviceName(const char *name);

/* Saves set aside by the sync (<save>.before-sync-<date>), newest first; one
 * chosen is used again from the next start, and goes to the other devices. */
int gevrSyncSavesAside(char (*names)[96], int max);
void gevrSyncUseSaveAside(const char *name);
int gevrSyncSaveChosen(char *buf, int size);   /* the one waiting for the next start */

/* The device's browser at that address (the sign-in page), where it has one. */
void gevrSyncOpenUrl(const char *url);

/* START: a save another device wrote since this launch (or one the launch
 * couldn't check) would otherwise be played over. Needed: a store is linked
 * and its saves weren't read in the last 2 minutes, or iCloud is on. Begin
 * runs the check off the launcher's thread; Poll says how it went (after about
 * 8 s with no answer: FAILED). Use puts the newer save in place now, this
 * device's set aside; Done forgets the check (Play now, Keep this one). */
int gevrSyncSaveCheckNeeded(void);
void gevrSyncSaveCheckBegin(void);
int gevrSyncSaveCheckPoll(GevrSyncSaveCheck *out);
void gevrSyncSaveCheckUse(void);
void gevrSyncSaveCheckDone(void);

#else

static inline void gevrSyncLaunch(void) {}
static inline void gevrSyncFileSaved(const char *path) { (void)path; }
static inline void gevrSyncStatus(char *buf, int size) { if (buf && size > 0) buf[0] = 0; }
static inline int gevrSyncTakeMessage(char *buf, int size) { if (buf && size > 0) buf[0] = 0; return 0; }
static inline int gevrSyncActive(void) { return 0; }
static inline int gevrSyncDrive(GevrSyncDriveInfo *out) { if (out) { out->state = GEVR_SYNC_DRIVE_NONE; out->syncing = 0; } return GEVR_SYNC_DRIVE_NONE; }
static inline void gevrSyncDriveLink(void) {}
static inline void gevrSyncDriveCancel(void) {}
static inline void gevrSyncDriveUnlink(void) {}
static inline void gevrSyncKick(void) {}
static inline int gevrSyncProgress(float *done, char *buf, int size) { (void)done; if (buf && size > 0) buf[0] = 0; return 0; }
static inline int gevrSyncTakeRomArrived(void) { return 0; }
static inline int gevrSyncPacks(void) { return 0; }
static inline void gevrSyncSetPacks(int on) { (void)on; }
static inline void gevrSyncDeviceName(char *buf, int size) { if (buf && size > 0) buf[0] = 0; }
static inline void gevrSyncSetDeviceName(const char *name) { (void)name; }
static inline int gevrSyncSavesAside(char (*names)[96], int max) { (void)names; (void)max; return 0; }
static inline void gevrSyncUseSaveAside(const char *name) { (void)name; }
static inline int gevrSyncSaveChosen(char *buf, int size) { if (buf && size > 0) buf[0] = 0; return 0; }
static inline void gevrSyncOpenUrl(const char *url) { (void)url; }
static inline int gevrSyncSaveCheckNeeded(void) { return 0; }
static inline void gevrSyncSaveCheckBegin(void) {}
static inline int gevrSyncSaveCheckPoll(GevrSyncSaveCheck *out) { if (out) out->state = GEVR_SAVECHECK_NONE; return GEVR_SAVECHECK_NONE; }
static inline void gevrSyncSaveCheckUse(void) {}
static inline void gevrSyncSaveCheckDone(void) {}

#endif

#ifdef __cplusplus
}
#endif
#endif
