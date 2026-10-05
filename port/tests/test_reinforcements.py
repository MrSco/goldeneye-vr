"""Run production corpse ticks, character lookups and reinforcement AI opcodes."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def block(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def build(directory, sources, name, pal=False):
    action, chr_source, ai = sources
    start = action.index("#ifdef GEVR\n/*\n * Bodies stay")
    end = action.index("\n#endif", start) + len("\n#endif")
    fixture = (ROOT / "port/tests/reinforcements_native.c").read_text(encoding="utf-8")
    parts = {
        "BODIES": action[start:end] + "\n" + block(action, "void chrlvTickDead("),
        "LOOKUPS": "\n".join((block(chr_source, "ChrRecord* chrFindByLiteralId("),
                               block(action, "s32 chrResolveId("),
                               block(action, "ChrRecord *chrFindById("))),
        "AI_EXISTENCE": block(ai, "case AI_IFChrDoesNotExist:\n                {"),
        "AI_CLONE": block(ai, "case AI_TRYCloningChr:\n                {"),
    }
    for marker, code in parts.items():
        fixture = fixture.replace("/* INSERT_" + marker + " */", code)
    source, exe = directory / (name + ".c"), directory / (name + ".exe")
    source.write_text(fixture, encoding="utf-8")
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions",
            "-Wno-builtin-declaration-mismatch"]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1",
                                "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    if pal:
        args.append("-DREFRESH_PAL=1")
    args += ["-I" + str(ROOT / path) for path in
             (".", "port/include", "include", "src", "src/game", "port/src")]
    result = subprocess.run(args + [str(source), "-o", str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return exe


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify-baseline", action="store_true")
    options = parser.parse_args()
    paths = ("src/game/chraction.c", "src/game/chr.c", "src/game/chrai.c")
    sources = [(ROOT / path).read_text(encoding="utf-8") for path in paths]
    with tempfile.TemporaryDirectory(prefix="gevr-reinforcements-") as temp:
        directory = Path(temp)
        for pal in (False, True):
            exe = build(directory, sources, "pal" if pal else "ntsc", pal)
            subprocess.run([str(exe)], check=True)
        if options.verify_baseline:
            old = [subprocess.check_output(["git", "show", "3ecab3b:" + path], cwd=ROOT, text=True)
                   for path in paths]
            exe = build(directory, old, "baseline")
            for mode in ("timing", "tracking"):
                result = subprocess.run([str(exe), mode], capture_output=True, text=True)
                assert result.returncode, "baseline unexpectedly passed " + mode
                assert "REGRESSION:" in result.stderr, result.stderr
                print("PASS: pre-fix source reproduces " + mode + " regression")
