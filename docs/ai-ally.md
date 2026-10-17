# AI ally: solo, campaign co-op and Horde (#194)

Enable **AI ally (Natalya)** in Play > Game rules before starting. The same
preference also appears in Multiplayer > Host > Match for Co-op and Horde;
weapon and durability choices are on Game rules. Off by default. One companion
joins the whole party, including solo Horde from PR #191. Supported in the
original GoldenEye campaign, in VR or flat presentation. Deathmatch, offline
split-screen, title/ending stages and hack campaigns ignore it.

The host alone chooses settings and runs the companion. Clients display the
normal replicated NPC; their saved preferences cannot spawn or tune one.
Settings are captured on mission load or Horde retry. Changes made during play
apply to the next load. A new host inherits the current run's companion and
settings, including the fact that she has already spawned if her body is gone.

Choose PP7 (default), silenced PP7, DD44, KF7, D5K, ZMG, AR33, RC-P90,
shotgun or automatic shotgun. Cautious aggression engages alerted enemies within
600 game units; normal engages within 1200, aggressive within 1800. She follows
the nearest living teammate, excluding downed players, disconnected slots and
spectators, and returns when more than 1000 units away. A downed host does not
stop her following a living teammate. Damage is 25–400%; health is 25–1000%
(100% = 40 NPC damage units). Invincibility uses the native NPC flag.

She uses NPC paths, doors, collision, animation and shooting. Her shots cannot
hurt players or friendly NPCs. Players can hurt her unless invincibility is on,
as with the original campaign ally. She spawns once after the intro, retries
an obstructed/full spawn, and does not respawn at wave breaks or after death.
A mission reload or Horde retry creates a new companion. She pauses during
scripted cinematics and stops firing immediately when Horde ends.

## Stages with an existing Natalya

Jungle keeps its original scripted Natalya: with the option on, there are two
Natalyas. The additional companion and the mission NPC ignore one another as
targets. Only the additional companion gets these weapon/durability settings.
Bunker II, Train and Control likewise retain their own Natalya and objectives.
The companion does not substitute for an escort objective. No mission character
IDs, objective bits or imported ROM action blocks are used.

## Co-op and Horde synchronization

The controller is in `src/game/gevr_ally.c`, reset from `initguards.c`, spawned
from `chrpropTick`, and called in place of script AI for this NPC only. It uses
the NPC pathfinder without the enemy crowd limit, so Fast reinforcements cannot
block following. The new NPC uses an allocated ID and checks it before reusing
its pointer, preventing a recycled corpse slot from becoming a companion.

The normal reliable NPC spawn/roster records mark the companion and carry its
host configuration atomically, so a client elected before the first periodic
AI snapshot can still restore health, invincibility and damage correctly. On takeover her action
starts afresh, preserving health, instead of running unreplicated puppet path
or attack internals.
`port/src/net/net_ally.c` also sends reliable epoch/revision snapshots for the
configuration and spawn lifecycle, including when the companion's body is gone.
Late joins receive the same settings; malformed, stale and previous-run snapshots
are rejected. Repeated roster records preserve the actor rather than duplicate it.

Horde assigns the companion a reserved network generation while leaving its
enemy ID zero. She never contributes to enemy quotas, kills, outstanding wave
counts, human player scaling or revive eligibility. All humans down still ends
the run even if the companion is alive/invincible. Her Horde gun is undroppable,
and retry resets the companion with the run. Enemy generation allocation skips
the reserved identity.

Protocol **22** (unreleased until v0.5.4, which also carries Horde and the
tracked multiplayer arms) includes the companion's spawn configuration and
lifecycle messages. Every headset must use v0.5.4; protocol 21 builds cannot
join. No lobby-service deployment is needed.

## Validation and remaining playtests

- `python3 port/tests/test_ally.py`: production controller, solo/co-op/Horde
  authority, nearest living teammate, downed/spectating host, settings, takeover,
  no replacement life, retry, target filtering, following and damage isolation.
- `python3 port/tests/test_ally_network.py`: production transport, host settings,
  late join, lifecycle, takeover revisions and malformed/stale/epoch rejection.
- `python3 port/tests/test_coop_spawns.py`: real spawn/roster packets, companion
  configuration on three clients with different slot maps, late join, duplicates,
  Horde identity isolation and existing 40-guard reinforcement replication.
- `python3 port/tests/test_horde.py`: director/runtime/network, 45 simulated
  minutes with bounded/recycled actors, companion exclusion from wave accounting,
  takeover quota reconciliation and all-down defeat while an ally exists.
- `python3 port/tests/test_ally_ui.py` and `test_launcher_ui.py`: production
  settings round trip/bounds, host/client/deathmatch/hack restrictions and layout.
- Existing campaign item, co-op start/intro/debrief/item and reinforcement checks.
  Linux uses a temporary system-header compiler wrapper for the existing shims.
- Android build/JVM command: `gradle --init-script /workspace/.gevr/android-init.gradle
  --no-daemon --max-workers=4 --console=plain assembleDebug testDebugUnitTest`.

Quest play is still required: two/four headsets, client guns/appearance, late
join before and after her death, host loss, downed host following, all-down
Horde defeat and retry, weapon choices and durability, Jungle with both Natalyas,
Bunker II/Train/Control scripts, civilians, doors/stairs/narrow passages and
sustained Horde performance. No ROM or Quest is attached to this environment;
synthetic checks do not establish headset behavior or mission completion.
