/* Exercise the actual resource-load body with the ROM/host-conversion hooks. */
#include "gevr_romcheck.h"
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>

typedef unsigned char u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef struct { s32 index; char *filename; u8 *hw_address; } fileentry;
typedef struct { u32 rom_size, poolRemaining; } resource_lookup_data_entry;
#define HUFT_SCRATCH_BYTES 16896
#define LOG_NOTE 0
static struct PendingHeader { int numSwitches, numtextures; } *gevrModelPendingHeader;
static u8 *gevrGexPendingFile;
static u32 gevrGexPendingLen;
static int copies, conversions;
static jmp_buf failure;
static char diagnostic[256];

void romCopy(void *dst, const void *src, u32 size) { copies++; memcpy(dst, src, size); }
void sysLogPrintf(int level, const char *fmt, ...) { (void)level; (void)fmt; }
void sysFatalError(const char *fmt, ...) __attribute__((noreturn));
void sysFatalError(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(diagnostic, sizeof(diagnostic), fmt, args);
    va_end(args);
    longjmp(failure, 1);
}
u32 gevrModelConvert(u8 *data, u32 size, u32 capacity, int switches, int textures, const char *name) {
    (void)data; (void)size; (void)capacity; (void)switches; (void)textures; (void)name;
    conversions++;
    return 0;
}
size_t gevrConvertStan(u8 *data, u32 size, s32 capacity) {
    (void)data; (void)capacity; conversions++; return size;
}
size_t gevrConvertSetup(u8 *data, u32 size, s32 capacity) {
    return gevrConvertStan(data, size, capacity);
}
void gevrRomSwapBriefing(u8 *data, u32 size) { (void)data; (void)size; conversions++; }
void gevrRomSwapLangBank(u8 *data, u32 size, const char *name) {
    (void)data; (void)size; (void)name; conversions++;
}

/* gevr_romswap.c / gevr_romlayout.h: the stage data fixes (load_resource) */
int gevrRomIsVariant(void) { return 0; }
int gevrRomFixData(const char *name, u8 *data, u32 size) { (void)name; (void)data; (void)size; return 0; }
/* gevr_modassets.c: the run's mod's model for a file; answers modModel bytes when it fits */
int gevrModAssetLast;
static u32 modModel;
static int modAsks;
u32 gevrModAssetModel(const char *name, u8 *dst, u32 capacity, int switches, int textures) {
    (void)name; (void)switches; (void)textures;
    modAsks++;
    if (!modModel || modModel > capacity) return 0;
    memset(dst, 'M', modModel);
    return modModel;
}
/* INSERT_RESOURCE */

int test_load(u8 *src, u32 srcSize, u8 *dst, s32 capacity, u32 *written) {
    fileentry file = {1, "LtestE", src};
    resource_lookup_data_entry lookup = {srcSize, 0};
    copies = conversions = 0;
    diagnostic[0] = '\0';
    *written = 0;
    if (setjmp(failure)) return 0;
    load_resource(dst, capacity, &file, &lookup);
    *written = lookup.poolRemaining;
    return 1;
}
/* A model file (a pending header): the mod's (mod bytes, 0 none), GoldenEye X's (gex) or the cartridge's */
int test_load_model(u8 *src, u32 srcSize, u8 *dst, s32 capacity, u32 *written, u32 mod, int gex) {
    static struct PendingHeader header = { 3, 4 };
    static u8 gexFile[3] = { 'G', 'E', 'X' };
    fileentry file = {1, "GtestZ", src};
    resource_lookup_data_entry lookup = {srcSize, 0};
    int ok = 1;
    copies = conversions = modAsks = 0;
    gevrModAssetLast = -1;
    modModel = mod;
    gevrModelPendingHeader = &header;
    gevrGexPendingFile = gex ? gexFile : NULL;
    gevrGexPendingLen = gex ? sizeof(gexFile) : 0;
    *written = 0;
    if (setjmp(failure)) ok = 0;
    else load_resource(dst, capacity, &file, &lookup);
    gevrModelPendingHeader = NULL;
    gevrGexPendingFile = NULL;
    gevrGexPendingLen = 0;
    *written = lookup.poolRemaining;
    return ok;
}
int test_mod_asks(void) { return modAsks; }
int test_mod_last(void) { return gevrModAssetLast; }
int test_copies(void) { return copies; }
int test_conversions(void) { return conversions; }
const char *test_diagnostic(void) { return diagnostic; }
