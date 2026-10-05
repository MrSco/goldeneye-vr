#include <ultra64.h>
#include "dyn.h"
#include <deb.h>
#include <token.h>
#include <str.h>
#include <memp.h>
#include <macro.h>
#ifdef GEVR
#include "player.h"
#include <bondconstants.h>
#include <boss.h>
#include <limits.h>
#include <string.h>
#include <system.h>
#endif

/**
 * This file handles memory usage for graphics related tasks.
 *
 * There are two pools, "gfx" and "vtx", which are used to store different data.
 *
 * The gfx pool (g_GfxBuffers) is sized based on the stage's -mgfx
 * argument. It contains only the master display list's GBI bytecode.
 * The master gdl is passed through all rendering functions in the game engine,
 * where each appends to the display list.
 *
 * The vtx pool (g_VtxBuffers) is sized based on the stage's -mvtx argument.
 * It is used for auxiliary graphics data such as vertex arrays, matrices and
 * colours.
 *
 * Both the gfx and vtx pools are split into two buffers of equal size.
 * Only one buffer is active at a time - the other is being drawn to the screen
 * while the active one is being built. Each time a frame is finished the active
 * buffer index is swapped to the other one.
 *
 * Both the gfx and vtx pools have a third element in them, but this is just a
 * marker for the end of the second element's allocation.
 */

u8 *g_GfxBuffers[3];
u8 *g_VtxBuffers[3];
u8 *g_GfxMemPos;
u8 g_GfxActiveBufferIndex;
s32 g_GfxRequestedDisplayList;
s32 D_800482E0 = 0;
#ifdef GEVR
/* indexed by getPlayerCount() - 1, to eight online (MAX_PLAYER_COUNT) */
s32 g_GfxSizesByPlayerCount[MAX_PLAYER_COUNT] = {0x10000, 0x18000, 0x20000, 0x28000, 0x28000, 0x28000, 0x28000, 0x28000};
s32 g_VtxSizesByPlayerCount[MAX_PLAYER_COUNT] = {0x10000, 0x18000, 0x20000, 0x28000, 0x30000, 0x38000, 0x40000, 0x48000};

#define GEVR_DYN_GFX_MIN (512 * 1024)
#define GEVR_DYN_VTX_MIN (256 * 1024)
#define GEVR_DYN_LOG_INTERVAL_US 5000000ULL
static s32 s_dynStage;
static size_t s_dynGfxCapacity, s_dynVtxCapacity;
static size_t s_dynGfxPeak, s_dynVtxPeak;
static u64 s_dynNextPeakLog;
static s32 s_dynPeakChanged, s_dynGfxWarned, s_dynVtxWarned;

static void gevrDynLogPeaks(s32 level)
{
    sysLogPrintf(level, "dyn: stage %d peak gfx %llu/%llu bytes, aux %llu/%llu bytes",
        s_dynStage, (unsigned long long)s_dynGfxPeak, (unsigned long long)s_dynGfxCapacity,
        (unsigned long long)s_dynVtxPeak, (unsigned long long)s_dynVtxCapacity);
}

static s32 gevrDynBudget(const char *token, const char *kind)
{
    char *end;
    long kib = strtol(token, &end, 0);
    if (end == token || kib <= 0 || kib > INT_MAX / 1024)
    {
        sysFatalError("dyn: stage %d invalid %s budget", (s32)bossGetStageNum(), kind);
    }
    return (s32)kib * 1024;
}

/* Keep vertices/lights/matrices at their original sizes; only dynAllocate's
 * byte requests are rounded to 16. Reject exhaustion before moving the cursor
 * or handing the caller a pointer into another frame's storage. */
