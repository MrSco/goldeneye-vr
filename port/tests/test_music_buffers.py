"""Crash 0a705c81: synthetic sequences through the production music players.

No ROM is required. Actual AL heap allocation and bounded DEFLATE are used;
only the parser/audio output is stubbed by music_buffers_native.c.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-baseline", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gevr-music-buffers-") as temp:
        directory = Path(temp)
        sizes = (128, 1999, 2000, 2001, 3999, 4000, 4001, 6344, 6345,
                 12000, 32769, 65535)
        bank = bytearray()
        metadata = [str(len(sizes))]
        for i, size in enumerate(sizes):
            # A stored block is larger than its output and exercises the old
            # staging copy extending beyond the uncompressed data.
            codec = zlib.compressobj(0 if i == 9 else 6, zlib.DEFLATED, -15)
            packed = b"\x11\x72" + codec.compress(bytes([i]) * size) + codec.flush()
            bank.extend(packed)
            metadata.append(f"{size} {len(packed)}")
        (directory / "bank.bin").write_bytes(bank)
        (directory / "sizes.txt").write_text("\n".join(metadata))
        exe = directory / "music-buffers"
        flags = ["-DGEVR=1", "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C=1",
                 "-DVERSION=2", "-DVERSION_US=1", "-DLANG_US=1",
                 "-DREFRESH_NTSC=1", "-fms-extensions", "-fno-strict-aliasing",
                 "-Wno-builtin-declaration-mismatch", "-Wno-pointer-to-int-cast",
                 "-ffunction-sections", "-fdata-sections"]
        includes = [".", "include", "include/PR", "port/include", "src",
                    "src/game", "src/libultra/audio"]
        compiler = shutil.which("gcc") or "gcc"
        for name in ("gevr_romcheck", "gevr_rom_manifest"):
            subprocess.run([compiler, "-std=c11", "-O1", "-ffunction-sections",
                            "-fdata-sections", "-I" + str(ROOT / "port/include"),
                            "-c", str(ROOT / f"port/src/{name}.c"),
                            "-o", str(directory / f"{name}.o")], check=True)
        fixture = ROOT / "port/tests/music_buffers_native.c"
        if args.verify_baseline:
            baseline = subprocess.check_output(
                ["git", "show", "bdeb44dc:src/music.c"], cwd=ROOT, text=True)
            start = baseline.index("    size = TRACK_1_DATA_SEQ_SIZE_BYTES;")
            end = baseline.index("    synconfig.maxVVoices", start)
            allocation = baseline[start:end]
            baseline_path = directory / "music-baseline.c"
            baseline_path.write_text(baseline)
            text = fixture.read_text().replace('"../../src/music.c"',
                                               '"' + baseline_path.as_posix() + '"')
            for name in ("heapinit", "heapalloc"):
                text = text.replace(f'"../../src/libultra/audio/{name}.c"',
                                    '"' + (ROOT / f"src/libultra/audio/{name}.c").as_posix() + '"')
            text = text.replace("/* INSERT_BASELINE_ALLOCATION */", allocation)
            fixture = directory / "music-baseline-test.c"
            fixture.write_text(text)
            flags.append("-DMUSIC_BASELINE=1")
        subprocess.run([compiler, "-std=c11", "-O1"] + flags +
                       ["-I" + str(ROOT / path) for path in includes] +
                       [str(fixture),
                        str(directory / "gevr_romcheck.o"),
                        str(directory / "gevr_rom_manifest.o"),
                        "-Wl,--gc-sections", "-lz", "-o", str(exe)], check=True)
        result = subprocess.run([str(exe), str(directory / "bank.bin"),
                                 str(directory / "sizes.txt")])
        if args.verify_baseline:
            assert result.returncode == 42, result.returncode
        else:
            result.check_returncode()


if __name__ == "__main__":
    main()
