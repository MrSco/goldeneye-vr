"""The names on the in-match Handicap and Teams screens (front.c frontPanelFitNames, frontPanelFit), compiled
as written over a fixed-width font: a list's names are cut alike, so a split screen's guests read "AGENT 44.(2)",
"AGENT 44.(3)" beside each other (owner, 2026-11-24: "AGENT 44." and "AGENT 446.(2)" before), the "(n)" or
"(Bot)" kept whole, no space before the cut, a name that fits left whole, and every name within its room."""
from pathlib import Path
import shutil, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
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


fixture = r'''
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef int32_t s32;
struct fontchar { int unused; };
struct font { int unused; };
/* every glyph 6 pixels wide */
static void textMeasure(s32 *h, s32 *w, const char *text, struct fontchar *c, struct font *f, s32 x)
{
    (void)c; (void)f; (void)x;
    *h = 8;
    *w = 6 * (s32)strlen(text);
}
#define PANEL_NAME_LEN 48
''' + function(front, 'static void frontPanelFit(') + '\n' + function(front, 'static s32 frontPanelNameBase(') + '\n' + \
    function(front, 'static void frontPanelNameCut(') + '\n' + function(front, 'static void frontPanelFitNames(') + r'''
static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

int main(void)
{
    static const char *const guests[] = { "AGENT 4462", "AGENT 4462 (2)", "AGENT 4462 (3)", "AGENT 4462 (4)" };
    static const char *const mixed[] = { "AGENT 4462 (2)", "Natalya (Bot)", "Q" };
    char out[4][PANEL_NAME_LEN];
    struct fontchar c;
    struct font f;

    /* room for 12 glyphs: the guests' own part cut alike, the host's name whole */
    frontPanelFitNames(out, guests, 4, 72, &c, &f);
    CHECK(!strcmp(out[0], "AGENT 4462"), "the host's name fits whole: %s", out[0]);
    CHECK(!strcmp(out[1], "AGENT 44.(2)") && !strcmp(out[2], "AGENT 44.(3)") && !strcmp(out[3], "AGENT 44.(4)"),
          "the guests alike: %s %s %s", out[1], out[2], out[3]);
    /* room for 9: no space before the cut, the host's name cut to the same length */
    frontPanelFitNames(out, guests, 4, 54, &c, &f);
    CHECK(!strcmp(out[1], "AGENT.(2)") && !strcmp(out[0], "AGENT."), "cut alike past the space: %s %s", out[0], out[1]);
    for (int i = 0; i < 4; i++) CHECK((s32)strlen(out[i]) * 6 <= 54, "within the room: %s", out[i]);
    /* everyone fits: nothing cut */
    frontPanelFitNames(out, guests, 4, 200, &c, &f);
    for (int i = 0; i < 4; i++) CHECK(!strcmp(out[i], guests[i]), "whole: %s", out[i]);
    /* a bot keeps its "(Bot)"; a short name stays as it is */
    frontPanelFitNames(out, mixed, 3, 66, &c, &f);
    CHECK(!strcmp(out[1], "Natal.(Bot)") && !strcmp(out[0], "AGENT.(2)") && !strcmp(out[2], "Q"), "%s %s %s", out[0], out[1], out[2]);
    /* too narrow for "A.(2)": each cut on its own, still within the room */
    frontPanelFitNames(out, guests, 4, 24, &c, &f);
    for (int i = 0; i < 4; i++) CHECK((s32)strlen(out[i]) * 6 <= 24, "the narrowest room: %s", out[i]);

    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("PASS: the Handicap and Teams screens' names cut alike, their (n) kept, within their room\n");
    return 0;
}
'''
compiler = shutil.which('gcc') or shutil.which('cc') or shutil.which('clang')
if compiler is None:
    raise SystemExit('SKIP: no C compiler')
with tempfile.TemporaryDirectory(prefix='gevr-panel-names-') as tmp:
    src = Path(tmp) / 'panel_names.c'
    exe = Path(tmp) / 'panel_names.exe'
    src.write_bytes(fixture.encode())
    subprocess.run([compiler, '-std=c99', '-Wall', '-Werror', '-Wno-unused-function', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