static void *gevrDynAllocate(s32 count, size_t elementSize, s32 align16, const char *kind)
{
    uintptr_t base = (uintptr_t)g_VtxBuffers[g_GfxActiveBufferIndex];
    uintptr_t end = (uintptr_t)g_VtxBuffers[g_GfxActiveBufferIndex + 1];
    uintptr_t pos = (uintptr_t)g_GfxMemPos;
    size_t bytes;

    if (count < 0 || (size_t)count > SIZE_MAX / elementSize)
    {
        sysFatalError("dyn: stage %d buffer %u invalid %s count %d",
            s_dynStage, (unsigned)g_GfxActiveBufferIndex, kind, count);
    }
    bytes = (size_t)count * elementSize;
    if (align16)
    {
        if (bytes > SIZE_MAX - 15)
        {
            sysFatalError("dyn: stage %d buffer %u %s size overflow",
                s_dynStage, (unsigned)g_GfxActiveBufferIndex, kind);
        }
        bytes = (bytes + 15) & ~(size_t)15;
    }
    if (!base || pos < base || pos > end)
    {
        sysFatalError("dyn: stage %d buffer %u invalid aux cursor %p (range %p..%p)",
            s_dynStage, (unsigned)g_GfxActiveBufferIndex, (void *)g_GfxMemPos,
            (void *)base, (void *)end);
    }
    if (bytes > end - pos)
    {
        sysFatalError("dyn: stage %d buffer %u %s exhausted: request %llu bytes, used %llu/%llu bytes",
            s_dynStage, (unsigned)g_GfxActiveBufferIndex, kind, (unsigned long long)bytes,
            (unsigned long long)(pos - base), (unsigned long long)(end - base));
    }
    g_GfxMemPos += bytes;
    if (pos - base + bytes > s_dynVtxPeak)
    {
        s_dynVtxPeak = pos - base + bytes;
        s_dynPeakChanged = TRUE;
    }
    return (void *)pos;
}
#else
s32 g_GfxSizesByPlayerCount[] = {0x10000, 0x18000, 0x20000, 0x28000};
s32 g_VtxSizesByPlayerCount[] = {0x10000, 0x18000, 0x20000, 0x28000};
#endif

char membars_string1[] = ">>>>>>>>>>>>>>>>>>>>>>>>>";
char membars_string2[] = "=========================";
char membars_string3[] = "-------------------------";

void dynInit(void) {
    debTryAdd(&D_800482E0, "dyn_c_debug");
}

