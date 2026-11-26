/*
 * test_rom_import.py's harness: gevr_romimport.c with the real byte-order
 * decoder (gevr_romcheck.c) and a layout table of synthetic headers in place
 * of gevr_romlayout.c's (whose CRCs are the real cartridges').
 *
 *   rom_import_native <dataDir>   prints "pending N", then the sort's result
 */
#include "gevr_romimport.h"
#include "gevr_romlayout.h"

#include <stdio.h>
#include <string.h>

static const struct gevrRomLayout s_layouts[] = {
	{ .name = "GoldenEye 007 (US)", .tag = "ge", .title = "GOLDENEYE", .code = "NGEE", .crc = { 1, 1, 1, 1, 1, 1, 1, 1 }, .romsize = 0xc00000 },
	{ .name = "Goldfinger 64", .tag = "gf64", .title = "GOLDFINGER", .code = "NGFE", .crc = { 2, 2, 2, 2, 2, 2, 2, 2 }, .romsize = 0x1800000 },
	{ .name = "Tomorrow Never Dies 64", .tag = "tnd64", .title = "GOLDENEYE", .code = "NGEE", .crc = { 3, 3, 3, 3, 3, 3, 3, 3 }, .romsize = 0x2000000 },
	{ .name = "Mirror Mode (Wreck)", .tag = "mirror", .title = "GOLDENEYE", .code = "NGEE", .crc = { 4, 4, 4, 4, 4, 4, 4, 4 }, .romsize = 0xc00000 },
	{ .name = "Mario Characters", .tag = "mario", .title = "GOLDENEYE", .code = "NGEE", .crc = { 5, 5, 5, 5, 5, 5, 5, 5 }, .romsize = 0x1000000 },
};

const struct gevrRomLayout *gevrRomLayoutMatchHeader(const uint8_t *header, uint32_t size)
{
	unsigned i;
	for (i = 0; i < sizeof(s_layouts) / sizeof(s_layouts[0]); i++) {
		const struct gevrRomLayout *L = &s_layouts[i];
		if (size == L->romsize && !memcmp(header + 0x20, L->title, strlen(L->title))
				&& !memcmp(header + 0x3b, L->code, 4) && !memcmp(header + 0x10, L->crc, 8))
			return L;
	}
	return NULL;
}

int main(int argc, char **argv)
{
	char summary[1024];
	int gotGe = -1, filed;
	if (argc < 2) return 2;
	if (!strcmp(argv[1], "take")) {
		/* take <dataDir> <file>...: a picker's pick; one line per file, then the count handed to the sort */
		int i;
		gevrRomImportTakeBegin(argv[2]);
		for (i = 3; i < argc; i++) {
			struct gevrRomImportTake t;
			const char *slash = strrchr(argv[i], '/');
			gevrRomImportTake(fopen(argv[i], "rb"), slash ? slash + 1 : argv[i], argv[2], &t);
			printf("file%d %d %d %d %s\n", i - 3, t.xbla, t.ce, t.staged, t.cePath[0] ? t.cePath : "-");
		}
		printf("moved %d\n", gevrRomImportTakeEnd(argv[2]));
		return 0;
	}
	printf("pending %d\n", gevrRomImportPending(argv[1]));
	filed = gevrRomImportRun(argv[1], summary, (int)sizeof(summary), &gotGe);
	printf("filed %d\ngotGe %d\nsummary %s\n", filed, gotGe, summary);
	printf("after %d\n", gevrRomImportPending(argv[1]));
	return 0;
}
