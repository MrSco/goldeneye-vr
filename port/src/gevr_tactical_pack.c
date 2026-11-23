#include "gevr_tactical_pack.h"
#include "fs.h"
#include <string.h>

#define TACTICAL_PACK "$B/texture-packs/tactical/"

static int readLine(FILE *file, char *line, int capacity)
{
	if (!fgets(line, capacity, file))
		return 0;
	size_t length = strlen(line);
	if (!length || line[length - 1] != '\n')
		return 0;
	line[--length] = '\0';
	if (length && line[length - 1] == '\r')
		line[--length] = '\0';
	return length != 0;
}

int gevrTacticalPackInstalled(struct gevrTacticalPackInfo *info)
{
	FILE *manifest = fsFileOpenRead(TACTICAL_PACK "tactical.manifest");
	if (!manifest)
		return 0;

	const char alnum[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	char header[16], version[35], digest[67];
	const int manifestOk = readLine(manifest, header, sizeof(header))
		&& strcmp(header, "GEVRTACTICAL1") == 0
		&& readLine(manifest, version, sizeof(version)) && strchr(alnum, version[0])
		&& strlen(version) <= 32
		&& strspn(version, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-") == strlen(version)
		&& readLine(manifest, digest, sizeof(digest))
		&& strlen(digest) == 64 && strspn(digest, "0123456789abcdef") == 64
		&& fgetc(manifest) == EOF && !ferror(manifest);
	fclose(manifest);

	if (!manifestOk || fsFileSize(TACTICAL_PACK "files.sha256") <= 0
		|| fsFileSize(TACTICAL_PACK "mods/tactical/gear/catalog.txt") <= 0
		|| fsFileSize(TACTICAL_PACK "mods/tactical/gear/presets.txt") <= 0
		|| fsFileSize(TACTICAL_PACK "mods/tactical/gear/stats.txt") <= 0)
		return 0;

	if (info) {
		strcpy(info->version, version);
		strcpy(info->contentId, digest);
	}
	return 1;
}
