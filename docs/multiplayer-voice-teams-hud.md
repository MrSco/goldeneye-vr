# Multiplayer voice, teams, and HUD changes

Implemented locally in `playtest-logging-feedback-731f59`, with protocol 14.
Older peers receive the existing version-mismatch rejection. No changes
to custom player names or the website were needed. Nothing has been pushed,
deployed, or installed on a headset.

## Behavior

- Host voice mode is persisted and replicated. Proximity is full gain through
  3,000 horizontal game units, 36.7% at 4,000, 21.25% at 4,500, and 10%
  from 6,000 onward, with a cubic falloff.
  Couch uses full distance gain in FFA. In team battles, teammates always
  use Couch gain and opponents always use Proximity gain. The player's voice slider applies afterward.
  Walls, radar visibility, and dot colors do not enter this calculation.
- Both modes use Steam Audio 4.8.1's default HRTF and bilinear interpolation.
  The listener basis comes from the existing head/body camera orientation,
  including pitch and roll. See `port/external/steamaudio/README.md` for the
  pinned archive, licenses, conversion rates, and stereo fallback.
- Active FFA players share voice. Active team players hear active teammates and opponents
  with the pair-specific gain above. Ordinary deaths remain active. Eliminated players and spectators
  share a cross-team channel at full distance gain. Sources with unknown
  positions are centered. Outside an ongoing match, all connected players talk.
  Eligibility is checked during host forwarding, receiving, and mixing.
  Group/phase changes reset decoder queues, captured speech, and binaural tails.
- Team scenarios require exact Red/Blue rosters: 2/2, 3/1, or 2/1. Players
  choose their own pending team; the host rejects over-capacity requests.
  An incomplete roster prevents countdown, while solo warmup is available.
  In-match choices affect the next round and clear lobby readiness. Stage
  loading acknowledgements are separate, so current players remain active.
  Active teams stay frozen; signed team scores are retained after departures.
- Ping uses smoothed ENet RTT to the current host and updates once per second.
  The host is zero; unknown or older-than-five-second samples are unavailable.
  Migration clears old samples. The game's ROM font represents unavailable
  ping as `--`; the launcher uses an em dash.
- Scores, ping, and speaker glyphs have reserved columns. Name-tag speaking
  glyphs are drawn beyond the name's right edge without changing name width,
  center, height, or scale.
- Multiplayer pause always opens on Status, adjacent to Pause. During gameplay,
  compact native red/blue health and armor arcs surround the radar. Gauges
  are suppressed on other open pause pages; hidden-radar damage feedback remains.
  Single-player watch behavior is retained.
- Teams have a persisted host-controlled friendly-fire switch, enabled by
  default. Changes apply immediately, including after migration and late joins.
  Disabling it blocks teammate bullet/explosion damage while preserving enemy,
  self, and environmental damage and native scoring when enabled.
- Setup ammo crates use host-authoritative physics and 20 Hz transform updates.
  State includes position, render origin, rotation, velocity, and respawn timer;
  late joiners receive a reliable copy. Round epochs reject old updates when
  reloading the same map. Pickup/respawn restores the original location and
  orientation, clears bullet holes, and releases prior projectile state.
- The head-locked quad follows its texture's aspect ratio while retaining
  distance and angular height. Captures restore framebuffer, viewport,
  scissor, flat/menu rendering state, and size state; uniform caches are
  invalidated consistently. Separate prompt/pickup/countdown captures restore
  their own size state. The aspect mismatch is a likely contributor to the
  reported distortion; headset acceptance remains necessary.

## Automated validation (2026-10-01)

| Check | Result |
| --- | --- |
| Worker runtime tests (`npm test`, `services/lobbies`) | 6 passed |
| Worker TypeScript (`npm run check`) | Passed |
| Android unit tests (`:app:testDebugUnitTest`) | 12 passed (10 lobby + 2 crash policy) |
| Native checks (`python port/tests/test_multiplayer.py`) | 34 passed |
| Headless ImGui (`python port/tests/test_launcher_ui.py`) | 8 scenarios passed |
| Android ARM64 debug APK (`:app:assembleDebug`) | Built locally |
| Android ARM64 release APK (`:app:assembleRelease`) | Built locally; v2 signature verified |

