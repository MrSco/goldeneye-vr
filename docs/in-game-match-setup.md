# In-game match setup: the Multiplayer Options folder online

An online deathmatch is set up in the game, on GoldenEye's own Multiplayer
Options folder, by everyone at once: the host sets the match, each player
their own character, health, team and loadout. Flat screens, split screen
(one to four views) and the headset all use the same screen.

Owner decisions (2026-11-24):

1. The folder opens by itself on arriving in a match and after RETURN TO
   LOBBY (and on NEXT MAP when a vote runs), not on every warmup.
2. Character and loadout live in the game; the last choice is remembered.
   The launcher's Player tab keeps the name.
3. Horde: the first wave waits for the host. That, Horde's rows and the
   co-op leftovers are separate work (see "Not done here").

## What it looks like

`front.c frontMpOptionsDraw` draws menu0E (`constructor_menu0E_mpoptions`)
on the folder's paper inside one player's view: the Zurich title
("Multiplayer Options:"), labels in a column and values in a second, the
selected row on a dark band, a row this seat may not change dimmed (the
original's 0x70 locked row), START and PREVIOUS on tabs down the right edge
(BankGothic turned 90 degrees, as `frontAddStartTabText`), and an info line
along the bottom (what START does now, the countdown, the lobby code). The
original folder's own labels are the ROM's strings (`TITLE_STR_77..83`:
"Players:", "Scenario:", "Level:", "Game Length:", "Weapons:", "Characters",
"Health"); a row that opens another screen has no colon, as the original's
Characters and Health.

The 3D folder model and the front end's single cursor belong to the title
stage, and the retail `interface_menu0E` writes the running match's globals,
so none of that is reused: this is rectangles and text over rows the caller
fills each frame (`FrontOptionRow`), and it changes nothing.

Text keeps its size. `frontMpOptionsLayout` picks the pitch (the original
0x14, closer when the rows need it), how many rows show and whether the
title fits; a short view (a half) uses the small font; a narrow one (a
quarter, the headset's HUD panel) puts each label and value on one line.
The rest scroll (`frontMpOptionsScroll`, arrows at the edge).
`frontScenarioSelectDraw` is menu13's list on the same sheet.

## The rows

`src/game/gevr_mpoptions.c gevrMpOptionsRowSet` (no game headers; tested
natively) gives a seat's rows for the mode, seat, phase, scenario and vote:

| Row | Who | What |
| --- | --- | --- |
| Players: | host | the match's player count; hidden in a team scenario |
| Scenario: | host | left/right steps; A opens the Scenario list (all ten, 3v3 and 4v4 too; what the party can't take dimmed: `net_core.c gevrNetScenarioAccepted`) |
| Level: | host | steps; A opens the stage photos |
| Next Map: | anyone | replaces Level while voting; A opens the photos to vote |
| Game Length: | host | hidden for You Only Live Twice |
| Weapons: | host | hidden for the Golden Gun |
| Custom Set | host | the custom set's four guns, a sheet |
| Next Weapons: | anyone | replaces Weapons while voting |
| Characters | own | steps; A opens Select Character |
| Health | own | the host's value is the match's health, anyone else's their own; A opens Handicap |
| Handicaps: | host | players pick their own health |
| Teams | own | team scenarios; A opens Teams |
| Dual Wield:, Loadouts: | host | |
| Your Loadout | own | the player's four guns, a sheet (Loadouts on) |
| Bots | host | mode, count, difficulty, a sheet; outside a round in progress |
| Next Round:, Friendly Fire: | host | friendly fire in team scenarios |
| Return to Lobby | host | during a countdown or a round |
| Drop Out | a split screen's guest | |
| Full Menu | the machine's first seat | the pause window (one player) or the full panel (split screen) |

The host's rows show at every seat, dimmed where they can't be changed.
Every row is a view over the LOBBY page's row (`mpmenu.c s_lobbyRows`,
found by its step), so the authority (`gevrRowEditable`: the host's own
seat) and the wire are those rows'. Co-op has no folder (its party menus);
Horde returns no rows yet.

## Input

Per seat (cursor, scroll, sheet and the screen it returns to are per slot):
up and down move between the rows and onto the START tab; left and right
step a value; A opens a row's screen or sheet, or steps its value as the
original's A did; B is PREVIOUS (a sheet back to the folder, the folder back
to where it came from); START is the START tab. A flat pad's held d-pad
steps once, a held stick repeats (`frontPanelPad`). The press that opened
the folder, or came back from a screen, is not a choice. On the headset
either stick moves, A/X or the trigger chooses, and Menu leaves it and
closes the menu, as for the other game screens.

The **START tab**: the host's START MATCH (`netHostStartRoundNow`; alone,
it restarts the warmup with the changes; with changes the warmup hasn't
loaded, it loads them into a new warmup first, and the info line says so),
anyone else's READY (dimmed in a team scenario until they have a team).
The info line reads WAITING FOR READY, STARTING IN n (and that a change
un-readies everyone), READY - WAITING FOR THE HOST and so on.

