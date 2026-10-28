# Dedicated servers (scoping)

Status: design only, nothing built (2026-10-28). Line references are to the
v0.5.14 batch branch (`claude/batch-release-agent-test-4c94dd`, 57f9445e).

## Goal

Offer dedicated servers next to today's player-hosted games. A server is the
same game built headless (no window, VR, sound or input), so bots, every
scenario, co-op, Horde and the round flow work unchanged, and every update
reaches the server by rebuilding it from the same sources.

The user chose the headless build over a thin network-only server
(2026-10-28): the thin server could not run bots, and it would need a second
copy of everything the host now reads from its own simulation.

## What the host does today

Each headset simulates every player. The host is a player headset that also:

- Network only: slot assignment, relaying state, events and voice, clock
  sync, drop timers, ready/vote/countdown/round reset, the bot roster, hit
  validation (`net_core.c` netHostStart 649, netHostRoundTick 4039,
  netResolveVotes 3908, netHitReportAllowed 2620-2702).
- From its live simulation:
  - running the bots (`gevr_bot.c`, A* over floor tiles in `gevr_botnav.c`);
  - ammo crate physics, broadcast every 50 ms (`net_core.c` 4438);
  - doors and glass;
  - co-op and Horde guard AI (`net_coop.c`, `gevr_horde.c`);
  - the round end (`mpmenu.c` 1181);
  - late-join snapshots of scores, positions, objects and crates
    (`net_core.c` 1191-1523).

The second list is why the server has to be the game.

## Reference: Dab's Mod `--dedicated`

Dab's Mod (`other_projects/dabs-mod`, cc90f36) runs its desktop binary with
`--dedicated`. Its netcode is server-authoritative (clients predict
snapshots), unlike ours, so we port the headless shell, not the netcode:

- `net.c` 99-117: dedicated sets server mode and local slot -1.
- `video.c` 172-232: a null window manager. The game still sees a
  320x220 viewport and builds its display lists, which are never drawn.
- Audio, input and settings saves are skipped (`main.c` 141, 287, 325;
  `input.c` 1025, 1373).
- `pdmain.c` 718: a 60 Hz tick accumulator (up to 8 ticks per pass), and
  between ticks the loop sleeps in ENet's service call so a packet wakes it.
- The world needs a first view pass even with no player: "pass 0 always
  runs" (`netsession.c` 4993-5015). The last view built is made current
  again, or the AI reads a NULL matrix (notes, `CLAUDE-notes/netplay.md`
  1560-1566).
- With sound off nothing drained the music event queue, which overflowed and
  crashed (`src/game/music.c` 278-286).

Dab's lacks three things we need and mostly have already:

- **Match start.** Dab's starts only from a test flag. Ours uses the lobby's
  countdown.
- **The next match.** Dab's server runs one match per process. Our results
  screen continues by itself after 30 s.
- **Advertising.** Dab's never lists its server. Ours needs the lobby
  Worker change below.

## Why our model makes this smaller

Every headset already runs a view pass for each other player's copy
(`lv.c` gevrViewPass 743, called from 973-987). A dedicated server is one
more simulating peer that happens to have no player: every slot is a copy.
What is missing is a world pass and a host that is not a slot.

## Parity rules

- **One source tree, one executable target.** A `gevr_server` executable for
  Linux aarch64 (Oracle, Raspberry Pi) and x86-64 is built from the same
  globs as the APK. It drops `port/fast3d` and most of `port/vr`, and keeps
  `vr_settings.cpp` for the `VrMp*` match settings.
- **No `#ifdef` in `src/game`.** The game asks `netIsDedicated()` at run
  time, so the APK runs exactly the code the server runs. The headless
  differences live in the port layer: the null renderer, the pump, `main`.
- **Same version, same release.** The protocol number is shared, and the
  server binary is attached to the same release tag as the APK. A
  mismatched server is refused like a mismatched headset.
- **A regression run per release.** A server and simulated clients play a
  bot match for a minute under WSL: scores agree, no crash, bytes per client
  within budget. Dab's `nettwelvetest.sh` is the model.

## Work

The sizes below are estimates. In total: about 1.5-2.5k new lines, plus
about 100 in the Worker.

1. **Build target** (`CMakeLists.txt` 8-11 stops every non-Android build).
   - Add a headless branch.
   - One stub file for the 249 symbols the game takes from `port/vr` and
     `port/fast3d`: `gfx_*` and the `vr_*`/`gevrVr*` pose, frame and haptics
     calls, which the game calls directly (bondview2.c, options.c).
   - The game issues no GL calls itself; it only builds N64 display lists.
   - Keep clang and the APK's flags (`-fms-extensions`, the token-paste
     allowance).
