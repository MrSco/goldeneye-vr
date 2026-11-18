#include <ultra64.h>
#include <bondgame.h>
#include "chrobjdata.h"

//D:8003D410
#include <assets/obseg/chr/chrModelFileHeaders.inc.c>

//struct ChrModelFileRecord c_item_entries[] = {
#include <assets/obseg/chr/chrModelFileRecords.inc.c>
#ifdef GEVR
/*
 * GoldenEye's own table, where the cartridge keeps it. A hack campaign points
 * c_item_entries at its own characters (gevr_romcampaign.c), but the code the
 * hack never repointed still reads this one: Mario Characters' pause reads
 * entry 41 (suit_lf_hand, the watch arm, scale 0.1) here, while its own entry
 * 41 is CmariointroZ at 1.0, which drew the pause's arm ten times too big.
 */
struct ChrModelFileRecord *const g_gevrRetailChrRecords = gevrRetailChrRecords;
#endif

//D:8003E464
#include <assets/obseg/chr/chrHeadHats.inc.c>
