# Horde mode (#189)

First playable implementation, not yet headset-accepted. Horde uses ordinary
armed GoldenEye guards and their existing movement, animations and gun combat.
The wave structure is the Zombies reference; enemy behavior stays GoldenEye's.

## Playing

Choose **Multiplayer → Host → Lobby → Mode: Horde**, then **Start Hosting** and
**Launch**. You can launch alone, or with up to four humans. LAN/direct-IP
hosting uses the existing transport; an internet lobby is optional for LAN play.
Horde currently uses the original GoldenEye campaign Archives map, on Agent
difficulty. The launcher disables multiplayer for other campaigns.

- Friendly fire defaults off and has its own saved Horde preference.
- Start with a PP7. Preparation and each wave break last 15 seconds.
- Eliminate the whole wave, including enemies still queued to spawn, to advance.
- Ammo is supplied automatically during each break. The Klobb becomes available
  after clearing wave 2, and the KF7 after wave 4. These are shared progression
  thresholds; each active player's inventory and ammunition are their own.
- Guards start with DD44s, use Klobbs from wave 3 and KF7s from wave 6. Enemy
  numbers and cadence increase; movement, health and damage use normal Agent
  rules. Guards' guns stay with their bodies so dropped weapons cannot accumulate.
- Stand beside a downed teammate for three seconds to revive them at half
  health. Horde checks intervening scenery/doors and floor height. Everyone
  down ends the run; a solo down ends it immediately.
- The HUD reports the wave, remaining enemies and preparation countdown.
  The pause menu shows cleared waves, time, total kills and revives. Once the
  run ends, the host can select **Retry Horde**. **Leave party** returns to the
  launcher. Campaign saves, completion times and unlocks are not awarded.
- A late join watches until the next wave break. The wave's player scaling is
  fixed when that wave begins. A player leaving does not reset the run.
- A solo host's pause menu freezes gameplay. With teammates, gameplay continues
  while a player has their menu open. Defeat freezes gameplay for everyone.

All participants must use this build: protocol **22** changes guard packet
layouts and excludes protocol 21 and older builds. The app release number has
not been bumped or published by this change.

## Implementation

`src/game/gevr_horde.c` records Archives' existing guard pads and the most common
guard body from the player's ROM. It replaces the intro inventory/camera setup,
skips mission actors/objectives and disables background mission AI. Doors,
scenery and navigation remain; doors' key requirements are cleared.

Wave guards use the existing persistent chase-and-attack AI. Spawn candidates
must be away from every living player, outside direct sight on the same height,
and connected to a player's navigation waypoint. Native spawn collision checks
still apply. Failed spawns retry without spending the wave quota. Guards stalled
out of sight get a path retry and then a replacement without a kill reward.

The director advances on the gameplay clock once per simulation tick. Initial
concurrency targets are 8/12/16 for one/two/three-or-four players. Actor storage
is reserved at stage load; dying actors have separate cleanup time. Wave quotas
flatten after wave 40 and arithmetic is bounded. These caps need device tuning.
Ragdoll snapshots keep their existing separate cosmetic limits and settings.

The host owns phases, spawning, damage resolution and run statistics. Regular
co-op guard replication, hit routing and downed-player behavior are reused;
campaign menus and mission endings remain separate. Horde snapshots include the
run epoch, revision, phase clock, quota, RNG, next enemy ID and statistics.
Each guard's network token combines the run epoch and spawn generation. Spawn,
state, hit, removal and AI messages carry that identity, preventing an old
slot's traffic from affecting its replacement. Pending spawns can be cancelled
by removal before a client has a free actor slot.

Late joins receive the director and guard roster. Host takeover retains local
enemy identities through the existing slot remap and reconciles outstanding
quota against adopted actors. A run retry advances the world epoch. Mode
discovery uses a distinct listing value, 127, rather than a campaign flag.
The lobby service's `create` validation accepts stage bytes 0–255 and filters
joins by protocol, so this first playable mode needs no service deployment.

## Validation

- Android `assembleDebug` and 30 JVM unit tests passed during implementation.
- `python3 port/tests/test_horde.py`: production director, production adapter
  against a synthetic map/world, 45 simulated minutes, slot exhaustion,
  repeated death accounting, despawn replacement, safe pads, finalized armed
  spawns, resupply/defeat, snapshots, malformed/old-epoch rejection, guard
  identities, seeded continuation and mode validation.
- `python3 port/tests/test_coop_spawns.py`: existing 40-guard reinforcement burst
  across three clients, plus Horde slot reuse, stale state rejection, duplicate
  roster handling and cancelled pending generations.
- Existing launcher layout, co-op start, intro, debrief, item and reinforcement
  regressions, four guard-weapon lifecycle checks and 33 production multiplayer
  core checks passed, including solo
  Horde readiness, PvE rules and prevention of deathmatch resets. Linux
  execution of the Windows-oriented core, item and
  reinforcement harnesses used a temporary compiler wrapper for system headers,
  pointer-conversion warnings and `libm`; production/test sources were retained.

No ROM or Quest is attached to this environment. This synthetic run does not
measure headset performance or establish that every Archives route feels good.
Before merging/releasing, play solo and two/four headsets: check starts, doors,
stairways, spawn visibility, both players' gun damage, revives, join/admission,
host loss in each phase, defeat/retry, and at least 45 minutes of combat on
Quest 2/3 with optional HD/Bean/ragdoll settings. The shop, team currency,
special waves and additional arenas from the design remain future milestones.
