# In-game match setup: the Multiplayer Options folder online

An online deathmatch or Horde run is set up in the game, on GoldenEye's own
Multiplayer Options folder, by everyone at once: the host sets the match, each
player their own character, health, team and loadout. Flat screens, split
screen (one to four views) and the headset all use the same screen. The
launcher only hosts (who may join, the mode, the advanced rules) and goes
straight in.

Owner decisions (2026-11-24):

1. The folder opens by itself on arriving in a match and after RETURN TO
   LOBBY (and on NEXT MAP when a vote runs), not on every warmup.
2. Character and loadout live in the game; the last choice is remembered.
   The launcher's Player tab keeps the name.
3. Horde: the first wave waits for the host (a setup break; START WAVE
   gives a 5 s countdown), and Retry Horde always returns to the setup
   break ([Horde](horde-mode.md#the-setup-break)).

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
quarter, the headset's HUD panel) puts each label and value on one line,
with short labels beside a value that takes half the row (Char., Mode:,
Guns:, Wave:, Ally:...; "CHARACTE." was the cut before). The rest scroll
(`frontMpOptionsScroll`, arrows at the edge). A tab's word falls back to a
shorter one where it is cut (PREVIOUS as BACK, START WAVE n as WAVE n,
WAITING FOR HOST as WAITING or WAIT: `frontMpOptionsTab` "LONG|SHORT").
`frontScenarioSelectDraw` is menu13's list on the same sheet. The Handicap
and Teams screens cut a list's names alike (`frontPanelFitNames`): a split
screen's guests read "AGENT 44.(2)", "AGENT 44.(3)", the "(n)" kept.

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

**Horde** (a run's setup break and during the run):

| Row | Who | What |
| --- | --- | --- |
| Level: | host | the run's map; A opens the stage photos filtered to Horde's 26 maps |
| Starting Wave: | host | 1..50; in the setup break it applies at once |
| Friendly Fire: | host | |
| AI Ally: | host | GoldenEye's campaign only (the host's `VrAlly`) |
| Characters | own | A opens Select Character |
| Drop Out / Full Menu | as above | |

No Health or Handicaps (PvE: the waves set the damage). A new map waits for
START WAVE (it restarts the run there) or Retry Horde; out of the setup
break every change waits for the retry.

The host's rows show at every seat, dimmed where they can't be changed.
Every row is a view over the LOBBY page's row (`mpmenu.c s_lobbyRows`,
found by its step), so the authority (`gevrRowEditable`: the host's own
seat) and the wire are those rows'. Co-op has no folder (its party menus).

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
un-readies everyone), READY - WAITING FOR THE HOST and so on. In Horde's
setup break it is the host's START WAVE n (RESTART ON NEW MAP with another
map chosen; `netHordeHostStart`), which closes every folder for the
countdown, and WAITING FOR HOST, dimmed, elsewhere; in a run there is no
tab and START leaves the folder.

On a pad the info line says `START: ...`; on the headset, where Menu leaves
the folder and the tab is reached with the stick and A, `START TAB: ...`.

## Where it opens

- **On arriving**: each local player's folder opens on the first warmup this
  machine loads after hosting or joining (a joiner mid-round: the next
  warmup).
