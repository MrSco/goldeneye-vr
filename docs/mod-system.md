# Mod system (spec, draft)

Status: proposal, nothing implemented. Written 2026-11-23 after the Tactical
PRs (#285–#289) were declined. This spec lives on the `mod-system` branch
and is tuned with mod authors' feedback: propose changes as pull requests
against that branch (or an issue). Tactical is the first mod built to it.

## Goals and rules

- GoldenEye VR carries **no mod's code**. A mod is a pack the player
  installs; the launcher lists it and can start it.
- Mods work on **every platform** we ship: Quest, PC (flat and PC VR),
  Linux, Android, iOS, Apple TV, Vision Pro, and the dedicated server.
  So mod logic is a script, never a native library (Apple platforms can't
  load one).
- **The mod's author owns the mod.** We own the spec and the API. If a
  game release breaks a mod that only uses the API as documented, that's
  our bug. If it relies on anything else, it's theirs. A release never waits
  for a mod.
- We take a mod only when it's finished. A mod in development is the
  author's local install, not a launcher entry.
- The game stays fast: rendering stays native, scripts react to events,
  and mods share a per-frame time budget.
- **Performance floor: Quest 2 at 90 fps or better.** The slowest headset we
  support sets the bar. A mod that can't hold 90 fps on a Quest 2 in its
  heaviest mission isn't ready, and fixing it is its author's job. It can
  still be listed, with a performance warning. Faster headsets get the same
  mod at their own rates (Quest 3 at 120 Hz).
- **A mod's own systems are Lua in the mod.** Its inventory, magazines,
  menus, loadout screens, rules and network state are the mod's scripts and
  data. The API only reports what the game does and lets a script steer
  systems the game already has. See "What the API covers" below.

## Pack format

A pack is a zip that unpacks into `texture-packs/mods/<id>/` (the Quest's
`files/`, `$S` on PC and Apple), beside today's packs:

```
mod.json            manifest (below)
scripts/main.lua    entry point; other .lua files under scripts/
assets/...          models, textures, data the scripts load:
  models/<Name>     a cartridge model file in the cartridge's place (Assets)
  textures/...      PNGs named as GLideN64 ("Rice") packs name them
files.sha256        checksum list of every other file
```

- The zip holds these at its top, or inside one folder (as a release zip
  often does). What a computer adds by itself (`__MACOSX/`, `.DS_Store`,
  `Thumbs.db`, `desktop.ini`) is left out. Anything outside `scripts/`
  (`.lua` only) and `assets/` is refused, except `.md` and `.txt` files at
  the top (a readme, a licence).
- `files.sha256` is `sha256sum`'s own output: a `<sha256>  <path>` line
  (or `<sha256> *<path>`) for every file but itself, each once, with `/`
  between folders. From the mod's folder:
  `find . -type f ! -name files.sha256 -printf '%P\0' | xargs -0 sha256sum > files.sha256`
- Names are printable ASCII without `\ : * ? " < > |`; no part is `.` or
  `..`, ends in a dot or a space, or is a Windows device name (`CON`,
  `NUL`, `COM1`...); no two names differ only in case. Every platform can
  then hold the same files.

`mod.json`:

```json
{
  "format": 1,
  "id": "tactical",
  "title": "Tactical",
  "by": "Kelly-D-Ripper",
  "version": "1.0.0",
  "api": 1,
  "kind": "campaign",
  "base": "ge",
  "coop": true,
  "site": "https://github.com/..."
}
```

- `id`: 1 to 32 lowercase letters, digits and `-`, not starting or ending
  with `-`; unique. It names the save file, the ghost tag and the folder.
- `title` and `by`: up to 48 plain ASCII characters. `version`: up to 24,
  no spaces. `site` (optional): an `https://` address.
- `api`: the API version the mod was written for (see Versioning).
- `kind`:
  - `campaign`: a separate entry in the campaign list, its own saves.
  - `overlay`: changes the current campaign (a look or a rule), selected on
    the Mods page like Oddjob's hat today.
- `base`: which cartridge it runs on: `ge`, `gf`, `tnd`, `mirror` or `mario`.
  The base ROM must be installed.
- `coop` (optional, `false` when left out): whether the mod supports
  online co-op (see Multiplayer).
- `affectsPlay` (overlays): `true` when the overlay changes rules, not only
  looks. It is then checked in the lobby (see Multiplayer).
- `modes` (overlays that affect play): the multiplayer modes it works in,
  from `coop`, `deathmatch` and `horde`.
- Keys this build doesn't know are skipped, so a later format can add some.
- The content ID is the SHA-256 of `files.sha256`, computed at install. It
  identifies the exact build of the mod.

## Installing

- **v1: install from file** on the Mods page (zip picker; adb push or a file
  manager on the Quest). We don't distribute mod content.
- **Later: a mod browser.** A `mods.json` index in the public
  goldeneye-vr-mods repo, holding only names, authors, descriptions,
  screenshots links and release URLs (no assets). The launcher's Mods page
  browses it under "Community mods", downloads from the author's release
  and installs it the same way as a file, with a line saying these are
  third-party. A mod gets listed when it's finished and its author asks.
  The author shows a perf log from a Quest 2 in the mod's heaviest mission;
  a mod that doesn't hold 90 fps there is listed with a performance warning
  on its row. We can delist it. Design the v1 installer so the browser only adds a
  download step in front of it.
- **One installer for every platform:** `port/src/gevr_modpack.c` (C,
  miniz) on the headset, PC, Apple, Android phones and the dedicated
  server. Each platform's file picker only hands it a zip, as an open file
  (`gevrLauncherModPicked`), and it installs on a thread of its own:
  - Quest and Android phones: the system picker; Java hands over the file's
    descriptor (`MainActivity.nativeModPicked`), so nothing is copied and
    a 2 GB mod needs no second 2 GB.
  - PC (Windows, Linux): the open dialog; the player's own file is read
    where it is.
  - iPhone and iPad, Apple TV (its upload page) and Vision Pro: the
    system's copy, deleted once installed. Klepton: its staged copy, the same.
- The Mods page's MODS row: **Install from file...**, the install's
  progress with Cancel, its outcome on the line, and **Installed (n)...**,
  which lists each mod (version, author, campaign or overlay, why it can't
  run) with Remove.
- Install checks, all before anything is in place: the manifest (above),
  `api` in the range this build runs, every file against `files.sha256` as
  it unpacks, the names (above), and the limits: 10,000 files, 256 MB a
  file, 2 GB in all, `mod.json` 64 KB, `files.sha256` 2 MB. The zip unpacks
  beside the installed mods (`.<id>.unpacking`) and only then replaces
  `<id>`. A failure or a cancel leaves the installed mods as they were; a
  crash's leftovers are tidied at the next start.
- Update: same as packs today (a newer release in the index or a newer zip).
- Today's own packs (HD, Bean, GE-X, Oddjob's hat) don't change.

## Launcher

