"""Stage memory strings cannot undo VR's room-geometry heap floor.

Runs the production stage selection, token parser and allocation path for
solo and eight-player table choices, with unrelated pool services stubbed.
Use --baseline-ref to demonstrate the retail regression on an older ref.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def block(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-ref")
    baseline = parser.parse_args().baseline_ref
    boss = (ROOT / "src/boss.c").read_text()
    if baseline:
        boss = subprocess.check_output(["git", "show", baseline + ":src/boss.c"], cwd=ROOT, text=True)
    tokens = (ROOT / "src/token.c").read_text()
    start = boss.index("struct memallocstring memallocstringtable[]")
    table = boss[start:boss.index("\n};", start) + 3]
    start = boss.index("        if (g_DebugAndUpdateStageFlag)", boss.index("void bossMainloop("))
    end = boss.index("        reset_play_data_ptrs();", start)
    load = boss[start:end]
    heap_define = next(line for line in boss.splitlines() if line.startswith("#define GEVR_ROOM_HEAP"))
    fixture = r'''
#include <ultra64.h>
#include <bondconstants.h>
#include "gevr_romlayout.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* HEAP_DEFINE */
#define MEMPOOL_STAGE 4
struct memallocstring { s32 id; const char *string; };
/* TABLE */
static u32 g_TokenString[160];
static s32 g_TokenCount;
static const char *g_Tokens[35];
/* TOKENS */
static u32 g_CurentMaMallocValue, allocated, room_bytes;
static s32 g_StageNum, g_DebugAndUpdateStageFlag = 1, players, coop;
static void *g_BgPortals;
static char buffer[8 * 1024 * 1024];
static const char *override;
static int get_selected_num_players(void) { return players; }
static int gevrCoopActive(void) { return coop; }
const char *gevrRomStageMemString(int stage, int multi) { (void)stage; (void)multi; return override; }
static void mempResetBank(int bank) { (void)bank; allocated = 0; }
static void obBlankResourcesLoadedInBank(int bank) { (void)bank; }
static void *mempAllocBytesInBank(u32 bytes, int bank)
{
    (void)bank;
    assert(bytes <= sizeof(buffer));
    allocated = bytes;
    return buffer;
}
static void memaReset(void *ptr, u32 bytes) { assert(ptr == buffer); room_bytes = bytes; }
static void load_stage(void)
{
    s32 stringIndex;
    /* LOAD */
}
int main(void)
{
    const int campaigns[] = {GEVR_CAMPAIGN_GE, GEVR_CAMPAIGN_GF64, GEVR_CAMPAIGN_TND64, GEVR_CAMPAIGN_MIRROR};
    for (int campaign = 0; campaign < (int)(sizeof(campaigns) / sizeof(campaigns[0])); campaign++)
    {
        gevrRomCampaignSet(campaigns[campaign]);
        for (players = 1; players <= 8; players += 7)
        {
            for (int row = 0; memallocstringtable[row].id; row++)
            {
                if (memallocstringtable[row].id >= 400) continue;
                g_StageNum = memallocstringtable[row].id;
                for (coop = 0; coop <= 1; coop++)
                {
                    g_CurentMaMallocValue = GEVR_ROOM_HEAP;
                    load_stage();
#ifdef GEVR
                    assert(allocated >= 4 * 1024 * 1024);
#else
                    assert(allocated < 4 * 1024 * 1024);
#endif
                    assert(allocated == room_bytes && allocated == g_CurentMaMallocValue);
                }
            }
        }
#ifdef GEVR
        g_StageNum = LEVELID_DAM;
        override = "-ma6144";
        load_stage();
        assert(allocated == 6 * 1024 * 1024); /* Preserve larger explicit budgets. */
        override = "-ma275";
        load_stage();
        assert(allocated == 4 * 1024 * 1024); /* Previous larger stage is not retained. */
        override = "-mt625";
        g_CurentMaMallocValue = 1024;
        load_stage();
        assert(allocated == 4 * 1024 * 1024); /* Missing -ma still receives the floor. */
        override = NULL;
#endif
    }
#ifdef GEVR
    puts("PASS: retail stage table retains the 4 MB room heap for every campaign flag and solo/eight-player/co-op selection; larger requests are preserved");
#else
    puts("PASS: non-VR stage tables retain their cartridge room heaps");
#endif
    return 0;
}
'''
    fixture = fixture.replace("/* HEAP_DEFINE */", heap_define).replace("/* TABLE */", table)
    fixture = fixture.replace("/* TOKENS */", "\n".join(block(tokens, signature) for signature in (
        "unsigned char *tokenSplit(", "void tokenSetString(", "const char *tokenFind(")))
    fixture = fixture.replace("/* LOAD */", load)
    with tempfile.TemporaryDirectory(prefix="gevr-room-heap-") as temp:
        source = Path(temp) / "test.c"
        source.write_text(fixture)
        for vr in (True, False):
            if baseline and not vr:
                continue
            exe = Path(temp) / ("vr.exe" if vr else "n64.exe")
            args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions",
                    "-Wno-builtin-declaration-mismatch", "-Wno-incompatible-pointer-types"]
            args += ["-D" + v for v in ("PLATFORM_64BIT=1", "_LANGUAGE_C=1", "VERSION=2", "VERSION_US=1")]
            if vr:
                args.append("-DGEVR=1")
            args += ["-I" + str(ROOT / p) for p in ("port/include", "include", "src", ".")]
            result = subprocess.run(args + [str(source), str(ROOT / "port/src/gevr_romlayout.c"),
                                           "-o", str(exe)], capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            if baseline:
                assert result.returncode, "Baseline unexpectedly passed"
                print("CONFIRMED: " + baseline + " fails the retail room-heap regression: " + result.stderr.strip())
            else:
                if result.returncode:
                    raise RuntimeError(result.stderr)
                print(result.stdout.strip())


if __name__ == "__main__":
    main()