- **After RETURN TO LOBBY**, at every machine. The host's round notice
  after the return's round reset carries a lobby flag (`NET_NOTICE_LOBBY`,
  flag 8 of `NET_MSG_ROUND_NOTICE`, protocol 29), and each client counts
  those (`net_core.c netLobbyReturnCount`). A notice with any other unknown
  flag is still refused.
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
- **Horde:** in each run's setup break, for every local player, once per
  load (arriving, Retry Horde, a restart on a new map). START in the setup
  break opens it (the headset's Menu too); with a split screen every seat's
  START is its own folder at any time (`input.c inputHordeStart`, which
  keeps the campaign watch out of it); one player's START in a run is the
  pause window, whose Match tab has a Multiplayer Options button.

In a warmup a player at the folder, or at a screen it opened, waits held and
unhurt (`netSetSlotChoosing`, as a drop-in choosing a character; local, so
nothing on the wire). In a round the folder is a page of the watch: the
player stands still and can be shot.

Round notices say `START: ...` in place of `OPEN MENU: ...` (`MENU: ...` on
the headset, `net_player_sync.c`), the quarter views' wordings as short as
before.

## The launcher (plan commit 8)

**Host** goes straight into the match (`vr_launcher.cpp gevrMpLaunch`): a
deathmatch's warmup, a Horde run's setup break, co-op's mission select; a
friend's "host to invite" does the same. The lobby service's registration
completes in the game (`gevrLobbyGameTick` resends the phase once it is
CREATED).

- **Host > Game:** who can join (Public / Friends / Private, VR players only,
  Hand reload only), the mode, and a line on what the game sets up, with the
  last match's choices.
- **Host > Advanced:** the fun rules and the live settings (movement speed,
  host hit equalization and its delay cap). The favorite maps and weapon
  sets stay their own page (Online > Favorites).
- **Player:** the name and a split screen's guests.
- Gone: stage, players, weapons and the custom set, the Match tab
  (scenario, length, health, dual wield, next round, loadouts, handicaps,
  bots, friendly fire, co-op's and Horde's rules), the hosting roster and its
  team and health rows, the client's waiting pane and Ready box, Player's
  character and loadout. The game saves the last choices back (`VrMp*`,
  `VrHorde*`, `VrMpChr`, `VrMpLoadout`) and they start the next match; the
  saved character is what a join asks for (`netSetPreferredCharacter`, every
  launcher frame).

A dedicated server builds its match from its ini and command line, as before.

## The LOBBY page and the pause window (plan commit 9)

The rows the folder holds are not repeated elsewhere. The LOBBY page (split
screen, a round in progress) is the quick page between rounds: the votes,
READY, START MATCH, RETURN TO LOBBY, MULTIPLAYER OPTIONS, DROP OUT and FULL
MENU (`GevrMenuPage.shows`, `gevrLobbyQuickRow`). The pause window
(`gevrPauseInFolder`) keeps the votes, READY, the favorites, its own rules
(host equalization, the fun rules, movement speed) and the audio, with a
Multiplayer Options button on its Match and Player tabs; co-op, which has no
folder, keeps its rows.

## Teams: a guest in a bot's place

A joiner who takes a bot's place (every place taken) takes the bot from the
side with the most room left for humans, and its side only while that side
has room for another human; else the other side with room, else neither
(`net_core.c netBotToReplace`, `netJoinerSide`). Mid-round they play the
bot's side and change at the next round (starred on Teams). Before, four
split-screen players on a 2v2 could put three on one side after the host's
own pick had moved the bots ("TOO MANY ON A TEAM", one of them not shown).

## Files

- `src/game/front.c` / `front.h`: `frontMpOptionsLayout`, `frontMpOptionsScroll`,
  `frontMpOptionsDraw`, `frontScenarioSelectDraw`, `FrontOptionRow`.
- `src/game/gevr_mpoptions.c` / `.h`: the row set (deathmatch and Horde).
- `src/game/mpmenu.c`: `MENU_SETUP` (`bondconstants.h`), listed in
  `s_gevrMenuPanels`; `gevrMpOptionsOpen`, `Tick`, `Draw`, the sheets, the
  hold, the automatic openings, `gevrMpOptionsDropIn`, `gevrMpOptionsStartPressed`,
  `gevrMpOptionsFromPause`; screens opened from the folder return to it
  (`s_panelToSetup`).
- `port/src/net/net_core.c`: `gevrNetScenarioAccepted`, `netSessionCount`,
  `netStageLoadCount`, `netLobbyReturnCount` (the notice's `NET_NOTICE_LOBBY`),
  `netVoteRequested`, `netWarmupSettingsPending`; the notices;
  `netBotToReplace`, `netJoinerSide`.
- `port/src/input.c`: the drop-in, the headset's Menu, Horde's START
  (`inputHordeStart`).
- `port/vr/gevr_pause_widgets.h`, `vr_pause_menu.cpp`: `GEVR_PAUSE_SETUP`;
  the Match and Player tabs' Multiplayer Options buttons.
- `port/vr/vr_launcher.cpp`: the Host page (`gevrHostLobbyOptions`,
  `gevrHostAdvancedOptions`, `gevrMpLaunch`).

## Tests

- `port/tests/test_mp_options_layout.py`: the layout fits full 4:3 and wide
  views, halves, quarters and the HUD panel on six headsets' eye buffers;
  rows stay above the info line and beside the tabs; the scroll keeps the
  cursor in view.
- `port/tests/test_mp_options_rows.py` (`mpoptions_rows_native.c`): the row
  set per mode, seat, phase, scenario, vote, split screen; host rows dimmed
  elsewhere; Horde's rows; no folder for co-op.
- `net_core_native.c`: `gevrNetScenarioAccepted`; `test_core_lobby_returns`
  (the return's notice carries the lobby flag, a client counts flagged
  notices only); `test_core_round_prompts` (unknown flags refused);
  `test_core_bot_join_teams`.
- `port/tests/test_panel_names.py`: names cut alike.
- `test_launcher_ui`, `test_launcher_frame`: the Host page's Game and
  Advanced tabs fit at Quest size; the removed pages stay gone.
- `test_ingame_fit`: the pause window's Rules and Player tabs at their
  fullest after the folder's rows left them.
- `pause_input_native.cpp`: the headset's Menu in a warmup opens the folder
  once, the next Menu leaves it.
- PC `--input-script` runs (with `GEVR_LOBBY_URL=http://127.0.0.1:9`): host
  alone (arrival, Level photos, Scenario list, START with changes); host and
  a client on 127.0.0.1 (the client's folder with the host's rows dimmed,
  READY, the countdown on both, the round, RETURN TO LOBBY opening both
  folders); split screen at two, three and four views; and the folder forced
  onto a Quest 2's HUD panel rectangle in a temporary flat build.
- Later runs (2026-11-24): the launcher's Host page, Host straight into the
  warmup with the folder open; Horde's folder in the setup break for the
  host and a client on 127.0.0.1 (WAITING FOR HOST, START WAVE closing both);
  a restart on a new map reopening it; Horde split screen at two and four
  views (each seat's own folder, a new seat on Select Character, START WAVE,
  a guest's folder in the run); RETURN TO LOBBY reopening a client's folder
  by the notice's flag; a 2v2 with four split-screen players (Teams and
  Handicap, the short labels).

## Not done here

- A one-load START with pending changes (it still reloads into a warmup
  first, then counts down on the next START).
- The launcher roster's Add friend went with the roster; the pause window's
  player list has no Add friend yet (the friend codes still travel,
  `NET_MSG_FRIEND_CODE`).
- The headset's new flows (Horde's folder on the HUD panel, the Menu in the
  setup break, `START TAB:` wording) are built for the Quest but not yet
  tried on one.
