"""A guard's (or another player's) first-person gun flash compares depth.

Runs gevr_heldgun.c's flash patch on a synthetic model file. With the USA ROM
(argv[1] or GEVR_GE_ROM), also on each first-person gun as the game loads it:
converted by the production converter, its lists left segmented.
"""
from pathlib import Path
import os
import re
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

GUNS = ["wppk", "tt33", "skorpion", "ak47", "uzi", "mp5k", "spectre", "m16", "fnp90", "shotgun",
        "autoshot", "sniperrifle", "ruger", "goldengun"]

rom = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("GEVR_GE_ROM")

with tempfile.TemporaryDirectory(prefix="gevr-heldgun-flash-") as temp:
    source, exe = Path(temp) / "test.c", Path(temp) / "test"
    source.write_text(fixture, encoding="utf-8")
    subprocess.run([os.environ.get("CC") or shutil.which("gcc") or "cc", "-std=c11", "-O1", "-g",
                    "-fms-extensions", "-include", "stdarg.h",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
                    *["-I" + str(ROOT / p) for p in (".", "include", "src", "src/game", "port/include")],
                    str(source), str(ROOT / "port/tests/gevr_bean_native_model.c"), "-lm", "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)

    if not rom:
        print("SKIP: the cartridge guns (pass the USA ROM or set GEVR_GE_ROM)")
        sys.exit(0)

    sys.path.insert(0, str(ROOT / "tools"))
    import gevr_model_probe as probe  # noqa: E402

    data = probe.load_rom(rom)
    checked = 0
    for gun in GUNS:
        name = "G%sZ" % gun
        off, size = probe.manifest_lookup(name)
        raw = probe.decompress_1172(data[off:off + size])
        if probe.be32(raw, 4) == 0:   # the sniper rifle shows no flash
            continue
        header = (ROOT / "assets/obseg/gun" / gun / "ModelFileHeader.inc.c").read_text(encoding="utf-8")
        args = re.sub(r"&SKELETON\([^)]*\)", "S", re.search(r"MODELFILEHEADER\((.*)\)", header).group(1))
        args = [a.strip() for a in args.split(",")]
        switches, matrices, textures = int(args[4], 0), int(args[5], 0), int(args[8], 0)
        path = Path(temp) / (name + ".bin")
        path.write_bytes(raw)
        subprocess.run([str(exe), str(path), name, str(switches), str(textures), str(matrices)], check=True)
        checked += 1
    print("PASS: %d cartridge guns' flashes compare depth as held, the rest of each gun untouched" % checked)
