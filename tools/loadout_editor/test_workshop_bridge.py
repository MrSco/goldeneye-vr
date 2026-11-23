"""Smoke-test the native Workshop bridge with bundled catalogs in a scratch directory."""

import argparse
import ctypes
import os
from pathlib import Path
import shutil
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build", type=Path, help="Directory containing the built editor and native DLL.")
parser.add_argument("data", type=Path, help="Tactical asset root containing mods/tactical/gear.")
args = parser.parse_args()
build = args.build.resolve()
assert (build / "GoldenEye Tactical Loadout Editor.exe").is_file()
catalogs = args.data.resolve() / "mods/tactical/gear"
native = ctypes.CDLL(str(build / "TacticalWorkshopNative.dll"))
native.EditorGunName.argtypes = (ctypes.c_int,)
native.EditorGunName.restype = ctypes.c_char_p
native.EditorMountCount.argtypes = (ctypes.c_int,)
native.EditorPresetCount.argtypes = (ctypes.c_int,)

cwd = Path.cwd()
with tempfile.TemporaryDirectory(prefix="tactical-editor-") as scratch:
    target = Path(scratch) / "mods/tactical/gear"
    target.mkdir(parents=True)
    for name in ("catalog.txt", "stats.txt", "presets.txt"):
        shutil.copy2(catalogs / name, target / name)
    os.chdir(scratch)
    try:
        keys = [key for key in range(native.EditorGunCount()) if native.EditorGunName(key)]
        assert keys and any(native.EditorMountCount(key) > 1 for key in keys)
        preset_counts = sum(native.EditorPresetCount(key) for key in keys)
        assert preset_counts > 0
        print(f"PASS: native Workshop bridge listed {len(keys)} weapons and {preset_counts} presets")
    finally:
        os.chdir(cwd)
