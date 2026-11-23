#ifndef GEVR_TACTICAL_PACK_H
#define GEVR_TACTICAL_PACK_H

#ifdef __cplusplus
extern "C" {
#endif

struct gevrTacticalPackInfo {
	char version[33];
	char contentId[65];  /* lowercase SHA-256 of the asset checksum list */
};

/* Reads the pack manifest and checks the required catalog files. info may be NULL. */
int gevrTacticalPackInstalled(struct gevrTacticalPackInfo *info);

#ifdef __cplusplus
}
#endif
#endif
