"""Check the real door obstruction routine with living, dying and dead occupants."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
COLLISION = "src/game/propobj.c"


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def run(directory, collision, expect_pass):
    code = function(collision, "s32 sub_GAME_7F0448A8(")
    fixture = (ROOT / "port/tests/door_obstruction_native.c").read_text(encoding="utf-8")
    source = directory / "door_obstruction.c"
    exe = directory / "door_obstruction.exe"
    source.write_text(fixture.replace("/* INSERT_PRODUCTION_CODE */", code), encoding="utf-8")
    cc = os.environ.get("CC") or shutil.which("clang") or "gcc"
    build = subprocess.run([cc, "-std=c11", "-O2", "-fms-extensions", "-Wno-pointer-to-int-cast",
                            "-DGEVR=1", "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C=1", "-DVERSION=2",
                            "-DVERSION_US=1", "-DLANG_US=1",
                            *("-I" + str(ROOT / path) for path in
                              (".", "port/include", "include", "src", "src/game")),
                            str(source), "-o", str(exe)], text=True, capture_output=True)
    if build.returncode:
        raise RuntimeError(build.stderr)
    result = subprocess.run([str(exe)], text=True, capture_output=True)
    if expect_pass:
        assert result.returncode == 0, result.stderr
        print(result.stdout.strip())
    else:
        assert result.returncode != 0, "baseline unexpectedly passed"
        assert result.stderr.splitlines() == ["FAIL: dead guard blocks door and starts fading"],             result.stderr
        print("PASS: baseline never releases a kept body that blocks a door")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-ref", help="Verify that this Git revision fails the regression")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gevr-door-obstruction-") as tmp:
        directory = Path(tmp)
        run(directory, (ROOT / COLLISION).read_text(encoding="utf-8"), expect_pass=True)
        if args.baseline_ref:
            old = subprocess.check_output(["git", "show", f"{args.baseline_ref}:{COLLISION}"],
                                          cwd=ROOT, text=True, encoding="utf-8")
            run(directory, old, expect_pass=False)
