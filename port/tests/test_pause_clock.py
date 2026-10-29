"""The pause menu's match clock (mpmenu.c gevrPauseMatchClock), compiled as written."""
from pathlib import Path
import shutil, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
menu = (root / 'src/game/mpmenu.c').read_text(encoding='utf-8')
start = menu.index('int gevrPauseMatchClock(')
end = menu.index('\n}\n', start) + 3
fixture = r'''
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef int s32;
s32 D_80048394, g_MpTime;
''' + menu[start:end] + r'''
static void check(s32 limit, s32 elapsed, const char *want, int ret) {
    char text[32];
    g_MpTime = limit; D_80048394 = elapsed;
    int got = gevrPauseMatchClock(text, sizeof(text));
    if (strcmp(text, want) || got != ret) { printf("limit %d elapsed %d: '%s' %d, want '%s' %d\n", limit, elapsed, text, got, want, ret); assert(0); }
}
int main(void) {
    check(36000, 0, "10:00 left", 600);          /* 10 minutes at 60 frames a second */
    check(36000, 1, "10:00 left", 600);          /* time left rounds up */
    check(36000, 36000 - 3600, "1:00 left", 60); /* "One minute left" */
    check(36000, 35999, "0:01 left", 1);
    check(36000, 36000, "0:00 left", 0);
    check(36000, 40000, "0:00 left", 0);         /* past the end, before the results */
    check(0, 0, "0:00 played", -1);              /* First to N, no limit: time played */
    check(0, 60 * 198 + 59, "3:18 played", -1);  /* played rounds down */
    puts("pause clock: time left in timed matches, time played otherwise");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-pause-clock-') as temp:
    c = Path(temp) / 'pause_clock.c'
    exe = Path(temp) / 'pause_clock.exe'
    c.write_text(fixture, encoding='utf-8')
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-O2', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