## Where it opens

- **On arriving**: each local player's folder opens on the first warmup this
  machine loads after hosting or joining (a joiner mid-round: the next
  warmup).
- **After RETURN TO LOBBY**, at every machine. Nothing new goes on the wire:
  a client tells the return from the other reloads into a warmup by the
  round reset itself: the lobby state says no round follows the load, and no
  start was asked for (`net_core.c netLobbyReturnCount`).
- **Votes**: when the host asks for them, on NEXT MAP for each local player
  who hasn't voted. At a match's end the results stay up; right (or START,
  but the host's START continues) leaves them for the folder.
- **START** in a deathmatch's warmup (a pad's START, the headset's Menu).
  In a round START opens the pause window or the LOBBY page as before, and
  both offer it: the pause window's Match tab has a Multiplayer Options
  button, the LOBBY page a MULTIPLAYER OPTIONS row.
- **A split screen's new seat** in a warmup gets the folder on its Select
  Character, which comes back to the folder; mid-round it is the drop-in's
  Select Character as before.

In a warmup a player at the folder, or at a screen it opened, waits held and
unhurt (`netSetSlotChoosing`, as a drop-in choosing a character; local, so
nothing on the wire). In a round the folder is a page of the watch: the
player stands still and can be shot.

Round notices say `START: ...` in place of `OPEN MENU: ...`, the quarter
views' wordings as short as before.

## Files

- `src/game/front.c` / `front.h`: `frontMpOptionsLayout`, `frontMpOptionsScroll`,
  `frontMpOptionsDraw`, `frontScenarioSelectDraw`, `FrontOptionRow`.
- `src/game/gevr_mpoptions.c` / `.h`: the row set.
- `src/game/mpmenu.c`: `MENU_SETUP` (`bondconstants.h`), listed in
  `s_gevrMenuPanels`; `gevrMpOptionsOpen`, `Tick`, `Draw`, the sheets, the
  hold, the automatic openings, `gevrMpOptionsDropIn`, `gevrMpOptionsStartPressed`,
  `gevrMpOptionsFromPause`; screens opened from the folder return to it
  (`s_panelToSetup`).
- `port/src/net/net_core.c`: `gevrNetScenarioAccepted`, `netSessionCount`,
  `netStageLoadCount`, `netLobbyReturnCount`, `netVoteRequested`,
  `netWarmupSettingsPending`; the notices.
- `port/src/input.c`: the drop-in, the headset's Menu.
- `port/vr/gevr_pause_widgets.h`, `vr_pause_menu.cpp`: `GEVR_PAUSE_SETUP`.

## Tests

- `port/tests/test_mp_options_layout.py`: the layout fits full 4:3 and wide
  views, halves, quarters and the HUD panel on six headsets' eye buffers;
  rows stay above the info line and beside the tabs; the scroll keeps the
  cursor in view.
- `port/tests/test_mp_options_rows.py` (`mpoptions_rows_native.c`): the row
  set per mode, seat, phase, scenario, vote, split screen; host rows dimmed
  elsewhere; no folder for co-op.
- `net_core_native.c`: `gevrNetScenarioAccepted`; `test_core_lobby_returns`.
- `pause_input_native.cpp`: the headset's Menu in a warmup opens the folder
  once, the next Menu leaves it.
- PC `--input-script` runs (with `GEVR_LOBBY_URL=http://127.0.0.1:9`): host
  alone (arrival, Level photos, Scenario list, START with changes); host and
  a client on 127.0.0.1 (the client's folder with the host's rows dimmed,
  READY, the countdown on both, the round, RETURN TO LOBBY opening both
  folders); split screen at two, three and four views; and the folder forced
  onto a Quest 2's HUD panel rectangle in a temporary flat build.

## Not done here

- **The launcher slim** (plan commit 8): one Host button straight into the
  warmup; the stage, players, weapons, Match tab, hosting roster, client
  waiting pane and Ready box removed; Player tab name and guests only; an
  Advanced section (fun rules, movement speed, host equalization,
  favorites).
- **Removing the moved rows** from the LOBBY page and the pause window's
  Match and Player tabs (plan commit 9).
- **Horde** (its rows, the first wave waiting for the host) and the **co-op
  leftovers**: another agent's work. `gevrMpOptionsRowSet` has a marked place
  for Horde's rows.
- A one-load START with pending changes (it still reloads into a warmup
  first, then counts down on the next START).