2. **`main` and the pump.**
   - A new server `main`: settings, ROM, `gameInit`, `netHostStart`,
     `bossEntry`.
   - Today's `gevrSchedBlockedRecv` (`gevr_engine_shim.c` 571) paces on
     `xrWaitFrame`. Headless, it waits in ENet like Dab's.
3. **Host without a slot** (`net_core.c`).
   - `netHostStart` seats the host in slot 0 as Bond (667-680).
   - `netIsHost` compares the local slot with the host slot (780).
   - `netSlotOccupied`/`netSlotOwned` always count the local slot (834,
     1010).
   - The idle kick (4110) must not fire for the server.
   - Every slot goes to a joiner; bots fill the rest as now.
   - Migration needs no new code: if the server dies, the existing election
     makes the lowest player the host and the match carries on as a
     player-hosted one.
4. **World pass.**
   - With no local slot, `shuffle_player_ids` (`player.c` 740-755) leaves
     every pass an extra pass, so the once-a-frame world work never runs:
     - room unload (`bg.c` 1222);
     - alarms, gas, sounds and prop defrag (`chrprop.c` 3251);
     - weapon ticks (`propobj.c` 3544);
     - guard AI (`chr.c` 2445).
   - The server runs the first occupied slot's pass as the first pass,
     without drawing. This is Dab's "pass 0 always runs".
   - Leaving `g_CurrentPlayer` on a real player afterwards replaces the
     player-0 fallback at `lv.c` 931-939.
5. **Sound, voice and input off.**
   - Drain or skip the music queue (Dab's fix).
   - The server relays voice frames without decoding them.
   - The HMD and hand reads in `net_player_sync.c` 651-690 apply only to a
     local player, so they never run.
6. **Lobby client in C.**
   - The headset's lobby client is Java (`LobbyClient.java`, 393 lines over
     HttpURLConnection). The server needs a C version with libcurl, about
     400-600 lines.
   - Joining stays ICE, so clients change nowhere but the listing:
     - libjuice offers the server's public address as a host candidate;
     - a server behind a home router also works through STUN/TURN, without
       port forwarding;
     - direct IP (port 27007) works from the first day.
7. **Lobby Worker** (`goldeneyevr.com/services/lobbies/src/index.ts`).
   - A `dedicated` flag, set only with a server key (a Worker secret), and
     shown in the list.
   - Exempt dedicated listings from the one-player minimum (225, 266, 270)
     and from the idle and lifespan timers (38-40).
   - Today anyone can name a lobby "Official"; the key is what tells a real
     server apart.
   - Deploying needs the user's OK.
8. **Server settings.**
   - The launcher's match options, the same `NetMatchConfig` the host
     already sends, start each match.
   - A server `.ini` holds the operator's limits (port range, concurrent
     matches, key) and the defaults for a match started without options.
9. **Server manager and on-demand matches.**
    - A Worker queue for "start a match": the launcher's options go in,
      and a free server's manager takes the request.
    - The manager starts a process with those options, the process opens
      its lobby, and the launcher joins it by code.
    - A "Host on a dedicated server" choice in the Host tab.
10. **Content hashes.** The session hash in HELLO and the stage hash in
    STAGE_READY, each part named on refusal (decision 4).
11. **Packaging.**
   - aarch64 and x86-64 tarballs on the release, a systemd unit and a short
     operator guide.
   - The operator supplies their own `ge.z64` (`gevr_romload.c` 77-104);
     nothing from the ROM ships.
   - The server needs no Bean, GE-X or mod files: they change only how
     things look. Hack campaigns are already refused online.

## Phases

| Phase | What | Testable by |
| --- | --- | --- |
| P0 spike | headless target links, runs a solo bot match under WSL; ticks per second and CPU logged | me, no headset |
| P1 | host without a slot, world pass, server `.ini`; Quest joins by direct IP | the user's Quest + WSL or the VM |
| P2 | C lobby client, Worker keyed listings, server manager and on-demand matches, "Host on a dedicated server" in the launcher, content hashes | Quest via the public list |
| P3 | release packaging, the Oracle VM in Ashburn, the per-release regression run, operator docs | release recipe |

## Hosting

- **Oracle Always Free**, from Oracle's own page (2026-10-28):
  - Ampere A1 with 2 OCPUs and 12 GB in the home region, and 10 TB of
    outbound data a month.
  - An 8-player match relays about 6 Mbit/s (MULTIPLAYER.md), about 2.7 GB
    an hour, so the data allowance covers over 3,000 match-hours.
  - "Out of host capacity" errors happen; retry in another availability
    domain.
  - Oracle reclaims an instance as idle when, over 7 days, 95th-percentile
    CPU, network and memory are all under 20%. An empty game server meets
    that. A pay-as-you-go account is the clean answer, and the free
    allowance still applies. Padding the load is not.
