"""Compile production co-op stage configuration and save queries for report a16b05b5."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def build(directory, files, core, name):
    menu = (ROOT / "port/src/net/net_coop_menu.c").read_text(encoding="utf-8")
    fixture = (ROOT / "port/tests/coop_debrief_native.c").read_text(encoding="utf-8")
    fixture = fixture.replace("/* INSERT_SAVE_FUNCTIONS */", "\n".join(function(files, signature) for signature in (
        "u32 fileGetSaveFolder(", "bool fileGetSaveFlagDoReset(",
        "save_data * fileGetSaveForFoldernum(", "s32 fileGetSaveStageDifficultyTime(")))
    fixture = fixture.replace("/* INSERT_FOLDER_PREPARATION */", function(menu, "void gevrCoopPrepareSaveFolder("))
    fixture = fixture.replace("/* INSERT_COOP_CONFIG */", function(core, "static void netApplyCoopConfig("))
    source = directory / (name + ".c")
    exe = directory / (name + ".exe")
    source.write_text(fixture, encoding="utf-8")
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions", "-Wno-builtin-declaration-mismatch"]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1", "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / path) for path in (".", "port/include", "include", "src", "src/game", "port/src")]
    args += [str(source), str(ROOT / "port/src/net/net_match.c"), "-o", str(exe)]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return exe


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify-baseline", action="store_true", help="prove the release's NULL-save call and folder setup fail")
    options = parser.parse_args()
    files = (ROOT / "src/game/file2.c").read_text(encoding="utf-8")
    core = (ROOT / "port/src/net/net_core.c").read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="gevr-coop-debrief-") as temp:
        directory = Path(temp)
        current = build(directory, files, core, "current")
        subprocess.run([str(current), "null-save"], check=True)
        subprocess.run([str(current)], check=True)
        if options.verify_baseline:
            old_files = subprocess.check_output(["git", "show", "9fb7be1:src/game/file2.c"], cwd=ROOT, text=True)
            original = build(directory, old_files, core, "release")
            result = subprocess.run([str(original), "null-save"], capture_output=True)
            assert result.returncode, "release unexpectedly passed the NULL-save regression"
            print("v0.4.6 NULL-save call fails as expected:", result.returncode)
            old_core = subprocess.check_output(["git", "show", "9fb7be1:port/src/net/net_core.c"], cwd=ROOT, text=True)
            original_config = build(directory, files, old_core, "release-config")
            result = subprocess.run([str(original_config)], capture_output=True)
            assert result.returncode, "release unexpectedly passed the missing-folder regression"
            assert b"selected_folder_num == FOLDER1" in result.stderr, result.stderr.decode(errors="replace")
            print("v0.4.6 folder setup fails as expected:", result.returncode)
