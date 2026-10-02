/* Exercise the actual 64-bit combat allocator with canaries after every bank
 * allocation, including reset and repeated allocation/free/merge cycles. */
#include <stdlib.h>
#include <string.h>
#include "game/front.h"
#include "game/lv.h"
#include "game/chrai.h"
#include "game/player.h"
#include "../../src/game/vtxstore.c"
#define EXPORT __declspec(dllexport)
static struct { unsigned char *data; size_t size; } blocks[8];
static int block_count;
void *mempAllocBytesInBank(u32 size, u8 bank) {
    (void)bank;
    unsigned char *p = malloc(size+8192);memset(p,0,size);memset(p+size,0xa5,64);
    blocks[block_count].data=p;blocks[block_count++].size=size;return p;
}
s32 getPlayerCount(void) { return 4; }
s32 lvlGetCurrentStageToLoad(void) { return 39; }
PropRecord *chrpropGetActiveTail(void) { return NULL; }
union ModelRwData *modelGetNodeRwData(Model *model, ModelNode *node) { (void)model;(void)node;return NULL; }
void sub_GAME_7F056690(void) {}
static int canaries(void) {
    for(int i=0;i<block_count;i++) for(int j=0;j<64;j++)
        if(blocks[i].data[blocks[i].size+j]!=0xa5) return 0;
    return 1;
}
EXPORT int test_vtxstore(void) {
    block_count=0;sub_GAME_7F09B820();
    if(!canaries()) return 1;
    if(blocks[0].size != 80*sizeof(struct unk_09B7A0_struct_parent) ||
       blocks[2].size != 20*sizeof(struct unk_09B7A0_struct_parent)) return 2;
    for(int pass=0;pass<100;pass++) {
        Vertex *a=vtxstore_allocate(4,0xcccc,(void*)0x100000001ULL,0);
        Vertex *b=vtxstore_allocate(8,0xcccc,(void*)0x200000001ULL,0);
        Vertex *c=vtxstore_allocate(12,0xb0b,(void*)0x100000001ULL,1);
        Vertex *d=vtxstore_allocate(16,0xb0b,(void*)0x200000001ULL,1);
        if(!a||!b||!c||!d || a==b || c==d) return 3;
        memset(a,0x11,4*sizeof(Vertex));memset(b,0x22,8*sizeof(Vertex));
        memset(c,0x33,12*sizeof(Vertex));memset(d,0x44,16*sizeof(Vertex));
        if(!canaries()) return 4;
        sub_GAME_7F09C044(b);sub_GAME_7F09C044(a);sub_GAME_7F09C044(c);sub_GAME_7F09C044(d);
        if(word_CODE_bss_8007A0F0!=3000 || word_CODE_bss_8007A0F2!=500) return 5;
        sub_GAME_7F09BBBC();if(!canaries()) return 6;
    }
    for(int i=0;i<block_count;i++) free(blocks[i].data);
    return 0;
}
