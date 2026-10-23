# All maps in Horde and Deathmatch (#219)

Horde's **Host > Match > Map** picker and Deathmatch's stage picker now share
all 26 existing GoldenEye gameplay maps. Archives remains the saved-setting
fallback for Horde. Every map name is available in the browser and pause UI,
including multiplayer-only maps which are not co-op missions.

Protocol **24** is required on every headset: earlier versions reject the new
stage set. Original Deathmatch indices 0–12 are preserved for favourites and
ballots; campaign-only stages are appended. Co-op retains its 20 mission list.

## Map checklist

All entries below are implemented in both pickers and accepted by network
validation. This is an implementation checklist; ROM/headset playtesting is
pending for every newly enabled map/mode combination.

| Map | Deathmatch setup | Horde setup |
| --- | --- | --- |
| Facility | Retail MP | Campaign |
| Complex | Retail MP | Retail MP + generated guard paths |
| Temple | Retail MP | Retail MP + generated guard paths |
| Stack | Retail MP | Retail MP + generated guard paths |
| Caverns | Retail MP | Campaign |
| Library | Retail MP | Retail MP + generated guard paths |
| Basement | Retail MP | Retail MP + generated guard paths |
| Caves | Retail MP | Retail MP + generated guard paths |
| Egypt / Egyptian | Retail MP | Campaign |
| Bunker II | Retail MP | Campaign |
| Archives | Retail MP | Campaign |
| Statue | Retail MP | Campaign |
| Cradle | Retail MP | Campaign |
| Dam | Generated from campaign | Campaign |
| Runway | Generated from campaign | Campaign |
| Surface I | Generated from campaign | Campaign |
| Bunker I | Generated from campaign | Campaign |
| Silo | Generated from campaign | Campaign |
| Frigate | Generated from campaign | Campaign |
| Surface II | Generated from campaign | Campaign |
| Streets | Generated from campaign | Campaign |
| Depot | Generated from campaign | Campaign |
| Train | Generated from campaign | Campaign |
| Jungle | Generated from campaign | Campaign |
| Control | Generated from campaign | Campaign |
| Aztec | Generated from campaign | Campaign |

## Setup adaptation

`gevr_arena.c` selects existing guard/navigation pads and spreads eight
Deathmatch starts with farthest-point sampling. Campaign-only Deathmatch maps
retain scenery and doors, gain 16 weapon/ammo pairs using the selected retail
weapon set, two armour pickups and a Flag Tag briefcase. Campaign inventory,
actors, objectives, script links and background AI are suppressed. Original
command indices are retained so scenery references remain valid. The original
multiplayer setups and co-op missions keep their own data and behavior.

Horde preserves a valid starting pad and selects guard/navigation pads throughout
the map. Multiplayer-only setups have no guard paths, so their floor graph is
converted into native waypoints/groups through shared edge midpoints. Guard AI
and replication continue through the existing code. Candidate wave/commerce
pads must connect to the starting area through both native guard paths and
floor collision after openable doors are held open. Closed-off spaces cannot
hold shops or initial enemy spawns. The established spawn safety, wave quota,
stalled-enemy replacement, economy, revive and retry rules still apply. Open
arenas allow visible spawns after five seconds without a successful hidden
spawn, retaining the player exclusion distance so a wave can continue.

## Validation

- `python3 port/tests/test_arena.py`: production setup generation for every
  campaign-only Deathmatch map; preservation of co-op; all six multiplayer-only
  Horde arenas; actual native path validation/routing over generated graphs;
  floor routing rejects disconnected floors and closed doors while bot routing
  continues to allow doors it can open.
- `python3 port/tests/test_horde.py`: director/economy and 45 simulated minutes
  of waves, guard-free setup fallback, inaccessible candidate rejection,
  network config round trips for all 26 maps, invalid-stage rejection,
  snapshots and initial/late player admission.
- Launcher layout and multiplayer core regressions cover the expanded catalogue
  and preservation of existing voting/favourites indices. All 35 native
  multiplayer core checks pass.
- `python3 port/tests/test_vr_display.py`: real settings reader/writer preserves
  every map in independent processes and rejects invalid IDs, for desktop and
  Quest defaults.
- Android `assembleDebug testDebugUnitTest`: debug APK builds; all 31 JVM tests
  pass. No APK installation or gameplay test was performed.

No ROM or headset is supplied in this environment. Before accepting the feature
in hardware, play Statue in Horde through several waves, defeat and retry,
then test each map solo and with two/four players. Check shop access, stairs,
guard pursuit, spawn visibility, joins, host loss, and frame/memory limits.
Exercise Deathmatch bots, team/scenario pickups, 2–8 player starts, rotation
and voting on the new campaign arenas. Large outdoor stages and the generated
guard graphs need measured Quest frame times and longer sessions.
