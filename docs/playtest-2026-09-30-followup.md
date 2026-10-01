# Later multiplayer crash and comfort follow-up

Work remains local in `playtest-logging-feedback-731f59`. No push, deployment,
report upload, or headset installation occurred. The earlier lobby lifecycle,
team scoring, voice positioning, and ammo synchronization changes are retained.

## What the later logs establish

The supplied `gevr-1a32be23.txt`, `gevr-0ae91b16.txt`, and
`gevr-d2d64626.txt` decode from base64 to game logs and Android diagnostic
history. Their latest native crashes all match release ELF build ID
`96e9b2b422514e9cc1b299c938be3ca7280fe41e`. Matching symbols were preserved
before rebuilding at `%TEMP%/gevr-crash-96e9b2b4-libgevr.so`.

| Latest crash | Symbolized failure |
| --- | --- |
| 21:59:30, first peer | `modelUpdateRelationsQuick + 88`, reading `node->Next` |
| 21:59:14, second peer | Same model traversal failure |
| 22:18:38, later two-player session | `modelGetNodeRwData + 172`, called from `door7F054FB4 + 208` during `objTick` |

All three show a model-node pointer with its low four bytes replaced by
`ffffffff`. The later crash occurring on only one headset is consistent with
local memory corruption; it does not establish a synchronized network crash.

The previous vertex allocator fixes were real defects, but these logs show
they did not eliminate the entire crash problem. A separate confirmed
overflow was found in `alloc_additional_item_slots`: it reserved 20 bytes
per inventory item, while native 64-bit `InvItem` occupies 40 bytes. The
production `bondinvReinitInv` writes `type = -1` for every declared entry,
overrunning that allocation during initial setup and subsequent resets,
including respawns. Allocation now uses aligned `sizeof(InvItem)` storage.

The regression compiles the actual allocator and inventory reset, surrounds
the allocation with guard memory, and exercises 100 inventory sizes with
100 resets each. The original 20-byte allocation fails the guard check;
the corrected allocation passes. This proves the overflow and its fix.
Its writes fit the observed `ffffffff` corruption, but the logs do not
capture the exact write that damaged each model. Sustained combat on
headsets is still needed to establish whether other crash causes remain.

## Changes in this build

- Crash reporting checks more Android exit history, recognizes fatal signals,
  Java crashes and ANRs as well as native crash records, and excludes the
  relaunch helper process and ordinary SIGKILL exits. A persisted foreground
  marker offers an unexpected-exit report if Android has no usable record.
  Normal backgrounding and launcher restart clear that marker. The launcher
  polls after startup rather than checking before Java initialization finishes,
  and acknowledges the offer only when the report page is drawn. Sending
  remains a user action.
- A local player already overlapping a door can move outward to reduce that
  overlap. Entering from outside and moving farther inside still use normal
  collision. Late-join door snapshots now update bounds and clipped collision
  vertices. Existing player-overlap escape remains. These changes do not
  establish or resolve every reported wall trap.
- Multiplayer pause keeps current head orientation and adds head translation
  relative to the pose at menu opening, separately from the stationary player
  body and its network position. Hands, arms and weapons are hidden. Controller
  poses continue updating, and motion melee is disabled while the menu is up.
- Red health and blue armor have independent horizontal anchors. Native gauge
  triangles and radar/text rectangles now use the same capture viewport
  transform; capture state also restores the cached internal render scale.
  Armor is anchored to the right rim. Pixel alignment and comfort remain
  headset acceptance checks, including different render resolutions.
- During ordinary multiplayer death, a new press of A, B or trigger skips the
  blood/death sequence and respawns. YOLT elimination and spectator rules still
  prevent an eliminated player from respawning.
- In team battles, active teammates always receive full distance gain and
  active opponents always receive Proximity gain, regardless of the host's
  selected mode. FFA uses the selected mode. Eliminated players and spectators
  retain their separate full-gain channel. Outside an ongoing match everyone
  can talk. Existing binaural positioning and the voice-volume slider remain.
- Proximity is full gain through 3,000 horizontal game units, before the
  radar's 4,000-unit bright-dot threshold. From 3,000 to 6,000 it uses
  `0.10 + 0.90 * (1 - (distance - 3000) / 3000)^3`; beyond 6,000 it holds 10%.
  Gains are 36.7% at 4,000 and 21.25% at 4,500. Walls and radar visibility
  do not change attenuation.
- Multiplayer hurt/grunt sounds use a short 200–500-unit attenuation range
  with squared gain. Friendly-fire rejection occurs before hit reactions,
  hurt counters, hat/blood effects and associated noises are generated.
- Host launcher options show one page at a time: Game / lobby, Match rules,
  and Player. Audio and voice controls have a separate popup. Friendly fire
  is in Match rules. Roster ping explains its host-relative meaning.
- Protocol is 13 because team voice eligibility changed. All participating
  headsets need this protocol; older builds receive the version rejection.

## Ping and host advantage

The transport connects clients to the current player-host, which forwards
state and voice and resolves hit reports. It is peer-hosted, rather than a
full mesh of directly connected peers. ENet supplies smoothed RTT for those
client–host links. The displayed host value of 0 ms is correctly its RTT
to itself; it does not say that clients reach the host instantly. Their
rows show those link measurements.

The host can have a latency advantage: its local hit report is processed
without the client-to-host trip, and clients receive resolved damage after
network transmission. A pairwise matrix would require additional probes;
the current display does not measure every client-to-client path. No
latency compensation or pairwise-probe system was added in this change.

## Local validation and artifacts

- 28 native checks passed, including protocol/state, team voice rules and
  gain, allocator guards, outward door escape, ammo lifecycle, migration,
  latency freshness, and the actual pinned Steam Audio runtime.
- 12 Android unit tests passed: 10 lobby-client tests and 2 crash-exit policy
  tests. These do not run Android crash recovery on a headset.
- All 6 Worker runtime tests and the TypeScript check passed.
- ARM64 release and debug APKs built successfully. Release APK v2 signature
  verified; `git diff --check` passed.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `android/app/build/outputs/apk/release/app-release.apk` | 21,548,230 | `11a9859f3c940379e79ee6b67b3ec7689746c52ffc51ed160e1889b122534e58` |
| `android/app/build/outputs/apk/debug/app-debug.apk` | 31,307,772 | `4ef6e5656751f79f5e7c452264aeaba4d91c17b223bf86015234c8e6eea9766d` |

Matching unstripped release symbols and copies of both APKs are preserved in
`android/app/build/outputs/symbols/e4cbe011a260e2521cbb611aba04beed0a97639d/`.
Release ELF build ID is `e4cbe011a260e2521cbb611aba04beed0a97639d`.
App version remains 0.3.6, code 45.

Headset acceptance still needs sustained two-/three-player fighting and
respawns, next-launch crash prompting, opening/closing and late-join doors,
reported wall-trap reproduction, circular radar with armor on the right,
pause translation/rotation/recentering, hidden hands, death skip versus
YOLT elimination, teammate/opponent/spectator voice routing, the 10% voice
floor, and short-range hurt sounds with friendly fire enabled/disabled.
Automated checks do not validate rendered headset pixels or live multiplayer
comfort and synchronization.
