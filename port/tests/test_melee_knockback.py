"""Exercise the production push of a hit on a player (Slappers only, 2026-11-25).

A flat player slapped beside a wall online was thrown into it: the online punch
off the on-screen list (chrprop.c) gave the hit the distance to the target, up
to 100 units, where the game gives a unit vector, and record_damage_kills
(bondview2.c) pushes twice that a tick. Built natively from the game's own
gevrPunchDir, gevrHitPushDir and MoveBond's decay of the push."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
view = (ROOT / 'src/game/bondview2.c').read_text(encoding='utf-8')
chrprop = (ROOT / 'src/game/chrprop.c').read_text(encoding='utf-8')

punch_dir = block(chrprop, 'static void gevrPunchDir(')
push_dir = block(view, 'static void gevrHitPushDir(')

# MoveBond's decay of bondshotspeed, as the game runs it each tick
move = block(view, 'void MoveBond(')
norm = re.search(r'shotboost_norm = sqrtf\(.*?\);', move, re.S).group(0)
decay = block(move, 'for (i=0; i<3; i++)')

# The integration: the online punch passes the direction, never the distance,
# and every hit's push goes through the limit before it is added.
handler = block(chrprop, 'void chraiFistAttackHandler(s32 hand, s32 item_id)\n{')
assert 'gevrPunchDir(dx, dy, dz,' in handler, 'the online punch must pass a unit vector'
assert not re.search(r'vector\.x\s*=\s*dx\s*;', handler), 'the raw distance must not reach the hit'
record = block(view, 'void record_damage_kills(')
push_at = record.index('gevrHitPushDir(&vectorx, &vectorz);')
assert push_at < record.index('bondshotspeed.x = g_CurrentPlayer->bondshotspeed.x + 2.0f * vectorx;')
assert record.count('bondshotspeed.x =') == 1 and record.count('bondshotspeed.z =') == 1

fixture = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
typedef float f32;
typedef int s32;
typedef union coord3d { struct { f32 x, y, z; }; f32 f[3]; } coord3d;
static struct { coord3d bondshotspeed; } player, *g_CurrentPlayer = &player;
static f32 g_GlobalTimerDelta = 1.0f;
/* PUNCH_DIR */
/* PUSH_DIR */
/* Ticks until the push of a hit with this vector dies away, and the largest step it made. */
static int pushTicks(f32 vx, f32 vz, int limit, f32 *peak)
{
    s32 i;
    int ticks = 0;
    if (limit) gevrHitPushDir(&vx, &vz);
    player.bondshotspeed.x = 2.0f * vx;
    player.bondshotspeed.y = 0.0f;
    player.bondshotspeed.z = 2.0f * vz;
    *peak = sqrtf(player.bondshotspeed.x * player.bondshotspeed.x + player.bondshotspeed.z * player.bondshotspeed.z);
    while ((player.bondshotspeed.x != 0.0f || player.bondshotspeed.z != 0.0f) && ticks < 100000)
    {
        f32 shotboost_norm;
        /* NORM */
        /* DECAY */
        ticks++;
    }
    return ticks;
}
static int near(f32 a, f32 b) { return fabsf(a - b) < 1e-5f; }
int main(void)
{
    coord3d v;
    f32 x, z, peak;
    int before, after, unit;

    /* the punch's direction: unit length, toward the target, whatever the distance */
    gevrPunchDir(60.0f, 0.0f, 80.0f, 0.0f, 1.0f, &v);
    assert(near(v.x, 0.6f) && near(v.y, 0.0f) && near(v.z, 0.8f));
    gevrPunchDir(3.0f, -4.0f, 0.0f, 0.0f, 1.0f, &v);
    assert(near(v.x, 0.6f) && near(v.y, -0.8f) && near(v.z, 0.0f));
    gevrPunchDir(0.5f, 0.0f, 0.0f, 0.0f, 1.0f, &v);
    assert(near(v.x, 1.0f) && near(v.y, 0.0f) && near(v.z, 0.0f));
    /* right on top of the target: the way the puncher faces (facing is sin, -cos) */
    gevrPunchDir(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, &v);
    assert(near(v.x, 1.0f) && near(v.y, 0.0f) && near(v.z, 0.0f));
    gevrPunchDir(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, &v);
    assert(near(v.x, 0.0f) && near(v.z, -1.0f));

    /* the push: the game's unit vectors pass untouched, longer ones keep their direction at unit length */
    x = 0.6f; z = -0.8f; gevrHitPushDir(&x, &z); assert(x == 0.6f && z == -0.8f);
    x = 0.3f; z = 0.1f; gevrHitPushDir(&x, &z); assert(x == 0.3f && z == 0.1f);
    x = 0.0f; z = 0.0f; gevrHitPushDir(&x, &z); assert(x == 0.0f && z == 0.0f);
    x = 60.0f; z = -80.0f; gevrHitPushDir(&x, &z); assert(near(x, 0.6f) && near(z, -0.8f));
    x = NAN; z = 1.0f; gevrHitPushDir(&x, &z); assert(x == 0.0f && z == 0.0f);
    x = INFINITY; z = 0.0f; gevrHitPushDir(&x, &z); assert(x == 0.0f && z == 0.0f);

    /* the game's own push: 2 units, over in 30 ticks */
    unit = pushTicks(0.6f, 0.8f, 1, &peak);
    assert(near(peak, 2.0f) && unit <= 31);
    /* the v0.5.27 online slap from 70 units away, unlimited: 140 units a tick, for most of a minute */
    before = pushTicks(42.0f, 56.0f, 0, &peak);
    assert(peak > 139.0f && before > 2000);
    /* the same slap now, and the same packet from a v0.5.27 copy: the game's push */
    gevrPunchDir(42.0f, 0.0f, 56.0f, 0.0f, 1.0f, &v);
    after = pushTicks(v.x, v.z, 1, &peak);
    assert(near(peak, 2.0f) && after == unit);
    after = pushTicks(42.0f, 56.0f, 1, &peak);
    assert(near(peak, 2.0f) && after == unit);
    printf("push: game %d ticks at 2 units; raw slap %d ticks at 140 units; limited %d ticks\n", unit, before, after);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-melee-knockback-') as temp:
    source, exe = Path(temp) / 'test.c', Path(temp) / 'test.exe'
    source.write_text(fixture.replace('/* PUNCH_DIR */', punch_dir).replace('/* PUSH_DIR */', push_dir)
                      .replace('/* NORM */', norm).replace('/* DECAY */', decay), encoding='utf-8')
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-O2', '-Wall', '-Werror', str(source), '-lm', '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: a hit pushes a player by a unit vector: online punches pass the direction, every push is limited')