- Installed `campaign` mods appear in the campaign list after the hack
  campaigns, titled from the manifest, with "(mod)" and the author in the
  tooltip. One whose cartridge's ROM is missing is listed greyed, "needs
  Goldfinger 64". Choosing it sets both the cartridge and the mod.
- A mod campaign stays on its own cartridge: choosing another campaign,
  adopting a hack's ROM, a missing ROM's fallback or joining a listed game
  of another campaign turns the mod off. START waits while an install runs.
- A mod whose base ROM is missing, or whose `api` this build doesn't
  support, shows as unavailable with the reason. Starting it never falls
  back silently to another campaign without a note (as the hacks do today).
- A mod campaign is a cartridge plus a mod: `VrCampaign` keeps naming the
  cartridge (the manifest's `base`) and `CampaignMod=<id>` in
  goldeneye-vr.ini names the mod. The tables keyed by cartridge (ROM
  layouts, names, the lobby's campaign part) don't change. Only what sets
  a mod campaign apart reads the id: the save file, the ghost tag, the
  launcher's list, the friends and lobby names, the content check. No
  per-mod entries in our code.
- At start (`romdataInit`) the chosen mod is checked again: its manifest,
  its `api`, every listed file present. One that can't run is off for that
  run, and the launcher says why (`gevr_campaign_failed.txt`, as for the
  hacks). If its cartridge doesn't load, the mod is off too and GoldenEye
  starts, with a note naming both.

## Saves and records

- A campaign mod saves to `eeprom-mod-<id>.bin`.
- Mod settings and data the script stores: `$S/mod-data/<id>/` through the
  API (`mod.save`, `mod.setting`), at most 1 MB. It sits outside the mod's
  install folder, so an update, which replaces that folder, keeps it.
- Speedrun ghosts use the tag `mod-<id>`. Online ghost boards are off for
  mods in v1 (the content ID would need to be part of the board key).
- iCloud sync leaves mods out in v1.

## Multiplayer

- Co-op in a mod campaign needs the same mod on every machine. The lobby's
  content check (`net_protocol.h` `NET_CONTENT_*`) gets one part: the mod's
  content ID. The host refuses a joiner without the same mod, and says
  which mod and version is needed.
  - The part (`NET_CONTENT_MOD`) is the content ID's first 8 bytes, or 1
    for "no mod". Every peer sends it (protocol 28), since a part that is 0
    on either side counts as unknown and is let through.
  - A host with a mod refuses a joiner without it or with another build;
    a host without one refuses a joiner with one. The mod is named before
    the campaign, as it names its cartridge too.
  - Before the refusal the host sends `NET_MSG_MOD_REQUIRED` (its mod's id,
    title and version), so the joiner reads "this party plays the mod
    Tactical 1.0.0: install it on the Mods page", "...and yours is 0.9", "a
    different build", or "this party plays without a mod".
  - Before the game starts, the launcher's chosen mod is the one checked
    (`gevrModPlay`), as the chosen cartridge is.
- The internet lobby lists a mod campaign's game under its cartridge (the
  lobby service is unchanged); friends see the mod's title in presence.
- An `overlay` mod that changes only looks is not checked: like Bean or HD
  today, it doesn't matter what another player sees. An overlay that changes
  rules declares `"affectsPlay": true` and is checked like a campaign mod.
- Mods with `"coop": false` aren't offered in online co-op: the Online
  page says the campaign is for one player, and neither Friends panel
  joins or invites. A mod campaign plays co-op only, like a hack's.
- A dedicated server's mod campaign comes from `CampaignMod=` in its
  goldeneye-vr.ini, checked at its start like any other.
- Deathmatch and Horde: campaign mods stay out (those modes use GoldenEye's
  maps and tables). An overlay that changes rules works in any mode **only
  when every player has the same mod** (same content ID, checked in the
  lobby like co-op). It declares the modes it supports in the manifest
  (`"modes": ["coop", "deathmatch", "horde"]`); a lobby whose host has
  it on refuses joiners without it. Bots follow the host's rules.
- Scripts run on every machine. The host is authoritative, as for guards
  today: the API has `mod.net.send` / `mod.net.on` over one generic message
  (`NET_MSG_MOD`, at most 1 KB per message, rate-limited; the calls are
  under API v1's "Where each hook sits", Network). The mod decides what it
  syncs. The dedicated server runs scripts too.
- **Where an event fires:** on the machine where the game already settles
  that outcome (`net_coop.c`).
  - **Guards:** the host runs every guard's AI and applies all damage to
    guards; a client only reports its hits. Guard events, and
    `before_damage` for a guard, fire on the host only (except
    `guard_spawning` for the setup's guards at load, below). A client's
    script that needs them gets them from the host over `mod.net`.
  - **Players:** a player's shots, throws and damage are settled on that
    player's headset, so `weapon_fire_attempt`, and `before_damage` and
    `damage_applied` for that player, fire there.
  - **Guards:** calls that change a guard (spawn one, give one a weapon)
    work on the host only and return false on a client. The game's co-op
    sync carries the result to the others, as it does for the host's
    spawns today.
  - **Pickups:** the setup's pickups are shared. Each headset places its
    own copy at load, and when a player takes one the others' copy goes too
    (`NET_MSG_OBJECT_STATE`, by the record's setup index; `net_core.c`
    `netSendObjectEvent`). A swap keeps the record's index and kind, so
    this holds for swapped pickups as long as every headset swaps the same
    way (`mod.random`, below). What the game makes at run time, such as a
    dead guard's gun, is per headset (`docs/issue-94-coop.md`). So is a
    pickup a mod spawns (not built yet): it's placed only on the headset
    that calls it, so a mod that wants every player to have it calls it on
    every headset (mid-mission, by sending the choice over `mod.net`, say
    from the host's `guard_killed`), and settles over `mod.net` which
    player may take it, if it limits that.
- **The same choices on every headset.** Each headset loads the mission
  itself from a shared seed, and the setup's guards and pickups line up
  slot for slot. Events during load (`pickup_spawning`, and
  `guard_spawning` for the setup's guards) run on every headset and must
  choose the same on all of them. The sandbox replaces `math.random` with
  `mod.random`, which in co-op is seeded from the seed every headset shares
  for the mission (`gevrCoopIntroSeed()`). A choice made from anything
  else, like a player's own save, can differ.
- **Weapon stats belong to the weapon type** (magazine size, fire rate,
  damage), not to one gun, so a change applies to everyone holding that
  weapon. Every headset sets the same values, and the game puts the ROM's
  values back at mission end.
- **A mod turned off on one headset** (a script error, or over budget) is
  turned off on every headset: that headset tells the host and the host
  tells the others. Otherwise the players drift apart for the rest of the
  mission. Built as `NET_MSG_MOD`'s `NET_MOD_OFF`: each headset that hears
  it stops the mod for the mission, puts back what it changed and shows a
  note ("it stopped on another player's headset"), without telling anyone
  again. In solo a stop is told to no one.

## Scripting

- **Language:** Lua 5.4 (MIT licence, plain C, builds on all our targets
  including the server). The interpreter only: no JIT (iOS forbids it).
  Lua 5.4.7, vendored unchanged in `port/external/lua` and built as its own
  library (`gevr_lua`); the script host is `port/src/gevr_modscript.c`.
- **Sandbox:** no `io`, `os`, `package`, `debug` or `load` of binary chunks.
  Files only through the API, inside the mod's own folder.
  - Open: the base, coroutine, math, string, table and utf8 libraries.
    Gone: `dofile`, `loadfile`, `collectgarbage`, `string.dump`,
    `math.randomseed`; `load` takes text only; `print` is `mod.log`.
  - `require "a.b"` loads `scripts/a/b.lua` from the mod, once. The entry
    point is `scripts/main.lua`; a mod without one is data alone and runs
    nothing.
- **The calls of phase 3** (the lifecycle row of API v1, and storage):
  - `mod.on(event, function)`: `campaign_enter()`, `mission_prepare(stage)`,
    `mission_start(stage)`, `mission_end(stage, "complete" | "left")`,
    `tick(dt)` (seconds, at most 0.25). An unknown event is an error.
  - `mod.log(...)`: one log line, `mod <id>: ...`, at most 30 a second.
  - `mod.setting(name, default)`: the player's choice for it
    (`settings.txt` in its data folder, `name=value` lines), as the
    default's type, else the default.
  - `mod.capabilities()`: `vr`, `hands`, `headless`, `platform` (`quest`,
    `android`, `pc`, `ios`, `tvos`, `visionos`, `server`).
  - `mod.random([m [, n]])`, also `math.random`: seeded each mission, in
    co-op from the seed every headset shares.
  - `mod.save.read/write/remove(name)`: files in its data folder; names are
    letters, digits, `_`, `-` and `.`, not first a dot; writes are whole or
    not at all.
  - `mod.id`, `mod.title`, `mod.version`, `mod.api`.
- **Budget:** 0.25 ms per frame for **all mods together**, script time and
  garbage collection included. Why: the game's own logic is about 1.5 ms
  of the early tick (4.1 ms mean), and heavy levels already run DRAW/TICK
  at 7.3–8.3 ms (docs/bean-performance-*.md, docs/jungle-bean-perf.md).
  0.25 ms is about 2% of a Quest 2 frame at 90 Hz (11.1 ms), 3% of a
  120 Hz frame (8.33 ms) and a sixth of the game's logic: room for several
  hundred script calls on Quest. An event-driven mod should use a few
  hundredths of a millisecond.
  - Checked as an average over one second, so one heavy event (a mission
    start, a burst of pickups) doesn't trip it. Lua's garbage collector runs
    in small steps inside the budget.
  - A count hook stops a single runaway call (an endless loop) at once:
    over 100 ms in play, over 2 s while loading.
  - Over budget for 3 seconds: the most expensive mod is paused for the
    mission with a note on screen (in co-op on every headset, see
    Multiplayer).
  - The perf log (`--perf-log`) shows each mod's time; `mission_start`
    and loading are not counted (loading screen).
  - The budget is held on the **Quest 2 at 90 Hz** (11.1 ms frames, the
    slowest CPU we support), not only on faster headsets. On a Quest 3 the
    same scripts take less time.
  - The number is a starting value, dialed in as the first mod (Tactical)
    is built to the spec and we see how it runs. Phase 7's runs on the heaviest levels
    (Jungle, Bean, body piles) with the sample mod, on a Quest 2 at 90 Hz
    and a Quest 3 at 120 Hz, confirm or change it.
- **Memory:** an allocator cap per mod (start at 32 MB).
- **Errors:** a script error disables that mod for the rest of the
  mission and logs the error with the mod's name and a traceback, never
  crashing the game; a note on screen says so. One in `scripts/main.lua` or
  `campaign_enter` turns it off for the run (it plays without its scripts).
  Whatever the mod changed goes back (weapon stats return to the ROM's
  values); pickups it swapped stay as they were placed until the mission
  ends, and what it gave or took stays the player's. In co-op it is turned
  off on every headset (see Multiplayer).
  Crash reports name the active mods.

## API v1

Events and calls, all on the game's tick, never per draw call. This list
is the proposal to refine with mod authors (Tactical first).

| Area | Events | Calls |
|---|---|---|
| Lifecycle | `campaign_enter`, `mission_prepare`, `mission_start`, `mission_end`, `tick(dt)` (capped) | `mod.log`, `mod.setting`, `mod.capabilities` |
| Weapons | `weapon_fire_attempt`, `weapon_fire`, `reload_attempt`, `reload_start/end`, `weapon_drawn`, `weapon_dropped` | read and set loaded ammo, magazine size, fire rate, damage (within limits) |
| Hands (VR) | `grip`, `release`, `hand_near(anchor)` | hand pose, body anchors (body slots), haptics |
| Items | `pickup_spawning` (can swap a weapon, ammo or armour pickup at its existing pad), `item_pickup`, `item_spawned` | spawn a pickup, take or give an item |
| Guards | `guard_spawning`, `guard_spawned`, `guard_alerted`, `guard_shot`, `guard_killed`, `corpse_retiring` | read state, give a guard's weapon or outfit (from the ROM's bodies), spawn a guard and send it along a patrol path through the game's own spawn and AI commands |
| Damage and player | `before_damage`, `damage_applied` (a player's too: no separate `player_damaged`), `player_downed`, `player_revived`, `player_died` | health and armour |
| Assets | — | the pack's model files in the cartridge's place and its textures over the HD pack (no call: they're there); attach a model to a hand, gun or body slot (next); `mod.audio.play` (ROM sound effects only), `mod.effect.play` |
| UI | `ui_click`, `ui_change` | a page on the in-game screen from a simple layout (text, toggles, choices, sliders, buttons): `mod.ui.page`, `remove`, `open`, `close` |
| Network | `peer_ready`, `mod.net.on` (includes the sender's player ID) | `mod.net.send`, `mod.net.role`, `mod.net.slot` |
| Storage | — | read and write in `mods/<id>/save/` |

Not in v1: drawing, shaders, level geometry, new AI behaviour, the
multiplayer modes, anything per vertex or per draw call.

### What the API covers

Each event and call is a thin adapter over a system the game already has
(for example the body slots, hand reload, pickups, guard spawning, the
in-game screen). We maintain the adapters, not the mods.

- **In the API:** events for what the game does, a way to cancel or change
  it at that moment (a shot, a pickup, damage, a guard's loadout), and calls
  that drive existing systems.
- **In the mod, as Lua:** anything the game doesn't already do. Per-item
  state (magazines, chambers, attachments, durability), inventories and
  transfers, loadout and Workshop screens, custom rules, and what the mod
  syncs between players and how it settles conflicts (`mod.net`, the host
  decides). The mod's menus are built from the in-game screen's page
  layout (`mod.ui`).
- **New game systems** (a new kind of menu, per-instance weapon state, new
  rendering features, new network channels) are not mod requests. They join
  the API only as game features: useful beyond one mod, contributed or
  agreed as part of the game, and working on every platform we ship
  (Quest, PC, Linux, Android, Apple, the dedicated server) with tests. Until
  then a mod does without them or does them in Lua.
- A request for the API names the existing game system the adapter would
  sit on. If there isn't one, it's a new game system (above).

### Where each hook sits

- **Campaign and mission.** Emit `campaign_enter` once the selected
  campaign's base ROM is loaded by `romdataInit()`. Emit `mission_prepare`
  in `lvlStageLoad()` before `proplvreset2()` sets up mission props and
  guards. `mod.capabilities` reports VR hands from the active stereo
  mode (`g_gevrStereo`) and VR readiness (`gevrVrReady()`). Headless
  capability comes from the build state (`GEVR_HEADLESS`).
- **Shots and throws.** Emit `weapon_fire_attempt` before ammo is spent
  in `gunTickHandState()` for trigger shots and `gevrMotionThrowTick()`
  for VR throws. A script can cancel either action. Loaded ammo uses the
  hand's existing `weapon_ammo_in_magazine` count, read by
  `get_ammo_in_hands_magazine()`.
  - Built (`port/src/gevr_modapi.c` over `src/game/gevr_modgame.c`; the
    hooks in `gunfire.c` and `bondview2.c`): a shot is asked once per
    shot (an automatic, at each of its rate's ticks), after the firing
    switch and before the round, the sound, the beam and the damage. A
    declined shot clicks dry as an empty gun does (`DRY_FIRE`, nothing
    more until the trigger is let go). A grenade, knife or mine is asked
    as the hand lets go, a motion throw before it leaves the hand, a
    cooked grenade's drop too; declined, nothing leaves the hand and a
    cooking grenade stops cooking. Melee isn't asked.
  - `reload_attempt(hand, item)` before a reload takes rounds from the
    reserve: the native reload as it starts to lower the gun (the button,
    an empty gun's automatic reload, and the hand reload's gestures that
    use it: the chest cross, the belt's touch for a gun without a
    magazine), and, with hand reload, a fresh magazine taken at the belt
    by either hand, a dual-wielded gun's touch at the belt, and a round
    taken for a gun loaded a round at a time. Hand reload's magazine path
    is the same for the original, GoldenEye X and Bean models. A handler
    returning false declines it: the gun stays as it is and nothing
    leaves the reserve. An empty gun's automatic reload, declined, isn't
    asked again until the hand's item or its reserve changes; a reload
    the player asks for is always asked. A magazine pulled out and put
    back isn't asked: its rounds aren't the reserve's.
  - `weapon_fire(hand, item, loaded)` after each spend; `reload_start` as
    the native reload lowers the gun or a hand reload's magazine leaves
    it (`gevrGexMagOut`); `reload_end(hand, item, loaded)` after the
    native reload's ammo move or a magazine seated (`gevrGexMagIn`).
    Each `reload_end` follows a `reload_start`: a magazine topped up at
    the belt while still in the gun sends its start as it goes in.
    `weapon_drawn` at a switch's end. A hand reload's single rounds
    (`gevrGexRoundTick`) send no `reload_start` or `reload_end` yet.
  - The reserve moves without an event in three places a script that
    keeps magazines itself should know: the native reload and a fresh
    magazine top the gun up from it (loaded becomes the lesser of the
    magazine size and loaded plus the reserve) before `reload_end`; a
    drawn gun is topped up the same way before `weapon_drawn` (unless
    hand reload kept its magazine); and a magazine dropped from the hand
    or the gun gives its rounds back to the reserve. Setting the reserve
    and the loaded rounds in `reload_end` and `weapon_drawn`, and
    declining in `reload_attempt`, covers all three.
  - Only for this machine's human player (`!netIsActive() || the current
    player == netGetLocalSlot()`), never a copy or the host's bot. `hand`
    is `"right"` or `"left"` (the game's gun hands); `item` an id from
    `mod.items`.
  - Calls: `mod.weapon.held(hand)`, `loaded(hand)`, `setLoaded(hand,
    rounds)` (0 to the held weapon's magazine), `stats(item)` (`magazine`,
    `fireRate` for an automatic, `damage`) and `setStats(item, t)`:
    magazine 1 to 255, fireRate 1 to 60 ticks (automatics only), damage 0
    to 10 times the weapon's own; only items with stats of their own.
    Stats are per weapon type, guards' too, and go back to the game's own
    at a mission's end and when a mod stops.
  - Scopes (VR): `mod.weapon.scope(hand, t)` puts the game's per-hand
    scope (issue #40, `gevrScopeBeginHand()`: the world drawn again
    through a camera on the shot's line, shown as a lens on the gun) on
    the gun the hand holds. `t = { at = { x, y, z }, radius = r, zoom = m }`:
    the eyepiece's centre and radius in the drawn model's own coordinates,
    as a model editor shows them (the game's own table, `s_gevrScopes`,
    is in the same units: the sniper rifle's is `13.5, 126.5, -128, 23.5`),
    and `m` the magnification, 1 to 25. In model units, the lens follows
    the player's gun size, the gun's place in the hand and a GoldenEye X
    gun's mirroring in the left hand, as the game's own scopes do; it has
    no Gun fit trims of its own. Calling it again (a new `zoom`, say)
    changes it at once. `false` takes the scope off, even the game's own
    on its scoped guns (the KF7's and AR33's magnifiers, for iron sights);
    `nil` gives the game's own back. It holds for the gun in the hand now:
    each draw puts the game's own back, so `weapon_drawn` is the place to
    set it, and it goes when a mod stops. True when set, false with
    nothing in the hand; it shows only in VR, and each lens costs a draw
    of the world, as the game's own do (two with a scope in each hand).
    The reticle is the game's red sight, with the "Aim: crosshair"
    option, as on the game's own scopes. `mod.weapon.look(hand)` says
    which models the hand's gun is drawn with (`"original"`, `"gex"`,
    `"bean"`), so a mod can keep an eyepiece for each.
  - Known gaps: on a pump gun a lower `setLoaded` reads as a shot (the
    rack, `gevr_rack.h`); in co-op another headset's copy of the player
    still shows a declined shot's fire (the fire bit comes from the
    trigger and the rounds), for the network step (5).
- **Pickups.** During `proplvreset2()`, emit `pickup_spawning` only for
  loose weapon, ammo and armour pickups at existing pads. A script can
  replace an eligible pickup before its placement by `weaponAssignToHome()`
  or `domakedefaultobj()`. Keys and other mission items use their native
  setup. Weapons flagged `PROPFLAG_ASSIGNEDTOCHR` use `guard_spawning`.
  Which pickups qualify, and what a swap can do: "Pickup swaps" below.
  - Built (`port/src/gevr_modapi.c` over `src/game/gevr_modgame.c`; the
    hooks in `prop.c` and `propobj.c`): before the stage's first record is
    placed, `gevrModPickupScan()` notes the records a swap leaves alone.
    Each eligible record is asked as it's about to be placed: a weapon in
    `weaponAssignToHome()` after multiplayer's weapon set, before its
    models load; armour once its amount is read; a box or a crate before
    `domakedefaultobj()` and the crate's rounds' models. Asked while the
    mission loads (the loading time limit, outside the frame budget).
  - `item_pickup(kind, id, rounds)` in `propPickupByPlayer()` once the
    game has taken the pickup, before a freed one is gone, for this
    machine's human player: `"weapon", item`; `"ammo", type, rounds` (a
    crate sends one for each type it held, its 9mm_2 as 9mm, solo's
    multiplier applied); `"armour", amount`; `"key"`; `"object"` for
    anything else (a mission document, say). It reports; it can't refuse.
  - Calls: `mod.inventory.has(item)` (any item), `give(item)` and
    `take(item)` (guns and thrown weapons only, the swap list below).
    `give` returns true when the item is new and brings no rounds; remote
    mines bring the detonator, as picking them up does. `take` returns
    true when the item was there, takes its dual pair too, and a hand
    holding it goes empty (the gun hand to its fist, the other unarmed).
    `mod.ammo.get(type)` and `set(type, rounds)` read and set the spare
    rounds (not the loaded ones, `mod.weapon.loaded`), 0 to
    `mod.ammo.max(type)`. `mod.ammo` also names the types: `9mm`,
    `9mm_2` (the Klobb's), `rifle`, `shotgun`, `grenade`, `rockets`,
    `remotemine`, `proxmine`, `timedmine`, `knife`, `grenaderound`,
    `magnum`, `goldengun` (1 to 13).
  - Not built yet: spawning a pickup at run time, `item_spawned`, and a
    pickup a mod can refuse.
- **Hands (VR).** Built (`port/src/gevr_modapi.c` over
  `src/game/gevr_modgame.c`; the hooks in `bondview2.c` and
  `gevr_bodyslots.c`), for this machine's human player in stereo play.
  `hand` is the game's, as in the weapons area: `"right"` is the gun hand
  and `"left"` the off hand, whichever controller holds each (left-handed
  mode swaps them).
  - `grip(hand, anchor, taken)` and `release(hand, anchor)`: the grip as
    the game reads it (`port/src/input.c`'s poll, the press the grip
    gestures take), told on the next game tick, once
    `gevrGripGestureTick()` has decided the press. `anchor` is the body
    slot the hand is in, or nil. `taken` is true when one of the game's
    grips used it: a slot drew or put away, the reload belt, a force
    pull's catch, a door or a switch. A mod can't take a grip from the game.
  - `hand_near(hand, anchor)`: the hand came into a body slot, with the
    slot's own buzz. Only with the Body slots option on, and only at a slot
    with something there for that hand.
  - Anchors: `hip_gun`, `hip_off`, `back_gun`, `back_off`, `chest` and
    `belt`, the body slots' places, each named for the hand whose side
    it's on.
  - Calls: `mod.hands.position(hand)` gives x, y, z and
    `mod.hands.anchor(name)` x, y, z and the anchor's reach, in cm, in the
    body slots' torso frame: x toward the gun hand's side, y up, z ahead,
    from the eye's place upright over the torso. They work with either
    handedness and with the slots off, so a mod can place its own pouches
    from them; nil without VR hands in play or for a hand that isn't
    tracked. `mod.hands.haptic(hand, amplitude, seconds)`: a pulse,
    amplitude 0 to 1, up to 0.5 s. The player's Haptics settings have a
    Mods row that scales its strength and caps its length (0 turns mods'
    pulses off); false without VR haptics.
- **Guards and corpses.** Emit `guard_spawning` during
  `expand_09_characters()` and `chrSpawnAtPad()`. Allow a ROM body and
  head choice before the model is created and a native weapon choice
  through `weaponAssignToHome()` or `chrGiveWeapon()`. Guard spawning and
  patrol calls use `AI_TRYSpawningChrAtPad` (`chrSpawnAtPad()`) and
  `AI_StartPatrol` (`if_actor_able_set_on_path()`). Emit `guard_alerted`
  once per transition into alert. Report `heard` for noise through
  `chrCheckGuardsHeardSound()` and `chrlvAlertGuardToPlayerPosition()`,
  `saw` when the sight check finds the player (`chrCheckTargetInSight()`),
  `shot` when a guard turns on the player after being hit
  (`chrlvTickArgh()`), or `alarm` for an alert through `alarmActivate()`.
  Not `chrlvSetTargetToPlayer()` itself: it also runs on every tick of an
  attack and after a sidestep or jump-out. Emit `corpse_retiring` in
  `chrlvTickDead()` before the body is marked for removal, while its
  position is available.
  - Built, part 1: the events and reading a guard (`port/src/gevr_modapi.c`
    over `src/game/gevr_modgame.c`; the hooks in `chraction.c`, `chrai.c`,
    `chr.c` and `prop.c`). A guard's id is its `chrnum`: the setup's for
    its own guards, the game's own number for one it spawns.
  - `guard_spawning(g)` in `expand_09_characters()`, for the setup's
    guards as the stage loads, on every headset: after the game picks the
    body and head, before the model is made. `g` has `id`, `index` (the
    setup record's), `body`, `head` (nil for a body with its own) and
    `weapon` (its setup gun, nil for none). A handler returns nil to keep
    the guard, or a table that changes `body` (a body row of the
    cartridge's table, not a head's), `head` (a head row; none for a body
    with its own head, and a body that needs one and gets none has the
    game pick it as it would) or `weapon` (a gun, `ITEM_WPPK` to
    `ITEM_ROCKETLAUNCH` without the watch laser; it replaces the guard's
    setup guns in `weaponAssignToHome()`, before the enemy-rockets cheat,
    so only a guard with a setup gun can get one). Handlers run in turn as
    for `pickup_spawning`; an answer the game can't take stops the mod for
    the mission and the guard keeps its setup. A guard can't be left out
    yet.
  - `guard_alerted(id, how, player)`, once per guard: `"saw"` before
    `chrCheckTargetInSight()` sets the target, `"shot"` as `chrlvTickArgh()`'s
    reaction ends, `"heard"` in `chrlvAlertGuardToPlayerPosition()`,
    `"alarm"` when the guard's own list runs `AI_AlarmOn`, and `"other"` for
    anything else that turns it on a player (`chrlvSetTargetToPlayer()`,
    say a script's attack). A guard that saw or heard a player before
    (the game's own marks, as a co-op host change carries them) isn't told
    again. `player` is the player slot the guard was acting on (in co-op
    the nearest, or for ten seconds whoever shot it).
  - `guard_killed(id, how, item, player)` as the death begins: `"shot"` in
    `triggered_on_shot_hit()` with the gun's item, `"explosion"` in
    `chrlvExplosionDamage()`. `player` is the slot whose shot hit the guard
    last (`handles_shot_actors()` records every hit, nil for a guard's
    shot; the host applies a client's hit with the shooter current), nil
    for an explosion (its owner isn't known there). Not yet: deaths by
    other ways (horde's nuke, the cheats).
  - `corpse_retiring(id, x, y, z)` in `chrlvTickDead()`, world units, before
    the body is marked for removal. A body the Bodies stay option keeps is
    told when the game retires its id (`gevrBodyRetireForAi()`, after the
    usual fade's time, when reinforcements may take its place), though it
    may lie there a while longer.
  - Alerts, deaths and corpses are told on the machine that runs the
    guards (solo, the co-op host), each once per guard; a slot's new guard
    starts again (`chrAllocate()`).
  - Calls: `mod.guards.list()` (the stage's guards' ids, living or dying),
    `mod.guards.get(id)`: `id`, `body`, `head`, `alive`, `alerted`,
    `weapon`, `weaponLeft`, `health` (what's left of `maxHealth`) and `x`,
    `y`, `z` (world units), or nil. On a co-op client it's the host's
    copy as last sent.
  - Built, part 2: the calls that change a guard. They work where the
    guards run (solo, the co-op host) in a stage; elsewhere they return
    false. The game's co-op sync carries what they do to the others, as
    for the host's own scripts (the spawn's announcement, the guns in the
    guard's state). An order the game can't take (a bad pad, body, head,
    list or gun) is the mod's error.
    - `mod.guards.spawn{pad, body, head, ai, weapon, weaponLeft}` spawns
      through `chrSpawnAtPad()`, as `AI_TRYSpawningChrAtPad` does, then
      arms it with `chrGiveWeapon()`. `pad` is a setup pad, or 10000 and
      up a bound pad, checked against the stage's. `body` and `head` as
      for `guard_spawning`; no head lets the game pick one. `ai` is
      `"standard"` (the default: idles, then clones or runs to attack),
      `"simple"` (stands or patrols until it notices), `"deaf"` (the same,
      by sight only), `"chase"`, or a level's own guard list's id; never a
      subroutine, the alarm raiser (it runs to a preset pad a spawned
      guard has none of) or a level script's list. The game tries the pad,
      then eight spots 60 units around it, each clear and out of the
      player's view (`chrAdjustPosForSpawn()`, as its own scripts usually
      ask); none, or no free slot: false.
    - `mod.guards.giveWeapon(id, item, hand)`: a gun into a living guard's
      hand (`"right"`, the default, or `"left"`); the gun it held goes
      (freed, as co-op's copies lose theirs; a dual pair's other gun is
      unlinked from it).
    - `mod.guards.patrol(id, path)`: `if_actor_able_set_on_path()` along
      the stage's path, as `AI_StartPatrol`; `mod.guards.paths()` lists
      the stage's path ids.
    - `guard_spawned(id)` for a guard the game spawns in play
      (`AI_TRYSpawningChrAtPad`, `AI_TRYSpawningChrNextToChr`, a
      reinforcement's clone once armed and its id settled), where the
      guards run. A script's guard gets its gun from its own list
      afterwards. A mod's own spawn returns the id instead: telling it
      during the mod's call would run its handlers inside that call.
    - `guard_shot` is left to the damage area's `before_damage` and
      `damage_applied`.
- **Damage and revives.** Emit `before_damage` and `damage_applied` around
  native damage in `handles_shot_actors()` and `chrlvExplosionDamage()`
  for guards and `record_damage_kills()` for players. The before event
  allows the script to change or cancel the pending damage. The after
  event reports the result. `player_downed` and `player_revived` report
  the co-op transitions in `coopSetLocalDowned()` and
  `coopAnnounceDowned()`.
  - Built (`port/src/gevr_modapi.c` over `src/game/gevr_modgame.c`; the
    hooks in `chraction.c`, `bondview2.c` and `net_coop.c`). A guard's
    damage is told where the guards run (solo, the co-op host); a
    player's, on that player's own headset.
  - `before_damage(d)` and `damage_applied(d)`, each with a table:
    `target` (`"guard"` or `"player"`), `id` (a guard's chrnum, a
    player's slot), `amount` (the game's units: a guard's as its health,
    `mod.guards.get`'s; a player's, 1 a full bar, before the difficulty's
    handicap), `how` (`"shot"` for a guard; for a player `"explosion"`,
    `"gas"` or `"hit"`, anything else: a guard's shot, an autogun, another
    player), `part` (a guard's shot: `"head"`, `"body"`, `"limb"`, `"gun"`,
    `"hat"`), `item` (a shot's gun) and `by` (the player slot behind it,
    when the game knows: a player's shot, a player's own blast or another
    player's). `damage_applied` adds `health` (what's left, in the same
    units; a player's 0 to 1) and a player's `armour` (0 to 1).
  - `before_damage` is asked in turn, as `pickup_spawning` is: nil keeps
    the damage, false leaves none (no one after is asked), a number or a
    table's `amount` (0 to 1000) changes it. Where: a guard's shot in
    `handles_shot_actors()` once its damage is reckoned (the gun's, the
    part's multiplier, horde's) and before the guard takes it; with none,
    it still flinches, as it was still shot. A shot that does no damage (a
    hat, a gun) isn't asked. A blast in `chrlvExplosionDamage()` before it
    touches the guard (declined, it never hit; any blast that lands kills,
    as GoldenEye has it, so only cancelling changes the outcome). A
    player's in `record_damage_kills()` after co-op's own checks (a
    spectator, a teammate's copy, a player who's down) and before the
    game's own rules (invincibility, the watch, the last hit's flash):
    those can still take it to nothing, and then `damage_applied` isn't
    told.
  - `damage_applied` after the damage is taken: a guard's shot, a blast
    once the guard is dying, a player's when health or armour moved.
    An answer the game can't take is the mod's error, as for the other
    asked events.
  - `player_downed(slot)` and `player_revived(slot)` on every headset, for
    every player: this headset's own as it changes
    (`coopSetLocalDowned()`), the others' as they're announced
    (`coopAnnounceDowned()`). `player_died(slot)` as this headset's player
    dies (`bondviewKillCurrentPlayer()`, once per death; in co-op a player
    goes down instead).
  - Calls: `mod.player.health()` gives health and armour (0 to 1),
    `setHealth(x)` (more than 0, up to 1) and `setArmour(x)` (0 to 1) set
    them; false while the player is dead or down.
- **Sound and effects.** `mod.audio.play` uses `sndPlaySfx()` to play
  an existing ROM sound effect by ID. Positional playback uses
  `chrobjSndCreatePostEventDefault()`.
  `mod.effect.play` selects built-in effects such as smoke and bullet
  impacts through `explosionCreateSmoke()` and
  `explosionCreateBulletImpact()`.
  - Built (`port/src/gevr_modapi.c` over `src/game/gevr_modgame.c`), on
    this headset only: a mod that wants every player to hear or see one
    plays it on every headset, sending the choice over `mod.net`.
  - `mod.audio.count()`: the cartridge's sound effects bank's size; ids
    are 1 to it, as `sndPlaySfx()` takes them (the hack's own bank on a
    hack cartridge). `mod.audio.play(id, volume)` plays one as the game
    plays its own; `mod.audio.playAt(id, x, y, z, volume)` from a place in
    world units, as `chrobjSndCreatePostEvent()` places one (the
    distance's volume, 5000 to 6000 units as `...Default` has it, scaled
    by `volume`; the port's spatial place). `volume` is 0 to 1; without
    it the sound keeps its own. False outside a stage or when the game
    has no voice free.
  - `mod.effect.play(name, at)`: smoke from `explosionCreateSmoke()`'s
    own kinds, `"puff"`, `"smoke_small"`, `"smoke"`, `"smoke_light"`,
    `"smoke_dark"`, `"smoke_large"` and `"smoke_huge"`, where something is:
    `"player"` (this headset's), a guard's id, or `{ pad = n }`. Smoke needs
    a tile to stand on and its rooms, which a bare point doesn't give.
    False when there's no such place; the game drops it when its 20
    smokes are all in use.
  - Not built: bullet impacts. `explosionCreateBulletImpact()` marks a
    surface a shot hit, with its normal and room; a mod has no shot's hit
    to give it.
- **Network.** One message, `NET_MSG_MOD` (protocol 28), sent and
  checked in `net_core.c` (`netSendModMessage()`, `netReceiveMod()`);
  the calls and the held messages are in `gevr_modapi.c`.
  - `mod.net.send(channel, data [, to])`: `data` is a string of any
    bytes, at most 1024; `channel` is 1 to 32 printable characters with
    no spaces; `to` is a player's slot (0 to 7), or left out for everyone
    else. True when sent. False in solo, to this headset's own slot, or
    past the mod's rate: 30 messages and 16 KB a second. The host's to an
    empty seat is false too; a player's goes to the host, which drops it.
    A channel, message or slot past its limits is a script error.
  - `mod.net.on(channel, fn(data, from))`: each message of this mod's on
    that channel, to its handlers in the order they were added. `from`
    is the sender's slot, nil for a dedicated server (it has no slot). A
    mod hears only messages sent by the same mod.
  - Messages wait for the next frame in play and reach the handlers
    before its `tick`, so one that comes while the mission loads waits
    for it to start. A mission preparing drops the last one's. At most 64
    wait; more are dropped, with a log line.
  - Routing: a player's message goes to the host. The host relays one
    for everyone to the others, never back to its sender, and forwards
    one for a single player to that player alone. The sender's slot is
    the seat of the connection it came on: the host drops a message whose
    slot isn't its connection's, and a player takes messages only from
    the host's connection, and only those for everyone or for it. The
    host relays at most 64 a second of each player's. A malformed one (a
    name with spaces or no end, a length that doesn't match) is dropped
    whole.
  - `mod.net.role()`: `"solo"`, `"host"`, `"client"` or `"server"` (a
    dedicated server), from `netIsHost()` and `netIsDedicated()`.
    `mod.net.slot()`: this headset's player's slot, 0 in solo, nil on a
    dedicated server.
  - `peer_ready(slot)` on the host (or the dedicated server) as each
    other player first reaches the stage: the validated stage-ready
    transition in `netReceiveStageReady()`. It waits like a message, so a
    player who loads before the host's mission starts is told as it
    starts.
  - A mod stopped on one headset is stopped on all of them
    (`NET_MOD_OFF`; see Multiplayer).
- **The UI.** A mod's page is a sub-tab under the in-game screen's
  Mods/Fun tab, beside the section's own rows (now under Options): the
  hold-Menu screen, the solo panel and the online match screen's Settings
  tab alike (owner, 2026-11-23: the watch's VR page is gone since the
  launcher unification, so the in-game screen is the place). Built in
  `gevr_modapi.c`, drawn by `port/vr/gevr_modui_ui.h`.
  - `mod.ui.page(id, { title, text, rows, buttons })` makes or replaces a
    page. `title` (1 to 12 characters) names the tab; `text` (up to 160)
    sits above the rows. Up to 8 rows, each with an `id`, a `label` (up to
    24 characters), an optional `tip` (up to 160) and `blocked` (false, or
    the reason it can't be used now, up to 32: greyed with it):
    - `{ kind = "toggle", value = true }`;
    - `{ kind = "choice", choices = { ... }, value = n }`: 2 to 12 names
      of up to 24 characters, `value` the chosen one's number from 1;
    - `{ kind = "slider", min = a, max = b, value = v }`: whole numbers,
      -100000 to 100000, at most 1000 steps.
    Up to 4 `buttons`, each an `id`, a `label` (up to 14) and `blocked`.
    The rows are drawn as the settings' rows are, so they look and fit
    the same (one column, or two when they fit); no Reset.
  - At most 4 pages, every mod's together. `mod.ui.page` returns true
    when the page is made or replaced, and false when all four are taken
    by others (a mod can't know how many the others use); a malformed
    page is still an error. A page stays until the mod removes it
    (`mod.ui.remove(id)`), across missions; a stopped mod's pages hide
    until it runs again.
  - `ui_change(page, row, value)` and `ui_click(page, button)` reach only
    the page's own mod, at its next frame (before `tick`), even while the
    solo panel holds the mission paused. A change is kept on the page at
    once; a value is a toggle's boolean, a choice's number from 1 or a
    slider's number, and a slider dragged sends its last value once. A
    blocked row or button sends nothing. A mission's loading drops the
    clicks not yet sent.
  - `mod.ui.open(id)` opens the in-game screen on the page, as the hold
    opens it: a solo mission pauses under it, and online the match screen
    comes up on Settings. False where there's no screen (the dedicated
    server). Asked during a solo mission's intro (from `mission_start`,
    say), the screen comes up when the intro ends, paused before play:
    the pause is a Start press, which the intro would take as a skip
    (`gevrIntroPlaying()`). `mod.ui.close()` closes the screen, as Resume
    does.
  - The limits keep the fullest page (four pages, eight rows of the
    longest labels, choices and reasons, four buttons) on both screens
    without scrolling at the Quest's font size
    (`port/tests/ingame_fit_native.cpp`).
- **Assets, part one: the pack's files** (owner, 2026-11-23: these first,
  attachments after). A GoldenEye model file has no header of its own: its
  switch and texture counts and its skeleton are the game's, for the file
  it is (SubDrag's editors import into an existing object's slot). So a
  mod's model takes a cartridge model's place, and a mod's textures go over
  the player's HD pack. Both are the run's mod campaign's, the same build on
  every machine of a party (the content check), and need no call.
  `port/src/gevr_modassets.c`.
  - `assets/models/<Name>`: a model file (a gun, prop or character:
    `GpistolZ`, `PcartonZ`, `CdjbondZ`) in place of the cartridge's,
    wherever the game loads it (`ob.c` `load_resource`), on the dedicated
    server too. As the cartridge stores it (Rare's 1172 compression) or
    decompressed, at most 4 MB. It keeps the cartridge's header, so it must
    fit it: one that won't decompress into the room the game makes for the
    cartridge's file, or that `gevr_model.c` can't rebuild with the
    header's counts, leaves the cartridge's, with a log line.
  - GoldenEye X's guns, when they're on, are drawn instead of a mod's
    (theirs are whole models of their own). The patches made for the
    cartridge's meshes leave a mod's model alone: the VR hands' closed
    shells, Oddjob's hat, Bean's meshes and the magazines taken out by
    hand (all measured on the cartridge's). So with Bean on, a model the
    mod replaces keeps the mod's look, and a gun it replaces has no
    cartridge magazine to take out.
  - `assets/textures/`: PNGs named `IDENT#CRC#F#S[#PAL]_all.png` as an HD
    pack's are, in any folders, read as a layer of their own
    (`gevr_texpack.cpp`): asked first, over the player's pack and Bean's
    fill, and kept when the player switches HD textures off. Bean's own
    pictures are bound before any pack is asked, so they win where Bean
    has one. No HD pack needs to be chosen. A mod that can't start shows
    none. They match textures by content, so a mod over Goldfinger 64 or
    Tomorrow Never Dies names that cartridge's own.

### Pickup swaps

`pickup_spawning` rewrites a pickup's setup record before the game places
it, the way multiplayer weapon sets already do in `weaponAssignToHome()`:
a new `weaponnum`, `obj` and `extrascale`, or no pickup at all. A gun
swapped in is then preloaded through `weaponLoadProjectileModels()` like
any other gun in the level.

A pickup can be swapped only when both of these hold:

- **It's an eligible kind.** Of the kinds a player can collect
  (`objIsCollectable()`):
  - Weapons (`PROPDEF_COLLECTABLE`, not `PROPFLAG_ASSIGNEDTOCHR`) whose
    item is a gun or a thrown weapon: `ITEM_KNIFE` to `ITEM_ROCKETLAUNCH`
    except `ITEM_WATCHLASER`, and `ITEM_GRENADE`, `ITEM_TIMEDMINE`,
    `ITEM_PROXIMITYMINE` and `ITEM_REMOTEMINE`. The item is read through
    `gevrItemAs()`, so a hack campaign's gun on a special item's slot
    (Goldfinger's Luger on the taser's) counts as the gun it is.
  - Ammo: single-type boxes (`PROPDEF_MAGAZINE`) and crates
    (`PROPDEF_AMMO`).
  - Body armour (`PROPDEF_ARMOUR`).
  - Never keys (`PROPDEF_KEY`), hats, gadgets or mission items.
- **No setup record points at it.** Objectives and level scripts find
  their objects through tags (`PROPDEF_TAG`, `objFindByTagId()`), so a
  tagged pickup can be a mission item even when it's a gun. A rename
  (`PROPDEF_RENAME`) gives a mission item its own name, a link
  (`PROPDEF_LINK`) pairs two guns as a dual pickup, and a safe's record
  (`PROPDEF_SAFE_ITEM`) names what's inside it. A pickup any of these
  point at keeps its setup, and so does one inside another object
  (`PROPFLAG_INSIDEANOTHEROBJ`).

A swap keeps the pickup's kind, since each kind is a different setup
record:

- a weapon becomes another eligible weapon (it takes that gun's floor
  model, `getPropForHeldItem()`), or nothing;
- an ammo box changes its type (1 to 13, `mod.ammo`), or becomes
  nothing; the rounds come with the type, as for any box
  (`get_ammo_in_magazine()`), and the box takes the type's look (its
  magazine or shells, or the round or thrown weapon itself);
- an ammo crate changes how many rounds of each type it holds (0 to the
  type's most), or becomes nothing;
- armour changes its amount (more than 0, up to 1, a full vest), or
  becomes nothing.

A handler gets a table: `kind` (`"weapon"`, `"ammo"`, `"crate"` or
`"armour"`), `index` (the record's, the same on every headset), and
`item`, `ammo`, `crate` (type = rounds) or `amount`. It returns nil to
keep the pickup, false for none, or a table of the same kind; a field
it leaves out keeps the pickup's own, so a handler can change the table
it was given and return it. Handlers run in turn, each given what the
last decided; after a false none are asked. An answer the game can't
take (another kind, an item off the list, a value past its limit) is a
script error: the mod stops for the mission and the pickup keeps its
setup.

Every headset runs the swaps at load, so in co-op the choices come from
`mod.random` (see Multiplayer).

## Versioning

- `api` is one integer. A build supports a range (for example 1–2).
- Within a version, we only add events and calls, never change or remove
  them.
- A breaking change makes a new version. The old version stays
  supported for at least two releases after it, with a note in the release
  notes and in the launcher when an installed mod uses it.
- The API reference (`docs/mod-api.md`) lists each call with the API
  version that added it.

## For mod authors

- A sample mod (in goldeneye-vr-mods) that uses every area of the API.
- A desktop editor or tool for a mod stays in the mod's own repo.
- Test on a Quest 2 at 90 Hz with `--perf-log` before release. The log's
  per-mod line shows script time against the budget.
- Asking for a new event or call: an issue with the use case. We add it
  generically if it fits the API; we don't add mod-specific hooks.

## Plan

1. **Agree on this spec** (done 2026-11-23): Tactical's list of needs,
   its first real test, is in from #291.
2. **Packs and launcher:** manifest, install from file, the generic mod
   campaign entry, saves, the content check. No scripting yet: a
   data-only mod can be installed and started. In three parts:
   - **2a, core (C):** the manifest, install from a zip with every check,
     the installed list and remove, the content ID, the run's mod
     campaign, its save file and ghost tag, boards and iCloud leaving mods
     out (`port/src/gevr_modpack.c`, `port/tests/test_modpack.py`).
   - **2b, launcher:** the Mods page's install from file and installed
     mods, campaign mods in the campaign list, each platform's file picker.
   - **2c, co-op:** the content check's mod part and the refusal message.
3. **Scripting core:** Lua, sandbox, budget, errors, the lifecycle and
   storage calls, the perf log line.
4. **API areas** one at a time, each with native tests (as the repo's
   `port/tests/` do): weapons and items, hands and body slots, guards,
   player, assets, UI. Weapons, then pickups and items, then hands
   (`port/tests/test_modapi.py`; the grips' timing on the production
   poll in `port/tests/test_body_slot_interactions.py`), then guards'
   events and reads (`test_modapi.py`, which also runs
   `gevr_modgame.c`'s told-once bookkeeping), then the calls that change a
   guard, then damage and the player, then sound and smoke; after the
   network (5), the UI's pages on the in-game screen (the fit on both
   screens in `port/tests/test_ingame_fit.py`). Then assets: the pack's
   files first (`port/tests/test_modassets.py`, the routing in
   `port/tests/test_romcheck.py`), attachments next.
5. **Network:** `NET_MSG_MOD`, co-op with mods, the dedicated server.
   `mod.net`, `peer_ready` and a stop told to every headset
   (`port/tests/test_modapi.py`; the routing and checks on the production
   `net_core.c` in `port/tests/test_multiplayer.py`). Co-op's content
   check came in 2c.
6. **Sample mod and API reference.**
7. **Platform check:** Quest, PC, Apple, server, with the sample mod.
   Quest 2 at 90 Hz and Quest 3 at 120 Hz on the heaviest levels settle the
   script budget; the Quest 2 must hold 90 fps.
8. **Mod browser** (after v1): the community index and the Mods page's
   "Community mods" list.
