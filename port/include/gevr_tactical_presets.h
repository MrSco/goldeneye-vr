#ifndef GEVR_TACTICAL_PRESETS_H
#define GEVR_TACTICAL_PRESETS_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	GEVR_PRESET_ID_SIZE = 25,
	GEVR_PRESET_NAME_SIZE = 25,
	GEVR_PRESET_PATH_SIZE = 160,
	GEVR_PRESET_CHOICES_MAX = 95
};

struct gevrTacticalPresetChoice {
	char path[GEVR_PRESET_PATH_SIZE];
	char part[GEVR_PRESET_ID_SIZE];
};

struct gevrTacticalPreset {
	char weapon[GEVR_PRESET_ID_SIZE];
	char name[GEVR_PRESET_NAME_SIZE];
	int count;
	struct gevrTacticalPresetChoice choices[GEVR_PRESET_CHOICES_MAX];
};

int gevrTacticalPresetReadHeader(FILE *file);
/* Returns 1 for a complete preset, 0 at end of file, or -1 for malformed input. */
int gevrTacticalPresetRead(FILE *file, struct gevrTacticalPreset *preset);
int gevrTacticalPresetWriteHeader(FILE *file);
int gevrTacticalPresetWrite(FILE *file, const struct gevrTacticalPreset *preset);

#ifdef __cplusplus
}
#endif
#endif
