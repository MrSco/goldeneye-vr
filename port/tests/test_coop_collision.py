"""Exercise the production player collision rule with narrow co-op starts."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
rule = block((ROOT / 'src/game/stan.c').read_text(),
             'static s32 gevrNetInsidePlayerProp(')
fixture = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
typedef int s32;
typedef float f32;
typedef uint64_t u64;
#define PROP_TYPE_VIEWER 1
#define LOG_NOTE 0
struct pos { float x, y, z; };
typedef struct PropRecord { int type; struct pos pos; } PropRecord;
struct player { PropRecord *prop; struct { struct pos collision_position; float collision_radius; } field_488; };
static struct player local, *g_CurrentPlayer = &local;
static int online = 1, occupied = 1, coop = 1;
static int netIsActive(void) { return online; }
static int netSlotOccupied(int slot) { return occupied; }
static int getPlayerPointerIndex(PropRecord *prop) { return 1; }
static int get_cur_playernum(void) { return 0; }
static int gevrCoopActive(void) { return coop; }
static u64 sysGetMicroseconds(void) { return 0; }
static void sysLogPrintf(int level, const char *fmt, ...) {}
static void chrpropGetCollisionBounds(PropRecord *prop, float *r, float *h, float *u) { *r = 30; *h = 180; *u = 0; }
/* RULE */
int main(void) {
    PropRecord self = {1, {0,0,0}}, other = {1, {0,0,0}};
    local.prop = &self; local.field_488.collision_radius = 30;
    /* Coincident vent starts, moving toward and through a teammate, and
       separated teammates blocking a narrow corridor must all be passable. */
    assert(gevrNetInsidePlayerProp(&other, 0, 10, "line"));
    other.pos.z = 34;
    assert(gevrNetInsidePlayerProp(&other, 0, 10, "volume"));
    other.pos.z = 100;
    assert(gevrNetInsidePlayerProp(&other, 0, 70, "line"));
    /* Competitive entry and movement deeper into overlap remain blocked. */
    coop = 0;
    assert(!gevrNetInsidePlayerProp(&other, 0, 70, "line"));
    other.pos.z = 34;
    assert(!gevrNetInsidePlayerProp(&other, 0, 10, "volume"));
    assert(gevrNetInsidePlayerProp(&other, 0, -10, "volume"));
    coop = 1;
    other.type = 2;
    assert(!gevrNetInsidePlayerProp(&other, 0, 10, "line"));
    other.type = 1; occupied = 0;
    assert(!gevrNetInsidePlayerProp(&other, 0, 10, "line"));
    occupied = 1; online = 0;
    assert(!gevrNetInsidePlayerProp(&other, 0, 10, "line"));
    online = 1;
    assert(!gevrNetInsidePlayerProp(&self, 0, 10, "line"));
    g_CurrentPlayer = 0;
    assert(!gevrNetInsidePlayerProp(&other, 0, 10, "line"));
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-coop-collision-') as temp:
    source, exe = Path(temp) / 'test.c', Path(temp) / 'test'
    source.write_text(fixture.replace('/* RULE */', rule))
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', str(source), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Co-op and competitive player collision checks passed')
