/**
 * Replacement for the SGI Indy host link (src/game/indy_comms.c).
 *
 * Rare's dev kits could serve assets over a cable from an Indy workstation
 * instead of reading them from the cartridge, and ob.c still falls back to that
 * path whenever a file's hw_address is NULL:
 *
 *     if (file_resource_table[index].hw_address == 0) //IF NULL, check indy
 *         resource_load_from_indy(...);
 *
 * The original implementation talks to the cartridge port directly, so on a
 * headset it faults - which is exactly what happened on the first boot that got
 * this far: langInit() asked for a file the ROM loader had not bound, took this
 * path, and died in resource_load_from_indy with SIGSEGV.
 *
 * There is no host on the other end here, so these report failure instead. That
 * turns a crash into a log line naming the file that is missing, which is the
 * difference between a stack trace and something we can act on.
 */

#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <PR/ultratypes.h>
#include "system.h"

s32 indycommInit(void)
{
	return 0;
}

void indycommHostinit(void)
{
}

void indycommHostCloseConnection(void)
{
}

void indycommHost7F0D0124(void)
{
}

void indycomm_removed(void)
{
}

/*
 * Reached when a file has no cartridge address. Reporting a size of zero makes
 * resource_load_from_indy() copy nothing and leave poolRemaining at 0, which
 * the callers already treat as "did not load".
 */
u8 *indycommHostCheckFileExists(char *name, s32 *size)
{
	if (size) {
		*size = 0;
	}

	/*
	 * fileGetIndex() calls this only after failing to strcmp the name against
	 * every row of file_resource_table, so reaching here means the name is not
	 * in the table at all - not that the file is missing from the cartridge.
	 * Returning 0 makes fileGetIndex give up and hand back index 0 (NULLFILE).
	 */
	sysLogPrintf(LOG_ERROR, "rom: '%s' is not in file_resource_table.",
			name ? name : "(null)");

	return NULL;
}

void indycommHostLoadFile(char *filename, u8 *targetloc)
{
	(void)targetloc;

	sysLogPrintf(LOG_ERROR, "rom: cannot load '%s': no host link on this port.",
			filename ? filename : "(null)");
}

void indycommHostRamRomLoad(char *filename, u8 *target, s32 size)
{
	(void)target;
	(void)size;

	sysLogPrintf(LOG_ERROR, "rom: cannot load demo '%s': no host link on this port.",
			filename ? filename : "(null)");
}

void indycommHostSaveFile(char *filename, s32 size, u8 *data)
{
	(void)filename;
	(void)size;
	(void)data;
}

void indycommHostSendDump(char *filename, u8 *data, u32 size)
{
	(void)filename;
	(void)data;
	(void)size;
}

u8 *indycommHostSendCmd(u8 *cmdstr)
{
	(void)cmdstr;
	return NULL;
}