- **Community servers.** The same binary runs on a Raspberry Pi 5 or any
  Linux PC, keyed listings decide which servers are listed, and ICE gets
  players through home routers.
- **Not suitable:** Google's free e2-micro has 1 GB of outbound data a month
  (about 20 minutes of an 8-player match). AWS now gives 6 months of credits
  instead of a lasting free tier. Cloudflare Workers cannot run a UDP game
  server.

## Decisions (user, 2026-10-28)

1. **Matches start on demand, with the launcher's options.**
   - In the launcher's Host tab, "Host on a dedicated server" sends the
     player's options to the Worker. An idle server process takes them and
     opens a lobby, and the player joins it like any other.
   - A running match keeps the settings it started with. After that, the
     existing ballots and rotation choose the next map.
   - Each machine runs a small **server manager** that starts one game
     process per match, each on its own UDP port. A process exits after a
     few minutes with nobody in it.
   - The machine itself stays up. Booting a VM per match takes minutes and
     gains nothing inside a fixed free allowance.
   - The number of matches a machine takes at once comes from the P0 CPU
     measurement.
   - Dab's lobby daemon can create a dedicated room over HTTP but has no
     spawner (`tools/pdlobbyd`), so the manager is new code. It is small: a
     loop that polls the Worker and starts processes.
2. **Community servers too.**
   - The server code and binaries are published.
   - Each operator gets a key. The Worker stores hashed keys and lists a
     server only with a valid one.
   - Keys are added with `wrangler` until volume calls for more.
3. **US east coast.**
   - The Oracle tenancy's home region is Ashburn (`us-ashburn-1`). It is
     chosen at sign-up and cannot be changed.
4. **Content check, Dab's way** (`dabs-mod/port/src/net/nethash.c`).
   - **Session hash, in HELLO.** It covers what decides the simulation: the
     ROM's gameplay data and the converter and import versions.
   - It never covers textures, Bean visuals, GE-X models, music or text:
     nothing that only changes what a player sees or hears.
   - **Stage hash, in STAGE_READY.** It covers every setup, pad, tile and BG
     file the stage loaded, plus the seed.
   - Each part is hashed separately, so a refusal names the part that
     differs.
   - This also protects player-hosted games.

## Netcode model: ours and Dab's

The two were written separately:

- **Ours** began 2026-09-27. The plan was to port Perfect Dark's `port-net`
  (server simulation). Within the first day the work moved to
  "each headset simulates, the owner is authoritative for its own player",
  and testing kept it. It is about 11.5k lines in `port/src/net`.
- **Dab's** began 2026-10-04, a week later. It was written from scratch,
  Quake 3 style: only the host simulates, and clients send commands, show
  delta-coded snapshots, predict their own player and get lag-compensated
  hits. It is about 53k lines with a CI gate per feature.

| | Ours | Dab's |
| --- | --- | --- |
| World consistency | each headset's copy can drift; many past bugs were drift (dead copies, double kills, ammo crates, double explosions, dropped guns still not mirrored) | one simulation, nothing to drift |
| Bandwidth | about 6 Mbit/s from the host at 8 players, roughly 100 KB/s per client | gated at 32 KB/s per client, 12 players tested |
| Headset CPU | every headset runs a view pass for every player (up to 14 at 8 players) | clients show snapshots; only the host simulates |
| Own body in VR | never corrected, because the owner decides | predicted; a misprediction moves the player's view, which hurts most in VR |
| Cheating | the owner decides its own hits and deaths | host-validated |
| Dedicated server | one more simulating peer (this plan) | natural: the server is the simulation |

Dab's design is the stronger one for consistency, bandwidth, headset CPU and
player count. Ours has one real VR advantage, and it already plays 8-player
matches that testers checked.

Switching would not be a copy: Dab's Mod runs GoldenEye inside the Perfect
Dark engine, so its snapshots serialize PD's structures, and ours would be
re-ported onto the GoldenEye decomp. It is a rewrite of online play, not a
step in this plan. A dedicated server would be the natural place to start
that move, because the server already runs the whole simulation.

## Risks

- **CPU.** Up to 8 players plus bots each get view passes, and display
  lists are still built every tick. P0 measures this on the A1 shape;
  Dab's notes measured that drawing calls are 29-43% of host CPU and could
  be skipped later.
- **Room visibility.** It comes from each pass's camera. With no head pass,
  props and guards far from every player tick as they would on a client
  whose player is elsewhere. Horde and co-op guard AI on the server needs a
  check in P1.
- **Single-owner game logic.** Anything that asks for "the" player, like
  Dab's opening cinema owner, needs an explicit owner on a server. These
  are found by the P0/P1 runs.
