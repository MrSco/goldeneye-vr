"""A thrown projectile's room walk stays inside its buffers.

v0.5.27, a Linux player in an online match of proximity mines and throwing
knives crashed again and again. gunInitProjectileFromPlayer (gun.c), which
every thrown grenade, knife, mine and launched grenade goes through - the
thrower's own and every remote player's copied throw - handed
bgFindRoomsAlongSegment (bg.c) a 2-byte start-room array, which it reads 8
bytes of, and ONE s32 for the rooms the throw crosses, which it fills up to
0x14 entries. On the N64 the frame's padding (pad_a) took the overflow. In
the released Linux build the s32 is the last slot of the frame: a throw
crossing one portal (a doorway) wrote the second room over the saved rbx,
which generate_player_thrown_knife and _grenade keep the new projectile in,
and the next `wor->runtime_bitflags` faulted. The Windows build keeps the
callee-saved xmm6/xmm7 there, so it took 10 rooms to reach a register.

This builds the real gunInitProjectileFromPlayer, bgFindRoomsAlongSegment
and addToByteSetMaxSize15 around a chain of portals a throw crosses and
checks the rooms come back right; with AddressSanitizer (Linux, Apple) any
read or write past a buffer fails the run, and without it the rooms buffer
is checked by the source.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gevr_testcc  # noqa: E402


def function(source, signature):
    match = re.search(re.escape(signature) + r"(?:\s|//[^\n]*\n|/\*.*?\*/\s*)*\{", source, re.S)
    assert match, signature
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


FIXTURE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int s32;
typedef unsigned int u32;
typedef unsigned char u8;
typedef float f32;
typedef struct coord3d { float x, y, z; } coord3d;
typedef struct Mtxf { float m[4][4]; } Mtxf;
typedef struct StandTile { int unused; } StandTile;
typedef struct PropRecord { coord3d pos; StandTile *stan; } PropRecord;
typedef struct Projectile {
    s32 flags;
    u8 unkCC[8];
    f32 unkd4, unkD8, unkDC;
} Projectile;
typedef struct ObjectRecord {
    PropRecord *prop;
    u32 runtime_bitflags;
    Projectile *projectile;
} ObjectRecord;
typedef struct bg_portal_entry { int unused; } bg_portal_entry;
typedef struct { bg_portal_entry *offset_portal; u8 connectedRoom1, connectedRoom2; } bg_portal;
#define PORTMAX 200
#define PROJECTILEFLAG_00000100 0x100

/* a chain of doorways: room 1 -> 2 -> ... -> CHAIN + 1, all crossed */
#define CHAIN 6
static bg_portal_entry s_entry;
bg_portal g_BgPortals[CHAIN + 1];
s32 sub_GAME_7F0B9F14(s32 portalnum, coord3d *from, coord3d *to) { (void)portalnum; (void)from; (void)to; return 1; }

static PropRecord s_player = { { 0, 0, 0 }, NULL };
static coord3d s_eye = { 0, 40, 0 };
void *g_CurrentPlayer;
PropRecord *getCurrentPlayerProp(void) { return &s_player; }
f32 bondviewGetPlayerStanHeight(void *player) { (void)player; return 30.0f; }
void bondviewUpdateGuardTankFlagsRelated(PropRecord *prop, s32 on) { (void)prop; (void)on; }
s32 stanTestLineUnobstructed(StandTile **tile, f32 x1, f32 z1, f32 x2, f32 z2, s32 flags, f32 yhi, f32 ylo,
                             f32 a, f32 b)
{ (void)tile; (void)x1; (void)z1; (void)x2; (void)z2; (void)flags; (void)yhi; (void)ylo; (void)a; (void)b; return 1; }
void gunInitProjectileObject(ObjectRecord *obj, coord3d *pos, StandTile *tile, Mtxf *a, coord3d *v, Mtxf *b, PropRecord *owner)
{ (void)obj; (void)pos; (void)tile; (void)a; (void)v; (void)b; (void)owner; }
u8 bondviewGetCurrentPlayersRoom(void) { return 1; }
coord3d *bondviewGetCurrentPlayersPosition3(void) { return &s_eye; }

/* INSERT_BG */

/* INSERT_GUN */

int main(void)
{
    int i;
    for (i = 0; i < CHAIN; i++) {
        g_BgPortals[i].offset_portal = &s_entry;
        g_BgPortals[i].connectedRoom1 = (u8)(1 + i);
        g_BgPortals[i].connectedRoom2 = (u8)(2 + i);
    }
    g_BgPortals[CHAIN].offset_portal = NULL;

    PropRecord knifeprop = { { 0 } };
    Projectile projectile;
    memset(&projectile, 0x5a, sizeof(projectile));
    ObjectRecord knife = { &knifeprop, 0x80, &projectile };
    coord3d target = { 300, 40, 0 }, velocity = { 25, 5, 0 };
    Mtxf rot = { { { 1 } } }, spin = { { { 1 } } };

    gunInitProjectileFromPlayer(&knife, &target, &rot, &velocity, &spin);

    /* the projectile's rooms: the last set found, ended */
    if (projectile.unkCC[0] != CHAIN + 1 || projectile.unkCC[1] != 0xff) {
        printf("rooms %d %d\n", projectile.unkCC[0], projectile.unkCC[1]);
        return 1;
    }

    /* the room walk itself: every room crossed, never past what it was given */
    {
        u8 from[8] = { 1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
        u8 final[8];
        s32 guard[64];
        s32 count = -1;
        for (i = 0; i < 64; i++) guard[i] = -7;
        bgFindRoomsAlongSegment(&s_eye, &target, from, final, guard + 8, &count, 0x14);
        if (count != CHAIN + 1) { printf("count %d\n", count); return 1; }
        for (i = 0; i < count; i++) if (guard[8 + i] != 1 + i) { printf("room %d is %d\n", i, guard[8 + i]); return 1; }
        for (i = 0; i < 64; i++) if ((i < 8 || i >= 8 + count) && guard[i] != -7) { printf("wrote slot %d\n", i); return 1; }
    }
    puts("throw rooms: ok");
    return 0;
}
"""


