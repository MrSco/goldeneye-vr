# The pre-match lobby

An online deathmatch party gathers in a quiet lobby before each match: no
map loaded, no character models or guns, the file select's music quietly
under it, every player's Multiplayer Options folder up. Everyone sets their
character, health, team and loadout and readies up; the host sets the match
and presses START; then the map loads for everyone at once. After the match
everyone comes back to it for the next map's vote. It is GoldenEye's own
multiplayer setup in the front end, played online.

Owner decisions (2026-11-25):

1. The lobby is the default; the host can turn it off (the old drop-in
   warmup).
2. In the lobby nothing is drawn but the menus: no map, music, character
   models or guns. The map and everyone's choices load when the match starts.
3. An optional warmup (practice on the map) after the lobby's START: Off by
   default, the host picks 30 s, 60 s or 2 min.
4. Music: the file select's music, quietly.
5. The lobby's menus are the match's: one player on the screen (flat or in a
   headset) has the full pause window, the launcher's look, with the laser
   pointer in a headset; a split screen, where the window does not fit, gives
   each local player their own Multiplayer Options folder in their part of
   the screen.
6. Dedicated servers run the lobby too.

## The host's rules

Two rows of the pause window's Rules tab and of the folder (the host's;
dimmed elsewhere), saved in
`goldeneye-vr.ini` and in the match config (`NetMatchConfig.lobby` and
`.warmup`, protocol 29, which is unreleased, so no bump):

| Row | Values | What |
| --- | --- | --- |
| Lobby: | On (default) / Off | `MpLobby`. Off: hosting and RETURN TO LOBBY go to the live warmup on the map, as before |
| Warmup: | Off (default) / 30 s / 60 s / 2 min | `MpWarmup`. Shown with the lobby on: the practice between the lobby's START and the match |

The launcher's Host page has the same Lobby switch ("Lobby before each
match") for hosting with it off.

## How it runs

`port/src/net/net_core.c`. The lobby is a round whose stage is the title
stage: `NetRoundSettings.lobby` (one byte at the end of the round settings,
in every message that carries them), so `netRoundLoadStage` loads
`LEVELID_TITLE`, as co-op's menus do. The phase stays `NET_PHASE_WARMUP`,
so READY, the votes, START MATCH, the countdown and joining are the
warmup's.

- **Hosting** (`netLobbyHostLaunchMatch`): with the rule on, the first round
  is the lobby. Joiners land in it.
- **The clock**: none in the lobby (`netWarmupClockSeconds`); the host
  starts it. A dedicated server starts once every player is READY, or 60 s
  after the first one is (`port/server/server_host.c`). A headless test
  client READYs by itself.
- **START** (`netTryHostStartRequest`): everyone READY, a 5 s countdown in the
  lobby (`NET_LOBBY_COUNTDOWN_SECONDS`); then the map loads with the latched
  choices (`netHostRoundTick`), straight into the match, or into the Warmup
  rule's practice, after which the match starts as a warmup's does. Alone in
  the lobby, START practices on the map.
- **After a match** (`netHostContinue`, at the end of the results): everyone
  back to the lobby (`netHostEnterLobby`). Shuffle and Playlist turn then, so
  the lobby shows the next map; a vote goes on in the lobby until START.
- **RETURN TO LOBBY** during a round or the practice: the lobby. During the
  lobby's countdown the row is Cancel Start.
- **A dedicated server left empty**: back to the lobby (`netHostIdleReset`).
- The silence and idle clocks don't run in the lobby (nothing sends poses).

Joining mid-match still drops straight into the match.

## The screen

`src/game/front.c` `MENU_MP_LOBBY` on the title stage (`menu_init` sends the
title stage there whenever the round is the lobby), the file select's music
quietly; what is on it is the match's own menus, by the same rule:

- **One player on the screen** (flat, or always in a headset): the full pause
  window (`port/vr/vr_pause_menu.cpp`, `gevr_pause_widgets.h` with
  `GevrPauseView.lobby`), up for the whole lobby (`input.c`
  `gevrNativePauseOpen`): the map, weapons and scenario on Match, the Lobby
  and Warmup rules on Rules, the character and loadout on Player, the players
  and who is READY, then Start match (the host; alone, Practice on the map)
  or Ready up, Cancel start in the countdown, and Leave match. No Resume (no
  game under it), no scores and no health arcs. In a headset the laser
  points at it as at the launcher. Its Select Character, map photos,
  Handicap and Teams buttons open the game's own screens, drawn here
  (`mpmenu.c gevrLobbyStage*`), and the window comes back when they close.
- **A split screen** (two players: halves; three or four: quarters): each
  player's Multiplayer Options folder, driven by their own pad; the info
  line adds "n/m" READY. The folder stays up: B on it does nothing, a screen
  it opens (Select Character, the stage photos, Handicap, Teams, the sheets)
  comes back to it. **Leave Match** (A twice) leaves; a guest has Drop Out.
  A new pad's START joins here as in a match (the window gives way to the
  folders); a guest leaving brings the window back.
- The first lobby shows the host's Level and Weapons; after a match with
  Next Round on Vote, Next Map and Next Weapons are the vote.

The folder and its screens kept their state on the player struct
(`mpmenuon`/`mpmenumode`); the title stage has no players, so `mpmenu.c`
reads and writes that state through `gevrSeatMenu`, which in the lobby is a
per-slot copy. `menuActingSlot` is the seat being ticked or drawn. Pads: in
the lobby `input.c` routes each local seat's pad by its slot, as in a match
(the front end's menus otherwise read only this machine's first player), and
a new pad's START joins a split screen there too.

## Not yet

- The internet game list says "warmup" for a game in the lobby.
- The game's own screens (Select Character, the map photos) take the pads'
  sticks, as in a match, not the laser.
