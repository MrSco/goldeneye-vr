#include <stdlib.h>
#include <string.h>
#include "game/bondview.h"
#include "../../src/game/inititemslots.c"
#define EXPORT __declspec(dllexport)
struct player *g_CurrentPlayer;
static unsigned char *block;
static size_t allocated;
void *mempAllocBytesInBank(u32 size, u8 bank) {
    (void)bank; allocated=size; block=malloc(size+8192);
    memset(block,0,size); memset(block+size,0xa5,8192); return block;
}
/* INSERT_PRODUCTION_REINIT */
EXPORT int test_inventory_bounds(void) {
    struct player player={0}; g_CurrentPlayer=&player;
    for(int extra=0;extra<100;extra++) {
        alloc_additional_item_slots(extra);
        for(int life=0;life<100;life++) {
            bondinvReinitInv();
            for(int i=0;i<128;i++) if(block[allocated+i]!=0xa5) {free(block);return 1;}
            for(int i=0;i<player.equipmaxitems;i++) if(player.p_itemcur[i].type!=-1) {free(block);return 2;}
        }
        free(block);
    }
    return 0;
}
