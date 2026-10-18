# Horde mode (#189)

First playable implementation, not yet headset-accepted. Horde uses ordinary
armed GoldenEye guards and their existing movement, animations and gun combat.
The wave structure is the Zombies reference; enemy behavior stays GoldenEye's.

## Playing

Choose **Multiplayer → Host → Lobby → Mode: Horde**, then **Start Hosting** and
**Launch**. You can launch alone, or with up to four humans. LAN/direct-IP
hosting uses the existing transport; an internet lobby is optional for LAN play.
The host picks the map (Archives, Bunker I, Bunker II, Control or Facility, from
the original GoldenEye campaign) and the starting wave (1-50) in the Horde
rules column; Agent difficulty. A later start brings the points the skipped
waves would have paid (500 + 400 per skipped wave). The launcher disables
multiplayer for other campaigns.

- Friendly fire defaults off and has its own saved Horde preference.
- Start with a PP7, 42 starting rounds and **500 points (credits)**. Preparation and each wave break last 15 seconds.
- Eliminate the whole wave, including enemies still queued to spawn, to advance.
- Every door the level lets open is open and stays open; locked and padlocked
  doors stay shut. Wall buys and the box stand clear of doors.
- **Special rounds:** waves 5, 15, 25... are an **Oddjob swarm** (half again as
  many, small, fast, weak, pistols); waves 10, 20... are **boss rounds** (one
  to three of Jaws, Xenia, Oddjob, Trevelyan and Ourumov, ten times a guard's
  health and more per player, AR33 or RC-P90).
- **Enemy kinds:** from wave 4 about a quarter are armoured Janus marines
  (175% health), from wave 8 about a fifth Janus special forces (150%); both
  carry the next gun up. Guards run 2% faster each wave (up to 140%) and their
  accuracy starts at 1.0 and climbs 2% a wave.
- **Two guns:** you carry two (the PP7 is the first). A third gun from a wall
  buy or the box replaces the one in your hand, or the older one; the prompt
  says which (`... FOR PP7`). The Moonraker Laser has a 20-shot pool (two on
  pickup, ten more per crate).
- **Call of Duty Point System (per-player credits):**
  - **Non-lethal hit:** `+10 PTS`
  - **Standard body/limb kill:** `+60 PTS`
  - **Critical headshot kill (`HIT_HEAD`):** `+100 PTS`
  - A kill no player is known to have caused (no player hit the guard) pays nobody.
- **Call of Duty Mystery Box (`950 PTS`):**
  - Located at one of four candidate box pads in the arena, shown by an ammo crate that moves with it. Stand near the crate and press **B / Reload-Activate**, or reach a hand to it and press **Grip**, to spend `950 PTS`. A grip away from a station (a holster, a pickup, a door) buys nothing.
  - Cycles through 12 GoldenEye weapons (Cougar Magnum, ZMG 9mm, Phantom, RC-P90, Auto Shotgun, Sniper Rifle, Silver PP7, Gold PP7, Moonraker Laser, Golden Gun, Grenade Launcher, Rocket Launcher) for 3 seconds, then presents the rolled weapon for 8 seconds for the buyer to claim.
  - **Teddy Bear relocation:** After 4+ uses at a location, rolls have a 20% chance (guaranteed by the 10th use) to trigger a Teddy Bear relocation, refunding `950 PTS` to the buyer and moving the Mystery Box to another room's box location.
- **Wall Buys:** Six stations sell weapons or full Body Armor: Klobb
  `500 PTS`, Shotgun `750 PTS`, KF7 Soviet `1200 PTS`, D5K Deutsche
  `1400 PTS`, AR33 Assault Rifle `1800 PTS`, and Body Armor `1500 PTS`.
  A newly acquired gun comes with one magazine, added to any rounds you hold. Owned weapons cannot be
  repurchased for ammo, and rolling an owned Mystery Box gun gives no ammo.
