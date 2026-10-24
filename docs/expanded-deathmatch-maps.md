# Expanded campaign layouts for deathmatch: investigation

Investigated 2026-10-24. This is a feasibility report; no gameplay changes have
been made or verified on a headset.

## Answer to the suggestion

Deathmatch currently uses the original multiplayer setups. Selecting Facility
does **not** select the single-player setup or automatically open its laboratory
and bottling/tank areas. Separate original and expanded entries are a feasible
direction, with Facility as the first candidate. Access to every requested area
still needs a ROM/setup inspection and an in-game traversal check.

Suggested player-facing names: **Facility** and **Facility (Expanded)**.
“Expanded” describes a deathmatch adaptation without suggesting that selecting
it starts the single-player mission.

## What the code establishes

- `port/src/net/net_match.c`, `s_stages`: deathmatch offers 13 entries,
  including Facility (LEVELID 34), Bunker II, Archives and Caverns. There are
  no expanded variants. Statue and Cradle use the ROM's cut multiplayer setups.
- `src/game/prop.c`, `proplvreset2`: when `gevrMpRules()` is true, the loader
  inserts `mp_` into the setup filename. Facility therefore loads
  `Ump_setuparkZ` rather than `UsetuparkZ`.
- `src/game/player.c`, `gevrMpRules`: multiplayer rules apply with two or more
  players outside the co-op/PvE rules path. Replacing this predicate to obtain
  a campaign setup would also change other gameplay behavior; setup selection
  needs its own decision.
- `src/game/bg.c`, `levelinfotable`: Facility has one background/standing-tile
  resource pair (`bg/bg_ark_all_p.seg`, `Tbg_ark_all_p_stanZ`). The setup loader
  separately selects campaign or multiplayer props. The code therefore supports
  reusing existing world resources rather than importing a new map, but does
  not prove which specific doors/blockers need changes.
- `src/game/gevr_horde.c`, `gevrHordePrepareSetup`/`gevrHordeSkipProp`, and
  `src/game/prop.c` already demonstrate adapting campaign setups: replacing the
  intro, filtering mission entities and clearing door key flags for Horde.
  These are useful patterns, not a ready-made deathmatch conversion. Padlocks,
  script-controlled doors and other blockers still need individual review.
- `src/game/bondview_r.c`, `gevrAddOnlineStartPads`: existing extra-player
  support adds starts at suitable ammo spots. It does not distribute a complete
  set of deathmatch spawns and pickups across newly opened campaign rooms.
- `port/src/net/net_protocol.h`, `NetMatchConfig.stage`, and
  `port/src/net/net_match.c`, `netStageIndexOf`: a match identifies its map by
  LEVELID. Two rows with LEVELID 34 would be indistinguishable on the network
  and lookup would always find the first row. A distinct layout identity is
  required, together with validation and protocol compatibility updates.
- Favorites and ballots use stage-list positions. New entries should be
  appended, preserving existing indices; lobby names, voting, rotation and
  saved preferences must carry the chosen variant consistently.

## Recommended implementation scope

Start with Facility only. Preserve the existing Facility entry and append an
expanded variant, identified separately from its underlying engine LEVELID.
Keep deathmatch rules and explicitly select or construct the expanded setup.

Inspect the campaign and multiplayer setup records before choosing between a
campaign-derived setup and targeted changes to the multiplayer setup. Retain
the necessary doors, glass and scenery; remove guards, mission AI, objectives,
cutscenes, mission exits and mission-only hazards/interactions. Make routes
through the laboratory and tank areas usable without mission progression.
Provide deliberate deathmatch spawn, weapon, ammo and armour placements across
the expanded layout, retaining selected weapon-set behavior.

Review Bunker II, Archives and Caverns individually afterward. Their shared
campaign origins do not establish that the same conversion will work for each.

## Acceptance checks before claiming full access

1. Traverse both directions between Facility's original arena, laboratory and
   tank/bottling areas without keys, guards or mission scripts.
2. Verify doors, switches, padlocks, collision and room visibility on host and
   client, including after a late join and after respawning.
3. Verify 2–8 players spawn on valid floors and respawn throughout the layout;
   weapon sets and pickups work in the additional rooms.
4. Verify deathmatch scenarios and bots in the added rooms, with no mission
   completion/failure or cutscene triggers ending or interrupting the match.
5. Verify the two variants survive lobby selection, joining, voting, favorites
   and rotation as distinct choices; incompatible builds cannot misload them.
6. Compare the original Facility against its current behavior and measure
   headset performance in the expanded layout.

No ROM image was present in the workspace, so this investigation could not
compare the actual setup records or verify physical access in-game. Source
inspection establishes the current selection behavior and the integration
requirements, not a playable expanded map.
