"""GoldenEye's own cameras outside the level: the ray test against its room triangles (DAB gebeanStageTickCamera)."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
rooms = (ROOT / 'port/src/gevr_bean_rooms.inc.h').read_text()


def function(source, signature):
    start = source.index(signature)
    end, depth = source.index('{', start) + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


production = '\n'.join([
    rooms[rooms.index('struct beanRoomTri {'):rooms.index('static unsigned roomBe32(')],
    function(rooms, 'static int roomReferenceCompare('),
    function(rooms, 'static int roomReferenceTree('),
    function(rooms, 'static void roomRayNode('),
])

fixture = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
/* PRODUCTION */
static void tri(const float a[3], const float b[3], const float c[3], int culled)
{
    struct beanRoomTri *t = &s_reference[s_referenceCount++];
    const float *v[3] = {a, b, c};
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) t->p[i][k] = v[i][k];
    for (int k = 0; k < 3; k++) {
        t->lo[k] = fminf(a[k], fminf(b[k], c[k]));
        t->hi[k] = fmaxf(a[k], fmaxf(b[k], c[k]));
    }
    t->opaque = 1;
    t->culled = culled;
    t->room = 1;
}
/* a room 200 units a side, each wall's front (e1 x e2) facing in, as the
 * side G_CULL_BACK keeps is the one the player inside sees */
static void room(int culled)
{
    static const float c[8][3] = {{-100,-100,-100},{100,-100,-100},{100,100,-100},{-100,100,-100},
                                  {-100,-100,100},{100,-100,100},{100,100,100},{-100,100,100}};
    static const int quads[6][4] = {{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{4,5,1,0},{3,2,6,7}};
    s_referenceCount = 0;
    for (int q = 0; q < 6; q++) {
        tri(c[quads[q][0]], c[quads[q][1]], c[quads[q][2]], culled);
        tri(c[quads[q][0]], c[quads[q][2]], c[quads[q][3]], culled);
    }
    s_treeCount = 0;
    roomReferenceTree(0, s_referenceCount);
}
static int look(float x, float y, float z, float dx, float dy, float dz)
{
    float o[3] = {x, y, z}, d[3] = {dx, dy, dz}, inv[3], best = 1e9f;
    int facing = 0;
    const float len = sqrtf(dx * dx + dy * dy + dz * dz);
    for (int k = 0; k < 3; k++) {
        d[k] /= len;
        inv[k] = 1.0f / (d[k] != 0.0f ? d[k] : 1e-9f);
    }
    roomRayNode(0, o, d, inv, &best, &facing);
    return facing;
}
int main(void)
{
    s_reference = calloc(64, sizeof(*s_reference));
    s_tree = calloc(128, sizeof(*s_tree));
    room(1);
    /* inside, every way: the fronts of its walls */
    assert(look(0, 0, 0, 1, 0, 0) == 1 && look(0, 0, 0, 0, -1, 0) == 1 && look(10, 20, 30, 0.3f, 0.2f, -1) == 1);
    /* outside (under the floor, in the rock beside it): their backs */
    assert(look(0, -150, 0, 0, 1, 0) == -1 && look(-150, 0, 0, 1, 0.1f, 0) == -1);
    /* looking away from it: nothing; nearer than a unit: nothing */
    assert(look(0, -150, 0, 0, -1, 0) == 0 && look(0, -100.5f, 0, 0, 1, 0) == 1);
    /* a room GoldenEye draws two-sided is never "outside" */
    room(0);
    assert(look(0, -150, 0, 0, 1, 0) == 1);
    puts("PASS: camera-outside rays: fronts from inside, backs from outside, two-sided rooms never outside");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp)
    (path / 'test.c').write_text(fixture.replace('/* PRODUCTION */', production))
    exe = path / 'test.exe'
    subprocess.run([shutil.which('gcc') or 'cc', '-std=c11', '-O1', str(path / 'test.c'), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
