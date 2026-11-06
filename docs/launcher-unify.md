# Launcher screens: one frame, one settings table (v0.5.15 batch)

Agreed with the owner on 2026-11-05:

- **Full unification:** every setting is defined once and drawn by the launcher, the
  in-game screen and GoldenEye's watch VR page alike.
- **One in-game screen:** tapping or holding Menu opens it.
- **Two tab rows:** no sub-pages behind buttons, no Done buttons.
- **The in-game header** keeps the radar, the gauges and the session.

Standing rule: **no page scrolls.** Pages are laid out to fit. A long list can still
scroll inside its own box, as the friends, lobbies and players lists do.

## Why

A survey on 2026-11-05 found:

- **Each setting is written twice.**
  - The launcher draws it in `port/vr/vr_launcher.cpp`, as ImGui checkboxes, sliders
    and radios.
  - The hold-Menu panel draws it from `src/game/options.c`, as stepper rows. The same
    rows also feed the watch's VR page.
- **The two copies have drifted.**
  - The panel lacks anti-aliasing, the third-person sliders, ragdolls and
    dismemberment.
  - It has a Controllers row that the launcher doesn't.
- **Some settings are written a third time.**
  - Match, Fun, Player and Audio for the online pause, in `mpmenu.c`.
  - Movement speed, fast reinforcements and gun size, in up to three places each.
- **Three layout systems.**
  - The launcher flows its content.
  - The hold-Menu panel and the online pause are placed by fixed pixels, each with its
    own tab style.
- **Pages scroll or clip.**
  - The launcher's body allows scrolling. Ticking third person adds three slider rows
    to Play, which then scrolls.
  - Sub-pages drawn straight into the window clip their overflow.

## The settings table

There is one C table, `port/src/gevr_settings_table.c`, plus a header, readable from C
and C++. Each row holds:

- An **id** and a **label**.
- A **section**: the tab or sub-tab it lives on.
- A **kind:**
  - toggle;
  - choice, with its value labels;
  - int range or float range, with min, max, step, unit and format;
  - action, which is a button.
- The **variable** (a pointer), and its **default**, used by Reset.
- A **tooltip**.
- **Where it shows,** as flags:
  - launcher, in-game, online, host only;
  - solo only or multiplayer only;
  - flat only or stereo only;
  - Bean only or GE-X only;
  - WIP.
- **When it applies:**
  - live, which saves and calls a hook (for example, refresh rate calls
    `vr_apply_refresh_rate`);
  - next level;
  - restart.
- **A visibility predicate,** for rows that depend on others: the third-person
  sliders show only while third person is on.

There are **three renderers**, all reading this table:

- ImGui controls for the launcher and the in-game screen: a checkbox, a slider with
  Reset, radios or a combo, or a stepper.
- `options.c` stepper rows for the watch's VR page, keeping its retail look. The
  `s_gevrVrLabels`, sections and getters become the table's.
- The live multiplayer fields, Match, Rules, Player and Audio, become table rows too,
  each with a host-only or live flag.

## The frame

Every screen is built from the same parts: one window, one header, two tab rows, the
body and one footer.

- **Window:** 1280×960, plus the Friends side panel. One font scale.
- **Header:**
  - Left: the title and the context.
  - Launcher: the build, the ROM and Send debug log.
  - In-game: the radar, the gauges and the session (`gevrPauseHeader`).
- **Tab rows:**
  - Top row: the screen's tabs.
  - Second row: the sub-tabs, only where a top tab has them.
- **Body:** filled by a layout engine.
  - Sections flow into one or two columns.
  - A page that would overflow at its fullest fails a test instead of scrolling.
- **Footer:** the screen's buttons.
  - Launcher: START plus the campaign row.
  - Lobby: Launch, Ready, Stop or Disconnect.
  - In-game: Resume, HD textures, Reset sound, Back to launcher.

## Screens and tabs (proposal, for the owner to check)

### Launcher

| Top tab | Sub-tabs | Contents |
|---|---|---|
| **Play** | none | Play mode (stereo or flat). Third person, with its sliders shown only while it is on. Show stats. Offer test builds. |
| **Multiplayer** | Host · Join · Player · Audio · Favorites | Host: lobby and roster, then Match, Fun. Join: the Public, Private, LAN and Direct lists. Footer: Launch, Ready, Stop or Disconnect. |
| **Controls** | Hands · Gestures · Throwing · Haptics | Hands: hands, sticks, aim, gun fit, watch face, controllers. Gestures: the grips and force pull. Throwing: motion throw and recoil. |
| **Comfort** | none | Turning, turn speed, movement speed, vignette, when hit. |
| **Screen** | none | Flat or curved, passthrough, size, distance, refresh rate, SpaceWarp, anti-aliasing. |
| **Rules** | Game rules · AI ally · Cheats | |
| **Mods** | none | Bean, HD fill, GE-X, blood, Oddjob's hat, gun size, and the mod packs. |

