"""Execute production campaign unlocks, ending transitions and boat transforms.

--baseline-ref verifies that each reported PR 172 regression fails on that ref.
Only save storage and unrelated menu/room services are stubbed.
"""
import argparse
from pathlib import Path
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


def run(directory, name, source, dependencies, baseline):
    cfile, exe = directory / (name + ".c"), directory / (name + ".exe")
    cfile.write_text(source, encoding="utf-8")
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions",
            "-Wno-builtin-declaration-mismatch"]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1",
                                "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / p) for p in ("port/include", "include", "src", ".")]
    compiled = subprocess.run(args + [str(cfile)] + dependencies + ["-lm", "-o", str(exe)],
                              capture_output=True, text=True)
    if compiled.returncode:
        raise RuntimeError(compiled.stderr)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    if baseline:
        assert result.returncode, name + ": baseline unexpectedly passed"
        print("CONFIRMED: " + name + " fails on " + baseline + ": " + result.stderr.strip())
    else:
        if result.returncode:
            raise RuntimeError(result.stderr)
        print(result.stdout.strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-ref")
    baseline = parser.parse_args().baseline_ref

    def read(path):
        if baseline:
            return subprocess.check_output(["git", "show", baseline + ":" + path],
                                           cwd=ROOT, text=True)
        return (ROOT / path).read_text(encoding="utf-8")

    files = read("src/game/file2.c")
    front = read("src/game/front.c")
    functions = "\n".join(block(files, signature) for signature in (
        "STAGESTATUS fileIsStageUnlockedAtDifficulty(",
        "void fileGetHighestStageDifficultyCompletedForFolder(",
        "LEVEL_SOLO_SEQUENCE fileGetHighestStageUnlockedForFolder(",
        "bool fileIsCradleCompletedForFolder(",
        "bool fileIsAztecCompletedOnSecretOr00ForFolder(",
        "bool fileIsEgyptCompletedOn00ForFolder(",
        "s32 fileIs007ModeUnlocked("))
    success = block(front, "if (frontCompleteAllObjectivesAliveSuccess() && (!g_AppendCheatSinglePlayer))")
    success = success[success.index("{"):]
    credits = block(front, "else if (full_actor_intro != 0)")
    credits = credits[credits.index("{"):]
    campaign = (ROOT / "port/tests/campaign_progression_native.c").read_text()
    campaign = campaign.replace("/* INSERT_FILE */", functions)
    campaign = campaign.replace("/* INSERT_COMPLETION */", success)
    campaign = campaign.replace("/* INSERT_CREDITS */", credits)
    # Baseline functions must retain their original retail constants. The current
    # headers supply declarations and types, but are not substituted into bodies.
    boat = (ROOT / "port/tests/speedboat_transform_native.c").read_text()
    transform = "\n".join(block(read("src/game/matrixmath.c"), signature) for signature in (
        "void matrix_4x4_set_position_and_rotation_around_y(",
        "void matrix_scalar_multiply("))
    boat = boat.replace("/* INSERT_TRANSFORM */", transform)
    boat = boat.replace("/* INSERT_PUSH */", block(read("src/game/gevr_dam_boat.c"),
                                                  "static void gevrDamBoatPush("))
    with tempfile.TemporaryDirectory(prefix="gevr-campaign-progression-") as temp:
        directory = Path(temp)
        layout = [str(ROOT / "port/src/gevr_romlayout.c")]
        for name in ("unlocks", "ending"):
            run(directory, name, campaign.replace("/* TEST_MODE */", name + "();"), layout, baseline)
        run(directory, "boat", boat, [str(ROOT / "port/src/gevr_speedboat.c")], baseline)


if __name__ == "__main__":
    main()
