"""Build the real Jolt backend and adapter against synthetic game models.

Requires CMake and a C/C++ compiler. Pass --jolt-source for an existing v5.6.0
checkout; otherwise CMake fetches the pinned upstream source normally.
"""
from pathlib import Path
import argparse
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    start = re.search(r"^[A-Za-z_][\w *]*\b" + re.escape(name)
                      + r"\([^;{]*\)\s*\{", source, re.M).start()
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--jolt-source", type=Path)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-ragdoll-tests")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    model = (ROOT / "src/game/model.c").read_text()
    fixture = (ROOT / "port/tests/ragdoll_native.c").read_text()
    lookups = "\n".join(function(model, name) for name in
                         ("modelGetNodeRwData", "modelApplyHeadRelations", "modelFindNodeMtxIndex"))
    maths = (ROOT / "src/game/matrixmath.c").read_text()
    lookups += "\n" + "\n".join(function(maths, name) for name in
        ("matrix_4x4_copy", "matrix_4x4_multiply", "matrix_4x4_multiply_in_place",
         "matrix_4x4_set_identity", "matrix_4x4_set_position", "matrix_4x4_set_identity_and_position",
         "matrix_4x4_set_rotation_around_y", "matrix_4x4_set_rotation_around_z",
         "matrix_column_3_scalar_multiply_2"))
    props = (ROOT / "src/game/propobj.c").read_text()
    lookups += "\n" + "\n".join(function(props, name) for name in
        ("chrobjGetBboxFromObjFile", "chrobjGetBboxFromObjectRecord",
         "sub_GAME_7F04F244", "door7F0526EC"))
    test_source = build / "ragdoll_native.c"
    test_source.write_text(fixture.replace("/* PRODUCTION_LOOKUPS */", lookups))
    configure = ["cmake", "-S", str(ROOT / "port/tests/ragdoll"), "-B", str(build),
                 "-DCMAKE_BUILD_TYPE=Release", "-DGEVR_TEST_SOURCE=" + str(test_source)]
    if args.jolt_source:
        configure.append("-DFETCHCONTENT_SOURCE_DIR_JOLTPHYSICS=" + str(args.jolt_source.resolve()))
    subprocess.run(configure, check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "4"], check=True)
    subprocess.run([str(build / "ragdoll_native")], check=True)