Native checks compile production serializers/rules and the actual pinned
Windows Steam Audio runtime. The core fixture keeps tested production function
bodies and replaces unrelated game/world functions for a headless harness.
It covers invalid configurations, exact team rosters and capacities,
readiness versus loading, active versus pending teams, voice groups, signed
scores across departures, round serialization/truncation, late-join snapshots,
host migration, ping expiry/wrap, attenuation thresholds, binaural direction
and elevation, arbitrary buffer sizes, and reset of buffered speech.
These checks do not simulate multiple headsets or validate rendered pixels.
The combat vertex allocator regression checks allocation boundaries and
repeated allocation/free/merge cycles. The original code overwrites its buffer
guard; retaining its old allocation-count read fails independent allocations.
Both checks pass with the fixes. See `playtest-2026-09-30-crash-and-ammo.md`.

The earlier timeout fixes close actual lifecycle gaps (solo warmup expiry,
paused native liveness, recovery after listing loss, and orderly lobby restart).
The previous waiting, lifetime, and heartbeat expiry paths also existed and
worked. Without live logs, the particular lobby that vanished cannot be tied
to one cause.

## Headset acceptance still required

Use protocol-14 builds on every participating headset when installation is
separately authorized. These checks have not been performed here:

1. Try all three team formats, invalid capacity choices, incomplete rosters,
   solo warmup, late joins, next-round team changes, and host migration.
   Confirm active teams and earned scores survive and nobody is rebalanced.
   Toggle friendly fire during a round; verify bullets and explosions against
   teammates, enemies, and self with both settings.
2. Check lobby, FFA, teams, ordinary deaths, YOLT elimination, and spectator
   chat. Speak while changing groups and confirm old speech does not cross.
   Check mute, zero voice volume, live mode changes, and persistence.
3. In Proximity, compare 0/3,000/4,000/4,500/6,000/8,000/beyond distances, through walls
   and with radar hidden. Confirm distant voice stays audible at the floor.
   In both modes, compare left/right/front/back/above/below, head pitch/roll,
   body turns, and recentering. Listen for artifacts and clipping.
4. Use maximum-length names and multiple simultaneous speakers. Compare
   name, tag, score, and ping anchors while speaking starts/stops. Confirm
   latency updates in both launcher rosters and the scoreboard; migration
   temporarily shows unavailable ping instead of previous-host measurements.
5. In single-player and multiplayer, compare radar width/height at multiple
   render resolutions. Capture the same scene with return-to-launcher prompts,
   pickups, and countdown text individually and together. Existing message
   bounds must stay stationary. Repeat across pause/watch transitions.
6. On Pause, Lobby, Scores, and Status, take damage and confirm gauges appear
   only on Status while the menu is open. Close it and confirm ordinary
   health feedback, then check the single-player watch.

APK: `android/app/build/outputs/apk/debug/app-debug.apk`.

See `playtest-2026-09-30-followup.md` for the later inventory overflow,
crash prompt, pause tracking, door overlap, and audio fixes.

## October 1 launcher and fun follow-up

Host and Join use real tabs, with bounded content and a fixed footer. Host pages are
Lobby, Match, Fun, Player, Audio, and Favorites. Join has Public/Lobby, Private,
LAN, Direct IP, Player, and Audio. The host's Launch button supplies host consent;
joined players still ready up. Scenario changes clear pending teams and readiness,
while other round settings clear readiness. Voice and friendly fire stay live.
Clients can consent to edited next-round settings from the pause Lobby page.

DK, Paintball, Line, and Normal/Tiny/Big gun size are host-controlled next-round
options, persisted separately from single-player cheats. Protocol 14 carries the
validated two-byte extension in both pending and active configurations. Late joins
and migration retain both copies. Tiny/Big use 0.2x/2x visual scaling locally and
on remote held guns, without changing ammunition, damage, or projectiles. Online
Line mode uses OpenGL world edges because the native coverage visualization is
specific to the N64; menus, radar, and text stay filled.

Radar gauges share the radar center and rotate health +17.5 degrees and armor
-17.5 degrees in screen coordinates. The inner rim is three scaled native HUD
pixels outside the radar, preserving the native segments, fill, and colors.

The October 1 build report covers eight headless ImGui scenarios and the latest
APK checks separately from headset acceptance.
