# Full Deathmatch maps

Seven Deathmatch maps exist twice: as the cartridge's multiplayer map and as
the whole mission level, listed as "(Full)". PR #224 ("expanded" layouts)
proposed them first; v0.5.14 builds them on the campaign arena code of #220
instead of a second setup builder, and carries over #224's map identity on
rejoin, its active-match limit, its decomp record check and this document's
comparison.

## Map choices

| Choice | Stage code | LEVELID | Campaign / MP door records |
| --- | --- | --- | --- |
| Facility (Full) | 98 | 34 | 46 / 29 |
| Caverns (Full) | 103 | 39 | 25 / 17 |
| Egypt (Full) | 96 | 32 | 25 / 6 |
| Bunker II (Full) | 91 | 27 | 38 / 36 |
| Archives (Full) | 88 | 24 | 35 / 10 |
| Statue (Full) | 86 | 22 | 2 / 2 |
| Cradle (Full) | 105 | 41 | 5 / 5 |

A stage code is the LEVELID with `NET_STAGE_FULL` (0x40) set; every LEVELID in
the list is below it, and the lobby's Horde (0x7f) and co-op (0x80) markers stay
clear of it. Door counts are from the n64decomp/007 setup records (#224);
they show setup differences, not playable area. Statue's multiplayer gates
carry key flags the campaign setup has not; Cradle's multiplayer setup has a
locked door and different scenery. The other five open real space.

The entries follow the 26 existing ones, so saved favourites and ballots keep
their positions. Horde lists only the 26: it already plays the campaign setup
of every mission and holds its doors open.

## Implementation

- `net_match.c`: the seven entries, `netStageIsFull`, `netStageLevelId`,
  `netHordeStageCount`. Favourites are 64 bits (`MpFavStages`), ballots and
  the rotation hold 64 maps.
- `net_core.c`: `netRoundLoadStage` and `g_StageNum` load the LEVELID;
  `netRoundFullMap()` is true only while a match loads or runs (#224), and a
  headset back from a host migration reuses its world only if it loaded the
  round's stage code, as Facility and Facility (Full) are both LEVELID 34
  (#224's `s_loaded_map`).
- `prop.c` loads `Usetup*Z` for a "(Full)" round; `bgfog.c` uses the mission's
  fog; `gevr_arena.c` treats it as a campaign Deathmatch arena: Dab's key rule
  (a door the level's own keys open opens for everyone, a door no key opens
  stays shut), the per-level door holds, every one-way door working both ways,
  eight starts and sixteen weapon and ammo stations by farthest-point sampling
  of guard and path pads, topped up from other floor pads below eight.
- #224's composer cleared every key and door link instead; the batch keeps
  Dab's rule, which the 13 campaign arenas already use and testers checked.

## Validation

```sh
GEVR_TEST_SANITIZE=1 GEVR_DECOMP_ROOT=/path/to/007 python port/tests/test_arena.py
```

With `GEVR_DECOMP_ROOT` set, `full_map_records.py` (#224's reader) turns each
level's setup source into cartridge records, which go through the game's own
converter and arena code under AddressSanitizer and UndefinedBehaviorSanitizer.
That run found the arena code reading word-aligned records through host
structs (fixed with aligned copies) and the reader ending Statue's pad list at
a pad named `""`. Floors are stubs: this checks the records, not where a
player can walk. `test_multiplayer.py` covers stage codes, the rotation past
32 maps, the active-match limit and rejoining across a layout change.

## Headset acceptance

- Walk each level's extra areas both ways; Facility's labs and bottling room
  first. Doors that stay shut in the mission stay shut.
- Starts and pickups clear of scenery, after respawns and a late join.
- 2-8 players, bots, every scenario (Flag Tag's briefcase, the Golden Gun).
- Switching between a map and its "(Full)" entry by vote, favourites,
  rotation and host migration; the plain maps unchanged.
- Frame rate with the extra scenery in view.
