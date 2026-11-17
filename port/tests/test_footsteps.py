"""The Footsteps patch's script commands (src/game/gevr_footsteps.c) on a real
GoldenEye with Mario Characters V3.17 cartridge.

footsteps_native.c binds the cartridge (romcampaign_native.c), runs commands
taken from its own level scripts and prints what changed; this checks them,
then runs every 2B command of every setup once and requires each address they
name to be memory the port has. No ROM is in the repo: set GEVR_MARIO_ROM to
the player's own patched cartridge; without it the test says SKIP.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_romcampaign as rc  # noqa: E402

ROOT = rc.ROOT


def ai_lengths():
    src = (ROOT / "port/src/gevr_romlayout.c").read_text(encoding="utf-8")
    body = src[src.index("g_AiLen[GEVR_AI_OPS] = {"):]
    body = body[body.index("{") + 1:body.index("};")]
    lens = [int(x) for x in re.findall(r"\d+", body)]
    assert len(lens) == 253
    return lens


def commands(rom):
    """Every 2B command of the cartridge's setups, with the PRINT after it."""
    d = rc.window(rom, rc.MARIO)
    lens = ai_lengths()
    rows = 0
    while struct.unpack_from(">I", d, 0x252C4 + 12 * rows)[0] == rows:
        rows += 1
    out = []
    for k in range(1, rows - 1):
        _, nptr, addr = struct.unpack_from(">III", d, 0x252C4 + 12 * k)
        nxt = struct.unpack_from(">I", d, 0x252C4 + 12 * (k + 1) + 8)[0]
        name = rc.cstr(d, nptr)
        if not (name.startswith("Usetup") or name.startswith("Ump_setup")):
            continue
        s = zlib.decompressobj(-15).decompress(rom[addr + 2:nxt])
        tab = struct.unpack_from(">I", s, 0x14)[0]
        i = 0
        while True:
            p, _ = struct.unpack_from(">II", s, tab + 8 * i)
            if not p:
                break
            i += 1
            at = p
            while at < len(s):
                op = s[at]
                if op >= 253:
                    break
                n = lens[op] or s.index(b"\0", at + 1) - at + 1
                if op == 0x2B and s[at + 1] >= 0xFB or op == 0x2B and 0 < s[at + 1] < 0x80:
                    text = ""
                    if s[at + 2] == 0xAD:
                        text = s[at + 3:s.index(b"\0", at + 3)].decode("latin1")
                    out.append("%02x %s" % (s[at + 1], text))
                if op == 4:
                    break
                at += n
    return out


def main():
    path = os.environ.get("GEVR_MARIO_ROM")
    if not path or not Path(path).is_file():
        print("SKIP: set GEVR_MARIO_ROM to GoldenEye with Mario Characters V3.17 .z64 to run its scripts' commands")
        return
    rom = Path(path).read_bytes()
    cmds = commands(rom)
    listing = Path(tempfile.mkdtemp(prefix="gevr-footsteps-")) / "commands.txt"
    listing.write_text("\n".join(cmds) + "\n", encoding="latin1")
    exe = rc.build("port/tests/footsteps_native.c", ("src/game/gevr_footsteps.c",))
    out = subprocess.run([str(exe), path, "4", str(listing)], capture_output=True, text=True, check=True).stdout
    tail = out[out.index("== footsteps"):].splitlines()[1:]
    fs = {}
    unmapped = []
    for line in tail:
        f = line.split()
        if f[0] == "fs":
            fs[f[1]] = f[2:]
        elif "is no memory the port has" in line:
            unmapped.append(line)
    bad = []

    def check(cond, what):
        if not cond:
            bad.append(what)

    check(fs.get("item2") == ["Gknife1Z"], "item 2 starts on its own file: %s" % fs.get("item2"))
    check(fs.get("item2poked") == ["Gknifffj"], "a rename reaches the item: %s" % fs.get("item2poked"))
    check(fs.get("item2back") == ["Gknife1Z"], "and back: %s" % fs.get("item2back"))
    check(fs.get("scale122") == ["0.53125"], "Tiny Mode's scale reaches the solo slot: %s" % fs.get("scale122"))
    check(fs.get("scale122back") == ["1.0000"], "and back: %s" % fs.get("scale122back"))
    check(fs.get("headpos") == ["0.0550"], "POV Height: %s" % fs.get("headpos"))
    check(fs.get("luigi") == ["7"] and fs.get("noluigi") == ["-1"],
          "a cheat's bit, copied and tested: %s %s" % (fs.get("luigi"), fs.get("noluigi")))
    check(fs.get("health") == ["1.000"] and fs.get("bondtype") == ["6"],
          "Bond's health and outfit: %s %s" % (fs.get("health"), fs.get("bondtype")))
    check(fs.get("armourlow") == ["5"], "a float compared by its bits: %s" % fs.get("armourlow"))
    check(fs.get("armoursum") == ["1.250"], "health added to armour: %s" % fs.get("armoursum"))
    check(fs.get("armourtick") == ["2.250"], "a float added a tick at a time: %s" % fs.get("armourtick"))
    check(fs.get("xtrack") == [str(0x31)], "the stage's X track: %s" % fs.get("xtrack"))
    check(fs.get("volume") == [str(0x7FFF)], "the music volume level start kept: %s" % fs.get("volume"))
    check(fs.get("objshown") == ["0", "4"], "objectives before the flag: %s" % fs.get("objshown"))
    check(fs.get("objflipped") == ["4", "0", "4"], "after AI 0x36: %s" % fs.get("objflipped"))
    check(fs.get("objstatus") == ["1"], "objective 2's status word: %s" % fs.get("objstatus"))
    check(fs.get("removed") == ["31", "killed", "1"], "items taken and Bond killed: %s" % fs.get("removed"))
    check(fs.get("cheats") == ["b002", "11", "b003", "11", "b005", "12", "b009", "12"],
          "its cheat rows (Play as Luigi, Peach, Mushroom Mode, Extra Weapons): %s" % fs.get("cheats"))
    check(fs.get("list0d") == ["1"], "global list 0x000D runs from the cartridge: %s" % fs.get("list0d"))
    check(fs.get("effects") == ["8", "9", "16", "-1", "-1", "6", "13"], "cheat effects: %s" % fs.get("effects"))
    check(fs.get("unlocks") == ["17", "17", "18", "19", "19", "14"], "cheat unlocks: %s" % fs.get("unlocks"))
    check(fs.get("fogfade") == ["0.0", "1", "120.0", "0"], "AI 0xFB's fog fade: %s" % fs.get("fogfade"))
    check(fs.get("ran") == [str(len(cmds))], "every command ran: %s of %d" % (fs.get("ran"), len(cmds)))
    check(len(cmds) > 12000, "its setups' commands: %d" % len(cmds))
    check(not unmapped, "addresses the port has no memory for:\n  " + "\n  ".join(unmapped[:20]))
    if bad:
        print("\n".join(bad))
        print("FAIL: Footsteps commands, %d mismatches" % len(bad))
        sys.exit(1)
    print("PASS: Footsteps commands on Mario Characters: renames, scales, POV height, cheat bits, Bond's fields, "
          "music, objectives, items, and all %d of its setups' commands on memory the port has" % len(cmds))


if __name__ == "__main__":
    main()
