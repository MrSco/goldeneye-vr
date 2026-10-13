"""Production mission-start search on linked floors, narrow rooms and obstacles."""
import ctypes as C
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]

class Pos(C.Structure):
    _fields_ = [(a, C.c_float) for a in ('x', 'y', 'z')]

def build_library(folder, source_ref=None, player_source_ref=None):
    stan = (ROOT / 'src/game/stan.c').read_text()
    names = ('f32 getShortest2dDispToInfTileEdge(', 'f32 distToTilePnt2D(',
             'bool stanPointProjectsOntoTileEdge(', 'int getRotationalDirectionBetween(',
             's32 sub_GAME_7F0B07BC(', 'bool sub_GAME_7F0B0914(',
             's32 walkTilesBetweenPoints_NoCallback(', 'StanCollisionResult sub_GAME_7F0B1DDC(StandTile **startTile',
             'f32 stanGetPositionYValue(', 'void noteTileRoomIfDifferentToPrev(StandTile *tile, StandTile *unused, struct StandTileWalkCallbackRecord *data)\n{',
             's32 sub_GAME_7F0B0C24(', 's32 stanLocusAddTileRoomIfNew(StandTile *tile, struct StandTileLocusCallbackRecord *rec)\n{',
             's32 incrNearEdgeCount(', 's32 sub_GAME_7F0B21B0(StandTile **tileStack, f32 target_x, f32 target_z, f32 radius, s32 *rooms, s32 *count_rtn, s32 bufMax)\n{',
             'f32 stanGetSignedPointLineDistance(', 'f32 distBetweenPoints2d(',
             'bool stanPointProjectsOntoEdge(', 's32 stanTestLineUnobstructed(',
             's32 stanTestVolume(')
    geometry = '\n\n'.join(block(stan, n) for n in names)
    # Compile the solver itself, not a reimplementation of its search.
    view = (ROOT / 'src/game/bondview_r.c').read_text()
    if source_ref:
        view = subprocess.check_output(['git', 'show', source_ref + ':src/game/bondview_r.c'],
                                       cwd=ROOT, text=True)
    solver = '\n'.join(block(view, n) for n in ('static s32 gevrCoopSpawnClear(',
        'static s32 gevrCoopSpawnPath(', 's32 gevrCoopFindSpawnSpot(') if n in view)
    fixture = (ROOT / 'port/tests/campaign_spawn_native.c').read_text()
    player_props = (ROOT / 'src/game/chrprop.c').read_text()
    player_view = (ROOT / 'src/game/bondview2.c').read_text()
    if player_source_ref:
        player_props = subprocess.check_output(['git', 'show', player_source_ref + ':src/game/chrprop.c'], cwd=ROOT, text=True)
        player_view = subprocess.check_output(['git', 'show', player_source_ref + ':src/game/bondview2.c'], cwd=ROOT, text=True)
    fixture = fixture.replace('/* INSERT_PLAYER_FILTER */', block(player_props, 's32 propIsOfCdType('))
    fixture = fixture.replace('/* INSERT_PLAYER_BOUNDS */', block(player_view, 'void bondviewGetPropHeightRelatedValues('))
    fixture = fixture.replace('/* INSERT_PLAYER_RULE */', block(stan, 'static s32 gevrNetInsidePlayerProp('))
    fixture = fixture.replace('/* INSERT_GEOMETRY */', geometry).replace('/* INSERT_SOLVER */', solver)
    fixture = fixture.replace('/* INSERT_START */',block(view,'static void gevrCoopStartSpot('))
    fixture = fixture.replace('/* INSERT_AI_SIZE */',
                              block((ROOT / 'src/game/chrai.c').read_text(), 's32 chraiitemsize('))
    fixture = fixture.replace('/* INSERT_POLYGON */',
        block((ROOT / 'src/game/chrprop.c').read_text(), 's32 chrpropTestPointInPolygon('))
    fixture = fixture.replace('/* INSERT_TOGGLE */',
        block((ROOT / 'src/game/propobj.c').read_text(), 'void sub_GAME_7F04F218(')
        + '\n' + block((ROOT / 'src/game/chrprop.c').read_text(), 'void sub_GAME_7F03D058('))
    fixture = fixture.replace('/* INSERT_INTERSECTION */',
        block((ROOT / 'src/game/stanintersection.c').read_text(), 'f32 calculateSegmentIntersectionFraction(')
        + '\n' + block(stan, 'bool doSegmentsIntersect('))
    source = folder / 'spawn.c'
    source.write_text(fixture)
    library = folder / ('spawn.dll' if os.name == 'nt' else 'spawn.so')
    args = [shutil.which('gcc') or 'gcc', '-shared', '-fPIC', '-std=c11', '-O2',
            '-fms-extensions', '-Wno-builtin-declaration-mismatch']
    args += ['-D' + v for v in ('GEVR=1', 'PLATFORM_64BIT=1', '_LANGUAGE_C=1',
                               'VERSION=2', 'VERSION_US=1', 'LANG_US=1')]
    if 'static s32 gevrCoopSpawnPath(' in view:
        args.append('-DAUDIT_SPAWN_PATH=1')
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
    lib.auditObstacleHeight.argtypes = [C.c_float, C.c_float]
    lib.auditWall.argtypes = [C.c_int, C.c_float, C.c_float, C.c_float]
    lib.auditViewer.argtypes = [C.c_int] * 4
    lib.auditViewerBounds.argtypes = [C.c_int]
    lib.auditViewerState.argtypes = [C.c_int] * 4
    lib.auditMoveVolume.argtypes = [C.POINTER(Pos), C.c_void_p]
    lib.auditMoveLine.argtypes = [C.POINTER(Pos), C.POINTER(Pos), C.c_void_p]
    lib.auditOnline.argtypes = [C.c_int]
    if hasattr(lib, 'auditPath'):
        lib.auditPath.argtypes = [C.POINTER(Pos), C.POINTER(Pos), C.POINTER(C.c_void_p)]
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref', help='run the same checks against an older spawn implementation')
    parser.add_argument('--player-baseline-ref', help='run player filtering checks against an older implementation')
    options = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gevr-coop-start-') as tmp:
        lib = build_library(Path(tmp), options.baseline_ref, options.player_baseline_ref)
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
            # Co-op reserves all four player props, even for a host alone.
            # Vacant/loading/departed slots must not expose stale or poisoned
            # footprints to the production line and volume collision tests.
            here, step, across = Pos(-1,170,0), Pos(-2,170,0), Pos(1,170,0)
            for player_slot in (1,2,3):
                for poison in (0,1):
                    lib.auditViewer(player_slot,1,0,poison)
                    assert lib.auditMoveVolume(C.byref(step),start) < 0, 'vacant slot blocks volume'
                    assert lib.auditMoveLine(C.byref(here),C.byref(across),start), 'vacant slot blocks line'
                    assert lib.auditViewerBounds(player_slot) == 0, 'vacant slot exposes uninitialised bounds'
                    # Occupied teammates use soft collision; their bounds
                    # remain available to guard and projectile queries.
                    lib.auditViewer(player_slot,1,1,0)
                    assert lib.auditViewerBounds(player_slot) == 4
                    assert lib.auditMoveVolume(C.byref(step),start) < 0
                    assert lib.auditMoveLine(C.byref(here),C.byref(across),start)
                    lib.auditViewer(player_slot,0,0,0)
            # A full four-player party has no vacant props. All teammates
            # remain passable and expose valid footprints to direct queries;
            # the moving player's disabled collision does not block itself.
            for player_slot in range(4):
                lib.auditViewer(player_slot,1,1,0)
            lib.auditViewerState(0,0,0,0)
            assert lib.auditMoveVolume(C.byref(step),start) < 0, 'full party blocks volume'
            assert lib.auditMoveLine(C.byref(here),C.byref(across),start), 'full party blocks line'
            for player_slot in (1,2,3):
                assert lib.auditViewerBounds(player_slot) == 4, 'live teammate loses footprint'
                lib.auditViewerState(player_slot,1,1,0)
                assert lib.auditViewerBounds(player_slot) == 0, 'cinematic player collision'
                lib.auditViewerState(player_slot,1,0,1)
                assert lib.auditViewerBounds(player_slot) == 0, 'dead player collision'
            for player_slot in range(4):
                lib.auditViewer(player_slot,0,0,0)
            # Offline split-screen player collision remains solid.
            lib.auditViewer(1,1,1,0); lib.auditOnline(0)
            assert lib.auditMoveVolume(C.byref(step),start) >= 0
            assert not lib.auditMoveLine(C.byref(here),C.byref(across),start)
            lib.auditViewer(1,0,0,0); lib.auditOnline(1)
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
            assert not lib.gevrCoopFindSpawnSpot(C.byref(pos), C.byref(tile), 0, reserved, 1), 'large object interior mistaken for clear floor'
            assert pos.x == 0 and pos.z == 0 and tile.value == start
            # A ceiling with the same X/Z footprint must not prevent the party
            # spawning below it. Likewise, props entirely beneath this floor.
            look = Pos(0,0,1)
            lib.auditObstacle(1,0,0,1000)
            for bottom, top in ((220,300),(-200,-40)):
                lib.auditObstacleHeight(bottom,top)
                placed = []
                for slot in (3,0,2,1):
                    pos, tile = Pos(0,0,0), C.c_void_p(start)
                    lib.auditStart(C.byref(pos),C.byref(tile),C.byref(look),slot)
                    placed.append((pos.x,pos.z))
                assert all((a[0]-b[0])**2+(a[1]-b[1])**2 >= 65**2-0.01
                           for i,a in enumerate(placed) for j,b in enumerate(placed) if i != j), 'height filtering'
            lib.auditObstacleHeight(0,180)
            # Player 1's original pad is partly overlapped by a prop. Validate
            # it too, then reserve its repaired position for the other slots.
            lib.auditObstacle(1,45,0,25)
            placed = []
            for slot in (0,1,2,3):
                pos, tile = Pos(0,0,0), C.c_void_p(start)
                lib.auditStart(C.byref(pos),C.byref(tile),C.byref(look),slot)
                assert abs(pos.x) >= 0.01 or abs(pos.z) >= 0.01, 'player 1 clearance'
                placed.append((pos.x,pos.z))
            assert all((a[0]-b[0])**2+(a[1]-b[1])**2 >= 65**2-0.01
                       for i,a in enumerate(placed) for j,b in enumerate(placed) if i != j)
            # Deep inside a large footprint, retail volume's edge-only test
            # says clear. The search must leave it and restore its collision.
            lib.auditObstacle(1,0,0,110)
            placed = []
            for slot in (3,1,0,2,3):
                pos, tile = Pos(0,0,0), C.c_void_p(start)
                lib.auditStart(C.byref(pos),C.byref(tile),C.byref(look),slot)
                assert abs(pos.x) >= 140-0.01 or abs(pos.z) >= 140-0.01, 'object interior'
                placed.append((slot,pos.x,pos.z))
            assert placed[0] == placed[-1], 'collision restored after placement'
            # Escape the object enclosing the pad, while an unrelated door
            # farther down the same path must still stop placement through it.
            origin, destination = Pos(0,0,0), Pos(0,0,-280)
            tile = C.c_void_p(start)
            assert lib.auditPath(C.byref(origin),C.byref(destination),C.byref(tile))
            lib.auditWall(1,0,-200,45)
            tile = C.c_void_p(start)
            assert not lib.auditPath(C.byref(origin),C.byref(destination),C.byref(tile)), 'unrelated door still blocks'
            lib.auditWall(0,0,0,0)
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
    print('PASS: vacant/poisoned player slot filtering, occupied/full-party soft collision, offline solidity, production scenery collision, height filtering, player 1 clearance, object interiors/escape, four reservations, narrow corridor, load order, safe failure')

if __name__ == '__main__':
    main()
