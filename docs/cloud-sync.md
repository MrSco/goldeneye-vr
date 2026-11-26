# Cross-platform sync (Google Drive)

Status 2026-11-25: steps 1 to 4 built. Settings, saves and ROMs (and, opted
in, texture packs) sync through the player's Google Drive once they link it
(launcher, Play > Sync). Settings, saves, ROMs and packs have been checked
live on the owner's Drive; step 4's page against a stand-in for Google.

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
  limit is shared by every player, so the client is frugal: a launch is
  one token refresh, one list and one read per device (about 100 + 5 x
  devices units), a settings or save change one write, nothing when idle.
  10,000 players launching in the same minute is about 1.2 million units:
  the busiest minute of a release day could see some 429s, which a launch
  treats as "couldn't reach" (nothing changes; the next launch syncs).
  Sign-in and app verification are free too (`drive.file` needs no
  paid security assessment). There is no server of ours in the path.
- **Sign-in: the device code flow on every platform (built).** Launcher,
  Play > Sync (its own sub-tab: on the General page it made flat screens
  scroll): Link Google Drive shows the address
  (google.com/device) and a code; the player enters it on a phone and allows
  GoldenEye VR; the page moves on by itself and shows the account. It works
  on the Quest and Steam Frame (no Google Play services), the Apple TV (no
  browser) and everywhere else. The device syncs from its next start (the
  game has already read its files). The refresh token stays in the save
  folder's `.gevr-sync/google.txt`, never synced. Unlink (with a confirm)
  forgets it on this device only: revoking it at Google would end the
  player's whole grant and sign their other devices out too (review
  2026-11-25); the Google Account's Security page removes the access
  everywhere. When Google refuses the token (the
  player removed the access, or Testing's 7 days ran out), the device signs
  itself out and the launcher says "Google Drive signed this device out.
  Link it again" (`.gevr-sync/google-signed-out.txt` until it is).
  Step 4 added a QR code of google.com/device beside the code (a phone's
  camera opens the page) and **Open in the browser** (SDL_OpenURL; not on
  the Apple TV). A one-tap sign-in in the device's own browser (a loopback
  redirect on PC, ASWebAuthenticationSession on Apple) would need two more
  OAuth clients (Desktop, iOS) in the Google project; not done.
- **Client ID and secret:** installed-app secrets are not confidential by
  Google's own rules, but they stay out of the public repo (a build setting,
  like the Discord SDK path), so forks use their own Google project.
- **Production** since 2026-11-25 (in Testing, refresh tokens die after 7 days
  and only listed test users can link). The owner set up the domain and
  branding. Still to do before release: a Google Drive section in the
  goldeneyevr.com privacy policy with Google's Limited Use statement (the live
  policy did not mention Google on 2026-11-25).

## Records: one writer per file

Drive has no transactions, so no two devices ever write the same file. Each
device writes only its own record; every device reads them all and works out
the same result. Nothing can be lost to two devices saving at once, however
many run together.