- **Report** becomes a header button.
- **START** stays in the footer.

### Multiplayer's Host sub-tab

Host has six pages today: Lobby, Match, Fun, Player, Audio and Favorites. Player,
Audio and Favorites move up beside Host and Join. That leaves Host with Lobby, Match and
Fun.

- **Default:** two columns, with the lobby and roster on the left and Match on the
  right. Fun opens as a pop-up from a button.
- **If that doesn't fit at 8 players with custom sets:** a third tab row for Lobby,
  Match and Fun, on Host only.

### In-game

The in-game screen opens on a tap or a hold of Menu.

| Top tab | Shown | Contents |
|---|---|---|
| **Match** | online | Match status, scores, and the players with Kick. |
| **Rules** | online | The live rules, which only the host can change. |
| **Player / Audio** | online | |
| **Controls · Comfort · Screen · Rules · Mods** | always | Each is the same sub-tab or section as in the launcher, its in-game rows only. |

- **Solo:** a tap of Menu still opens GoldenEye's watch, as now. A hold opens this
  screen.
- **Online:** both a tap and a hold open this screen.

## Phases

Each phase ends in a build for a headset check. Each fix is its own commit, on this
batch's branch.

1. **The table and its renderers.**
   - Move every setting into the table, including the watch's VR page.
   - Draw the hold-Menu panel's tabs from it with ImGui controls.
   - The drift goes: the panel gains anti-aliasing, the third-person sliders, ragdolls
     and dismemberment.
2. **The frame.**
   - Shared header, tab rows, body layout engine and footer.
   - The launcher's main tabs and its eight sub-pages move into it as tabs and
     sub-tabs.
   - Third person's sliders fit on Play.
3. **Multiplayer in the frame.**
   - Host and Join, with Match, Fun, Player and Audio as table rows with host-only and
     live flags.
4. **One in-game screen.**
   - The online pause's tabs and roster join the hold-Menu panel's settings tabs.
   - `mpmenu.c`'s field list gives way to the table.
5. **Fit tests.**
   - Every screen, top tab and sub-tab is drawn at its fullest: 8 players, every rule
     visible, the WIP rows on, flat and stereo.
   - Each must fit with no scroll.
   - Today's marker-cut fixtures in `test_launcher_ui`, `test_pause_ui` and
     `test_friends_ui` are rewritten against the frame.

## Progress

**Phase 1, done (7e3a9d7f).**

- The table is `port/src/gevr_settings.c`, with its header `port/include/gevr_settings.h`.
- `port/vr/gevr_settings_ui.h` draws it:
  - a toggle is a checkbox;
  - a choice is a dropdown;
  - a range is a slider with Reset.
- A section with more than seven rows takes two columns.
- The hold-Menu panel draws the table, and `options.c`'s watch rows are gone.

**Phase 2, done.**

- **Launcher places.** Each row also has a launcher place (`GEVR_LSEC_*`). By default it
  follows the row's section. A few rows move:
  - Play holds the play mode, third person with its camera, and Show stats.
  - The watch face goes on Hands.
  - Gun size goes with the cheats.
- **Launcher-only rows.** The play mode and the AI ally's six rows show only in the
  launcher. Show slots is new and shows in both.
- **Rows that depend on another** are greyed out, with the reason:
  - turn speed under snap turning;
  - the throw rows without motion throw;
  - Pull disarm and Pull visuals without force pull;
  - the slot rows without body slots;
  - third person in stereo.
- **Frame.** `gevrLauncherTabRows` in `gevr_launcher_ui.h` draws:
  - Play, Multiplayer, Controls (Hands · Gestures · Throwing · Haptics), Comfort,
    Screen, Rules (Game rules · AI ally · Cheats) and Mods;
  - each page above a START footer measured every frame.
- **Gone:** the Done buttons, and the launcher's own copies of the settings.
- **Pages that keep their own drawing:**
  - **Haptics:** its table scrolls inside its box.
  - **Cheats:** set closer, to fit under two tab rows.
  - **Mods:** its packs. Each pack now takes one line, with the error in the status's
    place. Bean, GE-X, the blood patch and the hat stay with their packs here. In game
    they are table rows.
  - **Multiplayer:** unchanged inside its tab until phase 3, with its own Host/Join and
    page rows.
