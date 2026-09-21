"""Host regression checks for the real save scans and POSIX sleep conversion.

Extracts the production enums/functions, mocks only save storage and nanosleep,
and compiles a small C executable. No game assets or Android device required.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(text, name):
    match = re.search(r'^\w[\w *]*\b' + name + r'\([^;]*?\)\s*\{', text, re.M)
    assert match, name
    depth = 1
    end = match.end()
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]


def main():
    constants = (ROOT / 'src/bondconstants.h').read_text()
    enums = '\n'.join(re.search(r'typedef enum ' + name + r'\s*\{.*?\}\s*' + name + ';',
                                constants, re.S).group()
                      for name in ('LEVEL_SOLO_SEQUENCE', 'DIFFICULTY'))
    saves = (ROOT / 'src/game/file2.c').read_text()
    scans = '\n'.join(function(saves, name) for name in (
        'fileGetSaveStageCompletedForDifficulty',
        'fileGetHighestStageDifficultyCompletedForFolder',
        'fileGetHighestStageUnlockedForFolder'))
    sleep = function((ROOT / 'port/src/system.c').read_text(), 'sysSleep')
    harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
typedef int32_t s32;
typedef int64_t s64;
#define FALSE 0
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
ENUMS
typedef struct { int times[4][20]; } save_data;
static save_data slots[1];
static int scans, unlockedStage = -1;
save_data *fileGetSaveForFoldernum(s32 n) { return n == 0 ? slots : NULL; }
s32 fileGetSaveStageDifficultyTime(save_data *s, LEVEL_SOLO_SEQUENCE l, DIFFICULTY d) {
    CHECK(l >= 0 && l < 20 && d >= 0 && d < 4);
    /* On an unsigned enum the loop cycles forever; reject repeated scans. */
    CHECK(++scans <= 80);
    return s->times[d][l];
}
int fileIsStageUnlockedAtDifficulty(s32 n, LEVEL_SOLO_SEQUENCE l, DIFFICULTY d) {
    CHECK(++scans <= 80);
    return n == 0 && l == unlockedStage && d == 0;
}
SCANS
static int sleepCalls, interruptOnce;
static struct timespec observed[2];
static int fake_nanosleep(const struct timespec *req, struct timespec *rem) {
    CHECK(sleepCalls < 2);
    observed[sleepCalls++] = *req;
    CHECK(req->tv_sec >= 0 && req->tv_nsec >= 0 && req->tv_nsec < 1000000000);
    if (interruptOnce) {
        interruptOnce = 0;
        rem->tv_sec = 0; rem->tv_nsec = 12345;
        errno = EINTR;
        return -1;
    }
    return 0;
}
#define nanosleep fake_nanosleep
SLEEP
int main(void) {
    LEVEL_SOLO_SEQUENCE level;
    DIFFICULTY difficulty;
    CHECK(SP_LEVEL_DAM == 0 && SP_LEVEL_EGYPT == 19 && SP_LEVEL_MAX == 20);
    CHECK(sizeof(LEVEL_SOLO_SEQUENCE) == 4);
    fileGetHighestStageDifficultyCompletedForFolder(0, &level, &difficulty);
    CHECK(scans == 80 && level == -1 && difficulty == -1);
    CHECK(level < SP_LEVEL_DAM);
    scans = 0;
    fileGetHighestStageDifficultyCompletedForFolder(4, &level, &difficulty);
    CHECK(scans == 0 && level == -1 && difficulty == -1);
    slots[0].times[DIFFICULTY_AGENT][SP_LEVEL_DAM] = 123;
    fileGetHighestStageDifficultyCompletedForFolder(0, &level, &difficulty);
    CHECK(scans == 80 && level == SP_LEVEL_DAM && difficulty == DIFFICULTY_AGENT);
    scans = 0;
    slots[0].times[DIFFICULTY_00][SP_LEVEL_FACILITY] = 456;
    slots[0].times[DIFFICULTY_AGENT][SP_LEVEL_EGYPT] = 789;
    fileGetHighestStageDifficultyCompletedForFolder(0, &level, &difficulty);
    CHECK(level == SP_LEVEL_FACILITY && difficulty == DIFFICULTY_00);
    scans = 0;
    CHECK(fileGetHighestStageUnlockedForFolder(0) == SP_LEVEL_DAM && scans == 80);
    scans = 0; unlockedStage = SP_LEVEL_EGYPT;
    CHECK(fileGetHighestStageUnlockedForFolder(0) == SP_LEVEL_EGYPT && scans == 1);
    scans = 0;
    CHECK(fileGetHighestStageUnlockedForFolder(4) == SP_LEVEL_DAM && scans == 0);
    sysSleep(10000000);
    CHECK(sleepCalls == 1 && observed[0].tv_sec == 1 && observed[0].tv_nsec == 0);
    sleepCalls = 0;
    sysSleep(25000001);
    CHECK(observed[0].tv_sec == 2 && observed[0].tv_nsec == 500000100);
    sleepCalls = 0;
    sysSleep(10000);
    CHECK(observed[0].tv_sec == 0 && observed[0].tv_nsec == 1000000);
    sleepCalls = 0; interruptOnce = 1;
    sysSleep(10000000);
    CHECK(sleepCalls == 2 && observed[1].tv_sec == 0 && observed[1].tv_nsec == 12345);
    sleepCalls = 0;
    sysSleep(0); sysSleep(-1);
    CHECK(sleepCalls == 0);
    puts("PASS: empty/missing/populated saves, descending scans, mission ids, sleep normalization and EINTR");
    return 0;
}
'''.replace('ENUMS', enums).replace('SCANS', scans).replace('SLEEP', sleep)
    with tempfile.TemporaryDirectory(prefix='gevr-menu-') as tmp:
        tmp = Path(tmp)
        source = tmp / 'probe.c'
        exe = tmp / ('probe.exe' if os.name == 'nt' else 'probe')
        source.write_text(harness)
        subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-O1', str(source), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=5)
        # Reproduce the original unsigned-enum failure without hanging the test.
        source.write_text(harness.replace('SP_LEVEL_NONE = -1,', '').replace('SP_LEVEL_NONE', '(SP_LEVEL_DAM - 1)'))
        subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-O1', str(source), '-o', str(exe)], check=True)
        try:
            failed = subprocess.run([str(exe)], capture_output=True, text=True, timeout=1)
        except subprocess.TimeoutExpired:
            # Invalid wrapped stage ids are rejected before reaching the mock;
            # the production loop keeps calling that rejection indefinitely.
            pass
        else:
            assert failed.returncode != 0 and '++scans <= 80' in failed.stderr, failed
        print('PASS: removing signed sentinel reproduces runaway save scan')


if __name__ == '__main__':
    main()
