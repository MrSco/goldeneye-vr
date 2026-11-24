"""Exercise the production pack mesh parser without the game or a ROM."""
import ctypes as c
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


class Picture(c.Structure):
    _fields_ = [("rgba", c.c_void_p), ("width", c.c_int), ("height", c.c_int)]


class View(c.Structure):
    _fields_ = [("pictures", Picture * 32), ("corners", c.c_void_p),
                ("textures", c.c_uint), ("triangles", c.c_uint),
                ("hasWatchMount", c.c_int), ("watchMount", c.c_float * 9)]


class Corner(c.Structure):
    _fields_ = [("position", c.c_float * 3), ("uv", c.c_float * 2),
                ("rgba", c.c_uint8 * 4), ("texture", c.c_uint8)]


def main():
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / ("packmesh.dll" if os.name == "nt" else "packmesh.so")
        cc = shutil.which("gcc") or "cc"
        subprocess.run([cc, "-std=c11", "-g", "-shared", "-fPIC", "-I", str(ROOT / "port/include"),
                        str(ROOT / "port/src/gevr_packmesh.c"), "-o", str(path)], check=True)
        lib = c.CDLL(str(path))
        lib.gevrPackMeshRead.argtypes = (c.c_void_p, c.c_size_t, c.POINTER(View))
        lib.gevrPackMeshRead.restype = c.c_int
        lib.gevrPackMeshCorner.argtypes = (c.POINTER(View), c.c_size_t, c.POINTER(Corner))
        lib.gevrPackMeshCorner.restype = None

        texel = bytes((29, 71, 251, 191))
        image = struct.pack("<HH", 1, 1) + texel
        corners = b"".join(struct.pack("<5f4BB3x", x, y, -0.125, y, x, 200, 110, 50, 255, 1)
                           for x, y in ((0., 0.), (1., 0.), (0., 1.)))
        anchor = (1., 0., 0., 0., 1., 0., 0.125, -0.25, 0.5)
        body = struct.pack("<I", 2) + image * 2 + struct.pack("<I", 1) + corners
        v1 = b"GEVRWPN1" + body
        v2 = b"GEVRWPN2" + body + struct.pack("<9f", *anchor)
        for version, mesh in enumerate((v1, v2)):
            buf = c.create_string_buffer(mesh)
            view = View()
            assert lib.gevrPackMeshRead(buf, len(mesh), c.byref(view)) == 1
            assert (view.textures, view.triangles, view.hasWatchMount) == (2, 1, version)
            assert (view.pictures[1].width, view.pictures[1].height) == (1, 1)
            assert c.string_at(view.pictures[1].rgba, 4) == texel
            corner = Corner()
            lib.gevrPackMeshCorner(c.byref(view), 2, c.byref(corner))
            assert list(corner.position) == [0., 1., -0.125] and list(corner.uv) == [1., 0.]
            assert list(corner.rgba) == [200, 110, 50, 255] and corner.texture == 1
            if version:
                assert tuple(view.watchMount) == anchor
            for cut in range(8, len(mesh)):
                assert lib.gevrPackMeshRead(buf, cut, c.byref(view)) == -1, cut

        def reject(mesh):
            assert lib.gevrPackMeshRead(mesh, len(mesh), c.byref(View())) == -1

        def changed(mesh, offset, data):
            return mesh[:offset] + data + mesh[offset + len(data):]

        reject(v1 + b"\0")
        reject(changed(v1, 8, struct.pack("<I", 0xffffffff)))  # image count
        reject(changed(v1, 12, struct.pack("<H", 513)))       # image dimension
        reject(changed(v1, 28, struct.pack("<I", 0xffffffff))) # triangle count
        reject(changed(v1, 32, struct.pack("<f", float("nan"))))
        reject(changed(v1, 56, b"\x02"))  # out-of-range triangle material
        reject(changed(v1, 84, b"\0"))    # mixed triangle materials
        reject(changed(v2, len(v2) - 4, struct.pack("<f", float("inf"))))
        assert lib.gevrPackMeshRead(b"RAW_ROM_MODEL", 13, c.byref(View())) == 0
        if os.name == "nt":
            # A DLL cannot be unlinked by TemporaryDirectory while loaded.
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)
    print("PASS: both self-describing mesh layouts, texture and corner data, mount data, malformed inputs and legacy dispatch")


if __name__ == "__main__":
    main()
