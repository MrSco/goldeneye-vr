"""The online Select Stage photo grid (front.c frontStageSelectPhoto, frontStageSelectLayout,
frontStageSelectStep), compiled as written against the online stage list (net_match.c):
every stage has a picture, the grid fits each split-screen view, and the cursor steps over
stages that cannot be chosen and stops at the edges."""
from pathlib import Path
import re, shutil, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
front = (root / 'src/game/front.c').read_text(encoding='utf-8')
header = (root / 'src/game/front.h').read_text(encoding='utf-8')
start = front.index('static const struct { u8 level; u16 image; } s_stageSelPhotos[]')
end = front.index('static s32 s_stageSelCursor[MAX_PLAYER_COUNT];', start)
tile = re.search(r'typedef struct \{[^}]*\} FrontStageTile;', header).group(0)

fixture = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
typedef int32_t s32; typedef uint8_t u8; typedef uint16_t u16; typedef float f32;
#include "bondconstants.h"
#include "assets/image_externs.h"
#include "net/net_match.h"
''' + tile + '\n' + front[start:end] + r'''
static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

static FrontStageTile tiles[16];
static void grid(const char *eligible) {
    for (int i = 0; i < 16; i++) tiles[i].eligible = eligible[i] == '1';
}

int main(void) {
    /* every stage the online list offers has a picture: its own, or its map's for "(Full)" */
    for (int i = 0; i < netStageCount(); i++) {
        const NetMatchStage *st = netStage(i);
        CHECK(frontStageSelectPhoto((s32)netStageLevelId(st->level_id)) >= 0, "%s has no picture", st->name);
    }
    CHECK(frontStageSelectPhoto(-1) == IMAGE_MP_RANDOM, "No vote is the screen's Random photo");
    CHECK(frontStageSelectPhoto(LEVELID_SILO) == IMAGE_MP_SILO, "Silo: the mission select's picture");
    CHECK(frontStageSelectPhoto(netStageLevelId(LEVELID_FACILITY | NET_STAGE_FULL)) == IMAGE_MP_FACILITY,
          "Facility (Full): Facility's photo");
    CHECK(frontStageSelectPhoto(LEVELID_TITLE) < 0, "the title has no picture");

    /* the grid fits each view (the game's 320 x 240 screen: one view, two, three or four),
     * four photos across or more, two rows or more shown, the rest scroll */
    {
        const s32 views[][2] = { { 304, 210 }, { 304, 100 }, { 152, 100 }, { 320, 240 }, { 160, 110 } };
        const s32 count = netStageCount() + 1;
        for (unsigned v = 0; v < sizeof(views) / sizeof(views[0]); v++) {
            const s32 vw = views[v][0], gridh = views[v][1] - 2 * 16 - 6;
            f32 s; s32 cols, rows;
            frontStageSelectLayout(vw, gridh, count, &s, &cols, &rows);
            CHECK(cols >= 4 && cols <= 8, "%dx%d: %d columns", vw, views[v][1], cols);
            CHECK(rows >= 2, "%dx%d: %d rows", vw, views[v][1], rows);
            CHECK(cols * (s32)(STAGESEL_PITCH_X * s) <= vw - 4, "%dx%d: %d columns of %.2f overrun the width", vw, views[v][1], cols, s);
            CHECK((rows - 1) * (s32)(STAGESEL_PITCH_Y * s) + (s32)(STAGESEL_BAND * s) <= gridh,
                  "%dx%d: %d rows of %.2f overrun the height", vw, views[v][1], rows, s);
            CHECK(s >= 0.4f && s <= 2.0f, "%dx%d: scale %.2f", vw, views[v][1], s);
        }
        /* a full view: the screen's own size, names on the photos */
        {
            f32 s; s32 cols, rows;
            frontStageSelectLayout(304, 210 - 38, count, &s, &cols, &rows);
            CHECK(cols == 4 && s >= 0.9f, "one view: four across at the screen's size (%d, %.2f)", cols, s);
        }
        /* a short list takes no more columns or rows than it has */
        {
            f32 s; s32 cols, rows;
            frontStageSelectLayout(304, 172, 3, &s, &cols, &rows);
            CHECK(cols == 3 && rows == 1, "three tiles: %d x %d", cols, rows);
        }
    }

    /* the cursor: 10 tiles four across, 2 and 5 not choosable */
    grid("1101101111");
    CHECK(frontStageSelectStep(tiles, 10, 4, 1, 1, 0) == 3, "right passes over a stage that cannot be chosen");
    CHECK(frontStageSelectStep(tiles, 10, 4, 0, -1, 0) == 0, "left at the first tile stays");
    CHECK(frontStageSelectStep(tiles, 10, 4, 9, 1, 0) == 9, "right at the last tile stays");
    CHECK(frontStageSelectStep(tiles, 10, 4, 1, 0, 1) == 4 || frontStageSelectStep(tiles, 10, 4, 1, 0, 1) == 6,
          "down onto a stage that cannot be chosen takes its row's nearest");
    CHECK(frontStageSelectStep(tiles, 10, 4, 3, 0, 1) == 7, "down a row");
    CHECK(frontStageSelectStep(tiles, 10, 4, 7, 0, 1) == 9, "down onto a short last row: its last tile");
    CHECK(frontStageSelectStep(tiles, 10, 4, 9, 0, 1) == 9, "down from the last row stays");
    CHECK(frontStageSelectStep(tiles, 10, 4, 1, 0, -1) == 1, "up from the first row stays");
    CHECK(frontStageSelectStep(tiles, 10, 4, 8, 0, -1) == 4, "up a row");
    grid("1000000000");
    CHECK(frontStageSelectStep(tiles, 10, 4, 0, 1, 0) == 0, "nothing else to choose: the cursor stays");
    CHECK(frontStageSelectStep(tiles, 10, 4, 0, 0, 1) == 0, "nothing below to choose: the cursor stays");
    CHECK(frontStageSelectStep(tiles, 10, 4, 42, 0, 0) == 0, "a cursor past the list starts over");

    if (failures) return 1;
    puts("PASS: online stage grid: every stage has a picture, the grid fits one to four views, the cursor passes over stages that cannot be chosen and stops at the edges");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='gevr-stage-grid-') as temp:
    c = Path(temp) / 'stage_grid.c'
    exe = Path(temp) / 'stage_grid.exe'
    c.write_text(fixture, encoding='utf-8')
    args = [shutil.which('gcc') or 'gcc', '-std=gnu11', '-O2', '-DGEVR=1', '-DPLATFORM_64BIT=1', '-D_LANGUAGE_C=1',
            '-I.', '-Iport/include', '-Iinclude', '-Isrc', '-Iport/src', str(c), 'port/src/net/net_match.c', '-o', str(exe)]
    subprocess.run(args, cwd=root, check=True)
    subprocess.run([str(exe)], cwd=root, check=True)
