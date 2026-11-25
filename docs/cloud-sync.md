# Cross-platform sync (Google Drive)

Status 2026-11-25: step 1 built (the engine, a folder store, the game's hooks,
the shared key table iCloud uses too). Google Drive (step 2) not yet: no
build syncs for players until it is.

User idea, 2026-11-25: give the devices without iCloud (Quest, Steam Frame,
Android, PC flat, PC VR) the same sync as [iCloud sync](icloud-sync.md):
settings, saves and ROMs, with big packs opt-in. Decisions (user):

- **Google Drive**, the player's own. The project stores and pays for nothing.
- **Off until linked:** nothing leaves a device until the player links Google
  Drive on it.
- **Apple devices join too,** so a Quest and an Apple TV sync with each other.
  It must be friendly and must not break iCloud sync.
- **Several devices on one account must never get confused,** for example
  two Apple TVs in two rooms, each with four players in split screen, in the
  same online match.

## Google side

- **One scope: `drive.file`.** The game sees only the files it made, in a
  visible **GoldenEye VR** folder in the player's My Drive. Non-sensitive
  (basic app verification only, no security audit). Uses the player's Drive
  quota (15 GB free).
  Not `drive.appdata` (the hidden app folder): Google's device code sign-in
  refuses it ("Invalid device flow scope"), although the docs list it.
  Tested 2026-11-25 against the project's client; `drive.file` is accepted.
  Since the player can see the files: every file carries an
  `appProperties` tag (`gevr`: its role), and the game finds files by tag,
  not by name or place, so a renamed or moved file still works. A
  `README.txt` in the folder says what it is and not to edit it. A trashed
  folder or file is treated as missing and made again from the devices.
  Players can download their own saves from Drive.
- **No billing, ever.** The project stays on Google's free Standard tier
  (the default; no billing account, never charged). Over the limit, Google
  answers 403/429 and the client backs off; it never bills. Never link a
  billing account or opt in to the Scaled tier (paid above its allowances
  from 2027-04-01). Standard tier limits (checked 2026-11-25): 1,000,000
  quota units a minute for the whole project, 325,000 per player; a list
  costs about 100 units, a read 5, a write 50, a download 200. The project
  limit is shared by every player, so the client is frugal: one changes
  list at launch, on resume and every 15 minutes, nothing when idle, and
  random jitter and backoff so a release day's launches don't arrive at
  once. 10,000 players in game at once is about 70,000 units a minute.
  Sign-in and app verification are free too (`drive.file` needs no
  paid security assessment). There is no server of ours in the path.
- **Sign-in, phase 1: the device code flow on every platform.** The launcher
  shows a QR code and a short code for google.com/device; the player approves
  on a phone. It works on the Quest and Steam Frame (no Google Play services),
  the Apple TV (no browser) and everywhere else. Where a browser exists
  (PC, phones), an **Open in browser** button goes to the same page.
  Later: the PC's own browser with a loopback redirect, and
  ASWebAuthenticationSession on iPhone, iPad and Vision Pro.
- **Client ID and secret:** installed-app secrets are not confidential by
  Google's own rules, but they stay out of the public repo (a build setting,
  like the Discord SDK path), so forks use their own Google project.
- **Before release:** the OAuth consent screen in **Production** (in
  Testing, refresh tokens die after 7 days); brand verification with the
  goldeneyevr.com privacy policy (add a Google Drive section and Google's
  Limited Use statement), the domain and the logo.

## Records: one writer per file

Drive has no transactions, so no two devices ever write the same file. Each
device writes only its own record; every device reads them all and works out
the same result. Nothing can be lost to two devices saving at once, however
many run together.

| File in the store | Written by | Holds |
| --- | --- | --- |
| `device-<deviceId>.txt` | that device | its name, kind, app version, last seen; every setting it set (stamped); its saves (bytes, time written, has-progress) |
| `files/<sha256>` (step 3) | the first uploader | a ROM or pack archive, by content (same ROM from two devices = one file) |
| `files/index-<deviceId>.txt` (step 3) | that device | which content it has, by path |

