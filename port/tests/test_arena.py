"""Run production all-map setup generation and native/floor navigation without a ROM."""
from pathlib import Path
import os
import shutil
import sys
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="gevr-arena-") as temp:
    exe = Path(temp) / "arena"
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions", "-ffunction-sections", "-fdata-sections",
            "-Wno-error=incompatible-pointer-types"]  # decomp's validation loop aliases its cursor
    args += ["-D" + flag for flag in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1",
                                     "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / p) for p in (".", "port/include", "include", "src", "src/game", "port/src", "port/src/net")]
    # GEVR_TEST_SANITIZE=1: AddressSanitizer and UndefinedBehaviorSanitizer (PR #224)
    if os.environ.get("GEVR_TEST_SANITIZE"):
        args += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    args += [str(ROOT / p) for p in ("port/tests/arena_native.c", "src/game/gevr_arena.c",
                                    "src/game/gevr_botnav.c", "src/game/padhalllv.c",
                                    "src/game/initpathtablelinks.c", "port/src/gevr_setup.c",
                                    "port/src/net/net_match.c")]
    result = subprocess.run(args + ["-Wl,--gc-sections", "-lm", "-o", str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    subprocess.run([str(exe)], check=True, timeout=30)
    # GEVR_DECOMP_ROOT=<n64decomp/007 checkout>: the seven "(Full)" maps' own
    # campaign setups through the converter and the arena (PR #224's check)
    reference = os.environ.get("GEVR_DECOMP_ROOT")
    if reference:
        sys.path.insert(0, str(ROOT / "port/tests"))
        from full_map_records import build_records
        campaigns, _ = build_records(reference, temp)
        levels = {"ark": 34, "sevb": 27, "arch": 24, "cave": 39, "cryp": 32, "statue": 22, "crad": 41}
        for campaign in campaigns:
            subprocess.run([str(exe), str(campaign), str(levels[campaign.stem])], check=True, timeout=60)
