"""The in-match Multiplayer Options folder's layout (front.c frontMpOptionsLayout, frontMpOptionsScroll),
compiled as written with the headset panel's fit (mpmenu.c gevrMenuPanelFit): the folder fits a full view
(4:3 and wide), a split screen's half and quarter views and the head-locked HUD panel on each headset; the
rows stay above the info line and left of the START / PREVIOUS tabs, at least three show in any view (all of
a short list), a quarter view goes compact, and the scroll keeps the cursor (a row or the START tab) in view."""
from pathlib import Path
import re, shutil, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
front = (root / 'src/game/front.c').read_text(encoding='utf-8')
menu = (root / 'src/game/mpmenu.c').read_text(encoding='utf-8')
header = (root / 'src/game/front.h').read_text(encoding='utf-8')


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    end = opening + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


layout = re.search(r'typedef struct \{[^}]*\} FrontOptionsLayout;', header).group(0)
pitch = front[front.index('#define MPOPT_PITCH_MAX'):front.index('\n', front.index('#define MPOPT_PITCH_MAX'))]
shown = menu[menu.index('#define GEVR_PANEL_HUD_SHOWN'):menu.index('\n', menu.index('#define GEVR_PANEL_HUD_SHOWN'))]

fixture = r'''
#include <stdio.h>
#include <stdint.h>
typedef int32_t s32; typedef float f32;
''' + layout + '\n' + pitch + '\n' + shown + '\n' + function(front, 'void frontMpOptionsLayout(') + '\n' + \
    function(front, 's32 frontMpOptionsScroll(') + '\n' + function(menu, 'void gevrMenuPanelFit(') + r'''
static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

static void view(s32 vw, s32 vh, const char *what) {
    /* the fonts' lines (STAGESEL_LINE): Zurich Bold and BankGothic, with a margin either way */
    for (s32 zline = 13; zline <= 18; zline++)
    for (s32 bline = 8; bline <= 12; bline++)
    for (s32 count = 1; count <= 20; count++) {
        FrontOptionsLayout l;
        frontMpOptionsLayout(vw, vh, count, zline, bline, &l);
        const s32 bottom = l.top + l.shown * l.pitch;
        CHECK(l.line == (l.small ? bline : zline), "%s: the rows' font", what);
        CHECK(l.top >= 2 && bottom <= vh - l.foot, "%s %dx%d, %d rows (%d/%d): rows %d..%d run into the info line at %d",
              what, vw, vh, count, zline, bline, l.top, bottom, vh - l.foot);
        CHECK(l.pitch >= l.line + 2 && l.pitch <= 0x14 + (l.line + 2 > 0x14 ? l.line + 2 - 0x14 : 0),
              "%s %dx%d: pitch %d for a line of %d", what, vw, vh, l.pitch, l.line);
        CHECK(l.shown >= (count < 3 ? count : 3), "%s %dx%d, %d rows (%d/%d): only %d show", what, vw, vh, count, zline, bline, l.shown);
        CHECK(l.shown <= count, "%s: more shown than there are", what);
        CHECK(l.tabw >= bline + 2 && l.tabw * 6 <= vw, "%s %dx%d: the tabs take %d", what, vw, vh, l.tabw);
        CHECK(!l.titled || l.top >= zline + 2, "%s: the title over the rows", what);
        if (vw < 200) CHECK(l.compact && l.small, "%s %dx%d: a narrow view is compact", what, vw, vh);
        if (vw >= 300 && vh >= 200) CHECK(!l.small && l.titled, "%s %dx%d: a full view keeps the folder's own font and title", what, vw, vh);
        /* the scroll: every cursor (the rows, then the START tab) stays in view, from any scroll */
        for (s32 from = 0; from < count; from++)
        for (s32 cursor = 0; cursor <= count; cursor++) {
            s32 s = frontMpOptionsScroll(from, cursor, count, l.shown);
            s32 c = cursor < count ? cursor : count - 1;
            CHECK(s >= 0 && s + l.shown <= count && c >= s && c < s + l.shown,
                  "%s: cursor %d of %d from %d: scroll %d shows %d", what, cursor, count, from, s, l.shown);
        }
    }
}

int main(void) {
    /* the game's 320 x 240 screen less the overscan, and wider ones: one view, two, three or four */
    view(304, 210, "one view");
    view(320, 240, "the whole screen");
    view(426, 240, "16:9");
    view(304, 100, "half (two players)");
    view(320, 110, "half");
    view(152, 100, "quarter");
    view(160, 110, "quarter");
    view(213, 110, "16:9 quarter");
    /* the head-locked HUD panel on each headset (mpmenu.c gevrMenuPanelRect in stereo) */
    {
        const s32 eyes[][2] = { { 1832, 1920 }, { 2064, 2208 }, { 1440, 1600 }, { 2160, 2160 }, { 2880, 2400 }, { 1920, 1080 } };
        for (unsigned e = 0; e < sizeof(eyes) / sizeof(eyes[0]); e++) {
            s32 x, y, w, h;
            gevrMenuPanelFit(320, 240, eyes[e][0], eyes[e][1], &x, &y, &w, &h);
            view(w, h, "VR HUD panel");
        }
    }
    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("PASS: the Multiplayer Options folder fits every view and the HUD panel, and scrolls to its cursor\n");
    return 0;
}
'''

compiler = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
if compiler is None:
    raise SystemExit('SKIP: no C compiler')
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'mpoptions_layout.c'
    exe = Path(tmp) / 'mpoptions_layout.exe'
    src.write_text(fixture, encoding='utf-8')
    subprocess.run([compiler, '-std=c99', '-Wall', '-Werror', '-o', str(exe), str(src)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    print(result.stdout, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
