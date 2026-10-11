"""Package local Bean CAFF meshes and the standalone Bean PNG pack for VR.

Game files remain in the ignored output directory, never in Git source.
Usage: python tools/package_bean.py --files path/to/xbla/files --textures path/to/GoldenEye-007-Bean/GOLDENEYE --version YYYY.MM.DD.N --output build/bean
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--files", type=Path, required=True)
parser.add_argument("--textures", type=Path, required=True)
parser.add_argument("--version", required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
meshes = sorted(p for category in ("background", "char", "gun", "head", "prop", "skydome")
                for p in (args.files / "new" / category).glob("*/default.bin"))
fonts = [args.files / "misc" / name / "default.dt" for name in ("alps3", "doc0")]
# the HUD, menu and effect pictures gevr_bean_images.c repaints with
pictures = sorted(p for base in (args.files / "texture", args.files / "new" / "texture")
                  for p in base.rglob("default.rba"))
textures = sorted(args.textures.rglob("*.png"))
assert meshes and pictures and textures and (args.files / "new/char/boilerbond/default.bin") in meshes
for path in meshes + fonts + pictures:
    with path.open("rb") as file:
        assert file.read(4) == b"CAFF", path

png = args.output / f"ge007-bean-{args.version}-gliden64-png.zip"
vr = args.output / f"ge007-bean-{args.version}-vr.zip"
for archive, include_meshes in ((png, False), (vr, True)):
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in textures:
            z.write(path, "GOLDENEYE/" + path.relative_to(args.textures).as_posix())
        if include_meshes:
            for path in meshes + fonts + pictures:
                z.write(path, "files/" + path.relative_to(args.files).as_posix())
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        unpacked = sum(e.file_size for e in z.infolist())
    print(json.dumps({"path": str(archive.resolve()), "bytes": archive.stat().st_size,
                      "unpackedBytes": unpacked, "meshes": len(meshes) if include_meshes else 0,
                      "fonts": len(fonts) if include_meshes else 0,
                      "pictures": len(pictures) if include_meshes else 0, "textures": len(textures), "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}))