- **Fit test.** `port/tests/test_launcher_frame.py` draws every settings page at its
  fullest against the real table. Every page must fit with no scrolling, and so must
  the tab rows, the update line and START.

**Phase 3, done.**

- **Sub-tabs.** Multiplayer's second row is Host · Join · Player · Audio · Favorites.
  - Host has a third row: Lobby · Match · Fun. This is the fallback the plan allowed.
    The two-column lobby and match layout was not tried: the existing pages already fit
    at 8 players (`launcher_compact_native.cpp`).
  - Join has a third row: Public · Private · LAN · Direct IP.
- **Footer.** It holds Launch, Ready, Stop Hosting, Disconnect, Cancel Join and Start
  Hosting. Back is gone, so a host leaves with Stop Hosting.
- **The lobby runs every launcher frame (`gevrMultiplayerTick`), whatever tab is open.**
  The tab only draws (`gevrMultiplayerPage`). The tick covers:
  - the lobby service's events;
  - ICE;
  - the host's heartbeat and the idle stop;
  - a client's match starting.

  Before, all of that ran only while the page was open, which tabs would have broken.
- **Friends.** A friend's lobby or an invite brings up Join, or Host.
- **Table rows.**
  - Fun (DK mode, paintball, line mode, no radar, hit immunity, gun size) and Audio
    (mute mic, music, sound effects, voices, voice mode) are table rows now.
  - Fun and the voice mode are the host's: greyed for a joiner, live for the host.
  - Match and Player keep their own drawing: stages, weapon sets and loadouts, teams, and
    the name box.

**Phase 4, done.**

- **One screen online.** The match screen (`gevr_pause_widgets.h`) gains a **Settings**
  tab: the panel's seven sections, drawn by the same `gevrSettingsTabs`, with HD
  textures and Reset sound under them.
  - A tap of Menu opens the screen on Match.
  - A hold opens it on Settings. The separate panel no longer opens online.
- **Solo is unchanged.** A tap opens GoldenEye's watch; a hold opens the panel, which is
  the same sections with the solo footer.
- **Not done: the online Match, Rules, Player and Audio tabs.** They still read
  `mpmenu.c`'s field list: the live match's votes, map, weapon sets, custom slots, bots,
  equalization and volumes. The launcher already has Fun and Audio as table rows; moving
  the in-game tabs onto them waits for a two-headset round.
- **Tests.** `test_pause_ui` checks:
  - that each section fits the Settings tab with no scrolling;
  - Reset sound and HD textures on the Settings tab.

**Phase 5, done.**

The fit tests draw every page at its fullest, against the real table. The fullest
state means:

- ragdolls and dismemberment, body slots, the ally, motion throw, force pull and GE-X
  all on;
- an update offered;
- Bean's 120 Hz warning showing.

Each test runs in four states:

- flat with third person;
- stereo, where third person is greyed: "Flat screen only";
- a co-op host;
- a deathmatch joiner, whose rows are greyed with their reason, such as "Host's choice"
  or "Host: 150%".

| Test | What it draws |
|---|---|
| `test_launcher_frame.py` | Every launcher settings page, plus Multiplayer's Host > Fun and Audio, cut from `gevrMultiplayerPage` (44 pages) |
| `test_ingame_fit.py` | Every section of the solo panel and of the online Settings tab (28) |

Each test checks that:

- no page or box scrolls;
- each label fits its column;
- each control, with its value or reason beside it, fits its column
  (`port/tests/settings_fit.h`);
- the tab rows fit without shrinking;
- START, the update line and the hint stay on the canvas.

Both tests fail when they should:

- the old one-line Audio note, about 1,460 px on a 1,244 px page;
- one column per section.

Covered by their own tests:

- Match and Lobby at 8 players (`launcher_compact_native.cpp`);
- Mods in every download state (`test_launcher_mods.py`);
- the online match tabs (`test_pause_ui`).

## Risks

- **The watch's VR page** (`options.c`) has its own retail navigation. Phase 1 keeps its
  look and changes only where its rows come from.
- **Pixel-clicked pause tests:** `test_pause_ui` clicks at fixed coordinates, so it is
  rewritten in phase 4.
- **Settings that apply on the next level or on restart** must say so on the row.
  SpaceWarp already does.
- **Size:** this touches about 5,000 lines of UI. The diff will be large. It is
  explained per phase in each commit.