| File in the store | Written by | Holds |
| --- | --- | --- |
| `device-<deviceId>.txt` | that device | its name, kind, app version, last seen; every setting it set (stamped); its saves (bytes, time written, has-progress) |
| `files-<deviceId>.txt` | that device | its ROMs and packs: path, kind, size, content id, when it changed |
| a blob (Drive: tagged `gevr=blob`, named `ge.z64` or `hd.zip`; folder: `blob-<id>`) | the first device with that content | a ROM, or a pack as one zip; its id is what is in it (`r`+SHA-256, or `p`+SHA-256 over each file's path, size and SHA-256), so the same content is uploaded once |

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
- **Saves: never merged** (the iCloud rule). Only a whole EEPROM (2048
  bytes) syncs: one cut short (a crash mid-write) never goes up, never wins,
  and a whole one replaces it (iCloud too, since the 2026-11-25 review). The save the game wrote last
  wins; a save with no progress never replaces one with progress; the one
  replaced is kept on the device (`eeprom.bin.before-sync-<date>`, last five).
  The winner is the same on every device: progress first, then the newest,
  then the device id. Each save in a record carries the save it replaced on
  its device; when the winner wasn't built on this device's own save (its
  device hadn't seen it), the launcher says so. Play > Sync lists the saves
  set aside (the newest three) with **Use this one**: the chosen save comes
  back at the next start, written then, so it is the newest everywhere; the
  one it replaces is set aside in turn.
- **The save check at START** (owner, 2026-11-25: "maybe they want that
  save... and then it makes another save"): saves come down at launch, so a
  save another device wrote while this one sat in the launcher would be
  played over. START asks first when a store is linked and its saves weren't
  read in the last 2 minutes (or the launch couldn't read them), and always
  with iCloud on (its look is local: the key-value store already has the
  other Apple devices' saves). "Checking your other devices' saves..." with
  **Play now**; off the launcher's thread, about 5 s per answer, 8 s at
  most. A save that would win by the usual rule: "Your save from Den is newer
  (2026-11-25 20:14)." **Use it** puts it in place before the game reads
  the EEPROM, this device's set aside; **Keep this one** starts as is. No
  answer: "Couldn't check your other devices for a newer save." **Play
  anyway** / **Try again**. Nothing newer: straight in. The launch's
  footer now says "couldn't check your other devices' settings and saves".
- **Joining:** the iCloud rule. A newly linked device first reads what is
  there; the others' settings win, and it adds only keys nobody has set.
- **ROMs and packs (step 3, gevr_sync_files.cpp):** ROMs (`*.z64/v64/n64`
  in the game's folder, `$B`, where the launcher looks: the Quest's
  files/data, the PC's exe folder; known by name alone, so the layouts meet)
  always; the packs in `texture-packs/` only
  with **Big packs too** (Play > Sync, this device's choice). Per path, the
  newest entry among the devices' indexes wins. iCloud's rules: **already
  here stays** (what a device has the first time it syncs keeps as it is),
  then a change here goes up and a change elsewhere comes down (both: the
  others' copy). A download whose target is there waits in
  `.gevr-sync/ready/` and goes in place at the next launch, before the game
  reads it. A download goes in place only whole: a ROM's SHA-256 is its id,
  a pack's zip has the store's size and unpacks. A pack needs twice its
  size free. This device's index goes up whenever the store's copy differs
  from it (a write that failed, a trashed file). Two devices that uploaded
  the same content at once leave two copies; the next list removes the
  newer one. Nothing is deleted on a device
  when a file is removed (it comes back); a blob no index names any more is
  removed by the device that replaced it. **Interrupted** (the app closed,
  a crash, a flat battery): every launch and every pass first puts right what
  a quit left (`filesRecover`): `.gevr-sync/work/` (partial downloads, a pack
  being zipped) is emptied; a `ready/` item without its descriptor (a pack
  half unpacked) goes; a ROM or pack left as `<target>.sync-old` between an
  install's two renames comes back if the target is missing, and goes if it
  is there. Indexes and `files-state.txt` are written whole (tmp, rename) and
  only after the blob is up or the file in place; a pass cut short redoes
  what it hadn't recorded, and a download whose content is already here is
  just recorded. **Never during a level:** the worker starts no pass and no
  new item while the game is past its title stage (`g_StageNum` other than
  90, where the menus are); the item in flight finishes, and the rest waits
  for the menus or the launcher. Starting the game never waits for it. Uploads are Drive's resumable
  upload in 4 MiB pieces (the session address is the reply's `Location`,
  which `pc_http` now hands back; Drive's "308, go on" mustn't be taken for a
  redirect, so the Drive calls set `followRedirects = false`). Not yet:
  "Wi-Fi only" on phones (iCloud has it).
- **When:** at launch (everyone's records read, the winners written into the
  files before the game reads them) and after the game writes a settings file
  or a save (this device's record only, 1.5 s later, in the background; a
  push lost to the game closing is caught at the next launch). Nothing comes
  in mid-session. A launch that can't read the store, or one record of it,
  changes nothing: a partial view could put this device's values over one it
  couldn't read. ROMs and packs: a background pass 4 s after a launch that
  synced, every 15 minutes, and when a ROM is chosen or a pack imported
  (gevrSyncKick); the line under START shows its bar; a ROM that came makes
  the launcher look for ROMs again.

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
  "Rocco". The host already numbers a joiner whose name is taken ("Rocco
  2"), but it also took a joiner with a connected player's name for that
  player back on a new connection, and dropped the first. Now that happens
  only when the first connection has been quiet for 2 s
  (`netSameNameIsReconnect`, net_rules.h; no protocol change), so both
  Apple TVs stay in. **Device names** (step 4): each device has one, "Den"
  or its kind's ("Apple TV") by default, set on Play > Sync; sync notes on
  the other devices use it ("the save from Den").
- **Both finish the same co-op mission:** same progress, either save is
  right. Different missions since the last sync: the conflict offer above.
- Online split screen across rooms (4 + 4): the batch's protocol 28 carries
  the guest seats; their names come from each device's own guest seats
  (never synced), so they don't collide through the sync.

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
- Settings files: goldeneye-vr.ini is the working folder's (`$B`,
  VR_INI_PATH), goldeneye.ini the save folder's (`$S`): the same folder on
  PC and Apple, files/data and files on the Quest (review 2026-11-25: the
  first version looked for both in `$S`, so the Quest never synced
  goldeneye-vr.ini).
- `port/pc/gevr_sync.cpp`: the files around it (`.gevr-sync/` in the save
  folder: `id.txt`, `state.txt` = the last sync, `mine.txt` = this device's
  record), the store interface (`gevr_sync_store.h`), the folder store, the
  game's entry points (`port/include/gevr_sync.h`).
- `port/pc/gevr_sync_drive.cpp`: Google Drive. The store (records found by
  `appProperties has { key='gevr' and value='record' }`, so the folder they
  sit in doesn't matter; a write PATCHes the record's file, or makes it in
  the oldest folder tagged `gevr=root`, made with its README when none is),
  the device-code sign-in, unlink. HTTP is the PC services' (`pc_http.cpp`:
  WinHTTP, libcurl, NSURLSession).
- The OAuth client: CMakeLists.txt reads `build/google/oauth.properties`
  (`GEVR_GOOGLE_OAUTH`, this checkout's `build/`, or the main checkout's,
  found through a worktree's git common dir) into the build folder's
  `port/include/gevr_google_oauth.h`. Without it the build has no Google
  Drive and the launcher shows nothing. The Mac's checkout needs its own
  copy of the file for the Apple builds.
- `port/pc/gevr_sync_files.cpp`: ROMs and packs (indexes, content ids, the
  move rule, zip with miniz, unpack with `unzipBounded`, the ready/ folder).
  The store interface's blob calls: the folder store copies, Drive's uses
  the resumable upload, `alt=media` downloads and DELETE.
- `port/vr/gevr_sync_ui.h`: Play > Sync (header only, so the launcher's
  frame test draws it in its longest states).
- `.gevr-sync/` also holds `files-mine.txt`, `files-state.txt`, `ready/`,
  `work/`, `packs.txt`, `name.txt` and `use-save.txt`.
- Hooks: main.c (launch), config.c and vr_settings.cpp (settings written),
  libultra.c (EEPROM written), vr_launcher.cpp (the note under START, the
  progress bar, the ROM list after a ROM came, gevrSyncKick when a ROM is
  chosen), gevr_bean_import.c (gevrSyncKick after a pack import).
- Builds: PC flat and VR, Linux, Apple (port/pc globbed), Quest and Frame
  APKs (listed in CMakeLists.txt). Not the Android phone APK (no native
  services there yet) or the server.
- Tests: `python port/tests/test_sync.py` (simulated devices with their own
  clocks, then the real glue launching save folders as separate processes);
  `python port/tests/test_sync_drive.py` (the Drive store and sign-in against
  a stand-in for Google on 127.0.0.1: code, polling, the folder made once,
  renamed and trashed records, two devices, token refresh, a revoked
  sign-in, unlink; then ROMs and packs: a ROM in resumable pieces, a pack
  zipped and unpacked, already here stays, a changed ROM waiting for the
  next launch, the old blob removed, a folder store; the device name in
  the record, a save set aside used again).
- Try it by hand: `GEVR_SYNC_DIR=<a folder>` (or that path in
  `.gevr-sync/folder.txt`) makes a folder the store; two PC run folders
  pointed at one folder sync with each other.

## Plan

1. Done 2026-11-25: the engine, the folder store, the hooks, the shared key
   table (iCloud uses it too), tests.
2. Done 2026-11-25: Google Drive (store, device-code sign-in in the
   launcher's Play > Sync, unlink, signed-out handling), tests. Tested live
   against the owner's Drive on 2026-11-25: sign-in, the folder and
   records made, a second device joining, a pushed change arriving, unlink
   revoking the token (the other copy of it then signed itself out).
   Trap: the first sign-in showed "Access blocked: has not completed the
   Google verification process" though the account was a test user; the
   same code worked a few minutes later.
3. Done 2026-11-25: ROMs and packs (above), tests against the stand-in.
   Live on the owner's Drive the same day (after the review fixes): a 5 MB
   ROM in resumable pieces and a pack zip from a PC folder to a Quest-layout
   folder (ROM into files/data, pack into texture-packs), identical by
   SHA-256, and nothing moving on the next pass. The test files were then
   deleted from the Drive.
4. Done 2026-11-25: the QR code and Open in the browser, device names, the
   saves set aside with Use this one, both devices of a player kept in one
   match. Not done: the one-tap browser sign-in (needs Desktop and iOS OAuth
   clients).

Owner setup (2026-11-25): Google Cloud project "GoldenEye VR", Drive API on,
OAuth client "GoldenEye VR devices" (TVs and Limited Input), consent screen
External, in Production since 2026-11-25 (owner: domain and branding set up). The client ID and secret
live only in the main checkout's ignored `build/google/oauth.properties`
(`gevr.googleClientId`, `gevr.googleClientSecret`); builds read them from
there, else build without Google Drive. Data access: `drive.file` (done).
Before release: the privacy policy's Google Drive section (Limited Use). Later: Desktop and iOS clients for
browser sign-in.
