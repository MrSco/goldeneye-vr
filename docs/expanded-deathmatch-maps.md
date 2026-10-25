# Expanded campaign layouts

PR implementation, 2026-10-25. Native checks pass; APK/headset acceptance is
still required. This adds layouts only for existing campaign-derived arenas,
not all twenty missions as new arenas.

## Map choices

Original deathmatch entries retain their map IDs, order and multiplayer setups.
Seven appended entries use distinct IDs, so lobbies, favorites, voting, rotation,
late joins and host migration can distinguish layouts of the same engine level.

| Additional choice | Map ID | Engine LEVELID | Campaign / MP door records |
| --- | --- | --- | --- |
| Facility (Expanded) | 100 | 34 | 46 / 29 |
| Bunker II (Expanded) | 101 | 27 | 38 / 36 |
| Archives (Expanded) | 102 | 24 | 35 / 10 |
| Caverns (Expanded) | 103 | 39 | 25 / 17 |
| Egypt (Expanded) | 104 | 32 | 25 / 6 |
| Statue (Expanded) | 105 | 22 | 2 / 2 |
| Cradle (Expanded) | 106 | 41 | 5 / 5 |

Counts are from the public `n64decomp/007` setup records, commit
`c4356466796c697dfd298010b9bed261f9ed8c6a`. Door counts show setup differences,
not a measurement of playable area. Statue's MP gates have key flags absent in
the campaign setup; Cradle also has a locked MP door and different scenery.
Those two variants mainly restore gate access/scenery rather than adding
background geometry. The other five have substantially different door setups.
Multiplayer-only arenas have no solo counterpart, so receive no duplicate entry.

## Horde audit

Horde already loads the campaign setups for Archives, Bunker I, Bunker II,
Control and Facility: `netCoopActive()` includes Horde, `gevrMpRules()` is false,
and `proplvreset2` loads `Usetup*Z` rather than `Ump_setup*Z`. The loader clears
campaign door key flags; the director opens and holds usable doors. The public
setups for these five contain no padlock-link records. No distinct smaller
Horde setup was found, so no duplicate expanded Horde entries are added.
Existing Horde map choices and gameplay remain intact. Physical traversal of
its laboratory/tank routes is still a headset acceptance check, not established
by the absence of a smaller setup.

## Implementation

- `net_match.c` separates stable map identity from engine LEVELID. Protocol 24
  prevents earlier peers from misinterpreting IDs 100–106. Existing indices and
  saved favorites remain valid.
- `net_core.c` maps the selected arena to its engine level when launching or
  resetting, and remembers loaded map identity when reconnecting. Original and
  expanded Facility cannot reuse each other's loaded world just because both
  have engine LEVELID 34.
- `prop.c` selects the campaign setup only for an expanded deathmatch. Weapon
  sets, scoring, scenarios and respawning retain multiplayer rules.
- `gevr_expanded.c` composes a fresh setup stream. It retains scenery, monitors,
  glass, doors and door-scale commands. Guards, objectives, mission AI,
  cinematics, mission equipment, alarms, gas triggers and mission links are
  excluded. Relative door links are cleared before command indices change;
  usable door leaves can be operated independently without keys or script locks.
- Eight starts, eight weapon/ammo stations, two armour pickups and the Living
  Daylights token use the original multiplayer record formats. Weapon slots
  retain the selected weapon set, including custom sets and scenario overrides;
  each ammo crate follows its gun for `lastmpweaponnum` resolution.
- Placements use deterministic farthest-point sampling of floor-checked campaign
  navigation/guard pads, falling back to ordinary pads on sparse maps. Starts
  and pickups use distinct locations while candidates remain. Physical props
  have not loaded at sampling time: headset checks must catch scenery blocking
  placements and adjust the selection policy if needed.

## Validation

Run the synthetic setup and production network checks:

```sh
python port/tests/test_expanded_maps.py
```

Optionally clone `https://github.com/n64decomp/007` outside the project and run
all seven public setup-record checks with sanitizers:

```sh
GEVR_TEST_SANITIZE=1 GEVR_DECOMP_ROOT=/path/to/007 python port/tests/test_expanded_maps.py
```

The optional reader treats source as numeric data, reconstructs setup records,
and runs the production ROM converter and arena composer. No ROM or setup assets
are bundled with the tests. Floor/collision queries are stubs in these native
fixtures; these checks establish stream integrity, not physical traversability.
AddressSanitizer and UndefinedBehaviorSanitizer pass, including deliberately
unaligned source records and deterministic reloads.

Network tests cover all seven IDs, original indices, packet config roundtrips,
2–8 player validation, voting, rotation, engine mapping, and reconnecting across
layout changes. Existing Horde admission, player-count, eight-slot, late-join,
bot-roster/join and migration checks also pass in the production core fixture.
The full Horde test script encounters existing missing inventory declarations
in `gevr_horde.c` on this GCC toolchain; that runtime suite is not claimed passed.
No Android SDK/NDK or ROM/headset was available for an APK or live match test.

## Headset acceptance before release

- Traverse each additional route in both directions without mission progression;
  start with Facility's laboratory and bottling/tank areas.
- Check doors, visibility, scenery collision and placements on host/client,
  after respawning and after a late join. Some campaign doors are intentionally
  immobile; this change does not invent movement for a zero-travel door.
- Exercise 2–8 players, bots, weapon sets, armour and every deathmatch scenario,
  especially the Living Daylights token and Golden Gun scenario.
- Verify original/expanded switching through voting, favorites, rotation and
  host migration, and verify original map behavior is unchanged.
- Traverse Horde's existing campaign maps to confirm no remaining access gap
  warrants a genuinely distinct expanded option.
- Measure Quest performance with the additional scenery visible.
