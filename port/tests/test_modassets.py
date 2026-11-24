"""A mod's assets (docs/mod-system.md, Assets).

Model files in the cartridge's place: port/src/gevr_modassets.c with the real
converter (gevr_model.c) and 1172 inflater (gevr_romcheck.c), over a fake mods
folder; with GEVR_GE_ROM set, the legal page's own file from the cartridge,
stored as the cartridge stores it and decompressed. ob.c's routing is
test_romcheck.py's. Textures as their own layer of the HD-pack index: the
production scan (gevr_texpack.cpp) over a mod's folder and a pack's. And
gfx_pc.cpp's and objecthandler_2.c's use of them, in the source.
"""
import ctypes as C
import os
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print("FAIL: " + what)


def compressed(data):
    codec = zlib.compressobj(6, zlib.DEFLATED, -15)
    return b"\x11\x72" + codec.compress(data) + codec.flush()


def build_models(temp):
    cc = shutil.which("gcc") or "gcc"
    objects = []
    for name, path in (("romcheck", "port/src/gevr_romcheck.c"), ("manifest", "port/src/gevr_rom_manifest.c")):
        obj = temp / (name + ".o")
        subprocess.run([cc, "-std=c11", "-O1", "-fPIC", "-I" + str(ROOT / "port/include"), "-c", str(ROOT / path),
                        "-o", str(obj)], check=True)
        objects.append(str(obj))
    lib = temp / ("modassets" + (".dll" if os.name == "nt" else ".so"))
    subprocess.run([cc, "-std=c11", "-O1", "-shared", "-fPIC", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
                    "-I" + str(ROOT), "-I" + str(ROOT / "include"), "-I" + str(ROOT / "src"),
                    "-I" + str(ROOT / "port/include"), str(ROOT / "port/tests/modassets_native.c"),
                    str(ROOT / "port/src/gevr_modassets.c"), str(ROOT / "port/src/gevr_model.c"), *objects, "-lz",
                    "-o", str(lib)], check=True)
    if os.name == "nt":   # the compiler's zlib and runtime DLLs
        os.add_dll_directory(str(Path(shutil.which(cc) or cc).resolve().parent))
    m = C.CDLL(str(lib))
    m.test_setup.argtypes = [C.c_char_p, C.c_char_p, C.c_int, C.c_int]
    m.test_model.argtypes = [C.c_char_p, C.c_void_p, C.c_uint, C.c_int, C.c_int]
    m.test_model.restype = C.c_uint
    m.test_convert.argtypes = [C.c_void_p, C.c_uint, C.c_uint, C.c_int, C.c_int]
    m.test_convert.restype = C.c_uint
    m.test_texture_dir.restype = C.c_char_p
    m.test_log.restype = C.c_char_p
    return m


