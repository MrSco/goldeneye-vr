/* Synthetic music bank; production allocation/load/play paths and AL heap.
 * Parser/synthesizer calls are stubs so this needs no ROM or audio device. */
#include <stddef.h>
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/music.c"
#include "../../src/libultra/audio/heapinit.c"
#include "../../src/libultra/audio/heapalloc.c"

s8 g_sndBootswitchSound;
static jmp_buf failure;
static int parsed, played;
static char diagnostic[256];
u8 *_musicsampletblSegmentRomStart, *_musicsampletblSegmentRomEnd;
void sysFatalError(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(diagnostic, sizeof(diagnostic), fmt, args);
    va_end(args);
    longjmp(failure, 1);
}
void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; (void)fmt; }
void sysSleep(s64 hns) { (void)hns; }
void gevrAudioFrame(void) {}
void gevrCSPForceStop(ALCSPlayer *seqp) { (void)seqp; }
s32 alCSPGetState(ALCSPlayer *seqp) { (void)seqp; return AL_STOPPED; }
void alCSPStop(ALCSPlayer *seqp) { (void)seqp; }
void alCSPPlay(ALCSPlayer *seqp) { (void)seqp; played++; }
void alCSPSetVol(ALCSPlayer *seqp, s16 vol) { (void)seqp; (void)vol; }
void alCSPSetSeq(ALCSPlayer *seqp, ALCSeq *seq) { (void)seqp; (void)seq; }
void alCSeqNew(ALCSeq *seq, u8 *data) { (void)seq; (void)data; parsed++; }

static u8 heap[MUSIC_ALLOCATION_BYTES] __attribute__((aligned(16)));
static ALCSPlayer players[3];
static u8 *buffers[3];
static RareALSeqBankFile *table;
static u8 *guard;

#ifdef MUSIC_BASELINE
static u32 s_musicSeqCapacity;
static void gevrMusicAllocSequenceBuffers(void)
{
    u32 size, ui;
    /* INSERT_BASELINE_ALLOCATION */
}
void romCopy(void *dst, void *src, u32 size)
{
    memcpy(dst, src, size);
    for (int i = 0; i < 128; i++) {
        if (guard[i] != 0xa5) {
            puts("baseline: compressed sequence staging overwrote the following filter allocation");
            exit(42);
        }
    }
    abort(); /* This case must hit the filter before decompression. */
}
u32 decompressdata(u8 *src, u8 *dst, struct huft *hlist)
{
    (void)src; (void)dst; (void)hlist;
    abort();
}
#endif

static void reset(void)
{
    memset(heap, 0xa5, sizeof(heap));
    alHeapInit(&g_musicHeap, heap, sizeof(heap));
    gevrMusicAllocSequenceBuffers();
    buffers[0] = g_musicXTrack1SeqData;
    buffers[1] = g_musicXTrack2SeqData;
    buffers[2] = g_musicXTrack3SeqData;
    guard = g_musicHeap.cur; /* alSynNew allocates ALSave here in the game. */
    g_musicXTrack1SeqPlayer = &players[0];
    g_musicXTrack2SeqPlayer = &players[1];
    g_musicXTrack3SeqPlayer = &players[2];
    parsed = played = 0;
}

static void play(int player, int track)
{
    switch (player) {
    case 0: musicTrack1Play(track); break;
    case 1: musicTrack2Play(track); break;
    case 2: musicTrack3Play(track); break;
    }
}

static void check_guard(void)
{
    for (int i = 0; i < 128; i++) assert(guard[i] == 0xa5);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    _musicsampletblSegmentRomStart = malloc(size);
    assert(fread(_musicsampletblSegmentRomStart, 1, size, file) == (size_t)size);
    fclose(file);
    _musicsampletblSegmentRomEnd = _musicsampletblSegmentRomStart + size;
    table = calloc(1, sizeof(*table) + NUM_MUSIC_TRACKS * sizeof(RareALSeqData));
    assert(table);
    table->seqCount = NUM_MUSIC_TRACKS;
    g_musicDataTable = table;
    /* argv[2] supplies count followed by raw size and packed size for each. */
    file = fopen(argv[2], "r");
    assert(file);
    int count;
    assert(fscanf(file, "%d", &count) == 1 && count < NUM_MUSIC_TRACKS);
    u8 *address = _musicsampletblSegmentRomStart;
    for (int i = 0; i < count; i++) {
        unsigned raw, packed;
        assert(fscanf(file, "%u %u", &raw, &packed) == 2);
        table->seqArray[i].address = address;
        table->seqArray[i].uncompressed_len = raw;
        table->seqArray[i].len = packed;
        address += packed;
    }
    fclose(file);
    reset();
#ifdef MUSIC_BASELINE
    /* The old copy alone corrupts the filter; no decoder mock is needed. */
    play(1, 9);
    abort();
#endif
    assert(s_musicSeqCapacity >= table->seqArray[count - 1].uncompressed_len);
    assert(buffers[0] + s_musicSeqCapacity <= buffers[1]);
    assert(buffers[1] + s_musicSeqCapacity <= buffers[2]);

    /* Every player can load every size, including odd and maximum u16 sizes.
     * Other players' live sequence data must survive each load. */
    for (int track = 0; track < count; track++) {
        for (int player = 0; player < 3; player++) {
            u8 before[3][0x10000];
            for (int j = 0; j < 3; j++) memcpy(before[j], buffers[j], s_musicSeqCapacity);
            play(player, track);
            for (int j = 0; j < 3; j++)
                if (j != player) assert(!memcmp(before[j], buffers[j], s_musicSeqCapacity));
            /* Python generated each output as repetition of its track index. */
            for (int k = 0; k < table->seqArray[track].uncompressed_len; k++)
                assert(buffers[player][k] == (u8)track);
            check_guard();
        }
    }
    assert(parsed == count * 3 && played == parsed);
    puts("music buffers: all sizes and all three play paths passed");

    /* Compressed bytes that expand past the advertised length are rejected
     * before parser/play and cannot damage either neighbour or the filter. */
    for (int player = 0; player < 3; player++) {
        reset();
        RareALSeqData saved = table->seqArray[count - 1];
        table->seqArray[count - 1].uncompressed_len = 32;
        memset(buffers[player], 0xa5, s_musicSeqCapacity);
        if (!setjmp(failure)) { play(player, count - 1); assert(!"accepted oversized output"); }
        assert(!parsed && !played && strstr(diagnostic, "cannot decompress"));
        for (u32 k = 32; k < s_musicSeqCapacity; k++) assert(buffers[player][k] == 0xa5);
        check_guard();
        table->seqArray[count - 1] = saved;
    }
    for (int bad = 0; bad < 6; bad++) {
        reset();
        RareALSeqData saved = table->seqArray[1];
        if (bad == 0) table->seqArray[1].address = _musicsampletblSegmentRomEnd;
        if (bad == 1) table->seqArray[1].address = (u8 *)((uintptr_t)_musicsampletblSegmentRomStart - 1);
        if (bad == 2) table->seqArray[1].len = 2;
        if (bad == 3) table->seqArray[1].uncompressed_len++;
        if (!setjmp(failure)) {
            play(0, bad == 4 ? -1 : bad == 5 ? NUM_MUSIC_TRACKS : 1);
            assert(!"accepted invalid sequence");
        }
        assert(!parsed && !played);
        check_guard();
        table->seqArray[1] = saved;
    }
    puts("music buffers: bounded failures passed");
    free(table);
    free(_musicsampletblSegmentRomStart);
    return 0;
}
