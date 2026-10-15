"""Run production wounds and model lookups under ASan/UBSan (53f092b3/71b515e8).

--verify-baseline requires the original code to fault at 0x05007830 and 0x8.
"""
from pathlib import Path
import shutil
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    start = re.search(r"^[A-Za-z_][\w *]*\b" + re.escape(name) + r"\(", source, re.M).start()
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


with tempfile.TemporaryDirectory(prefix="gevr-wounds-") as directory:
    temp = Path(directory)
    for baseline in ([False, True] if "--verify-baseline" in sys.argv else [False]):
        def read(path):
            if baseline:
                return subprocess.check_output(
                    ["git", "show", "f1dae9c:" + path], cwd=ROOT, text=True)
            return (ROOT / path).read_text()

        model = read("src/game/model.c")
        fixture = (ROOT / "port/tests/bean_wounds_native.c").read_text()
        lookup = "\n".join(function(model, name) for name in
                           ("modelGetNodeRwData", "modelApplyHeadRelations", "modelFindNodeMtxIndex"))
        fixture = fixture.replace("/* PRODUCTION MODEL LOOKUP */", lookup)
        fixture = fixture.replace("/* PRODUCTION MODEL INIT */", function(model, "modelInit"))
        (temp / "wounds.inc.h").write_text(read("port/src/gevr_bean_wounds.inc.h"))
        (temp / "test.c").write_text(fixture)
        exe = temp / "test"
        subprocess.run([
            shutil.which("gcc") or "cc", "-std=c11", "-O1", "-g", "-fms-extensions",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-Wno-incompatible-pointer-types",
            "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
            *(["-DBASELINE"] if baseline else []),
            *["-I" + str(ROOT / path) for path in
              (".", "include", "src", "src/game", "port/include")],
            str(temp / "test.c"), "-o", str(exe),
        ], check=True)
        for case, address in [("segmented-head", 0x05007830), ("null-head", 0x8)]:
            result = subprocess.run([str(exe), case], capture_output=True, text=True)
            if baseline:
                assert result.returncode != 0, "baseline unexpectedly passed"
                assert f"{address:012x}" in result.stderr, result.stderr
                assert "beanWoundsInAnchor" in result.stderr, result.stderr
                print(f"Baseline reproduced SIGSEGV at {address:#x} in beanWoundsInAnchor")
            else:
                if result.returncode:
                    print(result.stderr, file=sys.stderr)
                    raise SystemExit(result.returncode)
                assert "runtime error:" not in result.stderr, result.stderr
                print(result.stdout, end="")
