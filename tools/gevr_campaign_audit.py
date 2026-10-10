"""Audit all twenty campaign starts using an owned USA ROM and production floor code.

No ROM bytes are written. This checks floor paths, full-radius clearance and
party separation; the native game/headset must also verify loaded scenery,
doors, ceiling clearance, mission progression and movement.
"""
import argparse
import ctypes as C
from pathlib import Path
import re
import struct
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'port/tests'))
from test_coop_start import Pos, build_library, unload
from gevr_rom_probe import load_rom

def assets(rom):
    rows = re.findall(r'\{\s*(0x\w+),\s*(\d+),\s*(\d+)\s*\},\s*/\*\s*\w+\s+(\w+)',
                      (ROOT / 'port/src/gevr_rom_manifest.c').read_text())
    result = {}
    for offset, size, compressed, name in rows:
        offset, size = int(offset,16), int(size)
        if offset and (name.startswith('Usetup') or name.startswith('Tbg_')):
            data = rom[offset:offset+size]
            result[name] = zlib.decompress(data[2:], -15) if int(compressed) else data
    return result

def missions():
    match = (ROOT / 'port/src/net/net_match.c').read_text()
    mission_table = match.split('s_coop_missions[] = {')[1].split('};')[0]
    stages = re.findall(r'\{ "([^"]+)", (\d+) \}', mission_table)
    setup = (ROOT / 'src/game/chraidata.c').read_text().split('char *setup_text_pointers[] = {')[1].split('};')[0]
    setups = re.findall(r'NULL|"[^"]+"', setup)
    bg = (ROOT / 'src/game/bg.c').read_text().split('struct levelentry levelinfotable[] = {')[1].split('};')[0]
    constants = (ROOT / 'src/bondconstants.h').read_text()
    ids, value = {}, -1
    enum = constants.split('typedef enum LEVELID')[1].split('LEVELID_TITLE')[0]
    for name, explicit in re.findall(r'(LEVELID_\w+)\s*(?:=\s*(-?\d+))?\s*,',enum):
        value = int(explicit) if explicit else value+1
        ids[name] = value
    floors = {}
    for level, name, scale in re.findall(r'\{(LEVELID_\w+),\s*"[^"]+",\s*"([^"]+)",\s*([\d.]+)',bg):
        floors[ids[level]] = name, float(scale)
    return [(name, setups[int(stage)].strip('"'), *floors[int(stage)]) for name,stage in stages]

def held_tags(data, lib):
    source = (ROOT / 'port/src/gevr_setup.c').read_text()
    words = source.split('static const int words[] = {')[1].split('};')[0]
    words = re.sub(r'/\*.*?\*/', '', words, flags=re.S)
    sizes = [int(n,0)*4 for n in re.findall(r'0x[\da-f]+|\d+', words)]
    p = struct.unpack_from('>I',data,12)[0]
    records = []
    while data[p+3] != 48:
        kind = data[p+3]
        records.append((kind,p))
        p += sizes[kind]
    tags = {struct.unpack_from('>I',data,p+4)[0] for kind,p in records if kind in (28,29)}
    table = struct.unpack_from('>I',data,20)[0]
    while table:
        pointer, _ = struct.unpack_from('>II',data,table)
        table += 8
        if not pointer: break
        code = C.create_string_buffer(data[pointer:])
        offset = 0
        while data[pointer+offset] != 4:
            if data[pointer+offset] == lib.auditCollectedOpcode(): tags.add(data[pointer+offset+1])
            offset += lib.chraiitemsize(code,offset)
    return tags

def start(data):
    intro = struct.unpack_from('>I',data,8)[0]
    sizes = (12,16,16,32,8,8,40,12,8,4)
    while struct.unpack_from('>I',data,intro)[0] != 9:
        kind = struct.unpack_from('>I',data,intro)[0]
        if kind == 0 and struct.unpack_from('>I',data,intro+8)[0] == 0:
            index = struct.unpack_from('>I',data,intro+4)[0]
            pads = struct.unpack_from('>I',data,24)[0]
            p = pads+44*index
            pos = Pos(*struct.unpack_from('>3f',data,p))
            look = struct.unpack_from('>3f',data,p+24)
            string = struct.unpack_from('>I',data,p+36)[0]
            tile_name = data[string:data.index(0,string)].decode('ascii')
            return pos, look, tile_name
        intro += sizes[kind]
    raise ValueError('No gameplay start')

def tile_offset(stan, tile_name):
    m = re.fullmatch(r'([pq])(\d+)([a-z])([0-7]?)',tile_name)
    assert m, tile_name
    letter, number, file, sub = m.groups()
    high = (ord(letter)-ord('p'))<<15 | int(number)
    low = (ord(file)-ord('a'))<<3 | int(sub or '0')
    p = struct.unpack_from('>I',stan,4)[0]
    while struct.unpack_from('>I',stan,p)[0]:
        if struct.unpack_from('>H',stan,p)[0] == high and stan[p+2] == low: return p
        count = struct.unpack_from('>H',stan,p+6)[0]>>12
        p += 8+8*max(3,count)
    raise ValueError(f'Tile {tile_name} not found')

def main():
    import math
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('rom')
    args = ap.parse_args()
    rom,_ = load_rom(args.rom)
    assert rom[0x3e] == ord('E'), 'USA ROM required by the production manifest'
    files = assets(rom)
    failures = []
    with tempfile.TemporaryDirectory(prefix='gevr-campaign-audit-') as tmp:
        lib = build_library(Path(tmp))
        try:
            for name,setup,stan_name,scale in missions():
                data, floor = files[setup], files[stan_name]
                pos, look, tile_name = start(data)
                cart_tile = tile_offset(floor,tile_name)
                first = struct.unpack_from('>I',floor,4)[0]
                buffer = C.create_string_buffer(floor, len(floor)+65536)
                size = lib.gevrConvertStan(buffer, len(floor), len(buffer))
                assert size, name
                lib.auditGeometry(buffer,scale,size)
                tile = C.c_void_p.from_buffer(buffer,8).value+cart_tile-first
                facing = math.atan2(look[0],look[2])+math.pi
                reserved = (Pos*4)(); reserved[0] = pos
                distances = []
                for slot in range(1,4):
                    candidate, selected = Pos(pos.x,pos.y,pos.z), C.c_void_p(tile)
                    if not lib.gevrCoopFindSpawnSpot(C.byref(candidate), C.byref(selected), facing, reserved, slot):
                        failures.append(f'{name}: slot {slot}'); break
                    distances.append(round(math.hypot(candidate.x-pos.x,candidate.z-pos.z)))
                    reserved[slot] = candidate
                tags = held_tags(data, lib)
                look_pos = Pos(*look)
                for slot in (3,1,2):
                    candidate, selected = Pos(pos.x,pos.y,pos.z), C.c_void_p(tile)
                    lib.auditStart(C.byref(candidate),C.byref(selected),C.byref(look_pos),slot)
                    assert abs(candidate.x-reserved[slot].x)<0.01 and abs(candidate.z-reserved[slot].z)<0.01, (name,slot)
                if len(tags)>8: failures.append(f'{name}: {len(tags)} mission tags exceed packet capacity')
                print(f'{name:12} offsets={distances} mission_inventory_tags={len(tags)}')
        finally:
            unload(lib)
    if failures: raise SystemExit('\n'.join(failures))
    print('PASS: all twenty missions, two/three/four-player floor placements and held-tag capacity')

if __name__ == '__main__': main()
