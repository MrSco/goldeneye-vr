#include "gevr_tactical_presets.h"
#include <stdlib.h>
#include <string.h>

/* Both the native Workshop and the Windows editor use GEVRPRESETS1. */
static const char format[] = "GEVRPRESETS1";

static int presetLine(FILE *file, char *line, size_t capacity)
{
	size_t length = 0;
	int byte;
	while ((byte = fgetc(file)) != '\n' && byte != EOF) {
		if (byte == 0 || length == capacity - 1)
			return -1;
		line[length++] = (char)byte;
	}
	if (ferror(file))
		return -1;
	if (byte == EOF && !length)
		return 0;
	if (length && line[length - 1] == '\r')
		--length;
	line[length] = '\0';
	return 1;
}

static int validId(const char *id)
{
	return strspn(id, "0123456789abcdef") == 24 && id[24] == '\0';
}

static int validName(const char *name)
{
	const size_t length = strlen(name);
	return length && length < GEVR_PRESET_NAME_SIZE && name[0] != ' ' && name[length - 1] != ' '
		&& strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_") == length;
}

static int validChoice(const struct gevrTacticalPresetChoice *choice)
{
	return choice->path[0] && !strpbrk(choice->path, " \t\r\n\v\f")
		&& (validId(choice->part) || !strcmp(choice->part, "-"));
}

int gevrTacticalPresetReadHeader(FILE *file)
{
	char line[32];
	return presetLine(file, line, sizeof(line)) == 1 && !strcmp(line, format);
}

int gevrTacticalPresetRead(FILE *file, struct gevrTacticalPreset *preset)
{
	char line[256], count[4];
	int end = 0;
	const int state = presetLine(file, line, sizeof(line));
	if (state <= 0)
		return state;
	struct gevrTacticalPreset row = {0};
	if (sscanf(line, "P %24s %3[0-9] %24[^\n]%n", row.weapon, count, row.name, &end) != 3
		|| line[end] || !validId(row.weapon) || !validName(row.name))
		return -1;
	row.count = atoi(count);
	if (row.count > GEVR_PRESET_CHOICES_MAX)
		return -1;
	for (int i = 0; i < row.count; ++i) {
		struct gevrTacticalPresetChoice *choice = &row.choices[i];
		char extra;
		if (presetLine(file, line, sizeof(line)) != 1
			|| sscanf(line, "%159s %24s %c", choice->path, choice->part, &extra) != 2
			|| !validChoice(choice))
			return -1;
	}
	*preset = row;
	return 1;
}

int gevrTacticalPresetWriteHeader(FILE *file)
{
	return fprintf(file, "%s\n", format) >= 0;
}

int gevrTacticalPresetWrite(FILE *file, const struct gevrTacticalPreset *preset)
{
	if (!memchr(preset->weapon, '\0', sizeof(preset->weapon)) || !validId(preset->weapon)
		|| !memchr(preset->name, '\0', sizeof(preset->name)) || !validName(preset->name)
		|| preset->count < 0 || preset->count > GEVR_PRESET_CHOICES_MAX)
		return 0;
	for (int i = 0; i < preset->count; ++i) {
		const struct gevrTacticalPresetChoice *choice = &preset->choices[i];
		if (!memchr(choice->path, '\0', sizeof(choice->path))
			|| !memchr(choice->part, '\0', sizeof(choice->part)) || !validChoice(choice))
			return 0;
	}
	if (fprintf(file, "P %s %d %s\n", preset->weapon, preset->count, preset->name) < 0)
		return 0;
	for (int i = 0; i < preset->count; ++i) {
		if (fprintf(file, "%s %s\n", preset->choices[i].path, preset->choices[i].part) < 0)
			return 0;
	}
	return 1;
}
