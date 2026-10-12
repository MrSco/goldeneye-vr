#include <ultra64.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

static Mtx roomMatrices[1], snapshots[2];
static int used;
uintptr_t osVirtualToPhysical(void *addr) { return (uintptr_t)addr; }
s32 setupRoomTransformationMatrix(s32 room)
{
    roomMatrices[0].m[0][0] = room;
    return 0; /* Force eviction/reuse of the same cache slot. */
}
Mtx *dynAllocateMatrix(void) { assert(used < 2); return &snapshots[used++]; }

/* PRODUCTION */

int main(void)
{
    Gfx commands[2];
    assert(applyRoomMatrixToDisplayList(commands, 10) == commands + 1);
    assert(applyRoomMatrixToDisplayList(commands + 1, 20) == commands + 2);
    Mtx *first = (Mtx *)(uintptr_t)commands[0].words.w1;
    Mtx *second = (Mtx *)(uintptr_t)commands[1].words.w1;
    assert(first != second && first != roomMatrices && second != roomMatrices);
    assert(first->m[0][0] == 10 && second->m[0][0] == 20);
    puts("Bean room matrix commands retain their transforms after cache eviction");
    return 0;
}