- **Enemy ammo drops:** Each actual enemy death drops one shared crate. Walk
  over it to collect ammo for each ammo pool used by your owned weapons, once
  per pool: 16 pistol/SMG rounds, 12 rifle rounds, 4 shells or 3 magnum rounds.
  Special ammo grants one round per four crates collected. A crate goes to
  one player; both players cannot claim it. The host validates distance,
  doors and floor connectivity. At most 24 crates exist, expiring after 60
  gameplay seconds; old crates give way to new ones. There is no automatic
  intermission resupply or purchasable ammo refill. These quantities are
  initial tuning values pending multiplayer balance testing.
- **Guard Health & Weapon Progression:**
  - Guard health (`chr->maxdamage`) scales by `+15%` per wave from `100%` on Wave 1 up to `400%` on Wave 21+.
  - Guard loadouts progress across waves from DD44 Dostovei (Waves 1–2) to Klobb (Waves 3–4), D5K Deutsche (Waves 5–6), KF7 Soviet (Waves 7–9), AR33 (Waves 10–12) and RC-P90 (Waves 13+). Guards' guns stay with their bodies so dropped weapons cannot accumulate.
- Stand beside a downed teammate for three seconds to revive them at half
  health. Horde checks intervening scenery/doors and floor height. Everyone
  down ends the run; a solo down ends it immediately.
- A HUD message announces each wave with its enemy count, player points and defeat;
  during a break the countdown stays on screen until it ends, and standing near
  the Mystery Box or a Wall Buy displays a live interaction prompt (during a
  break it carries the countdown too). Each Wall Buy is marked by its gun,
  enlarged and turning above its pad; body armour by a vest. Station models
  cannot be picked up.
  The pause menu shows points, cleared waves, time, total kills and revives. Once the
  run ends, the host can select **Retry Horde**. **Leave party** returns to the
  launcher. Campaign saves, completion times and unlocks are not awarded.
- Players in the launcher party enter the initial preparation together; its
  countdown waits for their maps to load. Arrivals during preparation or a wave
  break can play immediately after loading. A combat-time arrival watches until
  the next break (including one whose load finishes during that break). The
  wave's player scaling is fixed when that wave begins. A player leaving does
  not reset the run.
- A solo host's pause menu freezes gameplay. With teammates, gameplay continues
  while a player has their menu open. Defeat freezes gameplay for everyone.

All participants must use this build: protocol **23** includes shared ammo drops, guard generations and
the optional AI ally, excluding protocol 22 and older builds. The companion
follows the party without counting as a wave enemy or a human survivor; see
[AI ally](ai-ally.md). The app release number has
not been bumped or published by this change.

## Implementation

`src/game/gevr_horde.c` records Archives' existing guard pads and the most common
guard body from the player's ROM. It replaces the intro inventory/camera setup,
skips mission actors/objectives and disables background mission AI. Doors,
scenery and navigation remain; doors' key requirements are cleared, and on the
first tick every door that can open is opened and held open (`hordeOpenDoors`).
Stations skip pads within 320 units of a door.

Wave guards use the existing persistent chase-and-attack AI. Spawn candidates
must be away from every living player, outside direct sight on the same height,
and connected to a player's navigation waypoint. Native spawn collision checks
still apply. Failed spawns retry without spending the wave quota. Guards request nearby blocking doors once a second. Guards stalled
out of sight get a path retry after six seconds and a replacement after
eighteen seconds without a kill reward.

The director advances on the gameplay clock once per simulation tick. Initial
concurrency targets are 8/12/16 for one/two/three-or-four players. Actor storage
is reserved at stage load; dying actors have separate cleanup time. Wave quotas
flatten after wave 40 and arithmetic is bounded. These caps need device tuning.
Ragdoll snapshots keep their separate cosmetic limits and settings. With
ragdolls on, Bodies stay is local to each headset, including Horde: the host
cannot force another client's count. With ragdolls off, animated body retention
is disabled online; Horde's dead actor cleanup runs separately after five seconds.
Snapshots skip duplicate native pose/hit-list work and cull against the current
render view. Forty-eight detailed physics bodies still require Quest frame-time
measurement; see [playtest fixes](horde-playtest-fixes.md).

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
  spawns, ammo drops/defeat, snapshots, malformed/old-epoch rejection, guard
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
Quest 2/3 with optional HD/Bean/ragdoll settings. The shop and team
currency from the design remain future milestones.
