"""Co-op: a door a script shuts behind "Bond" locks once the party is through (#246).

Control's background list opens Natalya's door, waits for Bond in the next
room, then closes and locks it. In co-op the first player in was Bond, so the
lock shut the others out. This compiles the production block from net_coop.c
against a small fixture and checks the chrai.c hooks by text.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
coop = (ROOT / "port/src/net/net_coop.c").read_text(encoding="utf-8")
ai = (ROOT / "src/game/chrai.c").read_text(encoding="utf-8")
action = (ROOT / "src/game/chraction.c").read_text(encoding="utf-8")

# chrai.c: a Bond room test is remembered for the list, a run starts clean,
# and a script lock asks first.
assert "gevrCoopBondRoomPassed(AiListp, gevrPresetRoom(ChrEntityp, pad_id))" in block(
    ai, "                case AI_IFBondInRoomWithPad:")
assert "gevrCoopDeferBehindLock(AiListp, obj, bits)" in block(ai, "                case AI_DoorSetLock:")
run = ai.index("void                   ai(PropDefHeaderRecord *Entityp")
assert "gevrCoopBondRoomPassed(NULL, -1)" in ai[run:ai.index("gevrCoopScriptEffect(AiListp, Offset)", run)]
assert "pad->stan ? pad->stan->room : -1" in block(action, "s32 gevrPresetRoom(")
assert "coopBehindLockTick();" in block(coop, "void netCoopHostTick(")
assert "coopBehindLockReset();" in block(coop, "void netCoopStageLoaded(")

start = coop.index('/* A door a list shuts behind "Bond" (#246).')
end = coop.index("static void coopGrantLocal(s32 item)")
production = coop[start:end]

FIXTURE = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef short s16; typedef int s32; typedef unsigned int u32; typedef float f32;
typedef struct AIRecord { u8 cmd; } AIRecord;
typedef struct ObjectRecord { int tag; } ObjectRecord;
typedef enum { DOORSTATE_STATIONARY, DOORSTATE_OPENING, DOORSTATE_CLOSING, DOORSTATE_WAITING } DOORSTATE;
typedef struct DoorRecord { ObjectRecord base; f32 openPosition; DOORSTATE openstate; u32 keyflags; } DoorRecord;
typedef struct StandTile { s16 room; } StandTile;
typedef struct PropRecord { StandTile *stan; } PropRecord;
struct player { PropRecord *prop; };
#define COOP_LOG(...) (printf("coop: " __VA_ARGS__), printf("\n"))

static struct player players[4];
static PropRecord props[4];
static StandTile tiles[4];
static struct player *g_playerPointers[4];
static int standing[4];
static int host = 1, lock_sends, closes;

static int coopPlayerTargetable(int i) { return standing[i]; }
static int gevrCoopHostGuards(void) { return host; }
static void netSendHostDoorLock(ObjectRecord *door) { (void)door; lock_sends++; }
void doorActivate(DoorRecord *door, DOORSTATE state) { door->openstate = state; closes++; }

@@PRODUCTION@@

static void put(int i, int room) { tiles[i].room = (s16)room; }

int main(void)
{
    AIRecord list[2] = {{0}}, other[2] = {{0}};
    DoorRecord door = {{1}, 0.0f, DOORSTATE_STATIONARY, 0};
    for (int i = 0; i < 4; i++) {
        props[i].stan = &tiles[i];
        players[i].prop = &props[i];
        g_playerPointers[i] = &players[i];
    }
    standing[0] = standing[1] = standing[2] = 1;   /* three players; slot 3 empty */
    put(0, 7); put(1, 3); put(2, 3);               /* the host is through, two behind */

    /* no Bond room test in this run: lock as solo */
    coopBehindLockReset();
    gevrCoopBondRoomPassed(NULL, -1);
    assert(!gevrCoopDeferBehindLock(list, (ObjectRecord *)&door, 1));
    /* another list's room test does not count */
    gevrCoopBondRoomPassed(other, 7);
    assert(!gevrCoopDeferBehindLock(list, (ObjectRecord *)&door, 1));
    /* a client never defers (it runs no lists) */
    host = 0;
    gevrCoopBondRoomPassed(list, 7);
    assert(!gevrCoopDeferBehindLock(list, (ObjectRecord *)&door, 1));
    host = 1;

    /* the first player in: closed by the script, unlocked until the rest arrive */
    gevrCoopBondRoomPassed(list, 7);
    assert(gevrCoopDeferBehindLock(list, (ObjectRecord *)&door, 1));
    assert(door.keyflags == 0);
    coopBehindLockTick();
    assert(door.keyflags == 0 && lock_sends == 0);

    /* a teammate opens it and comes through; the last one is still outside */
    put(1, 7);
    door.openPosition = 1.0f; door.openstate = DOORSTATE_WAITING;
    coopBehindLockTick();
    assert(door.keyflags == 0 && lock_sends == 0);

    /* a downed teammate outside does not hold it */
    standing[2] = 0;
    coopBehindLockTick();
    assert(door.keyflags == 1 && lock_sends == 1 && closes == 1 && door.openstate == DOORSTATE_CLOSING);
    coopBehindLockTick();
    assert(lock_sends == 1);   /* once */

    /* everyone already through: lock at once, as solo */
    DoorRecord second = {{2}, 0.0f, DOORSTATE_STATIONARY, 0};
    standing[2] = 1; put(2, 7);
    gevrCoopBondRoomPassed(list, 7);
    assert(!gevrCoopDeferBehindLock(list, (ObjectRecord *)&second, 1));

    /* a new stage forgets a pending lock */
    put(2, 3);
    gevrCoopBondRoomPassed(list, 7);
    assert(gevrCoopDeferBehindLock(list, (ObjectRecord *)&second, 1));
    coopBehindLockReset();
    put(2, 7);
    coopBehindLockTick();
    assert(second.keyflags == 0 && lock_sends == 1);
    puts("PASS: co-op door shut behind the first player locks once every standing player is through (#246)");
    return 0;
}
"""


def main():
    cc = shutil.which("gcc") or shutil.which("cc") or shutil.which("clang")
    assert cc, "no C compiler"
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "behind_lock.c"
        exe = Path(tmp) / "behind_lock.exe"
        src.write_text(FIXTURE.replace("@@PRODUCTION@@", production), encoding="utf-8")
        subprocess.run([cc, "-std=c11", "-O1", "-g", "-Wall", "-Werror", "-Wno-unused-function",
                        str(src), "-o", str(exe)], check=True)
        out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
        print(out.strip().splitlines()[-1])


if __name__ == "__main__":
    main()
