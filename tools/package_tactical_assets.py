"""Build and verify the data-only Tactical candidate for the upstream Mods page."""

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import tempfile
import zipfile

MARKER = "tactical.manifest"
INVENTORY = "files.sha256"
REQUIRED = ("mods/tactical/gear/catalog.txt", "mods/tactical/gear/presets.txt",
            "mods/tactical/gear/stats.txt", "tactical-editor-catalogue.tsv")
RUNTIME_SUFFIXES = {".bin", ".opt", ".png", ".txt", ".wav"}
MAX_ENTRY = 64 * 1024 * 1024
MAX_PACK = 4 * 1024 * 1024 * 1024


def inputs(source):
    files = [source / REQUIRED[-1]]
    for directory, suffixes in (("mods/tactical", RUNTIME_SUFFIXES), ("loadout-models", {".bin"})):
        for path in (source / directory).rglob("*"):
            if not path.is_file():
                continue
            if path.suffix not in suffixes or path.is_symlink():
                raise ValueError(f"Unexpected asset file {path}")
            files.append(path)
    members = sorted((path.relative_to(source).as_posix(), path) for path in files)
    names = [name for name, _ in members]
    if not set(REQUIRED).issubset(names):
        raise ValueError("Missing Tactical catalog, stats, presets, or editor data.")
    if len({name.casefold() for name in names}) != len(names):
        raise ValueError("Case-insensitive duplicate asset paths.")
    sizes = [path.stat().st_size for _, path in members]
    if any(size < 4 or size > MAX_ENTRY for size in sizes) or sum(sizes) > MAX_PACK:
        raise ValueError("Asset sizes exceed the current Mods archive limits.")
    return members


def zip_entry(name):
    entry = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
    entry.compress_type = zipfile.ZIP_DEFLATED
    return entry


def verify(path, expected_version=None):
    with zipfile.ZipFile(path) as archive:
        inventory = archive.read(INVENTORY)
        expected = {}
        for row in inventory.decode("utf-8").splitlines():
            digest, name = row.split("  ", 1)
            if (name.casefold() in expected or not re.fullmatch(r"[0-9a-f]{64}", digest)
                    or "\\" in name or ":" in name or any(part in ("", ".", "..") for part in name.split("/"))):
                raise ValueError(f"Invalid asset ledger entry {name!r}")
            expected[name.casefold()] = digest
        if not set(REQUIRED).issubset(expected):
            raise ValueError("Missing required asset entries.")
        members = [name for name in archive.namelist() if name not in (MARKER, INVENTORY)]
        if len(members) != len(expected) or {name.casefold() for name in members} != expected.keys():
            raise ValueError("Asset inventory does not match the archive.")
        header, version, content = archive.read(MARKER).decode("ascii").splitlines()
        if (header != "GEVRTACTICAL1" or content != hashlib.sha256(inventory).hexdigest()
                or (expected_version is not None and version != expected_version)):
            raise ValueError("Invalid Tactical pack manifest.")
        for name in members:
            with archive.open(name) as entry:
                if hashlib.file_digest(entry, "sha256").hexdigest() != expected[name.casefold()]:
                    raise ValueError(f"Checksum mismatch for {name}")
        return len(members), content


def build(source, output, version):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.-]{0,31}", version):
        raise ValueError("Expected a version with at most 32 letters, digits, dots and dashes.")
    members = inputs(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="asset-build-", dir=output.parent) as temporary:
        path = Path(temporary) / output.name
        index = []
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name, asset in members:
                with asset.open("rb") as data:
                    digest = hashlib.file_digest(data, "sha256").hexdigest()
                    data.seek(0)
                    with archive.open(zip_entry(name), "w") as entry:
                        shutil.copyfileobj(data, entry, length=1024 * 1024)
                index.append(f"{digest}  {name}\n")
            inventory = "".join(index).encode("utf-8")
            content_id = hashlib.sha256(inventory).hexdigest()
            archive.writestr(zip_entry(INVENTORY), inventory)
            archive.writestr(zip_entry(MARKER), f"GEVRTACTICAL1\n{version}\n{content_id}\n")
        count, content = verify(path, version)
        path.replace(output)
    with output.open("rb") as data:
        checksum = hashlib.file_digest(data, "sha256").hexdigest()
    output.with_suffix(".zip.sha256").write_text(f"{checksum}  {output.name}\n", encoding="ascii")
    print(f"{output.name}: {count} assets, {output.stat().st_size:,} bytes")
    print(f"Content ID {content}")
    print(f"Archive SHA-256 {checksum}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="An existing Tactical install containing the runtime and editor data.")
    parser.add_argument("output", type=Path, help="Destination ZIP file.")
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    build(args.source, args.output, args.version)