void dynInitMemory(void) {
    s32 playerCount = getPlayerCount();
    s32 gfxHalf, vtxHalf;
#ifdef GEVR
    if (playerCount < 1 || playerCount > MAX_PLAYER_COUNT)
    {
        sysFatalError("dyn: stage %d invalid player count %d", (s32)bossGetStageNum(), playerCount);
    }
#endif
    if (tokenFind(1, "-mgfx")) {
#ifdef GEVR
        g_GfxSizesByPlayerCount[playerCount - 1] = gevrDynBudget(tokenFind(1, "-mgfx"), "gfx");
#else
        g_GfxSizesByPlayerCount[playerCount - 1] = strtol(tokenFind(1, "-mgfx"), NULL, 0) * 1024;
#endif
    }
    if (tokenFind(1, "-mvtx")) {
#ifdef GEVR
        g_VtxSizesByPlayerCount[playerCount - 1] = gevrDynBudget(tokenFind(1, "-mvtx"), "aux");
        /*
         * The stages' -mvtx budgets are the N64's, for four players. Every
         * player past four adds view passes online (lv.c gevrViewPass) whose
         * prop matrices come from this buffer: grow by the same share per player.
         */
        if (playerCount > 4)
        {
            s64 scaled = (s64)g_VtxSizesByPlayerCount[playerCount - 1] / 4 * playerCount;
            if (scaled > INT_MAX / 2)
            {
                sysFatalError("dyn: stage %d aux budget too large", (s32)bossGetStageNum());
            }
            g_VtxSizesByPlayerCount[playerCount - 1] = (s32)scaled;
        }
#else
        g_VtxSizesByPlayerCount[playerCount - 1] = strtol(tokenFind(1, "-mvtx"), NULL, 0) * 1024;
#endif
    }

#ifdef GEVR
    /*
     * D95 (gepc-ref): the -mgfx budget (boss.c's per-level memallocstringtable)
     * is a byte count sized for the N64's 8-byte Gfx. Here a Gfx is 16 bytes,
     * so the same master display list needs twice the bytes, and `gdl` - bumped
     * with a bare gdl++ by every render function, never bounds-checked - ran
     * off the end of g_GfxBuffers[1] and [2] every frame. Past [2] lie the
     * per-frame vertex/matrix buffers and then, a few allocations on, the
     * start of the default texture pool, where texReset() loads the smoke,
     * impact and effect textures: they were overwritten with GBI commands a
     * little more each busy frame. Scale by sizeof(Gfx) / 8.
     *
     * Report 4bde84c2 exhausted Facility's scaled 140 KiB list with at least
     * 199.75 KiB of commands. Retained bodies, detailed guns and the watch add
     * work beyond the cartridge budgets. Give both pools headroom, without
     * reducing any larger stage/player budget or compounding it on a reload.
     */
    {
        s64 scaled = (s64)g_GfxSizesByPlayerCount[playerCount - 1] * ((s32)sizeof(Gfx) / 8);
        if (scaled <= 0 || scaled > INT_MAX / 2 || g_VtxSizesByPlayerCount[playerCount - 1] <= 0
            || g_VtxSizesByPlayerCount[playerCount - 1] > INT_MAX / 2)
        {
            sysFatalError("dyn: stage %d invalid frame buffer budgets", (s32)bossGetStageNum());
        }
        gfxHalf = scaled < GEVR_DYN_GFX_MIN ? GEVR_DYN_GFX_MIN : (s32)scaled;
        vtxHalf = g_VtxSizesByPlayerCount[playerCount - 1];
        if (vtxHalf < GEVR_DYN_VTX_MIN) vtxHalf = GEVR_DYN_VTX_MIN;
    }
    if (s_dynGfxCapacity) gevrDynLogPeaks(LOG_NOTE);
    s_dynStage = (s32)bossGetStageNum();
    s_dynGfxCapacity = gfxHalf;
    s_dynVtxCapacity = vtxHalf;
    s_dynGfxPeak = s_dynVtxPeak = 0;
    s_dynNextPeakLog = 0;
    s_dynPeakChanged = s_dynGfxWarned = s_dynVtxWarned = FALSE;
#else
    gfxHalf = g_GfxSizesByPlayerCount[playerCount - 1];
    vtxHalf = g_VtxSizesByPlayerCount[playerCount - 1];
#endif

    g_GfxBuffers[0] = mempAllocBytesInBank(gfxHalf * 2, MEMPOOL_STAGE);
    g_GfxBuffers[1] = g_GfxBuffers[0] + gfxHalf;
    g_GfxBuffers[2] = g_GfxBuffers[1] + gfxHalf;
    g_VtxBuffers[0] = mempAllocBytesInBank(vtxHalf * 2, MEMPOOL_STAGE);
    g_VtxBuffers[1] = g_VtxBuffers[0] + vtxHalf;
    g_VtxBuffers[2] = g_VtxBuffers[1] + vtxHalf;

    g_GfxActiveBufferIndex = 0;
    g_GfxRequestedDisplayList = FALSE;
    g_GfxMemPos = g_VtxBuffers[0];
#ifdef GEVR
    sysLogPrintf(LOG_NOTE, "dyn: stage %d players %d buffers: gfx %d bytes/half (%p..%p..%p), aux %d bytes/half (%p..%p..%p)",
        s_dynStage, playerCount, gfxHalf, (void *)g_GfxBuffers[0], (void *)g_GfxBuffers[1], (void *)g_GfxBuffers[2],
        vtxHalf, (void *)g_VtxBuffers[0], (void *)g_VtxBuffers[1], (void *)g_VtxBuffers[2]);
#endif
}

Gfx *dynGetMasterDisplayList(void) {
    g_GfxRequestedDisplayList = TRUE;

    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex];
}

