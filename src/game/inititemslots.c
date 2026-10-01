#include <ultra64.h>
#include <memp.h>
#include "player.h"
#include "bondinv.h"
#include "inititemslots.h"

void reinit_gunheld_totaltime(void) {
    s32 i;
  
    g_CurrentPlayer->equipallguns = FALSE;
    
    for (i = 0; i != 10; i++) {
        g_CurrentPlayer->gunheldarr[i].totaltime = -1;
    }
}

void alloc_additional_item_slots(s32 additionalentries) {
  g_CurrentPlayer->equipmaxitems = additionalentries + 0x1e;
    /* InvItem contains three pointers: its cartridge size (20 bytes) is
     * too small on ARM64. Inventory resets on respawn otherwise write -1
     * into the model allocations following this bank allocation. */
    g_CurrentPlayer->p_itemcur = mempAllocBytesInBank(
        (g_CurrentPlayer->equipmaxitems * sizeof(InvItem) + 15U) & ~15U, MEMPOOL_STAGE);
  bondinvReinitInv();
}
