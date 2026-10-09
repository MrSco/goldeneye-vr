"""Production mission-start search on linked floors, narrow rooms and obstacles."""
import ctypes as C
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]

class Pos(C.Structure):
    _fields_ = [(a, C.c_float) for a in ('x', 'y', 'z')]

def build_library(folder):
    stan = (ROOT / 'src/game/stan.c').read_text()
    names = ('f32 getShortest2dDispToInfTileEdge(', 'f32 distToTilePnt2D(',
             'bool stanPointProjectsOntoTileEdge(', 'int getRotationalDirectionBetween(',
             's32 sub_GAME_7F0B07BC(', 'bool sub_GAME_7F0B0914(',
             's32 walkTilesBetweenPoints_NoCallback(', 'StanCollisionResult sub_GAME_7F0B1DDC(StandTile **startTile',
             'f32 stanGetPositionYValue(')
    geometry = '\n\n'.join(block(stan, n) for n in names)
    # Compile the solver itself, not a reimplementation of its search.
    view = (ROOT / 'src/game/bondview_r.c').read_text()
    solver = block(view, 's32 gevrCoopFindSpawnSpot(')
    fixture = (ROOT / 'port/tests/campaign_spawn_native.c').read_text()
    fixture = fixture.replace('/* INSERT_GEOMETRY */', geometry).replace('/* INSERT_SOLVER */', solver)
    fixture = fixture.replace('/* INSERT_START */',block(view,'static void gevrCoopStartSpot('))
    fixture = fixture.replace('/* INSERT_AI_SIZE */',
                              block((ROOT / 'src/game/chrai.c').read_text(), 's32 chraiitemsize('))
    source = folder / 'spawn.c'
    source.write_text(fixture)
    library = folder / ('spawn.dll' if os.name == 'nt' else 'spawn.so')
    args = [shutil.which('gcc') or 'gcc', '-shared', '-fPIC', '-std=c11', '-O2',
            '-fms-extensions', '-Wno-builtin-declaration-mismatch']
    args += ['-D' + v for v in ('GEVR=1', 'PLATFORM_64BIT=1', '_LANGUAGE_C=1',
                               'VERSION=2', 'VERSION_US=1', 'LANG_US=1')]
    args += ['-I' + str(ROOT / p) for p in ('.', 'port/include', 'include', 'src', 'src/game')]
    result = subprocess.run(args + [str(source), str(ROOT / 'port/src/gevr_stage.c'),
                                   '-lm', '-o', str(library)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    lib = C.CDLL(str(library))
    lib.gevrCoopFindSpawnSpot.argtypes = [C.POINTER(Pos), C.POINTER(C.c_void_p), C.c_float,
                                        C.POINTER(Pos), C.c_int]
    lib.auditGeometry.argtypes = [C.c_void_p, C.c_float, C.c_size_t]
    lib.auditStart.argtypes = [C.POINTER(Pos), C.POINTER(C.c_void_p), C.POINTER(Pos), C.c_int]
    lib.auditObstacle.argtypes = [C.c_int, C.c_float, C.c_float, C.c_float]
    lib.gevrConvertStan.argtypes = [C.c_void_p, C.c_size_t, C.c_size_t]
    lib.gevrConvertStan.restype = C.c_size_t
    lib.chraiitemsize.argtypes = [C.c_void_p, C.c_int]
    return lib

def unload(lib):
    if os.name == 'nt':
        import _ctypes
        _ctypes.FreeLibrary(lib._handle)

def main():
    import struct
    with tempfile.TemporaryDirectory(prefix='gevr-coop-start-') as tmp:
        lib = build_library(Path(tmp))
        try:
            # Clockwise rectangle, one floor with no links, broad enough for four.
            data = struct.pack('>3I', 0, 12, 0)
            data += struct.pack('>IBBH', 0x00010101, 0, 0, 0x4120)
            data += b''.join(struct.pack('>hhhH', x, 0, z, 0)
                             for x, z in ((-200,-400),(-200,400),(200,400),(200,-400)))
            data += bytes(32)
            buf = C.create_string_buffer(data, 8192)
            size = lib.gevrConvertStan(buf, len(data), len(buf))
            assert size
            lib.auditGeometry(buf, 1.0, size)
            start = C.c_void_p.from_buffer(buf, 8).value
            lib.auditObstacle(1, 0, -70, 25)
            reserved = (Pos * 4)(); reserved[0] = Pos(0,0,0)
            for slot in range(1,4):
                pos, tile = Pos(0,0,0), C.c_void_p(start)
                assert lib.gevrCoopFindSpawnSpot(C.byref(pos), C.byref(tile), 3.14159265, reserved, slot)
                assert abs(pos.x) >= 55 or abs(pos.z + 70) >= 55
                assert all((pos.x-reserved[i].x)**2 + (pos.z-reserved[i].z)**2 >= 65**2-0.01 for i in range(slot))
                reserved[slot] = pos
            # No reachable room: leave the caller's position and tile intact.
            lib.auditObstacle(1,0,0,1000)
            pos, tile = Pos(0,0,0), C.c_void_p(start)
            assert not lib.gevrCoopFindSpawnSpot(C.byref(pos), C.byref(tile), 0, reserved, 1)
            assert pos.x == 0 and pos.z == 0 and tile.value == start
            # A one-person-wide corridor: the old right/left offset is blocked.
            # All three additional players must be spaced along the corridor.
            narrow = bytearray(data)
            for i,x in enumerate((-35,-35,35,35)): struct.pack_into('>h',narrow,20+i*8,x)
            buf2 = C.create_string_buffer(bytes(narrow),8192)
            size = lib.gevrConvertStan(buf2,len(narrow),len(buf2))
            lib.auditGeometry(buf2,1.0,size); lib.auditObstacle(0,0,0,0)
            start = C.c_void_p.from_buffer(buf2,8).value
            look = Pos(0,0,1)
            outcomes = {}
            for slot in (3,1,2,1,3):
                pos, tile = Pos(0,0,0), C.c_void_p(start)
                lib.auditStart(C.byref(pos),C.byref(tile),C.byref(look),slot)
                assert abs(pos.x)<=5.01 and abs(pos.z)>=70-0.01
                result=(pos.x,pos.z,tile.value)
                if slot in outcomes: assert outcomes[slot]==result
                outcomes[slot]=result
            assert all((a[0]-b[0])**2+(a[1]-b[1])**2>=65**2
                       for i,a in outcomes.items() for j,b in outcomes.items() if i!=j)
        finally:
            unload(lib)
    print('PASS: alternate directions, cylinder clearance, four reservations, narrow corridor, load order, safe failure')

if __name__ == '__main__':
    main()
