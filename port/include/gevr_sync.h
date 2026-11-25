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

#else

static inline void gevrSyncLaunch(void) {}
static inline void gevrSyncFileSaved(const char *path) { (void)path; }
static inline void gevrSyncStatus(char *buf, int size) { if (buf && size > 0) buf[0] = 0; }
static inline int gevrSyncTakeMessage(char *buf, int size) { if (buf && size > 0) buf[0] = 0; return 0; }
static inline int gevrSyncActive(void) { return 0; }

#endif

#ifdef __cplusplus
}
#endif
#endif
