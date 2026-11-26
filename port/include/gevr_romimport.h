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
 *   the XBLA copy (Bean)   -> not copied: the pickers pass it to the Mods page
 *
 * The picks are dataDir/picked-import/ (several files) and dataDir/picked.z64
 * (one file; it is sorted only when it is a .zip or another campaign's ROM, so
 * a bad GoldenEye dump still gets the launcher's own message). Nothing here
 * switches the campaign: a batch would leave the last one chosen.
 */
#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_ROM_IMPORT_DIR "picked-import"

/*
 * 1 when a pick is the player's GoldenEye XBLA copy rather than ROMs: a .7z,
 * an Xbox 360 package (LIVE, CON, PIRS) or a zip larger than any zip of ROMs
 * (GEVR_ROM_IMPORT_ZIP_MAX). The pickers hand it to the Mods page's Bean
 * import (ModManager.importOwn, pc_mods.cpp), which reads it where it is.
 * head is its first bytes (6 are enough), size its length.
 */
#define GEVR_ROM_IMPORT_ZIP_MAX (256u << 20)
int gevrRomImportIsXblaCopy(const unsigned char *head, unsigned long n, unsigned long long size);

/* 1 when there is something to sort. Cheap: reads one header at most. */
int gevrRomImportPending(const char *dataDir);

/*
 * Sorts every pick and deletes it. Returns the number of ROMs filed; summary
 * gets a line for the launcher ("Added GoldenEye 007, Goldfinger 64. Not
 * recognised: notes.txt."). *gotGe is 1 when picked.z64 now waits for the
 * launcher's check. Blocking: up to a few seconds for a large zip.
 */
int gevrRomImportRun(const char *dataDir, char *summary, int summaryLen, int *gotGe);

#ifdef __cplusplus
}
#endif
