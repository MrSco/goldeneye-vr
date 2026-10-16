# Issue #189: endless co-op Horde mode

Design proposal, 2026-10-16, initially based on source at `6c87eb6`.
The first playable Archives implementation is now documented in
[Horde mode](horde-mode.md), including implemented scope and validation limits.
Economy, shops, special waves and more arenas below remain future milestones.

Source: [#189, Horde Mode](https://github.com/MrSco/goldeneye-vr/issues/189).
The request is: “Sprinting around Archives and fighting through enemies is fun
but it ends too quickly. Endless enemies or an actual horde mode would be
awesome.” The user clarified that Black Ops Zombies is a reference for the
wave-based game mode only. Horde uses regular GoldenEye enemies, movement and
combat. Zombie movement, melee behavior and presentation are outside the scope.
The remaining choices below are recommendations.

## Recommended experience

Add **Horde** beside Deathmatch and Co-op campaign. Support solo and 2–4 players,
starting with the **campaign Archives setup**, which has guard navigation.
Keep the action in a curated combat area, with several approaches and a loop
players can retreat around. Validate that loop on the headset before choosing
exact pads, locks or resupply positions.

The run is: prepare → survive a wave → resupply/upgrade → survive the next wave,
until everyone is down. Show the current wave, enemies remaining and next-wave
countdown on the wrist/watch and a brief announcement at transitions. Results
show the highest completed wave, time, kills and revives, with Retry and Lobby.
There is no mission exit, objective escort or campaign completion/save unlock.

Use regular armed GoldenEye guards with their existing navigation, movement,
animations, targeting and firearm combat. Assign suitable existing combat AI
lists so spawned guards engage the players without depending on campaign
scripts. Validate their behavior through Archives' doors and stairs.

Increase pressure through wave size, spawn cadence, approach directions and
guard weapon mix. Keep normal enemy movement and combat behavior throughout.
Prefer the existing health/damage rules initially; avoid enemies that simply
absorb whole magazines. Special waves every fifth wave can introduce a curated
guard loadout once the basic loop is enjoyable.

## First playable scope

- One arena: Archives. Original GoldenEye ROM first; other campaigns require
  separate arena profiles and validation.
- Solo or up to four human players, no friendly AI, friendly fire off by default.
- Regular armed guards using existing GoldenEye movement and combat AI.
- A common starting pistol and limited ammo; fixed resupply points and guaranteed
  ammo between waves. Do not depend on random drops for a viable run.
- A short preparation period, then paced spawning with a finite wave quota.
  A wave ends only when its quota has spawned and its living enemies are gone.
- Reuse the existing three-second proximity revive and half-health recovery.
  Keep the initial version's existing no-bleedout behavior; solo down ends the
  run. Test and improve the revive feedback and wall/floor checks.
- Endless wave progression with bounded concurrent actors, objects and effects.
- Newly joining humans spectate until the next preparation phase. Freeze that
  wave's player-count scaling at its start; leaving must not strand a run.
- Clear results/retry flow, separate from the campaign debrief and saves.

Prototype tuning only: 15-second breaks; wave quota
`ceil((6 + 2 * (wave - 1)) * (1 + 0.5 * (players - 1)))`; start with a concurrent
enemy target of 8 solo, 12 with two players, 16 with three/four. These are
experiment values, **not measured Quest limits**. Measure wave duration and
flatten late-wave quota growth if waves become chores; keep late pressure in
mix/cadence. Bound counters and saturate tuning arithmetic so “endless” cannot
overflow or produce invalid timings. Keep spawning paced even below the cap.

## Progression and ideas worth adding

After the first playable loop, add a small between-wave upgrade station:
ammo, armour and weapon tiers, then a limited selection of perks. Use a shared
team supply pool, earning most supplies from wave completion and a smaller
amount from kills; revives also contribute. This avoids forcing teammates to
compete for the last hit. Exact prices and rewards need playtesting.

Good GoldenEye-specific options are timed access to a Golden Gun, a safe-room
resupply terminal, or a radio that summons a supply wave. Later, unlock another
arena section as the team advances, creating a useful decision between
upgrades and more movement space. A random weapon crate and elite/boss waves
are later additions. Keep the first release small enough to balance.

Use existing ROM guard models, weapons, sounds and animations. No new enemy
presentation is required for Horde. Additional maps and loadout combinations
can expand variety after the first arena is balanced.

## Existing systems and constraints

| System | Evidence in current source | Implication |
| --- | --- | --- |
| Co-op guards | `port/src/net/net_coop.c`: host runs AI, changed guard states at 10 Hz, reliable spawn/removal, hit reports and target selection | Reuse guard replication and damage routing; enemies do not occupy human player slots. |
| Spawn API | `src/game/chraction.c`: `chrSpawnAtCoord` / `chrSpawnAtPad` | Use validated pads and native floor/collision checks; a failed spawn must not consume the wave quota. |
| Actor allocation | `src/game/initguards.c`: `g_NumChrSlots = count + 10`; spawn API requires at least three free slots | Reserve Horde capacity explicitly at stage load on every headset, including room for dying actors and player bodies. The network's 512-slot arrays are not an available-enemy budget. |
| Revive | `net_coop.c`: downed players ignored by guards; nearby teammate for three seconds; all-down ends mission | Share the player-state behavior, but route defeat to Horde results. Validate proximity through walls and floors. |
| Guard combat AI | `src/game/chraidata.c`: persistent chase-and-attack list; `gevrCoopGuardTarget` chooses living players | Reuse existing navigation, movement, target selection and firearm attacks. Validate suitable existing AI lists for wave-spawned guards. |
| Fast reinforcements | `docs/body-retention-reinforcements.md` | Amplifies eligible mission scripts. It supplies neither quotas nor phase control and is not the wave director. |
| Corpses | Animated retention in `chraction.c`; independent physics snapshots in `port/src/gevr_ragdoll.c` | Dead actors can occupy slots until cleanup. Ragdolls release actor dependence but still consume rendering/physics/memory budgets. |
| Session rules | `net_match.h`, `net_core.c`, `net_protocol.h` | Current modes are only deathmatch/co-op. Co-op predicates also enable campaign menus, objectives, ends and saving; a new mode requires auditing these branches. |
| Map data | `src/game/prop.c`, `port/src/gevr_setup.c`, `assets/obseg/setup/readme.md` | Mission geometry/navigation and mission scripting load together. Horde must selectively replace mission logic while preserving needed doors, collision and navigation. |

`STATUS.md` and protocol headers are current (protocol 21). Some explanatory
sections in `MULTIPLAYER.md` still describe older releases. Existing multi-headset
co-op, bots and heavy-body performance also have outstanding acceptance checks.
Reuse reduces implementation work, not the need to validate those paths.

## Implementation architecture

1. **Separate mode and reusable capabilities.** Add `NET_MODE_HORDE` and explicit
   helpers for campaign sessions versus host-controlled PvE actors. Share the
   guard, revive, movement and voice mechanisms without teaching every campaign
   branch that Horde is a mission. Solo uses the same director with local
   authority; no dependency on a second player or a published lobby. Make solo
   pause freeze its gameplay clock, while co-op menus keep the world running.
2. **Arena profile and runtime setup.** Add an Archives profile containing start
   pads, candidate enemy pads, resupply anchors and supported doors/areas.
   At stage load, suppress original guards, mission background scripts,
   objectives, cinemas and exit triggers; retain required world props and
   navigation. Use focused runtime setup handling rather than modifying the
   player's ROM. Load the same profile and actor capacity on every headset.
   Disable campaign reinforcements in Horde to prevent untracked spawns.
3. **Director separated from adapters.** Proposed `src/game/gevr_horde.c/.h`
   owns PREPARE, WAVE, INTERMISSION and DEFEAT, quotas, tuning and seeded spawn
   choices. Keep transition logic testable without a renderer. Tick once on the
   gameplay clock, never once per eye or remote view. The adapter owns actual
   enemy allocation, AI, pickups and UI. Pausing for loading/migration advances neither
   the countdown nor spawn debt. Do not reload the map between waves.
4. **Reliable actor lifecycle.** Each Horde enemy has a run ID and spawn serial
   associated with its actor slot/generation. Count death once, separately from
   corpse removal; give no kill reward for despawn. Retry blocked/full spawns
   with backoff and alternate pads. Exclude player proximity and visible unsafe
   pads for every living player, and validate a route to the active combat area.
   A stalled enemy gets retarget/path recovery, then an out-of-view replacement
   under the same unresolved wave obligation, with no reward duplication.
5. **Networking adapter.** Proposed `port/src/net/net_horde.c/.h` transmits
   authoritative phase, wave, remaining quota/alive count, revision and remaining
   phase time. Send reliable transitions plus periodic full state. Late joins
   and host migration need director state, RNG state, enemy identities, run
   statistics and (when added) economy/transaction state alongside the existing
   world/guard snapshot. Reserve reliable final enemy setup before announcing a
   spawn; the existing spawn hook fires during allocation, before callers finish
   configuration. Reuse existing AI list IDs and the existing guard firearm
   damage route. Final guard loadouts must agree across headsets.
6. **Stale-state protection.** Audit the guard transport under rapid slot reuse:
   unreliable old states can arrive after a reliable replacement spawn. Ensure
   generation identity is checked for states, hit reports, removals and roster
   repair; existing slot numbers alone must not identify a new wave's enemy.
   On migration, rebind identities through the guard slot remap, reconcile alive
   counts from the adopted roster and resume only after a coherent snapshot.
7. **Mode negotiation and UI.** Update config validation/serialization, host and
   join UI, player limits, one-player start readiness, stage loading, spectators,
   results, persistence and LAN/internet listings. Existing co-op discovery uses
   a stage-byte flag; do not quietly overload it with Horde. Plan a protocol
   change (next available version, currently 22) and capability/minimum-version
   negotiation so older builds cannot join a mode they interpret as campaign or
   deathmatch. Check whether the separately hosted lobby service needs changes.

For a later shop, clients request purchases; the host validates phase, cost and
inventory and returns an idempotent grant. Snapshot acknowledged transaction IDs
so retries, reconnections and migration cannot duplicate supplies or purchases.

## Build order and acceptance

| Milestone | Deliverable | Exit check |
| --- | --- | --- |
| 1. Guard spawning/navigation prototype | Spawn regular armed guards in isolated Archives using existing combat AI, movement and loadouts | Guards cross the intended doors/stairs, engage either teammate and use their normal firearm combat. Confirm reliable loadouts and cleanup through repeated spawn/death cycles. |
| 2. Solo vertical slice | Isolated Archives setup, director, bounded spawning/cleanup, ammo breaks, wave HUD and defeat/retry | Play ten waves; exhaust ammo; block spawn pads; force slot pressure; kill the final enemies simultaneously; pause/retry repeatedly without mission exits or campaign save writes. |
| 3. Co-op slice | Same loop with 2–4 players, guard replication, revive, readiness, late join and director snapshots/migration | Two-headset run with host/client kills and revives; join between waves; host leaves in each phase; repeat with four players and verify equal wave/enemy totals. |
| 4. Sustained-load gate | Tune actor/prop/effect limits and spawn pacing on Quest 2 and Quest 3, including HD/Bean and optional ragdolls | At least 45 minutes at intended load: frame time, memory, actor slots, dropped objects, network upload and pending-spawn queues remain bounded. Set release caps from results. |
| 5. Progression and variety | Team supplies, small upgrade menu, guard weapon mix and one special wave | Purchases grant once, ammo remains viable, solo/co-op difficulty is reasonable and no runaway health/price scaling. |
| 6. More arenas | Curated Facility or Bunker profile with existing guards and weapons | Repeat route, spawn, mission-isolation and performance acceptance per arena. |

Add focused native checks when implementing: phase transitions with a fake
clock, failed-spawn accounting, death/removal idempotency, arithmetic bounds,
snapshot round trips, stale packets, join/migration recovery and purchase retries.
Extend the production-path co-op spawn fixtures for slot reuse and wave actor
configuration. Run relevant existing reinforcement, co-op start/spawn/item,
protocol and ragdoll regression checks for the code touched. Hardware tests are
required for guard navigation, gunfight balance, revive fairness and performance.

Recommended next engineering step: milestone 1, followed by a small Archives
solo slice. Prove regular guard spawning, combat and cleanup, then build the
endless-wave director around those existing systems before adding the full shop
or multiple arenas.
