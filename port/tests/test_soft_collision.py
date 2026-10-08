"""Exercise the production soft collision between online players (bondview2.c)."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
view = (ROOT / 'src/game/bondview2.c').read_text()
defines = '\n'.join(re.findall(r'^#define GEVR_SOFT_\w+ .*$', view, re.M))
rule = block(view, 'static void gevrSoftPlayerCollision(')
fixture = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
typedef int s32;
typedef int bool;
typedef float f32;
#define TRUE 1
#define FALSE 0
struct coord3d { float x, y, z; };
typedef struct PropRecord { struct coord3d pos; } PropRecord;
struct player {
    PropRecord *prop; int bonddead;
    struct { struct coord3d collision_position; float collision_radius; } field_488;
};
static struct player players[4], *g_playerPointers[4], *g_CurrentPlayer;
static PropRecord props[4];
static int online = 1, me = 0, count = 2, owned[4] = {1, 0, 0, 0}, spectator[4];
static f32 g_GlobalTimerDelta = 1.0f;
static bool netIsActive(void) { return online; }
static int get_cur_playernum(void) { return me; }
static int getPlayerCount(void) { return count; }
static bool netSlotOccupied(int slot) { return slot < count; }
static int gevrNetOwnsSlot(int slot) { return owned[slot]; }
static int netPlayerIsSpectator(int slot) { return spectator[slot]; }
static void chrpropGetCollisionBounds(PropRecord *prop, f32 *r, f32 *h, f32 *u) { *r = 30; *h = 180; *u = 0; }
/* DEFINES */
/* RULE */
static void place(int slot, float x, float z) {
    props[slot].pos.x = x; props[slot].pos.y = 0; props[slot].pos.z = z;
    players[slot].field_488.collision_position = props[slot].pos;
}
int main(void) {
    struct coord3d move;
    for (int i = 0; i < 4; i++) {
        players[i].prop = &props[i]; players[i].field_488.collision_radius = 30; g_playerPointers[i] = &players[i];
    }
    g_CurrentPlayer = &players[0];
    /* apart: nothing changes */
    place(0, 0, 0); place(1, 100, 0);
    move = (struct coord3d){ 10, 0, 0 };
    gevrSoftPlayerCollision(&move);
    assert(move.x == 10 && move.z == 0);
    /* 20 into a 60 reach: slowed to 70%, pushed away along -x by 0.12 of the 20 */
    place(1, 40, 0);
    move = (struct coord3d){ 10, 0, 0 };
    gevrSoftPlayerCollision(&move);
    assert(fabsf(move.x - (7.0f - 2.4f)) < 0.01f && move.z == 0);
    /* deeper pushes harder, up to 6 a tick */
    place(1, 5, 0);
    move = (struct coord3d){ 0, 0, 0 };
    gevrSoftPlayerCollision(&move);
    assert(fabsf(move.x + 6.0f) < 0.01f);
    /* two frames' worth of time, two frames' push */
    place(1, 40, 0);
    g_GlobalTimerDelta = 2.0f;
    move = (struct coord3d){ 0, 0, 0 };
    gevrSoftPlayerCollision(&move);
    assert(fabsf(move.x + 4.8f) < 0.01f);
    g_GlobalTimerDelta = 1.0f;
    /* on top of each other: the lower slot goes +x */
    place(1, 0, 0);
    move = (struct coord3d){ 0, 0, 0 };
    gevrSoftPlayerCollision(&move);
    assert(move.x > 0.0f);
    /* a floor above, a dead player, a spectator: no push */
    place(1, 40, 0); props[1].pos.y = 300;
    move = (struct coord3d){ 0, 0, 0 };
    gevrSoftPlayerCollision(&move);
    assert(move.x == 0);
    props[1].pos.y = 0; players[1].bonddead = 1;
    gevrSoftPlayerCollision(&move);
    assert(move.x == 0);
    players[1].bonddead = 0; spectator[1] = 1;
    gevrSoftPlayerCollision(&move);
    assert(move.x == 0);
    spectator[1] = 0;
    /* only the players this headset owns; nothing offline */
    me = 1; g_CurrentPlayer = &players[1];
    gevrSoftPlayerCollision(&move);
    assert(move.x == 0);
    me = 0; g_CurrentPlayer = &players[0]; online = 0;
    gevrSoftPlayerCollision(&move);
    assert(move.x == 0);
    /* two overlapping at once: the pushes add */
    online = 1; count = 3; place(1, 40, 0); place(2, 0, 40);
    move = (struct coord3d){ 0, 0, 0 };
    gevrSoftPlayerCollision(&move);
    assert(move.x < 0 && move.z < 0);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-soft-collision-') as temp:
    source, exe = Path(temp) / 'test.c', Path(temp) / 'test'
    source.write_text(fixture.replace('/* DEFINES */', defines).replace('/* RULE */', rule))
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', str(source), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: soft collision pushes owned players out of overlaps, slowed, capped, per floor')
