#ifndef GEVR_ICLOUD_H
#define GEVR_ICLOUD_H
/*
 * iCloud sync across the Apple builds (iPhone/iPad, Apple TV, Vision Pro):
 * docs/icloud-sync.md. One bundle id, so one iCloud store reaches all three.
 *
 *  - Settings and saves (goldeneye-vr.ini, goldeneye.ini, eeprom*.bin): the
 *    iCloud key-value store, merged key by key. Keys that belong to one kind of
 *    device (display, frame rate, touch layout...) are kept per device family.
 *  - ROMs (*.z64 in the data folder): CloudKit, the player's private database.
 *  - Texture packs (HD, AI, Bean, mods): CloudKit too, only when the player
 *    turns on "iCloud: big packs".
 *
 * Everything here is a no-op on the other platforms, and on an Apple build
 * signed without the iCloud entitlements (simulator builds, GEVR_ICLOUD unset).
 */
#ifdef __cplusplus
extern "C" {
#endif

#if defined(GEVR_IOS) && !defined(GEVR_HEADLESS)

/* main: after the save folder is known, before goldeneye.ini and
 * goldeneye-vr.ini are read. Merges the cloud's settings and saves into the
 * files (waiting a few seconds on a device's first sync) and starts the file
 * sync (ROMs, packs) in the background. */
void gevrCloudLaunch(void);

/* A settings or save file was just written (its path as written, "$S/..."
 * allowed): its changed keys go to the key-value store. */
void gevrCloudFileSaved(const char *path);

/* A ROM or a pack was added or replaced: the file sync looks again. */
void gevrCloudKick(void);

/* The launcher's "iCloud sync" and "iCloud: big packs" rows changed. */
void gevrCloudSetOptions(int sync, int packs);

/* One line for the launcher ("iCloud: downloading ge.z64 40%"), "" for none. */
void gevrCloudStatus(char *buf, int size);

/* An upload or download under way: 1, its share done (0..1) and its line
 * ("iCloud: uploading ge.z64 40%"); 0 when none is. */
int gevrCloudProgress(float *done, char *buf, int size);

/* A message that happened once ("ROM downloaded from iCloud."), taken. */
int gevrCloudTakeMessage(char *buf, int size);

/* 1 when this build can use iCloud at all (the settings rows show). */
int gevrCloudAvailable(void);

/* The check at START: 1 when iCloud has a save that would replace this
 * device's (another Apple device wrote it since this launch), its name and
 * time (seconds); and putting it in place (this one set aside). */
int gevrCloudSaveNewer(char *name, int nameSize, long long *when);
void gevrCloudSaveUseNewer(void);

#else

static inline void gevrCloudLaunch(void) {}
static inline void gevrCloudFileSaved(const char *path) { (void)path; }
static inline void gevrCloudKick(void) {}
static inline void gevrCloudSetOptions(int sync, int packs) { (void)sync; (void)packs; }
static inline void gevrCloudStatus(char *buf, int size) { if (buf && size > 0) buf[0] = 0; }
static inline int gevrCloudProgress(float *done, char *buf, int size) { (void)done; if (buf && size > 0) buf[0] = 0; return 0; }
static inline int gevrCloudTakeMessage(char *buf, int size) { if (buf && size > 0) buf[0] = 0; return 0; }
static inline int gevrCloudAvailable(void) { return 0; }
static inline int gevrCloudSaveNewer(char *name, int nameSize, long long *when) { (void)when; if (name && nameSize > 0) name[0] = 0; return 0; }
static inline void gevrCloudSaveUseNewer(void) {}

#endif

#ifdef __cplusplus
}
#endif
#endif
