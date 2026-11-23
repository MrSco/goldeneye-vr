#include "gevr_tactical_presets.h"
#include <assert.h>
#include <string.h>

static FILE *presetFile(const char *text)
{
	FILE *file = tmpfile();
	assert(file && fputs(text, file) >= 0);
	rewind(file);
	return file;
}

int main(int argc, char **argv)
{
	const char valid[] = "GEVRPRESETS1\r\nP 0123456789abcdef01234567 2 Sample Build\r\n"
		"mod_foregrip abcdef0123456789abcdef01\r\nmod_muzzle -\r\n";
	struct gevrTacticalPreset sample = {0}, roundtrip = {0};
	FILE *file = presetFile(valid);
	assert(gevrTacticalPresetReadHeader(file));
	assert(gevrTacticalPresetRead(file, &sample) == 1);
	assert(!strcmp(sample.weapon, "0123456789abcdef01234567"));
	assert(!strcmp(sample.name, "Sample Build") && sample.count == 2);
	assert(!strcmp(sample.choices[0].path, "mod_foregrip") && !strcmp(sample.choices[1].part, "-"));
	assert(gevrTacticalPresetRead(file, &sample) == 0);
	fclose(file);

	file = tmpfile();
	assert(file && gevrTacticalPresetWriteHeader(file) && gevrTacticalPresetWrite(file, &sample));
	rewind(file);
	assert(gevrTacticalPresetReadHeader(file));
	assert(gevrTacticalPresetRead(file, &roundtrip) == 1);
	assert(!memcmp(&roundtrip, &sample, sizeof(sample)));
	fclose(file);

	file = presetFile("P 0123456789abcdef01234567 96 Too Many\n");
	assert(gevrTacticalPresetRead(file, &roundtrip) == -1);
	assert(!memcmp(&roundtrip, &sample, sizeof(sample)));
	fclose(file);
	file = presetFile("P 0123456789abcdef01234567 2 Incomplete\nmod_muzzle -\n");
	assert(gevrTacticalPresetRead(file, &roundtrip) == -1);
	fclose(file);
	file = presetFile("P 0123456789abcdef01234567 1 Invalid Part\nmod_muzzle 123\n");
	assert(gevrTacticalPresetRead(file, &roundtrip) == -1);
	fclose(file);
	file = presetFile("GEVRPRESETS2\n");
	assert(!gevrTacticalPresetReadHeader(file));
	fclose(file);

	if (argc == 2) {
		file = fopen(argv[1], "rb");
		assert(file && gevrTacticalPresetReadHeader(file));
		int parsed = 0, next;
		while ((next = gevrTacticalPresetRead(file, &sample)) == 1)
			++parsed;
		assert(next == 0 && parsed > 0);
		fclose(file);
		printf("PASS: read %d bundled presets\n", parsed);
	}
	puts("PASS: Tactical preset parser and writer");
	return 0;
}