def models(temp):
    temp.mkdir(parents=True)
    m = build_models(temp)
    root = temp / "mods"
    models_dir = root / "tactical/assets/models"
    models_dir.mkdir(parents=True)

    def load(name, capacity, switches=0, textures=5):
        out = C.create_string_buffer(b"\xa5" * (capacity + 16), capacity + 16)
        m.test_log_clear()
        n = m.test_model(name.encode(), out, capacity, switches, textures)
        return n, out.raw, m.test_log().decode()

    m.test_setup(str(root).encode(), b"tactical", 0, 0)
    (models_dir / "PlegalpageZ").write_bytes(b"\x00" * 64)
    n, out, log = load("PlegalpageZ", 4096)
    check(n == 0 and log == "", "no mod running: the cartridge's, nothing read")
    m.test_setup(str(root).encode(), b"tactical", 1, 1)
    n, out, log = load("PcartonZ", 4096)
    check(n == 0 and log == "", "a file the mod hasn't: the cartridge's, nothing said")
    (models_dir.parent / "x").write_bytes(compressed(b"x" * 64))
    n, out, log = load("../x", 4096)
    check(n == 0 and log == "", "a name is never a path")
    n, out, log = load("PlegalpageZ", 4096)
    check(n == 0 and "isn't a model the cartridge's header fits" in log and out[:4096] == b"\xa5" * 4096,
          "not a model: the cartridge's, and the game's room untouched: " + log)
    (models_dir / "PlegalpageZ").write_bytes(b"\x11\x72" + b"\xff" * 40)
    n, out, log = load("PlegalpageZ", 4096)
    check(n == 0 and "won't decompress" in log and out[:4096] == b"\xa5" * 4096, "a broken 1172 stream: " + log)
    (models_dir / "PlegalpageZ").write_bytes(compressed(bytes(range(256)) * 20))
    n, out, log = load("PlegalpageZ", 1000)
    check(n == 0 and "into the 1000 bytes" in log, "past the game's room once decompressed: " + log)
    (models_dir / "PlegalpageZ").write_bytes(b"\x00" * 2000)
    n, out, log = load("PlegalpageZ", 1000)
    check(n == 0 and "won't decompress into the 1000 bytes" in log, "a decompressed one past the room: " + log)
    (models_dir / "PlegalpageZ").write_bytes(b"")
    n, out, log = load("PlegalpageZ", 4096)
    check(n == 0 and "empty or over 4 MB" in log, "empty: " + log)
    with open(models_dir / "PlegalpageZ", "wb") as f:
        f.truncate((4 << 20) + 1)
    n, out, log = load("PlegalpageZ", 4096)
    check(n == 0 and "empty or over 4 MB" in log, "over 4 MB: " + log)
    (models_dir / "PlegalpageZ").unlink()

    # mod.weapon.model's files: a cartridge file's kind of name, never a path; the run's mod's own
    check(m.test_name_ok(b"GtacAk74Z") and not m.test_name_ok(b"") and not m.test_name_ok(b"../x")
          and not m.test_name_ok(b"a b") and not m.test_name_ok(b"a" * 65), "a model's name")
    (models_dir / "GtacAk74Z").write_bytes(b"\x00" * 8)
    (models_dir / "GtacDir").mkdir()
    check(m.test_has_model(b"GtacAk74Z") and not m.test_has_model(b"GtacNoneZ") and not m.test_has_model(b"GtacDir")
          and not m.test_has_model(b"../x"), "the mod has the file (not a folder, never a path)")
    m.test_setup(str(root).encode(), b"tactical", 0, 1)
    check(not m.test_has_model(b"GtacAk74Z"), "no mod running: none")
    m.test_setup(str(root).encode(), b"tactical", 1, 1)
    (models_dir / "GtacAk74Z").unlink()
    (models_dir / "GtacDir").rmdir()

    # the textures' folder: the launcher's mod campaign's, when it has one
    check(m.test_texture_dir() == b"", "no assets/textures: none")
    (root / "tactical/assets/textures").mkdir(parents=True)
    check(m.test_texture_dir().replace(b"\\", b"/").endswith(b"/tactical/assets/textures"), "its textures' folder")
    m.test_setup(str(root).encode(), b"tactical", 1, 0)
    check(m.test_texture_dir() == b"", "no mod campaign chosen: none")
    m.test_setup(str(root).encode(), b"tactical", 1, 1)

    rom = os.environ.get("GEVR_GE_ROM")
    if not rom or not Path(rom).is_file():
        print("SKIP: the legal page's own model file: set GEVR_GE_ROM to a GoldenEye ROM")
        return
    sys.path.insert(0, str(ROOT / "tools"))
    import gevr_model_probe as probe
    image = probe.load_rom(rom)
    offset, size = probe.manifest_lookup("PlegalpageZ")
    stored = bytes(image[offset:offset + size])
    cart = probe.decompress_1172(stored)
    capacity = 0x3c000   # front.c's room for it
    buf = C.create_string_buffer(cart + b"\0" * (capacity - len(cart)), capacity)
    host = m.test_convert(buf, len(cart), capacity, 0, 5)   # MODELFILEHEADER(legalpage, ..., 0 switches, ..., 5 textures)
    check(host > len(cart), "the cartridge's legal page converts (%d bytes to %d)" % (len(cart), host))
    want = buf.raw[:host]
    for label, data in (("as the cartridge stores it", stored), ("decompressed", cart)):
        (models_dir / "PlegalpageZ").write_bytes(data)
        n, out, log = load("PlegalpageZ", capacity)
        check(n == host and out[:n] == want and "PlegalpageZ is the mod's" in log,
              "the mod's file %s: the same host layout as the cartridge's own (%d, %d bytes)" % (label, n, host))
        n, out, log = load("PlegalpageZ", len(cart))
        check(n == 0 and out[:len(cart)] == b"\xa5" * len(cart),
              "%s, in only the cartridge file's bytes: no room to widen it, the cartridge's: %s" % (label, log))
    print("models: the legal page's own file, stored and decompressed, rebuilt as the cartridge's is")


