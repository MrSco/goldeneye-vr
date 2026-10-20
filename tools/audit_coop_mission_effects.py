"""Inventory shared effects in all 20 retail mission setup sources.

Pass the assets/obseg/setup directory from a local n64decomp/007 checkout.
Only command names/counts are printed; no cartridge data is copied.
"""
import argparse
from collections import Counter
from pathlib import Path
import re

MISSIONS = {
    'Dam': 'UsetupdamZ.c', 'Facility': 'UsetuparkZ.c', 'Runway': 'UsetuprunZ.c',
    'Surface 1': 'UsetupsevxZ.c', 'Bunker 1': 'UsetupsevbunkerZ.c', 'Silo': 'u/UsetupsiloZ.c',
    'Frigate': 'u/UsetupdestZ.c', 'Surface 2': 'UsetupsevxbZ.c', 'Bunker 2': 'UsetupsevbZ.c',
    'Statue': 'u/UsetupstatueZ.c', 'Archives': 'UsetuparchZ.c', 'Streets': 'UsetuppeteZ.c',
    'Depot': 'UsetupdepoZ.c', 'Train': 'u/UsetuptraZ.c', 'Jungle': 'u/UsetupjunZ.c',
    'Control': 'UsetupcontrolZ.c', 'Caverns': 'UsetupcaveZ.c', 'Cradle': 'u/UsetupcradZ.c',
    'Aztec': 'UsetupaztZ.c', 'Egypt': 'UsetupcrypZ.c',
}
GROUPS = {
    'countdown': {'hud_countdown_set', 'HudCountdownSet'},
    'destroy': {'object_destroy', 'DestroyObject'},
    'move': {'object_move_to_pad', 'MoveObject'},
    'flags': {'object_flags_1_set_on', 'object_flags_1_set_off', 'object_flags_2_set_on',
              'object_flags_2_set_off', 'SetObjectFlags', 'UnsetObjectFlags', 'SetObjectFlags2', 'UnsetObjectFlags2'},
    'monitor': {'tv_change_screen_bank', 'TvChangeScreenBank'},
    'gas/fog': {'gas_leak_and_fade_fog', 'GasLeakAndFadeFog', 'switch_fog_instantly', 'SwitchSky'},
    'blast sequence': {'trigger_explosions_around_bond', 'TriggerExplosionsAroundBond'},
    'rocket': {'object_rocket_launch', 'ObjectRocketLaunch'},
}


def byte_commands(text):
    """Archives also contains AI lists expressed as literal byte arrays."""
    definitions = (Path(__file__).resolve().parents[1] / 'src/bondaicommands.h').read_text()
    ids = {name: int(value, 0) for name, value in re.findall(r'#define (\w+)_ID\s+(0x[0-9a-fA-F]+|\d+)', definitions)}
    sizes = {name: int(value, 0) for name, value in re.findall(r'#define (\w+)_LENGTH\s+(0x[0-9a-fA-F]+|\d+)', definitions)}
    opcodes = {number: (name, sizes[name]) for name, number in ids.items() if name in sizes}
    for body in re.findall(r'u8\s+\w+\[\]\s*=\s*\{([^}]+)\}', text):
        if not re.fullmatch(r'[\s,0-9a-fAxX]+', body):
            continue
        data = bytes(int(v.strip(), 0) for v in body.split(',') if v.strip())
        at = 0
        while at < len(data):
            name, size = opcodes[data[at]]
            yield name
            if name == 'ai_list_end':
                break
            if name == 'debug_log':
                size = data.index(0, at + 1) - at + 1
            if size < 1 or at + size > len(data):
                raise ValueError(f'invalid literal AI instruction at {at}')
            at += size


def inventory(path):
    text = path.read_text()
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    commands = Counter(re.findall(r'^\s*([A-Za-z_]\w*)\b', text, re.M))
    commands.update(byte_commands(text))
    found = {name: sum(commands[c] for c in names) for name, names in GROUPS.items()}
    found['audio'] = sum(n for c, n in commands.items() if c.startswith(('sfx_', 'Sfx')))
    found['music'] = sum(n for c, n in commands.items() if c.startswith(('music_xtrack_', 'MusicPlaySlot', 'MusicStopSlot')))
    found['vehicle AI (local)'] = sum(n for c, n in commands.items() if c.startswith(('vehicle_', 'aircraft_', 'Vehicle', 'Aircraft')))
    return {k: v for k, v in found.items() if v}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('setup_root', type=Path)
    root = parser.parse_args().setup_root
    print('| Mission | Shared effect instructions (source occurrences) |')
    print('| --- | --- |')
    for mission, filename in MISSIONS.items():
        counts = inventory(root / filename)
        print('| ' + mission + ' | ' + (', '.join(f'{name}: {n}' for name, n in counts.items()) or 'No effects in these categories') + ' |')
    print('\n20/20 mission sources scanned. This is a static inventory, not a gameplay test.')


if __name__ == '__main__':
    main()
