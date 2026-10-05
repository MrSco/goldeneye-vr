/* Production frame pools: capacity, overwrite guards, allocation failures and
 * validation at the existing pre-submission boundary. No renderer is mocked. */
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <system.h>
#include "../../src/game/dyn.c"

static struct { u8 *data; size_t size; } blocks[2];
static int blockCount, players = 1, stage = LEVELID_FACILITY;
static const char *gfxToken = "70", *vtxToken = "50";
static jmp_buf fatalJump;
static int expectFatal, logCount, warningCount;
static char fatalMessage[512], lastLog[512];
static u64 clockUs = 1000000;

void debTryAdd(void *data, const char *name) { (void)data; (void)name; }
s32 getPlayerCount(void) { return players; }
LEVELID bossGetStageNum(void) { return (LEVELID)stage; }
const char *tokenFind(s32 index, const char *token)
{
    (void)index;
    return !strcmp(token, "-mgfx") ? gfxToken : !strcmp(token, "-mvtx") ? vtxToken : NULL;
}
void *mempAllocBytesInBank(u32 size, u8 bank)
{
    (void)bank;
    if (blockCount >= 2) abort();
    /* Padding lets the original 4bde84c2 overrun fail the guard assertion,
     * instead of corrupting this test process's malloc metadata. */
    u8 *p = calloc(1, (size_t)size + 512 * 1024);
    if (!p) abort();
    memset(p + size, 0xa5, 64);
    blocks[blockCount].data = p;
    blocks[blockCount++].size = size;
    return p;
}
u64 sysGetMicroseconds(void) { return clockUs; }
void sysLogPrintf(s32 level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(lastLog, sizeof(lastLog), fmt, args);
    va_end(args);
    logCount++;
    if (level == LOG_WARNING) warningCount++;
}
void sysFatalError(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(fatalMessage, sizeof(fatalMessage), fmt, args);
    va_end(args);
    if (expectFatal) longjmp(fatalJump, 1);
    fprintf(stderr, "Unexpected fatal: %s\n", fatalMessage);
    exit(2);
}

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); return 1; } } while (0)
#define FATAL(action, text) do { \
    expectFatal = 1; \
    if (!setjmp(fatalJump)) { action; expectFatal = 0; \
        fprintf(stderr, "%s:%d: missing fatal for %s\n", __func__, __LINE__, #action); return 1; } \
    expectFatal = 0; CHECK(strstr(fatalMessage, text)); \
} while (0)

static void releaseBlocks(void)
{
    for (int i = 0; i < blockCount; i++) free(blocks[i].data);
    blockCount = 0;
}
static void init(int playerCount, int level, const char *gfx, const char *vtx)
{
    releaseBlocks();
    players = playerCount;
    stage = level;
    gfxToken = gfx;
    vtxToken = vtx;
    dynInitMemory();
    logCount = warningCount = 0;
    lastLog[0] = '\0';
}
static int guardsIntact(void)
{
    for (int i = 0; i < blockCount; i++)
        for (int j = 0; j < 64; j++)
            if (blocks[i].data[blocks[i].size + j] != 0xa5) return 0;
    return 1;
}

static int observedLists(void)
{
    /* The two possible lengths from each report's saved final pointer:
     * 4bde84c2, followed by 76717e5d. Exercise every length in both halves. */
    const size_t usedLengths[] = {347904, 204544, 349184, 205824};
    for (size_t scenario = 0; scenario < sizeof(usedLengths) / sizeof(usedLengths[0]); scenario++)
    {
        init(1, LEVELID_FACILITY, "70", "50");
        for (int half = 0; half < 2; half++)
        {
            const size_t used = usedLengths[scenario];
            u8 *aux = dynAllocate(16);
            memset(aux, 0x5a, 16);
            Gfx *start = dynGetMasterDisplayList(), *gdl = start;
            for (size_t i = 0; i < used / sizeof(Gfx) - 1; i++) gDPPipeSync(gdl++);
            gSPEndDisplayList(gdl++);
            CHECK(guardsIntact());
            CHECK(dynGetFreeGfx2(gdl) >= 0);
            for (int i = 0; i < 16; i++) CHECK(aux[i] == 0x5a);
            CHECK((u8 *)gdl - (u8 *)start == used);
            dynSwapBuffers();
        }
    }
    return 0;
}

static int budgetsAndReloads(void)
{
    for (int n = 1; n <= MAX_PLAYER_COUNT; n++)
    {
        init(n, LEVELID_FACILITY, "70", "50");
        CHECK(g_GfxBuffers[1] - g_GfxBuffers[0] == 512 * 1024);
        CHECK(g_VtxBuffers[1] - g_VtxBuffers[0] == 256 * 1024);
        CHECK(g_GfxBuffers[2] - g_GfxBuffers[1] == 512 * 1024);
        CHECK(g_VtxBuffers[2] - g_VtxBuffers[1] == 256 * 1024);
    }
    init(8, LEVELID_FACILITY, "400", "300");
    CHECK(g_GfxBuffers[1] - g_GfxBuffers[0] == 800 * 1024);
    CHECK(g_VtxBuffers[1] - g_VtxBuffers[0] == 600 * 1024);
    /* An identical reload must not double the eight-player budget again. */
    init(8, LEVELID_FACILITY, "400", "300");
    CHECK(g_VtxBuffers[1] - g_VtxBuffers[0] == 600 * 1024);
    init(1, LEVELID_TITLE, "80", "20");
    CHECK(g_GfxActiveBufferIndex == 0);
    CHECK(g_GfxMemPos == g_VtxBuffers[0]);
    CHECK(!g_GfxRequestedDisplayList);
    /* Tokenless initialization preserves a larger stored/default budget. */
    g_GfxSizesByPlayerCount[0] = 400 * 1024;
    g_VtxSizesByPlayerCount[0] = 300 * 1024;
    init(1, LEVELID_TITLE, NULL, NULL);
    CHECK(g_GfxBuffers[1] - g_GfxBuffers[0] == 800 * 1024);
    CHECK(g_VtxBuffers[1] - g_VtxBuffers[0] == 300 * 1024);
    CHECK(guardsIntact());
    return 0;
}

static int auxiliaryAllocations(void)
{
    for (int half = 0; half < 2; half++)
    {
        if (!half) init(1, LEVELID_FACILITY, "70", "50");
        u8 *base = g_GfxMemPos;
        CHECK((u8 *)dynAllocateVertices(3) == base);
        CHECK(g_GfxMemPos == base + 3 * sizeof(Vtx));
        base = g_GfxMemPos;
        CHECK((u8 *)dynAllocateLights(1) == base);
        CHECK(g_GfxMemPos == base + sizeof(Light));
        base = g_GfxMemPos;
        CHECK((u8 *)dynAllocateMatrix() == base);
        CHECK(g_GfxMemPos == base + sizeof(Mtx));
        base = g_GfxMemPos;
        CHECK(dynAllocate(17) == base);
        CHECK(g_GfxMemPos == base + 32);
        CHECK(dynAllocate(0) == g_GfxMemPos);
        CHECK((u8 *)dynAllocateVertices(0) == g_GfxMemPos);
        CHECK((u8 *)dynAllocateLights(0) == g_GfxMemPos);
        base = g_GfxMemPos;
        FATAL(dynAllocate(-1), "invalid bytes count");
        FATAL(dynAllocate(INT_MIN), "invalid bytes count");
        FATAL(dynAllocateVertices(-1), "invalid vertices count");
        FATAL(dynAllocateLights(-1), "invalid lights count");
        FATAL(dynAllocate(INT_MAX), "bytes exhausted");
        FATAL(dynAllocateVertices(INT_MAX), "vertices");
        FATAL(dynAllocateLights(INT_MAX), "lights");
        CHECK(g_GfxMemPos == base);
        CHECK(guardsIntact());
        dynSwapBuffers();
        CHECK(g_GfxMemPos == g_VtxBuffers[g_GfxActiveBufferIndex]);
        CHECK(!g_GfxRequestedDisplayList);
    }
    return 0;
}

static int exactCapacityAndFailures(void)
{
    for (int kind = 0; kind < 4; kind++)
    {
        init(1, LEVELID_FACILITY, "70", "50");
        s32 capacity = dynGetFreeVtx();
        if (kind == 0) dynAllocate(capacity);
        if (kind == 1) dynAllocateVertices(capacity / sizeof(Vtx));
        if (kind == 2) { dynAllocate(capacity - sizeof(Mtx)); dynAllocateMatrix(); }
        if (kind == 3) { dynAllocate(capacity - 16 * sizeof(Light)); dynAllocateLights(16); }
        CHECK(dynGetFreeVtx() == 0);
        u8 *end = g_GfxMemPos;
        FATAL(dynAllocate(1), "bytes exhausted");
        FATAL(dynAllocateVertices(1), "vertices exhausted");
        FATAL(dynAllocateMatrix(), "matrix exhausted");
        FATAL(dynAllocateLights(1), "lights exhausted");
        CHECK(g_GfxMemPos == end);
        CHECK(strstr(fatalMessage, "stage 34 buffer 0"));
        CHECK(strstr(fatalMessage, "262144/262144"));
        CHECK(guardsIntact());
    }
    init(1, LEVELID_FACILITY, "70", "50");
    dynAllocate(dynGetFreeVtx() - 16);
    u8 *pos = g_GfxMemPos;
    FATAL(dynAllocate(17), "request 32 bytes");
    CHECK(g_GfxMemPos == pos);
    /* The allocator also diagnoses an already-invalid cursor without advancing. */
    g_GfxMemPos = (u8 *)((uintptr_t)g_VtxBuffers[0] - 1);
    pos = g_GfxMemPos;
    FATAL(dynAllocate(1), "invalid aux cursor");
    CHECK(g_GfxMemPos == pos);
    return 0;
}

static int completedLists(void)
{
    init(1, LEVELID_FACILITY, "70", "50");
    for (int half = 0; half < 2; half++)
    {
        Gfx *base = dynGetMasterDisplayList();
        uintptr_t end = (uintptr_t)g_GfxBuffers[half + 1];
        CHECK(dynGetFreeGfx2((Gfx *)end) == 0);
        FATAL(dynGetFreeGfx2((Gfx *)(end + sizeof(Gfx))), "invalid master display list");
        FATAL(dynGetFreeGfx2((Gfx *)((uintptr_t)base - sizeof(Gfx))), "invalid master display list");
        FATAL(dynGetFreeGfx2((Gfx *)((uintptr_t)base + 1)), "invalid master display list");
        FATAL(dynGetFreeGfx2(NULL), "invalid master display list");
        CHECK(strstr(fatalMessage, "capacity 524288 bytes"));
        CHECK(g_GfxActiveBufferIndex == half);
        CHECK(g_GfxMemPos == g_VtxBuffers[half]);
        CHECK(g_GfxRequestedDisplayList);
        dynSwapBuffers();
    }
    return 0;
}

static int peakLogging(void)
{
    init(1, LEVELID_FACILITY, "70", "50");
    Gfx *base = dynGetMasterDisplayList();
    dynAllocate(16);
    dynGetFreeGfx2(base + 1);
    CHECK(logCount == 1);
    CHECK(strstr(lastLog, "stage 34 peak gfx 16/524288 bytes, aux 16/262144 bytes"));
    dynGetFreeGfx2(base + 1);
    CHECK(logCount == 1);
    dynAllocate(16);
    dynGetFreeGfx2(base + 2);
    CHECK(logCount == 1);
    clockUs += 5000000;
    dynGetFreeGfx2(base + 2);
    CHECK(logCount == 2);
    CHECK(strstr(lastLog, "aux 32/262144 bytes"));
    clockUs += 10000000;
    dynGetFreeGfx2(base + 2);
    CHECK(logCount == 2); /* unchanged peaks stay quiet */
    dynAllocate(210000);
    dynGetFreeGfx2(base + 2);
    CHECK(warningCount == 1);
    dynAllocate(16);
    dynGetFreeGfx2(base + 2);
    CHECK(warningCount == 1);
    dynGetFreeGfx2(base + 26215); /* crosses 80% gfx independently of aux */
    CHECK(warningCount == 2);
    init(1, LEVELID_TITLE, "80", "20");
    base = dynGetMasterDisplayList();
    dynGetFreeGfx2(base + 1);
    CHECK(strstr(lastLog, "stage 90 peak gfx 16/524288 bytes, aux 0/262144 bytes"));
    CHECK(warningCount == 0);
    return 0;
}

static int invalidBudgets(void)
{
    releaseBlocks();
    players = 0;
    FATAL(dynInitMemory(), "invalid player count 0");
    players = MAX_PLAYER_COUNT + 1;
    FATAL(dynInitMemory(), "invalid player count");
    players = 1;
    gfxToken = "-1";
    FATAL(dynInitMemory(), "invalid gfx budget");
    gfxToken = "99999999999999999999";
    FATAL(dynInitMemory(), "invalid gfx budget");
    gfxToken = "1048576";
    FATAL(dynInitMemory(), "invalid frame buffer budgets");
    gfxToken = "70";
    vtxToken = "0";
    FATAL(dynInitMemory(), "invalid aux budget");
    vtxToken = "1048576";
    players = 8;
    FATAL(dynInitMemory(), "aux budget too large");
    CHECK(blockCount == 0);
    return 0;
}

int main(int argc, char **argv)
{
    struct { const char *name; int (*test)(void); } cases[] = {
        {"observed_lists", observedLists}, {"budgets_and_reloads", budgetsAndReloads},
        {"auxiliary_allocations", auxiliaryAllocations}, {"exact_capacity_and_failures", exactCapacityAndFailures},
        {"completed_lists", completedLists}, {"peak_logging", peakLogging}, {"invalid_budgets", invalidBudgets}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        if (argc > 1 && strcmp(argv[1], cases[i].name)) continue;
        if (cases[i].test()) { releaseBlocks(); return 1; }
        printf("PASS %s\n", cases[i].name);
    }
    releaseBlocks();
    return 0;
}
