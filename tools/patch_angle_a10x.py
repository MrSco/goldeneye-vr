#!/usr/bin/env python3
"""Patch prebuilt 0.5.20 tvOS ANGLE libGLESv2 for Apple TV 4K Gen 1 (A10X / Apple3).

Upstream ANGLE commit 09a869c245 (included in rev e25e8d8565f7) bumped
rx::DisplayMtl::getMaxSupportedESVersion() from supportsEitherGPUFamily(3, 1)
to supportsEitherGPUFamily(4, 1), causing ANGLE to refuse OpenGL ES 3.0 on
Apple GPU Family 3 (A10X Fusion) and return ES 2.0 only. GoldenEye VR uses at
most 1 vertex UBO and fits well within Apple3 limits.

This script performs a strictly verified 1-instruction patch in the unsigned
v0.5.20 tvOS device libGLESv2 Mach-O binary:
  offset 0x52c8ac: 81 00 80 52 (mov w1, #0x4) -> 61 00 80 52 (mov w1, #0x3)
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
MH_DYLIB = 0x6

LC_SEGMENT_64 = 0x19
LC_CODE_SIGNATURE = 0x1D
LC_BUILD_VERSION = 0x32
PLATFORM_TVOS = 3

FUNC_OFFSET = 0x52C898
PATCH_OFFSET = 0x52C8AC

EXPECTED_BEFORE = bytes.fromhex(
    "f44fbea9"  # 0x52c898: stp x20, x19, [sp, #-0x20]!
    "fd7b01a9"  # 0x52c89c: stp x29, x30, [sp, #0x10]
    "fd430091"  # 0x52c8a0: add x29, sp, #0x10
    "f40300aa"  # 0x52c8a4: mov x20, x0
    "007440f9"  # 0x52c8a8: ldr x0, [x0, #0xe8]
)

ORIG_INSN = bytes.fromhex("81008052")     # 0x52800081: mov w1, #0x4
PATCHED_INSN = bytes.fromhex("61008052")  # 0x52800061: mov w1, #0x3

EXPECTED_AFTER = bytes.fromhex(
    "f64d0194"  # 0x52c8b0: bl 0x580088 (rx::mtl::SupportsAppleGPUFamily)
    "13608052"  # 0x52c8b4: mov w19, #0x300
    "e0000037"  # 0x52c8b8: tbnz w0, #0x0, 0x52c8d4
    "807640f9"  # 0x52c8bc: ldr x0, [x20, #0xe8]
    "21008052"  # 0x52c8c0: mov w1, #0x1
    "044e0194"  # 0x52c8c4: bl 0x5800d4 (rx::mtl::SupportsMacGPUFamily)
    "1f000071"  # 0x52c8c8: cmp w0, #0x0
    "08408052"  # 0x52c8cc: mov w8, #0x200
    "7312881a"  # 0x52c8d0: csel w19, w19, w8, ne
    "e00313aa"  # 0x52c8d4: mov x0, x19
    "fd7b41a9"  # 0x52c8d8: ldp x29, x30, [sp, #0x10]
    "f44fc2a8"  # 0x52c8dc: ldp x20, x19, [sp], #0x20
    "c0035fd6"  # 0x52c8e0: ret
)


def fail(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def parse_and_validate_macho(data: bytes) -> None:
    if len(data) < 32:
        fail(f"file too small to be a 64-bit Mach-O binary ({len(data)} bytes)")

    magic, cputype, _cpusubtype, filetype, ncmds, sizeofcmds, _flags, _reserved = (
        struct.unpack_from("<IIIIIIII", data, 0)
    )
    if magic != MH_MAGIC_64:
        fail(f"expected thin 64-bit Mach-O (0x{MH_MAGIC_64:x}), got 0x{magic:x}")
    if cputype != CPU_TYPE_ARM64:
        fail(f"expected arm64 cputype (0x{CPU_TYPE_ARM64:x}), got 0x{cputype:x}")
    if filetype != MH_DYLIB:
        fail(f"expected MH_DYLIB (0x{MH_DYLIB:x}), got 0x{filetype:x}")
    if 32 + sizeofcmds > len(data):
        fail("truncated Mach-O load commands")

    text_ok = False
    tvos_ok = False
    cursor = 32
    end_cmds = 32 + sizeofcmds
    for idx in range(ncmds):
        if cursor + 8 > end_cmds:
            fail(f"load command {idx} extends past sizeofcmds")
        cmd, cmdsize = struct.unpack_from("<II", data, cursor)
        if cmdsize < 8 or cursor + cmdsize > end_cmds:
            fail(f"invalid load command {idx} size ({cmdsize})")

        if cmd == LC_CODE_SIGNATURE:
            fail(
                "refusing to patch a signed Mach-O binary (LC_CODE_SIGNATURE present); "
                "strip the signature first"
            )
        elif cmd == LC_SEGMENT_64:
            if cmdsize < 72:
                fail("truncated LC_SEGMENT_64 command")
            segname_raw, vmaddr, _vmsize, fileoff, filesize = struct.unpack_from(
                "<16sQQQQ", data, cursor + 8
            )
            segname = segname_raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")
            if segname == "__TEXT":
                func_end = PATCH_OFFSET + len(ORIG_INSN) + len(EXPECTED_AFTER)
                if vmaddr != 0 or fileoff != 0:
                    fail(
                        f"unexpected __TEXT mapping: vmaddr=0x{vmaddr:x}, fileoff=0x{fileoff:x} "
                        "(expected 0)"
                    )
                if func_end > filesize or func_end > len(data):
                    fail(
                        f"__TEXT segment too small (filesize=0x{filesize:x}, need 0x{func_end:x})"
                    )
                text_ok = True
        elif cmd == LC_BUILD_VERSION:
            if cmdsize < 24:
                fail("truncated LC_BUILD_VERSION command")
            (platform,) = struct.unpack_from("<I", data, cursor + 8)
            if platform != PLATFORM_TVOS:
                fail(f"expected TVOS platform ({PLATFORM_TVOS}), got {platform}")
            tvos_ok = True

        cursor += cmdsize

    if not text_ok:
        fail("__TEXT LC_SEGMENT_64 not found")
    if not tvos_ok:
        fail("LC_BUILD_VERSION for TVOS not found")


def inspect_function_window(data: bytes) -> bytes:
    before = data[FUNC_OFFSET:PATCH_OFFSET]
    if before != EXPECTED_BEFORE:
        fail(
            f"unexpected bytes before 0x{PATCH_OFFSET:x}: "
            f"got {before.hex()}, expected {EXPECTED_BEFORE.hex()}"
        )

    after_start = PATCH_OFFSET + len(ORIG_INSN)
    after_end = after_start + len(EXPECTED_AFTER)
    after = data[after_start:after_end]
    if after != EXPECTED_AFTER:
        fail(
            f"unexpected bytes after 0x{PATCH_OFFSET:x}: "
            f"got {after.hex()}, expected {EXPECTED_AFTER.hex()}"
        )

    return data[PATCH_OFFSET:after_start]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Patch or verify 0.5.20 tvOS ANGLE libGLESv2 for A10X (MTLGPUFamilyApple3)."
    )
    parser.add_argument(
        "binary",
        type=pathlib.Path,
        help="Path to libGLESv2.framework/libGLESv2",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify the binary is already patched without modifying it.",
    )
    args = parser.parse_args()

    path: pathlib.Path = args.binary
    if not path.is_file():
        fail(f"file not found: {path}")

    raw = path.read_bytes()
    parse_and_validate_macho(raw)
    target = inspect_function_window(raw)

    if args.check:
        if target == PATCHED_INSN:
            sha = hashlib.sha256(raw).hexdigest()
            print(
                f"ok: {path} is patched for Apple3 at 0x{PATCH_OFFSET:x} "
                f"({PATCHED_INSN.hex()} = mov w1, #0x3), sha256={sha}"
            )
            return 0
        fail(
            f"{path} is NOT patched at 0x{PATCH_OFFSET:x}: "
            f"found {target.hex()} (expected {PATCHED_INSN.hex()})"
        )

    if target == PATCHED_INSN:
        sha = hashlib.sha256(raw).hexdigest()
        print(
            f"ok: {path} already patched at 0x{PATCH_OFFSET:x} "
            f"({PATCHED_INSN.hex()} = mov w1, #0x3), sha256={sha}"
        )
        return 0

    if target != ORIG_INSN:
        fail(
            f"unexpected instruction at 0x{PATCH_OFFSET:x}: "
            f"got {target.hex()}, expected {ORIG_INSN.hex()} (mov w1, #0x4)"
        )

    sha_before = hashlib.sha256(raw).hexdigest()
    buf = bytearray(raw)
    buf[PATCH_OFFSET : PATCH_OFFSET + len(PATCHED_INSN)] = PATCHED_INSN
    patched = bytes(buf)
    path.write_bytes(patched)

    # Re-read and verify on disk.
    verify_raw = path.read_bytes()
    parse_and_validate_macho(verify_raw)
    verify_target = inspect_function_window(verify_raw)
    if verify_target != PATCHED_INSN:
        fail("post-write verification failed")

    sha_after = hashlib.sha256(verify_raw).hexdigest()
    print(
        f"patched: {path} at 0x{PATCH_OFFSET:x}: "
        f"{ORIG_INSN.hex()} (mov w1, #0x4) -> {PATCHED_INSN.hex()} (mov w1, #0x3)"
    )
    print(f"  sha256 before: {sha_before}")
    print(f"  sha256 after:  {sha_after}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
