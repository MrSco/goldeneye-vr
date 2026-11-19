"""The race's ghost keeps GoldenEye's active prop list whole (#263).

Builds ghost_prop_list_native.c around the production ghostPose (gevr_ghost.c)
and the production list functions and propsTick (chrprop.c), then runs race
frames with a dropped gun falling: it must be ticked every frame, as outside
a race. Run under WSL: python3 port/tests/test_ghost_prop_list.py

--verify-baseline also builds v0.5.20's gevr_ghost.c (80792b12, or the file
given, for a worktree WSL's git can't read) and checks that it fails: there
the falling gun dropped out of the list.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASELINE = "80792b12"


def function(text, signature):
    """The function that starts with signature, through its closing brace."""
    start = text.index(signature)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[start:end]


def block(text, first, last):
    start = text.index(first)
    return text[start:text.index(last, start) + len(last)]


def production(ghost):
    prop = (ROOT / "src/game/chrprop.c").read_text(encoding="utf-8")
    parts = [line for line in ghost.splitlines() if re.match(r"#define (GSF_|GHOST_)", line)]
    parts.append(block(ghost, "struct gevrghostsample {", "};"))
    parts.append(block(ghost, "static struct {", "} s_race;"))
    parts.append(block(ghost, "static struct player s_ghostPlayer", ";"))
    parts += [function(ghost, s) for s in (
        "static f32 ghostUnRadians(", "static void ghostSetFlags(", "static StandTile *ghostTile(",
        "static void ghostSetHand(", "static void ghostPose(")]
    parts += [function(prop, s) for s in (
        "PropRecord *chrpropGetActiveTail(void)", "void chrpropActivate(", "void chrpropActivateThisFrame(",
        "void chrpropDelist(", "void propsTick(void)")]
    return "\n\n".join(parts)


def run(ghost, directory, name):
    fixture = (ROOT / "port/tests/ghost_prop_list_native.c").read_text(encoding="utf-8")
    source, exe = directory / (name + ".c"), directory / name
    source.write_text(fixture.replace("/* INSERT_PRODUCTION_FUNCTIONS */", production(ghost)), encoding="utf-8")
    result = subprocess.run([
        shutil.which("gcc") or "cc", "-std=c11", "-O1", "-g",
        "-U_FORTIFY_SOURCE",  # the repo's include/limits.h trips glibc's fortified MB_LEN_MAX check
        "-include", "stdarg.h", "-include", "stddef.h", "-include", "limits.h",
        "-fms-extensions", "-fno-strict-aliasing", "-Wno-incompatible-pointer-types",
        "-Wno-implicit-function-declaration", "-Wno-int-conversion",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
        *["-I" + str(ROOT / p) for p in (".", "include", "src", "src/game", "port/include")],
        str(source), "-lm", "-o", str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-baseline", nargs="?", const="", metavar="GEVR_GHOST_C")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gevr-ghost-props-") as directory:
        directory = Path(directory)
        result = run((ROOT / "src/game/gevr_ghost.c").read_text(encoding="utf-8"), directory, "current")
        print(result.stdout, end="")
        if result.returncode:
            raise SystemExit("FAIL: " + result.stderr)
        if args.verify_baseline is not None:
            if args.verify_baseline:
                old = Path(args.verify_baseline).read_text(encoding="utf-8")
            else:
                old = subprocess.check_output(["git", "show", BASELINE + ":src/game/gevr_ghost.c"], cwd=ROOT, text=True)
            result = run(old, directory, "baseline")
            if result.returncode == 0 or "runtime error:" in result.stderr or "AddressSanitizer" in result.stderr:
                raise SystemExit("FAIL: v0.5.20's ghost did not lose the gun: " + result.stdout + result.stderr)
            print("v0.5.20's ghost reproduced the bug: " + result.stderr.strip())
    print("OK")


if __name__ == "__main__":
    main()