s32 dynGetFreeGfx2(Gfx *gdl) {
#ifdef GEVR
    /* bossMainloop calls this after END and before swapping/submitting. The
     * writer still uses bare gdl++: this diagnoses an overrun after construction,
     * rather than letting the interpreter consume commands from adjacent data. */
    uintptr_t base = (uintptr_t)g_GfxBuffers[g_GfxActiveBufferIndex];
    uintptr_t end = (uintptr_t)g_GfxBuffers[g_GfxActiveBufferIndex + 1];
    uintptr_t pos = (uintptr_t)gdl;
    u64 now;
    s32 warnGfx, warnVtx;
    if (!base || pos < base || pos > end || (pos - base) % sizeof(Gfx))
    {
        sysFatalError("dyn: stage %d buffer %u invalid master display list: used %llu bytes, capacity %llu bytes, end %p, base %p",
            s_dynStage, (unsigned)g_GfxActiveBufferIndex,
            (unsigned long long)(pos >= base ? pos - base : 0), (unsigned long long)(end - base),
            (void *)gdl, (void *)base);
    }
    if (pos - base > s_dynGfxPeak)
    {
        s_dynGfxPeak = pos - base;
        s_dynPeakChanged = TRUE;
    }
    now = sysGetMicroseconds();
    warnGfx = !s_dynGfxWarned && s_dynGfxPeak >= s_dynGfxCapacity * 4 / 5;
    warnVtx = !s_dynVtxWarned && s_dynVtxPeak >= s_dynVtxCapacity * 4 / 5;
    if (warnGfx || warnVtx || (s_dynPeakChanged && now >= s_dynNextPeakLog))
    {
        gevrDynLogPeaks(warnGfx || warnVtx ? LOG_WARNING : LOG_NOTE);
        s_dynGfxWarned |= warnGfx;
        s_dynVtxWarned |= warnVtx;
        s_dynPeakChanged = FALSE;
        s_dynNextPeakLog = now + GEVR_DYN_LOG_INTERVAL_US;
    }
    return (s32)((end - pos) / sizeof(Gfx));
#else
    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl;
#endif
}

/**
 * Address: 7F0BD6C4
 */
Vtx *dynAllocateVertices(s32 count)
{
#ifdef GEVR
    return gevrDynAllocate(count, sizeof(Vtx), FALSE, "vertices");
#else
    void *ptr = g_GfxMemPos;
	g_GfxMemPos += count * sizeof(Vtx);
	return ptr;
#endif
}

Mtx *dynAllocateMatrix(void)
{
#ifdef GEVR
    return gevrDynAllocate(1, sizeof(Mtx), FALSE, "matrix");
#else
	void *ptr = g_GfxMemPos;
	g_GfxMemPos += sizeof(Mtx);
	return ptr;
#endif
}

/**
 * Address: 7F0BD6F8
 */
Light *dynAllocateLights(s32 count)
{
#ifdef GEVR
    return gevrDynAllocate(count, sizeof(Light), FALSE, "lights");
#else
    void *ptr = g_GfxMemPos;
    g_GfxMemPos += count * sizeof(Light);
    return ptr;
#endif
}

void *dynAllocate(s32 size) {
#ifdef GEVR
    return gevrDynAllocate(size, 1, TRUE, "bytes");
#else
    void *ptr = g_GfxMemPos;
	size = ALIGN16_a(size);
	g_GfxMemPos += size;
	return ptr;
#endif
}

void dynSwapBuffers(void) {
    g_GfxActiveBufferIndex = (g_GfxActiveBufferIndex ^ 1);
    g_GfxRequestedDisplayList = FALSE;
    g_GfxMemPos = g_VtxBuffers[g_GfxActiveBufferIndex];
}

void dynRemovedFunc(Gfx *gdl) {
}

s32 dynGetFreeGfx(Gfx *gdl) {
    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl;
}

s32 dynGetFreeVtx(void) {
	return g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos;
}

// Address 0x7F0BD7CC NTSC
void dynCalculateMembarLength(const char* arg0, f32 arg1, f32 arg2)
{
    s32 len;
    f32 zero = 0;
    
    len = strlen(arg0);
    
    arg1 /= arg2;
    
    if(zero);
    
    if (arg1 < zero && len > 1)
    {
        if (len > 1)
        {
            
        }
    }
}

void dynDrawMembars(Gfx *gdl) {
    dynCalculateMembarLength(membars_string2, ((Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl), ((Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex]));
    dynCalculateMembarLength(membars_string2, (g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos), (g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_VtxBuffers[g_GfxActiveBufferIndex]));
}
