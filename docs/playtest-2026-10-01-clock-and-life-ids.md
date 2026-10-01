# v0.3.7: clock synchronization and combat life IDs

Implemented in the existing playtest-logging-feedback-731f59 worktree. Version remains
0.3.7 (versionCode 46); network protocol advances from 14 to 15 with user authorization.
Everyone playing together must use this matching build. No push, deployment, upload,
headset installation or debug APK was performed. Earlier APKs and symbols are preserved.

## Behavior

- Host sends authenticated four-timestamp clock probes. It computes each client's
  monotonic clock offset and network RTT, excluding client reply processing time.
  A fixed ring of five samples selects the freshest minimum-RTT estimate. Samples
  expire after 10 seconds; exchanges longer than one second are invalid. Probes
  normally refresh every two seconds, retrying sooner when unsynchronized without
  overwriting an outstanding probe before its timeout. Clock offsets are not derived
  from ENet ping and clients cannot supply their own claimed host-clock offset.
- Hits carry a host/round epoch, source and target life IDs, firing-action ID,
  per-hit ID and source-clock firing timestamp. The host maps the firing timestamp
  into its clock. Shotgun pellets and penetrations share their firing-action identity
  and retain independent hit IDs; exact duplicate hit reports are rejected.
- Reports older than 500 ms are rejected. Future timestamps allow at most 50 ms
  plus bounded clock uncertainty (at most 50 ms), and accepted future timestamps
  are clamped to host now. Reports require a fresh clock model. Match start waits
  for fresh samples from loaded remote players, including after a round reload.
- For non-explosive attacks from a dead shooter, only shots from within 150 ms
  before recorded death remain eligible, with at most 25 ms of clock uncertainty
  around death. This replaces the previous 150 ms arrival-time grace. Grenades,
  mines and other blasts use impact time and retain damage after owner death.
  Projectile launch time is not carried through native projectile physics.
- Owner death time is retained across polls and transmitted with movement identity.
  Old movement cannot clear an observed death; an explicit respawn advances life.
  Source and target life IDs are checked on host acceptance and queued dispatch;
  recipients independently reject damage to a previous target life.
- Respawn messages explicitly carry old/new life IDs and the epoch. Duplicate,
  invalid successor, stale epoch and unauthorized respawns are rejected. Cached
  transforms/death state from the old life are invalidated until fresh movement.
- Lobby, late-join and match snapshots carry the authoritative combat identity.
  Older snapshots cannot reverse an optimistic local respawn in the same epoch.
  Round reset, disconnect/slot reuse and host migration invalidate old combat
  queues. Migration starts a new host-clock domain and fresh probes; old-domain
  reports are rejected. Shots during temporary loss of fresh synchronization can
  be rejected, rather than interpreted using an arrival-time fallback.
- Existing host equalization remains: target-specific half-RTT damage dispatch,
  bounded by the configured cap, with immediate local weapon feedback. Clock and
  accepted hit metadata are logged to help investigate subsequent multiplayer tests.

No position history or target rewind was added. Native hit detection still evaluates
current local copies. Target health continues to be applied by its owner. Clock
asymmetry, jitter, render cadence and delayed health/death messages still affect
trade outcomes. This is bounded timing validation, not proof of perfect fairness or
an anti-cheat system; matching timestamps do not guarantee simultaneous deaths.

## Original map caps

The port matches the native multiplayer stage table in src/game/front.c:
Egypt 2; Bunker II, Archives and Caverns 3; Facility, Complex, Temple, Stack,
Library, Basement and Caves 4. The limits remain unchanged. Egypt's original
2-player cap is also confirmed by GoldenEye: Source's Egyptian documentation:
https://docs.geshl2.com/levels/reimagined/ge_egyptian/
The original reason for each cap was not established by this investigation.

## Automated results

- 48 native checks passed. New checks execute production clock math, explicit
  shot/clock/movement serializers and all packet truncations, authenticated clock
  exchange, slow-probe timeout, positive/negative clock offsets, stale samples,
  old/future shot bounds, replay rejection, shared pellet identities, pre/post-death
  timing, life changes, queue cancellation, actual owner damage/respawn handlers,
  new-round identity, late snapshots, migration and clock-gated loaded-round start.
- 8 launcher UI checks passed.
- 6 Worker tests and TypeScript check passed locally.
- 10 Android lobby-client and 2 crash-exit-policy tests passed with fresh execution.
- assembleRelease succeeded. Version 0.3.7/code 46 and APK v2 signature verified.
  Packaged and unstripped native build IDs match; preserved ELF has debug info.
- git diff --check passed. Existing legacy native compiler warnings remain.

## Artifact

- APK: C:/Users/Occor/Documents/other_projects/goldeneye-vr/.claude/worktrees/playtest-logging-feedback-731f59/android/app/build/outputs/symbols/ae6a71cec360fbe4565ff2e5461859f74e22ec99/gevr-v0.3.7-release.apk
- Size: 21572806 bytes
- SHA-256: 4943912e840ceebfa4d9898aecfc13cfe6915f46432bc680945e1507b89e8880
- Native build ID: ae6a71cec360fbe4565ff2e5461859f74e22ec99
- Matching unstripped libgevr.so, manifest, automated logs, Android test XML,
  this report and source snapshot are preserved alongside the APK.

## Headset acceptance still needed

1. Use the same protocol 15 APK on two or three headsets. Confirm lobby/warmup,
   countdown and loaded-round start, including the next round and late join.
2. Repeated close-range lethal duels: compare host/client and client/client trades
   with host equalization on/off and several caps. A shot fired before death may
   still resolve; firing after death must not allow continued non-explosive damage.
   Test shotgun pellets, dual weapons, melee, bullets penetrating targets and FF.
3. Respawn rapidly while the other player shoots. Delayed previous-life hits must
   not damage the newly spawned life. Also test repeated deaths in warmup.
4. Disconnect/reconnect into a reused slot, and leave as host during a round.
   Confirm migration completes and damage resumes after clock resynchronization;
   reports from the previous host/slot/round must not apply to new identities.
5. Grenades/mines already thrown before death remain dangerous. Exercise explosions
   concurrent with bullets so nested damage does not disturb the firing timestamp.
6. Repeat with differing network latency/jitter. Inspect logs for Combat clock epoch,
   Clock synced and Hit entries; compare recorded source/target lives and shot ages.
   Running targets still use current-copy hit detection until rewind is implemented.
7. Continue the previous crash, ammo box, HUD, voice and weapon ownership acceptance
   checks. Automated results do not establish crash-free headset multiplayer.
