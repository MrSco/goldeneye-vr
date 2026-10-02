# October 1 multiplayer launcher and fun build

Implemented in the existing `playtest-logging-feedback-731f59` worktree, preserving
the earlier lobby, combat-memory, crash-reporting, voice, ammo, and pause fixes.
No push, deployment, report upload, or headset installation was performed.

## Changes

- Radar health and armor arcs now share the radar center. Health rotates
  clockwise 17.5 degrees, armor counterclockwise 17.5 degrees, moving their
  top ends inward and lower ends outward. Their inner radius is 19/16 of the
  radar radius (three scaled native HUD pixels of clearance). Native segment
  colors and fill remain. Rotation values are independent; single-player
  watch drawing is unchanged.
- The multiplayer launcher uses Host/Join tabs and separate settings tabs.
  Lobby rosters use fixed columns. Content scrolls inside a bounded child;
  Launch, Stop Hosting, Ready, Disconnect/Cancel Join, and Back remain in a
  fixed footer, with long status text shortened and available in a tooltip.
  Connection events, heartbeat, idle handling, and client launch processing
  run independently of the selected settings tab.
- Dropdowns keep their initial selected-row scroll but do not recenter while
  hovering or clicking. Laser ownership is resolved before gamepad activation
  in the same frame, so a trigger click selects the pointed row once. Stick
  navigation remains available.
- Launch supplies the host's readiness. Joined players still consent through
  Ready. Scenario changes clear pending teams and readiness, so 2v1 to Standard
  no longer retains a host team/Ready requirement. Round-affecting settings
  clear readiness and cancel pending countdowns; live voice and friendly fire
  do not. Solo warmup remains available. A real round requires the exact team
  roster, client readiness, and loading acknowledgements. Clients can ready
  for edited next-round settings from the pause Lobby page.
- Host-controlled DK, Paintball, Line, and Normal/Tiny/Big gun-size options
  are available on the launcher Fun tab and a dedicated multiplayer pause
  Fun page. Clients see read-only values. Changes apply next round, persist
  separately from single-player preferences, and do not modify the running
  round's teams or earned scores. Status remains the default pause page.
- Protocol 14 adds validated `fun_flags` and `gun_size` bytes to explicit match
  serialization, retaining separate pending and active configurations through
  late joins and migration. Native DK and Paintball effects are reused.
  Native Line mode depends on N64 coverage visualization; online Line mode
  draws OpenGL world edges with depth occlusion instead, preserving filled
  menus, radar, and text. Saved scope/eye draws retain the mode for redraws.
- Tiny and Big use 0.2x and 2x visual scales on local held models and remote
  held weapons. Fresh weapon creation applies the factor once, including
  weapon changes and respawns. Ammunition, damage and projectiles are unchanged.
  Single-player cheat activation cannot override online fun settings.

## Automated validation

| Check | Result |
| --- | --- |
| Native regression suite | 34 passed |
| Headless production ImGui widget scenarios | 8 passed |
| Android unit tests | 12 passed: 10 LobbyClient, 2 CrashExitPolicy |
| Worker runtime tests | 6 passed |
| Worker TypeScript check | Passed |
| Debug and release Android ARM64 builds | Passed |
| Release APK signature | APK v2 verified |
| `git diff --check` | Passed |

Native checks include match-config round trips and every truncation, invalid
fun fields, host authority, pending/active values, late joins and migration,
2v1 to Standard, incomplete rosters, client readiness, countdown cancellation,
unchanged current teams and loading state, and readiness with a migrated host.
Production visual queries and body-creation scale code are exercised repeatedly;
the production remote-hand function is tested through 100 ticks, weapon swaps,
and recreated hands. The line renderer is tested with recorded OpenGL calls for
all depth-write/opaque-cutout/polygon-offset combinations, including restored
bindings and uniforms. Edge indices include large persistent-ring offsets.
Gauge geometry checks symmetry and clearance at eight scales for every native
segment pair; it does not measure actual headset pixels.

ImGui checks cover clicks at the top, middle, and bottom of a scrolled popup,
disabled choices, stable hover positions, stick navigation/confirmation, pointer
ownership changes, and footer bounds with four maximum-length names, long messages,
and the launcher's 2.2x font/style scale at several render heights.

## Local artifacts

Release: `android/app/build/outputs/apk/release/app-release.apk`
- 21,556,422 bytes
- SHA-256 `4e55ecb135d91af336f4670ea6e448d3e2d1ff735fa4390fe487716631027f18`
- Native ELF build ID `e3a5ea9e6ad4023e74aaedd9cdaf062808fee731`

Debug: `android/app/build/outputs/apk/debug/app-debug.apk`
- 31,319,500 bytes
- SHA-256 `c7e35e6f30351a45d9aea05bd9711db09f95f60226fb677db93bc5a56cd82c15`
- Native ELF build ID `f0c956ce80f86d1c0da49f289cb18ca24654248e`

Both APKs, both unstripped native libraries, a manifest and test/build logs are
preserved at `android/app/build/outputs/symbols/e3a5ea9e6ad4023e74aaedd9cdaf062808fee731/`.
Earlier preserved builds remain available.

## Headset acceptance still required

Use protocol-14 builds on all participating headsets. Automated checks do not
establish sustained combat stability, headset comfort, or final visual appearance.

1. Verify circular radar and inward-rotated gauge arcs, with clearance at full,
   partial and empty health/armor. Open Status, other pause pages, and resume.
2. Navigate all launcher tabs while hosting; confirm bottom controls remain
   visible with four long names and connection messages. Select visible rows
   at the top and bottom of character/gun popups with the laser. Switch between
   stick and laser, including a stationary laser trigger click.
3. Host 2v1, change to Standard, and launch solo without restarting the lobby.
   Repeat with joined players, unready clients, incomplete teams, and a
   migrated host. After changing next-round options, ready through pause Lobby.
4. Exercise DK/Paintball/Line separately and together, both gun sizes, dual
   wield, weapon changes and respawns. Check remote guns as well as local guns.
   Verify menus/text remain filled in Line mode and assess its frame rate.
   Confirm edits during combat wait until the next round, including late joins
   and host migration, while current teams and scores remain intact.
5. Continue the earlier combat, crash-report offer, door/wall escape, ammo-box
   synchronization and respawn, team voice routing, audible 10% distance floor,
   short-range hurt sounds, death skip, and pause tracking acceptance checks.
   The logs previously supplied identified confirmed memory defects; this
   build cannot establish that every possible crash cause is eliminated.

## Service status

Cloudflare services were inspected read-only before this implementation. The
website and lobby service already had September 30 deployments; these local
changes have not been deployed. The lobby API accepts and filters protocol
versions separately. Existing protocol-9/10 players can still connect to matching
older-version hosts; they cannot join protocol-14 games. This build makes no
host-fairness or dedicated-server changes.