def main():
    gun = (ROOT / "src/game/gun.c").read_text(encoding="utf-8").replace("\r\n", "\n")
    bg = (ROOT / "src/game/bg.c").read_text(encoding="utf-8").replace("\r\n", "\n")
    init = function(gun, "void gunInitProjectileFromPlayer(ObjectRecord *obj, coord3d *targetpos, Mtxf *arg2, coord3d *velocity, Mtxf *arg4)")
    # the host frame: buffers bgFindRoomsAlongSegment can fill (checked by source where no sanitizer links)
    gevr = init.split("#ifdef GEVR", 1)[1].split("#else", 1)[0]
    assert re.search(r"u8 rooms\[8\]", gevr) and re.search(r"s32 sp54\[0x14\]", gevr), \
        "gunInitProjectileFromPlayer must give the room walk 8 start rooms and 0x14 crossed rooms"
    walk = function(bg, "void bgFindRoomsAlongSegment(coord3d *fromPos, coord3d *toPos, u8 *fromRooms, u8 *finalRooms, s32 *traversedRooms, s32 *traversedRoomCount, s32 maxTraversedRooms)")
    add = function(bg, "void addToByteSetMaxSize15(u8* set, u8 newElement)")
    fixture = FIXTURE.replace("/* INSERT_BG */", add + "\n\n" + walk).replace("/* INSERT_GUN */", init)

    cc = shutil.which("gcc") or shutil.which("clang") or "cc"
    sanitizers = gevr_testcc.sanitize("-fsanitize=address", "-fno-omit-frame-pointer", cc=cc)
    with tempfile.TemporaryDirectory(prefix="gevr-throw-rooms-") as temp:
        source = Path(temp) / "throw_rooms.c"
        exe = Path(temp) / "throw_rooms.exe"
        source.write_text(fixture, encoding="utf-8")
        result = subprocess.run([cc, "-std=gnu11", "-O1", "-g", "-DGEVR=1", "-Wno-unused-variable",
                                 "-Wno-unused-but-set-variable", *sanitizers, str(source), "-o", str(exe)],
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=0:abort_on_error=0"}
        run = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
        assert run.returncode == 0, run.stdout + run.stderr
        print(run.stdout.strip(), "(AddressSanitizer)" if sanitizers else "(no sanitizer on this compiler)")
    print("test_throw_rooms: passed")


if __name__ == "__main__":
    main()
