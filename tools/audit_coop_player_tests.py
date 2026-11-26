"""List the player tests in all 20 retail mission scripts, for co-op.

A mission script asks about "Bond": is he in this room, near this pad,
holding this item, hurt, in the tank. Solo there is one answer. In co-op a
guard's list asks about the player it targets; a background list (an id of
0x1000 or more, no guard) has to ask about the whole party.

Pass the assets/obseg/setup directory from a local n64decomp/007 checkout.
Only list ids and command names are printed; no cartridge data is copied.
"""
import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re

from audit_coop_mission_effects import MISSIONS, byte_commands

PLAYER_TESTS = (
    'if_bond_distance_to_pad_less_than', 'if_bond_distance_to_pad_greater_than',
    'if_bond_in_room_with_pad', 'if_bond_collected_object', 'if_bond_has_item_equipped',
    'if_bond_used_gadget_on_object', 'if_bond_health_less_than', 'if_bond_health_greater_than',
    'if_bond_in_tank', 'if_bond_y_pos_less_than', 'if_bond_item_total_ammo_less_than',
    'if_bond_is_dead', 'if_bond_damage_and_pickups_disabled', 'if_object_was_activated',
)
# Archives is written with the newer names (src/aicommands2.h)
NEW_NAMES = {
    'IFBondDistanceToPadLessThan': 'if_bond_distance_to_pad_less_than',
    'IFBondDistanceToPadGreaterThan': 'if_bond_distance_to_pad_greater_than',
    'IFBondInRoomWithPad': 'if_bond_in_room_with_pad', 'IFBondCollectedObject': 'if_bond_collected_object',
    'IFBondHasItemEquipped': 'if_bond_has_item_equipped', 'IFBondUsedGadgetOnObject': 'if_bond_used_gadget_on_object',
    'IFBondHealthLessThan': 'if_bond_health_less_than', 'IFBondHealthGreaterThan': 'if_bond_health_greater_than',
    'IFBondInTank': 'if_bond_in_tank', 'IFBondYPosLessThan': 'if_bond_y_pos_less_than',
    'IFBondIsDead': 'if_bond_is_dead', 'IFBondDamageAndPickupsDisabled': 'if_bond_damage_and_pickups_disabled',
    'IFObjectWasActivated': 'if_object_was_activated',
}
LIST_ID = {'setGlobalAIListID': 0, 'setChrAIListID': 1025, 'setBGAIListID': 4096}


def lists(text):
    """(list id, [command names]) for each AI list in a setup source."""
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    bodies = {name: body for name, body in re.findall(r'u8\s+(\w+)\[\]\s*=\s*\{(.*?)\}\s*;', text, re.S)}
    table = re.search(r'ailists\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    for name, list_id in re.findall(r'\{\s*&?(\w+)\s*,\s*(0x[0-9a-fA-F]+|\d+|\w+\(\d+\))\s*\}', table.group(1)):
        macro = re.fullmatch(r'(\w+)\((\d+)\)', list_id)
        list_id = LIST_ID[macro.group(1)] + int(macro.group(2)) if macro else int(list_id, 0)
        body = bodies.get(name)
        if body is None:
            continue
        if re.fullmatch(r'[\s,0-9a-fAxX]+', body):
            names = list(byte_commands('u8 x[] = {' + body + '}'))
        else:
            names = [NEW_NAMES.get(n, n) for n in re.findall(r'^\s*([A-Za-z_]\w*)\b', body, re.M)]
        yield list_id, names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('setup_root', type=Path)
    root = parser.parse_args().setup_root
    total = defaultdict(Counter)
    print('| Mission | Background lists: player tests | Guard lists: player tests |')
    print('| --- | --- | --- |')
    for mission, filename in MISSIONS.items():
        found = {'bg': Counter(), 'chr': Counter()}
        for list_id, names in lists((root / filename).read_text()):
            kind = 'bg' if list_id >= 0x1000 else 'chr'
            for name in names:
                if name in PLAYER_TESTS:
                    found[kind][name.replace('if_bond_', '')] += 1
                    total[kind][name] += 1
        cell = lambda c: ', '.join(f'{k}: {v}' for k, v in sorted(c.items())) or '-'
        print(f'| {mission} | {cell(found["bg"])} | {cell(found["chr"])} |')
    print()
    for kind in ('bg', 'chr'):
        print(kind, dict(total[kind]))
    print('\n20/20 mission sources scanned. This is a static inventory, not a gameplay test.')


if __name__ == '__main__':
    main()