def textures(temp):
    temp.mkdir(parents=True)
    gxx = shutil.which("g++") or "g++"
    stb = temp / "stb.c"
    stb.write_bytes(b'#define STB_IMAGE_IMPLEMENTATION\n#include "external/stb_image.h"\n')
    exe = temp / ("texpack_mod" + (".exe" if os.name == "nt" else ""))
    cc = shutil.which("gcc") or "gcc"
    subprocess.run([cc, "-O1", "-I" + str(ROOT / "port/include"), "-c", str(stb), "-o", str(temp / "stb.o")], check=True)
    shim = []
    if os.name == "nt":   # MinGW's mkdir takes the path alone (the texture dump's, unused here)
        (temp / "winshim.h").write_bytes(b"#include <sys/stat.h>\n#include <io.h>\n#include <direct.h>\n"
                                         b"#define mkdir(path, mode) _mkdir(path)\n")
        shim = ["-include", str(temp / "winshim.h")]
    subprocess.run([gxx, "-std=c++17", "-O1", *shim, "-I" + str(ROOT / "port/fast3d"), "-I" + str(ROOT / "port/include"),
                    str(ROOT / "port/tests/texpack_mod_native.cpp"), str(ROOT / "port/fast3d/gevr_texpack.cpp"),
                    str(temp / "stb.o"), "-lz", "-pthread", "-o", str(exe)], check=True)
    pack, mod = temp / "pack", temp / "mod"
    (pack / "GOLDENEYE").mkdir(parents=True)
    (mod / "guns").mkdir(parents=True)
    for d, name in ((pack / "GOLDENEYE", "GOLDENEYE#1111AAAA#2#1_all.png"), (pack / "GOLDENEYE", "GOLDENEYE#2222BBBB#2#1_all.png"),
                    (mod, "TACTICAL#1111AAAA#2#1_all.png"), (mod / "guns", "TACTICAL#3333CCCC#0#2_all.png")):
        (d / name).write_bytes(b"")
    keys = ["1111AAAA", 2, 1, "2222BBBB", 2, 1, "3333CCCC", 0, 2, "1111AAAA", 0, 2]

    def run(p, m):
        out = subprocess.run([str(exe), p, m, *map(str, keys)], capture_output=True, text=True, timeout=60)
        return [l for l in out.stdout.splitlines() if l.startswith(("KEY", "HASMOD"))]

    got = run(str(pack), str(mod))
    check(got == ["KEY 1111AAAA mod=TACTICAL#1111AAAA#2#1_all.png pack=GOLDENEYE#1111AAAA#2#1_all.png",
                  "KEY 2222BBBB mod=- pack=GOLDENEYE#2222BBBB#2#1_all.png",
                  "KEY 3333CCCC mod=TACTICAL#3333CCCC#0#2_all.png pack=-",
                  "KEY 1111AAAA mod=- pack=-", "HASMOD 1"],
          "the mod's layer answers apart from the pack's, in subfolders too, by format and size: " + str(got))
    got = run("-", str(mod))
    check(got[0] == "KEY 1111AAAA mod=TACTICAL#1111AAAA#2#1_all.png pack=-" and got[-1] == "HASMOD 1",
          "the mod's textures with no HD pack chosen: " + str(got))
    got = run(str(pack), "-")
    check(got[0] == "KEY 1111AAAA mod=- pack=GOLDENEYE#1111AAAA#2#1_all.png" and got[-1] == "HASMOD 0",
          "no mod: the pack as before: " + str(got))


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def placement():
    gfx = (ROOT / "port/fast3d/gfx_pc.cpp").read_bytes().decode().replace("\r\n", "\n")
    find = function(gfx, "static int tp_find(uint64_t key, uint8_t fmt, uint8_t siz)")
    check(find.index("gevrtp::findMod(") < find.index("if (s_tpPackOff) return -1;") < find.index("gevrtp::find(key"),
          "tp_find: the mod's first, then nothing more with HD switched off, then the packs")
    frame = function(gfx, "static void gevr_texpack_frame(void)")
    check(frame.count("s_tpActive = s_tpIndexed && (!off || modLive);") == 1 and "gevrModActive() != NULL" in frame,
          "the mod's textures on through HD off, while the mod runs")
    check("gevrtp::start(normal.c_str(), nullptr, mod);" in function(gfx, 'extern "C" void gevrTexpackStartEarly(void)'),
          "the scan starts with the mod's folder")
    obj = (ROOT / "src/game/objecthandler_2.c").read_bytes().decode().replace("\r\n", "\n")
    fill = function(obj, "void load_object_fill_header(")
    lines = fill.split("\n")
    check(fill.index("gevrModAssetLast = 0;") < fill.index("_fileNameLoadToAddr(")
          and all(any(call in l and "!modModel" in l for l in lines)
                  for call in ("gevrHandPatchApply(", "gevrHatModelLoaded(", "gevrBeanModelApply(", "gevrN64MagApply(")),
          "a mod's model: the patches measured on the cartridge's meshes leave it alone")
    ob = (ROOT / "src/game/ob.c").read_bytes().decode().replace("\r\n", "\n")
    load = function(ob, "void load_resource(")
    check(load.index("gevrModAssetPendingName != NULL ? gevrModAssetPendingName : srcfile->filename")
          < load.index("gevrGexPendingFile == NULL && srcfile->filename != NULL") < load.index("gevrModAssetModel(")
          < load.index("converted = gevrModelConvert("), "load_resource: the mod's model, never over GoldenEye X's, else the cartridge's")


def main():
    placement()
    with tempfile.TemporaryDirectory(prefix="gevr-modassets-", ignore_cleanup_errors=True) as temp:   # Windows keeps the DLL
        models(Path(temp) / "m")
        textures(Path(temp) / "t")
    if failures:
        print("%d check(s) failed" % len(failures))
        sys.exit(1)
    print("PASS: mods' assets: model files in the cartridge's place (stored or decompressed, the game's room, a "
          "broken or wrong file refused, never a path), the textures' folder; textures as the HD index's own layer "
          "(by key, format and size, with or without a pack); the hooks in gfx_pc.cpp, objecthandler_2.c and ob.c")


if __name__ == "__main__":
    main()
