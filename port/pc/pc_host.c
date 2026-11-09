/*
 * The PC game's stand-ins for the in-VR launcher (docs/pc-platforms.md, P1).
 *
 * On the Quest, port/vr/vr_launcher.cpp runs before the ROM is read: ROM,
 * art set, comfort, multiplayer. The PC build has no launcher yet, so it
 * boots as the dedicated server does (port/server/server_host.c): settings
 * from goldeneye-vr.ini beside the exe, the ROM from the data folder, then
 * straight into the game.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <PR/ultratypes.h>

#include "system.h"

int gevrVrPumpBegin(void);   /* gevr_engine_shim.c */
void gevrVrPumpEnd(void);

/* port/flat/flat_headset.c: a dedicated server slows down while empty; a player's game never does */
int gevrServerIdle(void) { return 0; }

void gevrLauncherRun(void)
{
	/* The headset's first frame loads goldeneye-vr.ini (gevr_engine_shim.c gevrVrFrameBegin). */
	gevrVrPumpBegin();
	gevrVrPumpEnd();
	sysLogPrintf(LOG_NOTE, "pc: no launcher yet, booting the ROM in the data folder");
}

void gevrLobbyGameTick(void) { }
void gevrLobbySessionStopped(void) { }

/* The headset goes back to its launcher; with none, the game ends. */
void gevrRestartToLauncher(void)
{
	sysLogPrintf(LOG_NOTE, "pc: restart to launcher, exiting");
	exit(0);
}
