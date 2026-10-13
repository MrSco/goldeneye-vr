"""A guard's (or another player's) first-person gun flash compares depth.

Runs gevr_heldgun.c's flash patch on a synthetic model tree. With a ROM given
(argv[1] or GEVR_ROM), also checks that every first-person gun's flash really
is switch 1 lists setting a render mode without the depth compare, which is
what the patch looks for.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
held = (ROOT / "src/game/gevr_heldgun.c").read_text(encoding="utf-8")
fixture = (ROOT / "port/tests/heldgun_flash_native.c").read_text(encoding="utf-8")
fixture = fixture.replace("/* INSERT_HELDGUN */", "\n".join(block(held, signature) for signature in (
    "static void gevrHeldGunCompareDepth(", "static void gevrHeldGunFlashDepth(")))

with tempfile.TemporaryDirectory(prefix="gevr-heldgun-flash-") as temp:
    source, exe = Path(temp) / "test.c", Path(temp) / "test"
    source.write_text(fixture, encoding="utf-8")
    subprocess.run([shutil.which("gcc") or "cc", "-std=c11", "-O1", "-g", "-fms-extensions",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
                    "-I" + str(ROOT), "-I" + str(ROOT / "include"),
                    "-I" + str(ROOT / "src"), "-I" + str(ROOT / "port/include"),
                    str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

rom = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("GEVR_ROM")
if not rom:
    print("SKIP: the ROM check (pass the USA ROM or set GEVR_ROM)")
    sys.exit(0)

sys.path.insert(0, str(ROOT / "tools"))
import gevr_model_probe as probe  # noqa: E402

GUNS = ["wppk", "tt33", "skorpion", "ak47", "uzi", "mp5k", "spectre", "m16", "fnp90", "shotgun",
        "autoshot", "sniperrifle", "ruger", "goldengun"]
data_rom = probe.load_rom(rom)
bad, checked = [], 0
for gun in GUNS:
    off, size = probe.manifest_lookup("G%sZ" % gun)
    d = probe.decompress_1172(data_rom[off:off + size])
    sw = probe.ofs(probe.be32(d, 4))
    if sw is None:   # the sniper rifle shows no flash
        continue
    checked += 1
    found = []
    todo, seen = [probe.ofs(probe.be32(d, sw + 20))], set()
    while todo:
        n = todo.pop()
        if n is None or n in seen:
            continue
        seen.add(n)
        todo += [probe.ofs(probe.be32(d, n + 20)), probe.ofs(probe.be32(d, n + 12))]
        op, rod = probe.be16(d, n) & 0xFF, probe.ofs(probe.be32(d, n + 4))
        lists = [] if rod is None else [probe.ofs(probe.be32(d, rod)), probe.ofs(probe.be32(d, rod + 4))] if op == 4 \
            else [probe.ofs(probe.be32(d, rod + 8))] if op == 22 else []
        for o in lists:
            while o is not None and o + 8 <= len(d) and d[o] != 0xB8:
                if d[o] == 0xB9 and d[o + 2] == 3:
                    found.append(probe.be32(d, o + 4))
                o += 8
    if not found or any(m & 0x10 for m in found):
        bad.append("G%sZ: switch 1 render modes %s" % (gun, [hex(m) for m in found]))
if bad:
    sys.exit("FAIL: " + "; ".join(bad))
print("PASS: %d first-person guns' flashes are switch 1 lists setting a mode without the depth compare" % checked)
