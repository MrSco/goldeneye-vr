"""Compile production avatar math/protocol with sanitizers; no ROM or headset."""
from pathlib import Path
import subprocess
import tempfile
import re

ROOT = Path(__file__).resolve().parents[2]

def function(path, name):
    source = (ROOT / path).read_text()
    match = re.search(r"^[\w *]+\b" + re.escape(name) + r"\([^;{]*\)\s*\{", source, re.M)
    start = match.start()
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="gevr-avatar-") as temp:
        binary = Path(temp) / "avatar"
        flags = ["-std=c11", "-D_XOPEN_SOURCE=700", "-fms-extensions", "-include", "stddef.h",
                 "-Wno-builtin-declaration-mismatch", "-g", "-O1",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        flags += ["-D" + d for d in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1", "VERSION_US=1")]
        flags += ["-I" + str(ROOT / p) for p in (".", "port/include", "include", "src", "src/game", "port/src", "port/src/net")]
        sources = [ROOT / p for p in ("port/tests/avatar_math_native.c", "port/src/gevr_avatar_math.c", "port/src/net/netbuf.c")]
        subprocess.run(["gcc", *flags, *map(str, sources), "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        maths = ["matrix_4x4_set_identity", "matrix_4x4_multiply", "matrix_4x4_multiply_homogeneous",
                 "matrix_4x4_set_position", "matrix_4x4_set_rotation_around_xyz", "matrix_4x4_get_rotation_around_xyz",
                 "matrix_4x4_set_position_and_rotation_around_xyz", "matrix_4x4_set_position_and_rotation_around_y",
                 "matrix_4x4_set_rotation_around_y", "matrix_column_3_scalar_multiply_2", "matrix_scalar_multiply_2"]
        quat = ["quaternion_set_rotation_around_xyzf", "quaternion_to_matrix", "quaternion_to_transform_matrix",
                "quaternion_7F05BC68", "quaternion_ensure_shortest_path", "quaternion_slerp"]
        builders = "#define EPSILON 0.00001001f\n#define SQR(x) ((x)*(x))\n"
        builders += "typedef struct ModelGroupMtxBuildArg {u16 flags,pad;ModelRoData_GroupRecord *group;ModelNode *parentnode;} ModelGroupMtxBuildArg;\n"
        builders += "\n".join(function("src/game/matrixmath.c", name) for name in maths)
        builders += "\n" + "\n".join(function("src/game/quaternion.c", name) for name in quat)
        builders += "\n" + function("src/game/math_unk_05A9E0.c", "modelGetBendStretchScale")
        builders += "\n" + "\n".join(function("src/game/model.c", name) for name in
                                     ("sub_GAME_7F06D0CC", "process_01_group_heading", "modelBuildGroupMatrices", "sub_GAME_7F06DB5C", "process_02_position"))
        fixture = Path(temp) / "avatar_rig.c"
        fixture.write_text((ROOT / "port/tests/avatar_rig_native.c").read_text().replace("/* PRODUCTION_BUILDERS */", builders))
        binary = Path(temp) / "avatar_rig"
        sources = [fixture, ROOT / "port/src/gevr_avatar.c", ROOT / "port/src/gevr_avatar_math.c"]
        subprocess.run(["gcc", *flags, "-Wno-incompatible-pointer-types", *map(str, sources), "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
