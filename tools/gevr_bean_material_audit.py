"""Census local Bean CAFF assets with the production reader/decoder/loader.

Run under WSL or with a C compiler available:
  python3 tools/gevr_bean_material_audit.py XBLA_FILES OUTPUT.json [--meshes]

Reports counts and metadata, never copies ROMs, textures or geometry. Flat
material counts include switch alternatives and are not counts of visible bugs.
"""
import argparse
from collections import Counter
import json
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def findings(rows):
    """Count concrete authored inputs separately from visibility candidates."""
    hd = [r for r in rows if r["source"].startswith("new/")]
    mapped = set(re.findall(r'\{ "[^"]+", \w+, "([^"]+)" \}',
                            (ROOT / "port/include/gevr_bean_files.h").read_text()))
    scenes = {"new/background/" + s for s in re.findall(r'\{LEVELID_\w+, "([^" ]+)"',
                                                      (ROOT / "port/src/gevr_bean_world.c").read_text())}
    registered = mapped | scenes
    overlays = {"_0x059B9F65.tga.bin", "_0x00B5FD45.bin", "_0x00B5FD45.bmp.bin",
                "_0x03B30B95.tga.bin", "_0x018952E5.tga.bin", "_0x0F0BCEB5.tga.bin"}
    spheres = {"_0x0E1C2BF5.tga.bin", "_0x081346B5.tga.bin", "_0x0B9D6195.tga.bin",
               "_0x0C2BCE25.tga.bin", "_0x018248E5.tga.bin"}
    reflection_rows = []
    selection_differences = []
    for row in hd:
        for mat in row["materials"]:
            valid = {t for t, _, _ in mat["inputs"] if t < len(row["textures"])}
            if len(valid) > 1 and any(row["textures"][t]["name"] in overlays | spheres for t in valid):
                reflection_rows.append({"source": row["source"], "material": mat["offset"]})
    # DAB beanMaterialTexture (pinned adef1dc2; MIT license in port/licenses):
    # prefer non-overlay inputs, then the last largest picture; the special
    # four-input original-gun binding takes its fourth input. Invalid DAB
    # answers are recorded separately, not claimed as texture-selection bugs.
    for row in rows:
        textures = row["textures"]
        for mat in row["materials"]:
            chosen, area, found = 0, 0, False
            for t, _, _ in mat["inputs"]:
                name = textures[t]["name"] if t < len(textures) else ""
                current = textures[chosen]["name"] if chosen < len(textures) else ""
                size = textures[t]["headerSize"][0] * textures[t]["headerSize"][1] if t < len(textures) else 0
                if found and name in overlays:
                    continue
                if found and current in overlays:
                    chosen, area = t, size
                    continue
                if found and name in spheres:
                    continue
                if found and current in spheres:
                    chosen, area = t, size
                    continue
                if not found or size >= area:
                    chosen, area, found = t, size, True
            inputs = mat["inputs"]
            shape = len(inputs) == 4 and all(slot == k and
                    (sampler == [0, 0, 1][k] if k < 3 else sampler >= 2)
                    for k, (_, slot, sampler) in enumerate(inputs))
            if shape and inputs[3][0] < len(textures):
                chosen = inputs[3][0]
            if chosen != mat.get("genericSelected", mat["selected"]):
                selection_differences.append({"source": row["source"], "material": mat["offset"],
                                              "dab": chosen, "port": mat.get("genericSelected", mat["selected"]),
                                              "dabIndexValid": chosen < len(textures)})
    rotation = [r["source"] for r in hd if any(m["rotation"] for m in r["motion"])]
    # Four records belong to flash cards that triWanted deliberately leaves to
    # native gun flashes. Do not count them as four proven broken game objects.
    rotation_props = [s for s in rotation if s in registered and not s.startswith("new/gun/")
                      and s not in {"new/prop/chrfnp90", "new/prop/chrm16"}]
    tinted = [r["source"] for r in hd if r["source"] in mapped and
              r["source"].split("/")[1] in {"char", "head"} and r.get("tintedFaces", 0)]
    arrays = [{"source": r["source"], "texture": i, "frames": t["frames"],
               "emittedFaces": r.get("textureFaces", [])[i]}
              for r in hd for i, t in enumerate(r["textures"])
              if t["type"] in (4, 5) and t["frames"] > 1]
    overlaps = [{"source": r["source"], "groups": r["coincidentNonDecalGroups"],
                 "faces": r["coincidentNonDecalFaces"], "oppositeWindingGroups": r["oppositeWindingGroups"]}
                for r in hd if r.get("coincidentNonDecalGroups", 0)]
    return {
        "hdAssets": len(hd), "hdTextureRecords": sum(len(r["textures"]) for r in hd),
        "hdMaterialRecords": sum(len(r["materials"]) for r in hd),
        "textureDecodeFailures": [{"source": r["source"], "texture": i}
                                  for r in rows for i, t in enumerate(r["textures"]) if not t["decoded"]],
        "emptyMeshNativeScreenFallbacks": [r["source"] for r in rows if r.get("meshLoaded") is False],
        "rotationAuthoredInHdAssets": rotation,
        "rotationMissingOnRegisteredHdProps": rotation_props,
        "additionalReflectionOrScratchInputs": reflection_rows,
        "registeredReflectionOrScratchAssets": sorted({r["source"] for r in reflection_rows if r["source"] in registered}),
        "genericSelectorDifferences": selection_differences,
        "mappedLitModelColourCandidates": tinted,
        "frameTextures": arrays,
        "coincidentGeometryCandidates": overlaps,
        "countCautions": ["Material records include switch alternatives.",
                          "Model colour candidates may include discarded pieces; they are not confirmed visual defects per model.",
                          "Coincident faces can be intentional; inspect coverage and headset depth behavior before changing them.",
                          "Array water bumps are unused by the single-input renderer; helicopter frames do not run in the authored metadata."]
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("files", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--meshes", action="store_true")
    args = p.parse_args()
    files = sorted(args.files.rglob("default.bin"))
    rows, failures = [], []
    with tempfile.TemporaryDirectory(prefix="bean-census-") as temp:
        exe = Path(temp) / "audit"
        subprocess.run([os.environ.get("CC") or shutil.which("gcc") or "cc",
                        "-std=c11", "-O2", "-include", "stdarg.h",
                        "-I" + str(ROOT / "port/include"),
                        str(ROOT / "port/tests/gevr_bean_material_audit.c"),
                        str(ROOT / "port/src/gevr_bean_logic.c"),
                        str(ROOT / "port/src/gevr_bean_tex.c"), "-lm", "-o", str(exe)], check=True)
        for i, path in enumerate(files):
            command = [str(exe), str(path)] + (["mesh"] if args.meshes else [])
            run = subprocess.run(command, capture_output=True, text=True)
            source = path.parent.relative_to(args.files).as_posix()
            if run.returncode:
                failures.append({"source": source, "code": run.returncode, "stderr": run.stderr[-1000:]})
            else:
                row = json.loads(run.stdout)
                row["source"] = source
                row["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
                rows.append(row)
            if (i + 1) % 50 == 0:
                print(f"Read {i+1}/{len(files)} files; {len(failures)} failures", flush=True)
    report = {"assetCount": len(rows), "groups": dict(Counter("/".join(r["source"].split("/")[:2]) for r in rows)),
              "textureHeaders": sum(len(r["textures"]) for r in rows),
              "materialRecords": sum(len(r["materials"]) for r in rows),
              "motionEntries": sum(len(r["motion"]) for r in rows),
              "meshSweep": args.meshes, "failures": failures, "assets": rows,
              "findings": findings(rows) if args.meshes else {}}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(json.dumps({k: v for k, v in report.items() if k not in {"assets", "findings"}}, indent=2))
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
