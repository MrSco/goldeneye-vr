"""Exercise production setup conversion with synthetic cartridge records (no ROM)."""
import ctypes as C
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="gevr-setup-") as tmp:
        tmp = Path(tmp)
        stub = tmp / "log.c"
        stub.write_text("void sysLogPrintf(int level, const char *fmt, ...) {}\n")
        dll = tmp / ("setup.dll" if os.name == "nt" else "setup.so")
        compiler = r"C:\Strawberry\c\bin\gcc.exe" if os.name == "nt" else "gcc"
        subprocess.run([compiler, "-shared", "-fPIC", "-std=c11", "-D_LANGUAGE_C=1",
                        "-I" + str(ROOT / "port/include"),
                        "-I" + str(ROOT / "include"),
                        str(ROOT / "port/src/gevr_setup.c"), str(stub),
                        "-o", str(dll)], check=True)
        lib = C.CDLL(str(dll))
        convert = lib.gevrConvertSetup
        convert.argtypes = [C.c_void_p, C.c_size_t, C.c_size_t]
        convert.restype = C.c_size_t

        # Distinct ID/offset values expose accidentally swapping them together.
        for tag_id, relative in [(1, -1), (0x1234, -23), (0xabcd, 17)]:
            header = struct.pack(">10I", 0, 0, 40, 84, 0, 0, 0, 0, 0, 0)
            camera = struct.pack(">10I", 6, 100, 200, 300, 400, 500, 0,
                                 0x00001234, 0x5678, 0)
            intro_end = struct.pack(">I", 9)
            tag = struct.pack(">HBBHhII", 256, 0, 22, tag_id, relative, 0, 0)
            data = header + camera + intro_end + tag + bytes([0, 0, 0, 48])
            buffer = C.create_string_buffer(data, 8192)
            assert convert(buffer, len(data), len(buffer)), lib.gevrSetupFailCode()
            result = buffer.raw
            intro, props = struct.unpack_from("=QQ", result, 16)
            assert struct.unpack_from("=Hh", result, props + 4) == (tag_id, relative)
            assert struct.unpack_from("=HH", result, intro + 32) == (0, 0x1234)
            assert struct.unpack_from("=Q", result, intro + 40)[0] == 0x5678
            assert struct.unpack_from("=Q", result, intro + 48)[0] == 0
            assert struct.unpack_from("=I", result, intro + 56)[0] == 9
        if os.name == "nt":
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)
        print("PASS: tag IDs, signed object offsets, camera caption IDs and host strides")


if __name__ == "__main__":
    main()