A record is tab-separated text, one line per item, so a player can read it.
Saves ride in the record (GoldenEye's EEPROM is 512 bytes): one file per
device, one read per device at launch. `deviceId` is random per install
(`.gevr-sync/id.txt` in the save folder).

- **Settings:** per key, the newest stamp wins, inside its scope:
  - **shared** (controls, look speed, gameplay, cheats, gun fits, name...),
  - **device kind** (frame rate, MSAA, screen, height, master volume...;
    kinds: quest, frame, pcvr, pc, ios, tv, vision): kept in the record as
    `key@kind`, so a PC that plays flat and in VR keeps one of each,
  - **this device only**: the split-screen guest seats, which pad is which
    player, the audio output, the window, the sync rows themselves. Never
    uploaded.
  One table for every sync: [port/include/gevr_sync_keys.h](../port/include/gevr_sync_keys.h).
  A stamp is the time, but never older than the newest stamp the device has
  seen, so a device whose clock is slow still orders its change after what it
  saw.
  Settings that arrive while the game runs apply at the next launch, never
  mid-match.
- **Saves: never merged** (the iCloud rule). The save the game wrote last
  wins; a save with no progress never replaces one with progress; the one
  replaced is kept on the device (`eeprom.bin.before-sync-<date>`, last five).
  The winner is the same on every device: progress first, then the newest,
  then the device id. When this device's own save and the winner were both
  written since its last launch sync, the launcher says so (built); offering
  to switch back ("Use the save from Den, written 20:14") is step 4.
- **Joining:** the iCloud rule. A newly linked device first reads what is
  there; the others' settings win, and it adds only keys nobody has set.
- **Files:** Drive's resumable upload; downloads land beside the target and
  are moved in at the next launch if the game has the file open. ROMs and
  saves by default; HD, AI, Bean and mod packs only with **Big packs too**.
  Nothing is deleted from Drive when a file is removed on a device.
- **When:** at launch (everyone's records read, the winners written into the
  files before the game reads them) and after the game writes a settings file
  or a save (this device's record only, 1.5 s later, in the background; a
  push lost to the game closing is caught at the next launch). Nothing comes
  in mid-session. A launch that can't read the store, or one record of it,
  changes nothing: a partial view could put this device's values over one it
  couldn't read. Files (step 3) will poll every 15 minutes.

## Many devices, one account (the two Apple TVs)

Sync moves **data**, never **who a device is online**:

- **Not synced, ever:** `gevr_player.json` (player ID, secret, friend code,
  restore code, Discord tokens), lobby owner tokens, `deviceId`. Each device
  stays its own player to lobbies and matches, so both Apple TVs can be in
  one match. This is already the rule on the friends side: linking a Discord
  account that another player has brings that player over and signs the
  other device out, so one online identity is only ever on one device.
- **Room settings stay in the room:** seats and pad assignments are "this
  device only", so setting up four pads in the den doesn't scramble the
  living room's.
- **Same name twice in a match:** the shared name makes both Apple TVs
  "Rocco". The lobby adds the device name when names collide in one match
  ("Rocco (Den)").
- **Both finish the same co-op mission:** same progress, either save is
  right. Different missions since the last sync: the conflict offer above.
- Online split screen across rooms (4 + 4) is phase 2 of split screen, a
  protocol change; sync neither needs it nor gets in its way.

## Apple devices: iCloud and Google Drive together

iCloud sync shipped on TestFlight in v0.5.26, so it stays as it is, beside
this sync, and both work on the save folder's files. On an Apple device,
iCloud's launch merge runs first (main.c: gevrCloudLaunch, then
gevrSyncLaunch); what it brought is a change in the files, which this sync
stamps and carries to the other devices; what this sync writes is passed to
iCloud (gevrCloudFileSaved) for the other Apple devices. Neither bounces a
value back: each uploads only what differs from what it last synced.

- iCloud stays automatic on Apple devices; Google Drive is opt-in everywhere.
- Both use one table of which keys travel (gevr_sync_keys.h). Moving iCloud
  onto it made the split-screen guest seats, pad assignments, audio output
  and window "this device only" in iCloud too: two Apple TVs in two rooms
  keep their own couch setups (they were per family, so the two TVs
  overwrote each other's).
- The one imprecision: a value iCloud brings at launch gets this launch's
  stamp, so if a non-Apple device changed the same setting between the two
  syncs, the Apple device's value wins. Saves aren't affected (a save keeps
  the time it was written).

## Code

- `port/pc/gevr_sync_core.cpp`: the rules (records, stamps, scopes, the save
  winner, joining), no I/O.
- `port/pc/gevr_sync.cpp`: the files around it (`.gevr-sync/` in the save
  folder: `id.txt`, `state.txt` = the last sync, `mine.txt` = this device's
  record), the store interface (`gevr_sync_store.h`), the folder store, the
  game's entry points (`port/include/gevr_sync.h`).
- Hooks: main.c (launch), config.c and vr_settings.cpp (settings written),
  libultra.c (EEPROM written), vr_launcher.cpp (the note under START).
- Builds: PC flat and VR, Linux, Apple (port/pc globbed), Quest and Frame
  APKs (listed in CMakeLists.txt). Not the Android phone APK (no native
  services there yet) or the server.
- Test: `python port/tests/test_sync.py` (simulated devices with their own
  clocks, then the real glue launching save folders as separate processes).
- Try it by hand: `GEVR_SYNC_DIR=<a folder>` (or that path in
  `.gevr-sync/folder.txt`) makes a folder the store; two PC run folders
  pointed at one folder sync with each other.

## Plan

1. Done 2026-11-25: the engine, the folder store, the hooks, the shared key
   table (iCloud uses it too), tests.
2. Google Drive carrier: device code sign-in in the launcher, folder
   records, settings and saves.
3. ROMs and packs (resumable upload, content hash).
4. Polish: native browser sign-in on PC and Apple, device names in the
   lobby, the save-conflict offer.

Owner setup (2026-11-25): Google Cloud project "GoldenEye VR", Drive API on,
OAuth client "GoldenEye VR devices" (TVs and Limited Input), consent screen
External, in Testing with the owner as test user. The client ID and secret
live only in the main checkout's ignored `build/google/oauth.properties`
(`gevr.googleClientId`, `gevr.googleClientSecret`); builds read them from
there, else build without Google Drive. Before release: scope `drive.file`
on the consent screen's Data access page, Production status, brand
verification. Later: Desktop and iOS clients for browser sign-in.
