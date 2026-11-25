"""The game's own screens in an online match on the headset (mpmenu.c gevrMenuPanelFit), compiled as
written with the Select Stage grid's layout (front.c frontStageSelectLayout): in stereo the panel goes on
the head-locked HUD, whose capture the game's screen covers at one scale (gfx_pc.cpp
gfx_adjust_viewport_or_scissor) and whose draws come out a ninth larger (the vertex shader's w = 0.9).
For each headset's eye buffer the panel lands wholly on the HUD quad, centred, with a margin, and the
Select Character panel (175 x 140 folder units) and the stage grid (two rows or more) fit in it."""
from pathlib import Path
import shutil, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
menu = (root / 'src/game/mpmenu.c').read_text(encoding='utf-8')
front = (root / 'src/game/front.c').read_text(encoding='utf-8')


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    end = opening + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


shown = menu[menu.index('#define GEVR_PANEL_HUD_SHOWN'):menu.index('\n', menu.index('#define GEVR_PANEL_HUD_SHOWN'))]
pitch = front[front.index('#define STAGESEL_PITCH_X'):front.index('#define STAGESEL_PHOTO_H')]

fixture = r'''
#include <stdio.h>
#include <stdint.h>
typedef int32_t s32; typedef float f32;
''' + shown + '\n' + pitch + '\n' + function(menu, 'void gevrMenuPanelFit(') + '\n' + \
    function(front, 'void frontStageSelectLayout(') + r'''
static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

int main(void) {
    /* eye buffers: Quest 2, Quest 3, Index, a Pico 4, a wide Pimax, a 16:9 one */
    const s32 eyes[][2] = { { 1832, 1920 }, { 2064, 2208 }, { 1440, 1600 }, { 2160, 2160 }, { 2880, 2400 }, { 1920, 1080 } };
    const s32 screens[][2] = { { 320, 240 }, { 640, 480 }, { 440, 330 } };
    for (unsigned s = 0; s < sizeof(screens) / sizeof(screens[0]); s++)
    for (unsigned e = 0; e < sizeof(eyes) / sizeof(eyes[0]); e++) {
        const s32 sw = screens[s][0], sh = screens[s][1], fbw = eyes[e][0], fbh = eyes[e][1];
        s32 x, y, w, h;
        gevrMenuPanelFit(sw, sh, fbw, fbh, &x, &y, &w, &h);
        /* what of the screen the capture holds: it covers the eye buffer at one scale */
        const f32 cover = (f32)fbw / sw > (f32)fbh / sh ? (f32)fbw / sw : (f32)fbh / sh;
        const f32 visw = fbw / cover, vish = fbh / cover;
        CHECK(x >= 0 && y >= 0 && x + w <= sw && y + h <= sh, "%dx%d on %dx%d: off the screen", sw, sh, fbw, fbh);
        CHECK(x + w / 2 >= sw / 2 - 1 && x + w / 2 <= sw / 2 + 1 && y + h / 2 >= sh / 2 - 1 && y + h / 2 <= sh / 2 + 1,
              "%dx%d on %dx%d: not centred", sw, sh, fbw, fbh);
        /* a ninth larger round the centre, it still lands inside the capture with a margin */
        CHECK(w / 0.9f <= visw * 0.97f && h / 0.9f <= vish * 0.97f,
              "%dx%d on %dx%d: %dx%d drawn a ninth larger overruns the HUD's %.0fx%.0f", sw, sh, fbw, fbh, w, h, visw, vish);
        CHECK(w / 0.9f >= visw * 0.9f && h / 0.9f >= vish * 0.9f, "%dx%d on %dx%d: %dx%d wastes the HUD", sw, sh, fbw, fbh, w, h);
        if (sw == 320) {
            /* the Select Character panel: 175 x 140 folder units at the rect's scale, not under half size */
            f32 scale = (f32)h / 140.0f;
            if ((f32)w / scale < 175.0f) scale = (f32)w / 175.0f;
            CHECK(scale >= 0.8f, "%dx%d: Select Character at %.2f", fbw, fbh, scale);
            /* the stage grid: two rows or more under its title and the line beneath */
            f32 gs; s32 cols, rows;
            frontStageSelectLayout(w, h - 2 * 16 - 6, 65, &gs, &cols, &rows);
            CHECK(cols >= 4 && rows >= 2 && gs >= 0.5f, "%dx%d: the grid %d x %d at %.2f", fbw, fbh, cols, rows, gs);
        }
    }
    /* a flat screen's (no eye buffer) panel is the screen less the margin, as a fallback */
    {
        s32 x, y, w, h;
        gevrMenuPanelFit(320, 240, 0, 0, &x, &y, &w, &h);
        CHECK(w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= 320 && y + h <= 240, "no eye buffer: %d %d %d %d", x, y, w, h);
    }
    if (failures) return 1;
    puts("PASS: in-match game screens on the headset: the panel lands wholly on the head-locked HUD for every eye shape, centred, Select Character and the stage grid fit");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='gevr-menu-panel-') as temp:
    c = Path(temp) / 'menu_panel.c'
    exe = Path(temp) / 'menu_panel.exe'
    c.write_text(fixture, encoding='utf-8')
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=gnu11', '-O2', str(c), '-o', str(exe)], cwd=root, check=True)
    subprocess.run([str(exe)], cwd=root, check=True)
