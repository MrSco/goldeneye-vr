/*
 * Mod packs (docs/mod-system.md): the manifest, install from a zip, the
 * installed list, and the mod campaign this run plays. One C module for
 * every build (the headset, PC, Apple, Android phones, the dedicated
 * server); the launchers' file pickers only hand it a zip.
 *
 * An installed mod is $S/texture-packs/mods/<id>/ holding mod.json,
 * files.sha256 and the files it lists. A mod campaign is a base cartridge
 * (VrCampaign) plus the mod's id (VrCampaignMod, ini CampaignMod=).
 */
#ifndef GEVR_MODPACK_H
#define GEVR_MODPACK_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_MOD_FORMAT 1
#define GEVR_MOD_API_MIN 1   /* the API versions this build runs */
#define GEVR_MOD_API_MAX 1
#define GEVR_MOD_ID_MAX 32
#define GEVR_MOD_TITLE_MAX 48
#define GEVR_MOD_VERSION_MAX 24
#define GEVR_MOD_SITE_MAX 200

/* Install limits (docs/mod-system.md, Installing). */
#define GEVR_MOD_MAX_FILES 10000
#define GEVR_MOD_MAX_FILE_BYTES (256LL << 20)
#define GEVR_MOD_MAX_TOTAL_BYTES (2LL << 30)
#define GEVR_MOD_MAX_MANIFEST_BYTES (64 << 10)
#define GEVR_MOD_MAX_LIST_BYTES (2 << 20)

enum { GEVR_MOD_CAMPAIGN = 1, GEVR_MOD_OVERLAY = 2 };
enum { GEVR_MOD_MODE_COOP = 1, GEVR_MOD_MODE_DEATHMATCH = 2, GEVR_MOD_MODE_HORDE = 4 };

typedef struct gevrModManifest {
    int format;
    int api;
    int kind;          /* GEVR_MOD_CAMPAIGN / GEVR_MOD_OVERLAY */
    int base;          /* GEVR_CAMPAIGN_* */
    int coop;
    int affectsPlay;
    unsigned modes;    /* GEVR_MOD_MODE_* */
    char id[GEVR_MOD_ID_MAX + 1];
    char title[GEVR_MOD_TITLE_MAX + 1];
    char by[GEVR_MOD_TITLE_MAX + 1];
    char version[GEVR_MOD_VERSION_MAX + 1];
    char site[GEVR_MOD_SITE_MAX + 1];
} gevrModManifest;

typedef struct gevrModInfo {
    gevrModManifest m;
    uint8_t content[32];   /* the content ID: SHA-256 of files.sha256 */
} gevrModInfo;

/* Each returns 0, or -1 with a sentence in why (-2: cancelled). */
int gevrModParseManifest(const char *json, size_t len, gevrModManifest *out, char *why, size_t whyLen);
/* Checks everything before anything is in place; a failure leaves the
 * installed mods as they were. progress(percent, ud) returning 0 cancels. */
int gevrModInstallZip(const char *zipPath, gevrModInfo *out, int (*progress)(int percent, void *ud), void *ud,
                      char *why, size_t whyLen);
/* The same from an open file (a picker's: Windows' wide path, Android's
 * descriptor); it must be seekable. The caller closes it. */
int gevrModInstallZipFile(FILE *f, gevrModInfo *out, int (*progress)(int percent, void *ud), void *ud,
                          char *why, size_t whyLen);
int gevrModLoad(const char *id, gevrModInfo *out, char *why, size_t whyLen);
int gevrModRemove(const char *id, char *why, size_t whyLen);
/* The installed mods' ids (folder names), sorted; returns how many (at most max). */
int gevrModList(char (*ids)[GEVR_MOD_ID_MAX + 1], int max);
int gevrModIdValid(const char *id);
/* "ge", "gf", "tnd", "mirror", "mario" -> GEVR_CAMPAIGN_*, or -1 */
int gevrModBaseCampaign(const char *base);
/* $S/texture-packs/mods, resolved at boot (gevrModBoot) */
const char *gevrModsRoot(void);

/* The launcher, on its own thread before it lists, installs or removes:
 * resolves the mods folder (fsFullPath isn't thread-safe) and tidies a
 * crash's leftovers. gevrModBoot does it too. */
void gevrModStartup(void);
/* romdataInit (gevr_romload.c), first: tidies the mods folder and, when
 * VrCampaignMod names a campaign mod, checks it and sets VrCampaign to its
 * cartridge. A mod that can't run is off for this run, with the launcher's
 * note (GEVR_CAMPAIGN_FAILED_FILE). */
void gevrModBoot(void);
/* romdataInit, when the cartridge did not load: the mod is off too. */
void gevrModBaseFailed(const char *campaignName);
/* The mod campaign this run plays, or NULL. */
const gevrModInfo *gevrModActive(void);

#ifdef __cplusplus
}
#endif

#endif
