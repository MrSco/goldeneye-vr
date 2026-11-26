#pragma once
/*
 * The launcher's ROM picker takes several files at once, or one .zip holding
 * them (e-ee's request): each ROM is told apart by its header and filed under
 * the name its picker would have given it.
 *
 *   GoldenEye 007          -> picked.z64, adopted as ge.z64 by the launcher
 *   GoldenEye X            -> picked-gex.z64, adopted by the Mods page
 *   Goldfinger 64, TND 64,
 *   Mirror Mode, Mario     -> their campaign files (gevr_romlayout.h), at once
 *   GoldenEye XBLA (Bean),
 *   the Community Edition  -> not copied: the pickers pass them to the Mods
 *                             page (ModManager.importOwn / applyCeWhenReady)
 *
 * Each picked file, or each ROM in a picked zip (a zip of everything, a zip
 * in it), is taken into dataDir/picked-import/ by gevrRomImportTake, which
 * also says whether the file holds the release or the CE updater. The
 * launcher then sorts picked-import/ (gevrRomImportRun), and dataDir/picked.z64
 * when a picker put a zip, the release or another campaign's ROM there (a bad
 * GoldenEye dump alone keeps the launcher's own message). Nothing here
 * switches the campaign: a batch would leave the last one chosen.
 */
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_ROM_IMPORT_DIR "picked-import"

/* 1 when there is something to sort. Cheap: reads one header at most. */
int gevrRomImportPending(const char *dataDir);

/*
 * Sorts every pick and deletes it. Returns the number of ROMs filed; summary
 * gets a line for the launcher ("Added GoldenEye 007, Goldfinger 64. Not
 * recognised: notes.txt."). *gotGe is 1 when picked.z64 now waits for the
 * launcher's check. Blocking: up to a few seconds for a large zip.
 */
int gevrRomImportRun(const char *dataDir, char *summary, int summaryLen, int *gotGe);

/*
 * The pickers' side, for one pick: Begin, Take for each file chosen (src is
 * read, seeking, and closed), End to hand what was taken to the sort at once.
 * out says what the file held besides ROMs: xbla, the release (unpacked, its
 * package, or an archive of them; the file goes to the Bean import as it is);
 * ce, the CE updater's files.diff in the file itself (the file goes to the CE
 * apply as it is); cePath, the updater as a zip inside the file, written out
 * there. staged counts the files taken for the sort, names it cannot use
 * included (the sort reports them). End returns that count for the pick.
 */
struct gevrRomImportTake {
	int xbla;
	int ce;
	char cePath[1024];
	int staged;
};
void gevrRomImportTakeBegin(const char *dataDir);
int gevrRomImportTake(FILE *src, const char *label, const char *dataDir, struct gevrRomImportTake *out);
int gevrRomImportTakeEnd(const char *dataDir);

#ifdef __cplusplus
}
#endif
