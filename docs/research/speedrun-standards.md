# Keep GoldenEye's own clock and wire it outward

GoldenEye VR does not need a new speedrun timer. It needs to show the one it already has, without changing how it counts, and send it to the tools runners already use. On every speedrun.com board checked, a game that ships a trustworthy in-game timer (IGT) is ranked by it: Celeste, Hades, Neon White, Dead Cells, Katana Zero and Spelunky 2. Games without one are timed in real time with community load removers. The GoldenEye community times individual levels (ILs) by the end-screen mission time in whole seconds. It times full-game runs in real time from Dam's start to Cradle's fade. It has also given every new version its own board, usually within days: XBLA, the 1964 emulator at 60 fps, Xbox and Switch in 2023, and the decomp PC port Project Midas, whose board went up the day after release. Frame-rate rules are the fight that keeps coming back. No VR GoldenEye board exists, so this port would set the norms. The highest-value work, in order:

1. Leave the 1/60 s `mission_timer` semantics untouched.
2. Add a precise readout and a full-run total beside the original truncated time.
3. Stamp the version and every gameplay-affecting setting on the debrief.
4. Add a one-button mission restart and precise local personal bests (PBs).
5. Drive desktop LiveSplit through its built-in TCP server. This is the only route that reaches a standalone Quest, because autosplitters can read only processes on the same PC.

A ghost race suits GoldenEye's level-based structure as a local practice feature built from recorded poses. But in an open-source game, neither ghosts nor an official online leaderboard can serve as proof. Video judged by moderators remains the standard.

## Every board with a trusted in-game clock ranks by it

Dedicated speedrun modes are still mostly an indie feature, and the ones that exist do much the same things:

- **Celeste:** its Speedrun Clock gained a File timer, a Quick Restart and a skippable stat tally in v1.2.0.0 ([Celeste changelog](https://www.celestegame.com/changelog.html)).
- **Dead Cells:** the 2023 Speedrun Mode tracks "the completion time of each biome" and "tracks your previous bests and compares them" ([Dead Cells wiki](https://deadcells.wiki.gg/wiki/Version_3.4)).
- **Axiom Verge:** random elements use a fixed seed, dialogue can be skipped, and the timer "counts elapsed frames rather than real time", with splits recorded at each boss. Tom Happ wanted "every playthrough… comparing apples-to-apples" ([PlayStation Blog](https://blog.playstation.com/archive/2015/01/26/inbound-ps4-platformer-axiom-verge-gets-speedrun-mode/)).
- **Katana Zero:** added a Speedrun Mode in its first free update ([PC Gamer](https://www.pcgamer.com/katana-zero-adds-hard-mode-speedrun-mode-in-latest-update)).

Across these games the bundle comes down to five things:

- a precise on-screen IGT
- per-segment splits
- comparison against your own best
- removal of non-gameplay friction
- deterministic randomness

Many of the most-run games ship none of this. Titanfall 2's PC runners rely on a LiveSplit autosplitter that "automatically removes load times", and console runners pause by hand ([TF2SR wiki](https://tf2sr.gitbook.io/wiki/welcome/getting-started)). Those autosplitters break when a game update shifts memory ([Refunct guide](https://www.speedrun.com/refunct/guide/276ii)).

On speedrun.com, real time (RTA) is always the base column, and moderators may add load-removed real time (LRT) or IGT ([speedrun.com support](https://www.speedrun.com/support/learn/editing-a-leaderboard)). The boards show clearly what a trustworthy clock buys:

- **Ranked by IGT:** every board examined for a game with a built-in timer: Celeste, Hades, Neon White, Dead Cells, Katana Zero and Spelunky 2.
- **Ranked by RTA or LRT:** every board examined for a game without one: Hollow Knight, Doom Eternal, Portal, Ori, Titanfall 2 and Super Mario Odyssey.

([Celeste board](https://www.speedrun.com/celeste); [Hollow Knight board](https://www.speedrun.com/hollowknight))

Communities switch as soon as they can read a game's internal timer. The Tetris Effect: Connected board moved Journey mode to IGT because it is "far more accurate, and ties to the second are very rare" ([speedrun.com Tetris Effect news](https://Speedrun.com/tetriseffect/news)). This matters more for GoldenEye VR than for most games. The port runs at different speeds on a Quest, a phone and a gaming PC, so load times depend on hardware. A timer that leaves out loads is the only figure that compares fairly across devices.

The hardest lesson is that **the timer's rules are a contract with runners**. Celeste's v1.2.1.0 made the chapter timer keep running through deaths, cutscenes and menus. The changelog labelled it a "Breaking Change" and said "Speedrun times for single-chapters will be cleared". A later fix for the clock freezing on unpause "adds 0.15s IGT to each unpause" ([Celeste changelog](https://www.celestegame.com/changelog.html)). Hades shows the opposite problem. Its clock stops in boon menus, transitions and survival rooms, and IGT categories then plan routes around those pauses ([Kotaku](https://kotaku.com/how-hades-speedrunners-clear-the-game-in-under-eight-mi-1845352749)). Any interval the timer leaves out becomes a place to save time. That is why the GoldenEye XBLA board requires mouse-and-keyboard runners to set `ge_always_allow_inputs` to false, which turns off camera and firing during cutscenes ([XBLA board](https://www.speedrun.com/goldeneye_007xbla)). Small rounding choices add up too. One runner complained about IGTs that round each segment down, so error builds up over a long run ([speedrun.com post](https://www.speedrun.com/posts/lj0nd)).

Developer guidance agrees on a short core:

- fast resets
- skippable cutscenes and text
- a timer that leaves out loads
- controllable randomness
- not patching out speed tech

([Game Developer](https://gamedeveloper.com/business/6-ways-to-make-your-game-more-appealing-to-speedrunners); [CritPoints](https://critpoints.net/2020/09/16/designing-for-speedrunners/))

The SDA knowledge base adds that a reset should land just before gameplay, so the runner can start their own timer ([SDA KB](https://kb.speeddemosarchive.com/Making_your_game_speedrunner-friendly)). Two Celeste details are worth borrowing directly ([Celeste changelog](https://www.celestegame.com/changelog.html)):

- **Verification stamps:** Chapter Complete screens show when Assist or Variant modes were used, and the Epilogue shows the version number.
- **No fixed restart combo:** the hard-coded L+R+Start Quick Restart was removed because it "can now conflict with custom rebinds". That matters for a VR port whose controller bindings already differ by headset.

The research found no case of a developer shipping an official LiveSplit integration. Autosplitters reach LiveSplit's registry as community submissions ([LiveSplit.AutoSplitters](https://github.com/LiveSplit/LiveSplit.AutoSplitters/blob/HEAD/README.md)).

| Feature | Where it stands | Exemplars |
|---|---|---|
| On-screen IGT you can turn on | Expected in new action and indie games | Celeste, Hades, Dead Cells, Axiom Verge |
| IGT shown to milliseconds or frames | Expected where boards rank by IGT; whole seconds cause ties | Celeste (ms), Axiom Verge (frames) |
| Written start, stop and pause rules | Essential; changing them later wipes boards | Celeste v1.2.1.0 |
| Per-level and whole-run totals | Standard in level-based games | Celeste Chapter/File, Dead Cells biomes |
| Fast reset | Standard | Celeste Quick Restart |
| Stored PBs with ahead/behind comparison | Common in level-based games | Celeste, Dead Cells, Trackmania |
| Ghost of your own PB | Standard in time trials, rare elsewhere | Mario Kart, Trackmania, Neon White |
| Version and assist stamps on the end screen | Rare verification aid | Celeste |
| Official online leaderboards | Risky without validation | Neon White, Ori Spirit Trials |
| LiveSplit hook shipped by the developer | Not found | none |

## GoldenEye runners give each new version its own board within days

GoldenEye's authority is The Elite, and it will never rank this port. It accepts times "only… from the original Nintendo 64 versions" and does "not accept times for remakes on newer systems or emulators" ([The Elite: Join](https://rankings.the-elite.net/join)). Its proof policy asks for direct-capture video that runs from "the very beginning of the first cutscene of the level" to an end screen "shown long enough to tell what the final time is". The video must be at least 360p and 25 fps, with audible sound effects. Passing off "an emulator or other banned version" run as a console run counts as cheating ([The Elite Proof Policy](https://rankings.the-elite.net/proof)). Stage times are whole seconds; the combined Agent record is 18:59 on NTSC ([The Elite system records](https://rankings.the-elite.net/goldeneye/systems)). Full-game single-segment runs are timed in real time "from the start of Dam to the fade to black after the last level", and the Agent record is 21:26 ([The Elite single segments](https://rankings.the-elite.net/goldeneye/single-segments/category/any)). The speedrun.com N64 board uses the same proof policy. A moderator has ruled out "mirroring the rankings of The Elite or integrat[ing] the two in any way" ([speedrun.com forum](https://www.speedrun.com/goldeneye_007_n64/forums/xbne6)). So the port should present its times as its own, never as comparable with Elite times. It should still show them in the Elite's format, because that is what every GoldenEye moderator checks.

The timer runners know is simple, and it counts lag. In the decompilation:

- `mission_timer += g_ClockTimer` runs only for player one, and only during first-person play or the fade to title ([bondview2.c](https://github.com/n64decomp/007/blob/master/src/game/bondview2.c)).
- `g_ClockTimer` is zero while controls are locked or the game is paused. Otherwise it equals the number of video fields since the last game frame ([lv.c](https://github.com/n64decomp/007/blob/master/src/game/lv.c); [frametiming.c](https://github.com/n64decomp/007/blob/master/src/game/frametiming.c)).
- PAL ticks are scaled by 60/50.
- The debrief prints `missiontimer / 60` as `%02d:%02d` and drops the fraction ([front.c](https://github.com/n64decomp/007/blob/master/src/game/front.c)).

Because the timer counts video fields rather than game frames, **lag still costs time**: during play the IGT runs at about real time.

This repository keeps that loop but changes two of its inputs:

- **Pacing:** `port/src/gevr_engine_shim.c` paces the game in 1/60 s slots against the wall clock. Hardware that keeps up steps the game one field per tick, like the 60 fps versions rather than the N64's roughly 20 fps ([DaisyFan guide](https://www.speedrun.com/goldeneye_007_xbox_and_nso/guides/mk9pc)). Under SpaceWarp at 45 Hz it steps 1, 1, 2 fields, as the N64 did when it dropped frames.
- **Stall cap:** `src/game/frametiming.c` limits the catch-up after a stall to 6 fields (about 100 ms), so an asset hitch does not turn into seconds of simulation.

The port's IGT therefore equals simulated time. It leaves out device stalls, which keeps Quest and PC fair against each other. But it differs from N64 IGT in exactly the long-frame cases that cause lag warps.

Two more port details matter here:

- **Co-op:** the mission time belongs to the local headset's player, not player one (`src/game/bondview2.c`).
- **Saved bests and target times:** the save file already keeps best times per mission and difficulty in whole seconds, and checks each finish against the cheat-unlock target times (`src/game/file.c`, `src/game/file2.c`). GoldenEye shipped PBs and par times in 1997; it never showed fractions.

The record says a new version gets a new board, and quickly. Switch Online and Xbox GoldenEye arrived on 27 January 2023. One runner predicted "it'll be added as a separate game on SRC". Another said the versions play "dramatically different", some with "framerate and frame time issues that cause the in game time to behave very different" ([speedrun.com forum](https://www.speedrun.com/goldeneye_007_n64/forums/asa5y)). The XBOX & NSO board went up on 30 January. It is now the busiest GoldenEye board on speedrun.com, with **2,045 runs and 16 active players**, against the N64 board's 688 runs and 2 active players ([XBOX & NSO board](https://www.speedrun.com/goldeneye_007_xbox_and_nso); [N64 board](https://www.speedrun.com/goldeneye_007_n64)). It splits ILs by Xbox One and Switch, and full-game runs by Series X|S, One and Switch. The reason given is that the Xbox version "will stay at 30fps compared to 20fps", and warping "isn't possible due to the better framerate" on Series consoles ([DaisyFan guide](https://www.speedrun.com/goldeneye_007_xbox_and_nso/guides/mk9pc)).

The other version boards set similar speed rules:

- **XBLA:** bans performance-induced warps in its default category and moves them to an "L-Warp" subcategory. It rejects Community Edition runs for "any other difference that saves time" ([XBLA variables](https://www.speedrun.com/api/v1/games/j1ne5nl1/variables); [XBLA rules](https://www.speedrun.com/de-DE/goldeneye_007xbla/forums/6xvp8)).
- **1964 emulator:** Stopwatch.exe must be visible at all times ([GoldenEye 1964 board](https://www.speedrun.com/ge60fps)).
- **Perfect Dark PC port:** requires "Tick rate divisor must be set to 1" ([Perfect Dark board](https://www.speedrun.com/perfect_dark)).
- **Project Midas (decomp PC port):** released on 17 October 2026, it had its own board the next day and **234 runs from 19 players** within three weeks. Its rules cap Max FPS at 240 and require the FPS counter to be shown ([Midas board API](https://www.speedrun.com/api/v1/games/268p559d?embed=categories,moderators,platforms,variables)). One runner's comment on a Midas run sums up where every port board starts: "In limbo until we establish fps rules" ([Midas runs API](https://www.speedrun.com/api/v1/runs?game=268p559d&max=200&embed=players)).

Outside GoldenEye, an Ocarina of Time forum thread called the decomp port Ship of Harkinian "a different game altogether", and it has its own board ([OoT forum](https://www.speedrun.com/oot/forums/uw7vj)).

Two community positions bear directly on VR. First, runners treat strafe-running as a core mechanic. When a player asked for a separate Xbox "dual analog" category, established runners replied "Strafing is not a glitch", and no category was added ([speedrun.com forum](https://www.speedrun.com/goldeneye_007_xbox_and_nso/forums/o7ic2)). Nobody has checked whether the port's VR stick locomotion keeps the original diagonal speed gain. That answer decides whether VR runs read as GoldenEye or as a handicapped variant. Second, the port already plays differently in stereo: aiming follows the tracked controller, and the trigger does no melee. VR and flat will be separate categories whatever the timer does.

Tooling is thin. LiveSplit's registry of 2,667 autosplitters includes other Bond games (Blood Stone, Quantum of Solace) but nothing for GoldenEye 007 or Perfect Dark ([LiveSplit.AutoSplitters.xml](https://github.com/LiveSplit/LiveSplit.AutoSplitters/blob/HEAD/LiveSplit.AutoSplitters.xml)). The newest GoldenEye tool is an OBS plugin that records N64 runs automatically ([the_golden_eye](https://github.com/acheronfail/the_golden_eye)). No speedrun.com board or subcategory exists for any VR GoldenEye ([speedrun.com API](https://www.speedrun.com/api/v1/games?name=GoldenEye%20007&embed=categories,platforms)). That includes the separate PC OpenXR port GEVR by no6969el ([Fix Gaming Channel](https://www.fixgamingchannel.com/goldeneye-007-is-now-playable-in-native-openxr-vr/)).

## VR boards police the playspace and the mirror, not the headset

VR speedrun communities are small, and each game keeps a single board. On speedrun.com, Boneworks has 200 players, SUPERHOT VR 298 and Half-Life: Alyx 63 ([Boneworks](https://www.speedrun.com/boneworks); [SUPERHOT VR](https://www.speedrun.com/shvr); [Half-Life: Alyx](https://www.speedrun.com/hla)). None of the boards examined keeps a separate Quest-standalone leaderboard. Bonelab ranks PC and Quest runs together with a platform column, and splits by patch version and glitch rules instead ([Bonelab](https://www.speedrun.com/bonelab)). Where locomotion changes speed, it becomes a board variable:

- **H3VR:** Arm Swinger, Twin Stick and Teleport ([H3VR](https://www.speedrun.com/h3vr)).
- **Into the Radius:** Teleportless and Teleporting ([Into the Radius](https://www.speedrun.com/into_the_radius_vr)).
- **Rec Room:** filtered VR against Screen and Walk against Teleport ([Rec Room rules](https://www.speedrun.com/posts/quan9)).

The rule nearly every VR board shares is a ban on playspace abuse:

- **SUPERHOT VR:** bans tricks that "can't be done in a reasonably small area" ([SUPERHOT VR](https://www.speedrun.com/shvr)). Bonelab and Boneworks use almost the same wording.
- **Rec Room:** required player height between 121 and 190 cm and banned recalibration, because taller players moved further ([Rec Room rules](https://www.speedrun.com/posts/quan9)).
- **Thrill of the Fight:** verified runs must show the game's strength multiplier on screen ([Thrill of the Fight](https://www.speedrun.com/thrill_of_the_fight_vr_boxing)).

Mods are banned except for whitelisted timer and fix mods; Bonelab allows only SpeedrunTimer and LoadMirror ([Bonelab](https://www.speedrun.com/bonelab)). Strikingly, **none of the 15 VR boards examined regulates headset refresh rate or snap versus smooth turning**, even though Quest headsets support up to 120 Hz ([Meta display frequency docs](https://developers.meta.com/vr/documentation/unity/unity-set-disp-freq/)). GoldenEye's port boards regulate frame rate because GoldenEye's physics and tricks depend on it. A GoldenEye VR board would probably be the first VR board to care.

Story-length VR runs are timed by load-removed real time from a LiveSplit autosplitter. Half-Life: Alyx states it plainly: "Timing is done using the Autosplitter built into Livesplit". Its runs are recorded from the desktop mirror using one eye, so that desktop-only HUD elements stay out of the frame ([Half-Life: Alyx](https://www.speedrun.com/hla)). Bonelab requires the mirror or its LoadMirror mod, so moderators can see load screens ([Bonelab](https://www.speedrun.com/bonelab)).

Eye of the Temple has the cleanest built-in design ([Eye of the Temple guide](https://www.speedrun.com/eye_of_the_temple/guides/iwtym)):

- speedrun challenges started from the game's menu
- a loadless in-game timer as the official time
- a completion popup that videos must show
- a corner timer on the PC desktop view only
- a LiveSplit autosplitter, which is "not available in the native Quest version"

The most-used community answer, jakzo's Bonelab SpeedrunTimer (about 9,200 downloads), put a live timer "on wrist and in loading screens" and later added a splits view during loads ([CHANGELOG](https://github.com/jakzo/SlzMods/blob/main/projects/Bonelab/SpeedrunTimer/CHANGELOG.md); [Thunderstore](https://thunderstore.io/c/bonelab/p/jakzo/SpeedrunTimer/)).

Standalone Quest is where the tooling runs out. A Quest runner on speedrun.com's forum recorded in the headset and added splits afterward with OBS and NWSplit. Their conclusion: "Doing splits in real time with an Oculus Quest is very difficult. Add splits in post." ([speedrun.com forum](https://www.speedrun.com/post/q1v25)). Capture itself is workable:

- **In-headset recording:** Meta recommends 1080p at "36 fps (variable)" ([Meta video capture guide](https://developers.meta.com/horizon/resources/video-capture-mr-vr/)), well above The Elite's 25 fps minimum.
- **Meta Quest Developer Hub:** casts to a PC with dominant-eye selection and a cropped 16:9 mode. Its PC-side recording is limited to three minutes without audio ([MQDH media docs](https://developers.meta.com/horizon/documentation/native/android/ts-mqdh-media/)).

Where to draw a live timer in the headset is a comfort question, not a speedrun one. General VR design guidance discourages head-locked content ([Meta design guideline](https://developers.meta.com/vr/design/mr-design-guideline)), which matches this project's own rule against wrist-attached and head-locked menus. Eye of the Temple's model fits both: the official time on the results screen and the live timer outside the headset. The network feed described next carries the live clock to viewers.

## LiveSplit's built-in server is the only hook that reaches a Quest

LiveSplit remains the standard timer and is actively maintained. Version 1.8.37 shipped in February 2026, and the repository was updated on 7 November 2026 ([LiveSplit releases](https://github.com/LiveSplit/LiveSplit/releases)). It reads Game Time from an autosplitter or an external program, compares against PB or Sum of Best, races on racetime.gg, and goes into OBS as a captured window ([LiveSplit README](https://github.com/LiveSplit/LiveSplit/blob/HEAD/README.md)). LiveSplit One brings the same engine to the browser and to an OBS source on Windows, Linux and macOS ([obs-livesplit-one](https://github.com/LiveSplit/obs-livesplit-one)). LibreSplit is the active Linux-native timer ([LibreSplit](https://github.com/LibreSplit/LibreSplit)).

The surrounding services have changed:

- **splits.io:** "has permanently shut down", and LiveSplit removed it in 1.8.34 ([splits-io](https://github.com/glacials/splits-io)).
- **therun.gg:** its live-tracking component has shipped with LiveSplit since March 2024 ([LiveSplit.TheRun](https://github.com/therungg/LiveSplit.TheRun)).
- **racetime.gg:** starts LiveSplit from the race countdown and finishes the race on the final split, needing nothing from the game ([racetime.gg](https://racetime.gg/about/livesplit)).

The typical streamer setup is therefore OBS plus a LiveSplit window or LiveSplit One source, global hotkeys for manual splits, and optionally therun.gg and racetime.gg. PC VR runners add tools such as OpenVR2Key to bind split hotkeys to their controllers ([Scanner Sombre VR guide](https://docs.google.com/document/d/e/2PACX-1vQOFOloso457cEeAC31RohH9pJ49wWUpiVIEf8iqZmUenwUVPPy-dRuY0ppKuQaMQA_2sENNvJWGrQF/pub)).

Autosplitters cannot help a Quest. ASL scripts read local process memory. The newer WebAssembly Auto Splitting Runtime runs on Windows, macOS and Linux with "no networking" ([livesplit-auto-splitting README](https://github.com/LiveSplit/livesplit-core/blob/HEAD/crates/livesplit-auto-splitting/README.md)). Its helper library has emulator support for seven consoles and none for the N64 ([asr emulator modules](https://github.com/LiveSplit/asr/tree/HEAD/src/emulator)).

On the PC builds, the proven way to make a native game easy to autosplit is to publish a tagged, versioned struct in memory:

- **Everest (Celeste):** exposes a page that begins with the marker "EVERESTAUTOSPLIT", followed by a layout version, chapter and file times, and flag words. It sets the version byte to 0xff while writing, so a reader never sees a half-written update ([Everest AutoSplitter.cs](https://github.com/EverestAPI/Everest/blob/HEAD/Celeste.Mod.mm/Mod/Everest/AutoSplitter.cs)).
- **SourceAutoRecord (Portal 2):** places a tick count and an action code between "SAR_TIMER_START" and "SAR_TIMER_END" ([SAR SpeedrunTimer.cpp](https://github.com/p2sr/SourceAutoRecord/blob/HEAD/src/Features/Speedrun/SpeedrunTimer.cpp)).
- **Bonelab mod:** builds its marker at runtime "to avoid finding this hardcoded array" in the binary ([Livesplit.cs](https://github.com/jakzo/SlzMods/blob/HEAD/projects/Bonelab/SpeedrunTimer/src/Utilities/Livesplit.cs)).

With a struct like this, the community can write one autosplitter that finds the marker and keeps working across port updates. That matters here because the port's 64-bit memory layout makes any N64-era memory address useless.

The route that does reach a headset is LiveSplit's own server. Since 1.8.29 (May 2024) a TCP server is built into LiveSplit, and since 1.8.30 it can run a WebSocket server instead. Both listen on port **16834**, the TCP listener accepts connections from other machines, and a named pipe is always open for local programs ([LiveSplit README](https://github.com/LiveSplit/LiveSplit/blob/HEAD/README.md); [CommandServer.cs](https://github.com/LiveSplit/LiveSplit/blob/HEAD/src/LiveSplit.Core/Server/CommandServer.cs)). Commands are plain text lines: `starttimer`, `split`, `reset`, `setgametime TIME`, `pausegametime` and about sixty more. The README's example client is three lines of Python. The catch is that the runner must start the TCP or WebSocket server from LiveSplit's Control menu, unless they set the "Startup Behavior" option added in 1.8.34 ([LiveSplit 1.8.34](https://github.com/LiveSplit/LiveSplit/releases/tag/1.8.34)).

Games already drive LiveSplit this way:

- **RUMBLE (VR):** a mod sends start, split and reset "asynchronously with a 2-second timeout to prevent game lag" ([Thunderstore RallyXAutoSplitter](https://thunderstore.io/c/rumble/p/ACutiePi/RallyXAutoSplitter/)).
- **Bonelab:** the mod runs a WebSocket server on the Quest itself (port 6162), and LiveSplit One connects to it ([SplitsServer.cs](https://github.com/jakzo/SlzMods/blob/HEAD/projects/Bonelab/SpeedrunTimer/src/SplitsServer.cs)).

That second direction is weaker. LiveSplit One's JSON server protocol is described as experimental and "will likely change a lot" ([server_protocol.rs](https://github.com/LiveSplit/livesplit-core/blob/HEAD/src/networking/server_protocol.rs)). And the hosted LiveSplit One page runs over https, so it cannot easily open an unencrypted `ws://` connection to an address on the local network ([LiveSplitOne issue #275](https://github.com/LiveSplit/LiveSplitOne/issues/275)). A game-side TCP client that connects to desktop LiveSplit covers Quest, Android, iOS, Linux and Windows with one code path.

Other channels matter less:

- **OBS text files:** OBS text sources that read a file appear to refresh only about once a second ([OBS forum](https://obsproject.com/forum/threads/obs-txt-file-read-speed.79879/)). That is too slow for a live clock but fine for a summary after the run.
- **Steam leaderboards:** they need Steamworks distribution, which the port does not have ([Steamworks leaderboards](https://partner.steamgames.com/doc/features/leaderboards)).
- **Video hosting:** Twitch's 100-hour cap on highlights and uploads took effect in April 2025, and several boards stopped accepting Twitch videos ([GamesRadar](https://www.gamesradar.com/games/a-really-sad-day-for-speedrunning-the-worlds-fastest-gamers-brace-for-brace-for-a-new-twitch-storage-limit-on-all-highlights-and-uploads)).
- **speedrun.com data:** its October 2026 terms put leaderboard data under an open licence that allows building on it, with a ban on AI training ([speedrun.com news](https://www.speedrun.com/news/wlvmlk81)). The port could therefore show board data in its launcher later.

## Ghosts teach and motivate, but prove nothing

Ghosts are an old time-trial convention:

- **Mario Kart:** Mario Kart 64 had staff ghosts on three courses, and every course has had one since Double Dash!! ([Super Mario Wiki](https://www.mariowiki.com/Ghost_(Mario_Kart_series))).
- **Trackmania:** players can load the ghost of any leaderboard record from the in-game leaderboard ([Trackmania docs](https://doc.trackmania.com/play/watch-replays)).
- **Neon White:** players can race their own ghost, and the developers set reference times by modifying "the ghost system to record developers' runs" ([VICE](https://www.vice.com/en/article/neon-whites-developers-being-too-good-at-their-own-game-caused-real-problems)). Ghosts and shortcut hints were added because playtesters tended to beat each level once and never return ([Wikipedia](https://en.wikipedia.org/wiki/Neon_White)).

None of the VR games examined advertises ghost racing, and story-length games almost never have it. GoldenEye is built like a time trial, though: 20 missions, three difficulties, a per-mission IGT and target times for unlocking cheats. A per-mission ghost of your own PB fits naturally. Neon White's reason for adding ghosts, getting players to replay levels, applies to a VR port whose players mostly play each mission once.

Ghosts and online leaderboards fail as verification, though:

- **Neon White:** days after launch, its boards were topped by 0.06-second runs "impossible for any level" ([Kotaku](https://kotaku.com/neon-white-speedrunning-cheaters-annapurna-interactive-1849115456)).
- **Trackmania:** replay-file records hid alleged cheating at the top until a tool extracted the inputs from the replays ([PC Gamer](https://www.pcgamer.com/cheating-allegations-catch-up-with-some-of-trackmanias-fastest-drivers)).
- **Portal 2:** runners concede it is "definitely possible to fake demos" ([speedrun.com Portal forum](https://www.speedrun.com/portal/forums/2c91j)).

In an open-source game, anyone can rebuild a client-side signature. Validating runs by replaying their inputs would need the simulation to give exactly the same result on Quest, PC and phone builds, across compilers and floating-point VR poses. No one has checked this for the port, and it is unlikely.

The workable design is a **pose-trace ghost**: record Bond's position, facing and perhaps head and hand poses each tick, play them back as a translucent figure that nothing collides with, and save the result as a local file the player can share. It needs no determinism, and it could reuse the port's existing drawing of remote players from co-op and multiplayer. Present it as a practice and comparison tool, not as proof.

A live race between people needs less than it seems. racetime.gg already runs synchronized races through LiveSplit with no game integration, so the LiveSplit client gives runners racing at no extra cost. A different kind of race would be one where each player runs a private copy of a mission while seeing the others' live ghosts over the port's netcode. The research found no precedent for that in GoldenEye or in VR. It is best left until local ghosts exist.

## What GoldenEye VR should build, in order

None of the reference projects this port draws on contains LiveSplit code, but Dab's Mod (other_projects/dabs-mod) already ships most of the in-game half and should be ported rather than redesigned:

- **HUD timer:** the mission timer with hundredths in GoldenEye's HUD figures, lower left (`port/src/gehud.c` `geHudRenderMissionTimer`).
- **Ghost Time Trials:** records 24-byte pose samples 20 times a second, races up to ten translucent ghosts, and shows a split against the fastest (`src/game/modghost.c`, `modghost.h`).
- **Trial rules:** a run only records under rules that switch off the mod's added moves and the cheats, and the file is stamped with that flag (`MODGHOSTHF_TRIALRULES`, `MODGHOSTHF_MODDED`).
- **Ghost server:** accounts, uploads, leaderboards and downloads over HTTPS (`port/src/ghostnet.c`), plus a ghost picker menu (`port/src/ghostmenu.c`).

The LiveSplit client, the debrief stamp and the PC memory struct are new work. The evidence above sets the order.

| Priority | Build | Precedent it follows |
|---|---|---|
| 1 | Freeze the timer's rules and publish them; add a precise readout and a run total from integer ticks | Celeste's IL wipe; IGT boards; complaint about rounding drift |
| 2 | Debrief stamp: version, platform, VR or flat, art set, locomotion, enhancements, display rate, step statistics | Celeste stamps; Midas FPS rule; Thrill of the Fight multiplier; XBLA "any other difference" |
| 3 | Speedrun mode: one-button restart before the intro cutscene, quick abort, full-game RTA plus IGT sum, practice file | SDA reset advice; Elite proof window; full-game rules on existing boards |
| 4 | LiveSplit TCP client (and the named pipe on Windows) | LiveSplit server; RUMBLE mod; Quest runners' "add splits in post" |
| 5 | Precise local PBs, ahead/behind delta, target time shown as par | Celeste, Dead Cells, Trackmania; GoldenEye's own save |
| 6 | Versioned memory struct on PC builds; JSON run summary file | Everest, SAR |
| 7 | Pose-trace ghost of your own PB, as a local shareable file | Mario Kart, Neon White, Trackmania |
| Later | Game-hosted WebSocket for LiveSplit One and overlays; opt-in unverified online PB board; live ghost races | Bonelab mod; Neon White warning |

### 1. Timer rules and precise readout

The timer comes first. Keep `mission_timer`, `getMissiontimer()` and the debrief's truncated `Time: mm:ss` exactly as the decompilation has them. Publish a one-page timer rule that says:

- the clock counts 1/60 s fields while Bond is under first-person control
- it stops while the game is paused, while controls are locked and during scripted cutscenes
- any single step counts at most six fields
- in co-op, it follows the local player

Add a precise figure beside the original time, never in its place. Derive it from integer ticks (ticks × 1000 / 60, rounded down) and show it as m:ss.mmm, since speedrun.com accepts milliseconds. Add a run total that sums ticks, not rounded mission times.

Before shipping, test whether pauses and cutscenes let a runner gain time:

- whether physical walking, head movement or held inputs during a pause, the watch menu or a cutscene carry over when the clock resumes
- whether the game accepts camera or fire input during cutscenes (the input the XBLA board makes runners switch off)

### 2. Debrief stamp

The debrief should show what moderators would otherwise need video to prove:

- port version and build
- platform and headset
- VR or flat
- art set (N64, XBLA Bean or GE-X)
- locomotion and turn mode
- aim assist
- any enabled enhancement, including look-only cheats that keep progress (`gevrCheatsBlockProgress`)
- display refresh rate and whether SpaceWarp is on
- the longest single step in fields, and how many steps hit the six-field cap

The step statistics are the port's equivalent of Midas's FPS counter. They show whether a run contained the long steps that make lag warps possible. The community then does not have to write FPS rules for a game whose display rate is not its tick rate.

### 3. Speedrun mode

A speedrun-mode toggle should bundle the time-savers:

- **Restart:** one button that lands at mission start, before the intro cutscene, so the recording contains everything The Elite's proof window requires.
- **Quick abort:** straight back to the briefing.
- **Full-game tracking with two clocks:** real time from pressing Start on Dam's briefing to the fade of Cradle, Aztec or Egyptian (matching every existing board), plus the summed IGT. Each board can choose.
- **Practice file:** all missions unlocked, stamped as practice the way Celeste stamps Assist mode.

Leave randomness alone: GoldenEye's randomness is part of its existing routes, and a seeded mode would make runs impossible to compare with any other version. Do not bind restart to a fixed button combo that can collide with remapped controllers.

### 4. LiveSplit connection

Implement a LiveSplit TCP client that never blocks the game, with an IP and port setting (default 16834). On Windows, the local named pipe needs no setup.

- **Run start:** send `starttimer`, unless `getcurrenttimerphase` reports that the timer is already running (so a racetime.gg countdown start takes precedence). Then send `pausegametime`.
- **During the run:** send `setgametime` from the tick total a few times a second and at every split.
- **Mission complete:** send `split`.
- **Abort:** send `reset`.

With game time paused and set by the game, LiveSplit shows pure IGT. This is the same approach ASL scripts use when they report the game's own timer.

### 5. Local PBs and ghosts

Keep PBs and ghosts on the device. Store a sidecar file next to the EEPROM save with precise bests per mission and difficulty. Show an ahead/behind delta on the debrief, display the cheat target time as par, and add the own-PB pose ghost (item 7).

### 6. PC hooks and run summary

On PC builds, publish an Everest-style struct with:

- a marker built at runtime
- a layout version and the port version
- stage and difficulty
- mission ticks and run ticks
- flags

Document its layout so the community can write and register an ASL or WebAssembly autosplitter. After each mission, write a JSON run summary file for records and moderator checks.

### Later, and what to leave to runners

A game-hosted WebSocket for LiveSplit One and OBS overlays, as in the Bonelab mod, can wait for demand, given LiveSplit One's unstable protocol. If an online board is ever added to the goldeneyevr.com Worker, label it unverified and filter outliers. Do not promise integrity an open-source client cannot deliver.

Finally, let runners own the rules. Invite them to request a GoldenEye VR board on speedrun.com with:

- VR and flat as separate categories
- platform as a column
- a version variable
- a playspace-abuse rule

Give them a draft built from The Elite's proof policy plus the debrief stamp. The Midas board shows that runners will set one up within a day if a port gives them something to time.

## Conclusion

The surprise is how little GoldenEye VR has to invent. GoldenEye already shipped an IGT, per-mission bests and par times in 1997. Runners' boards already ask for exactly what an end screen can show. LiveSplit already listens on a TCP port a headset can reach. The port's real speedrun risks lie in what it changed for VR and modern hardware: 60-field pacing, SpaceWarp stepping, the six-field stall cap, tracked aiming, room-scale walking, and locomotion that may or may not keep strafe-running. Those decide whether GoldenEye VR runs read as GoldenEye. They should be measured and stamped on the debrief before anyone sets a record.

The second lesson is timing. Every port board has started "in limbo" over speed rules, and Celeste had to wipe IL times when it redefined its clock. Publishing the timer rule and the debrief stamp before the first runner submits a time costs a page of documentation and a few lines of HUD; doing it afterwards costs the board. A standalone Quest port whose clock drives LiveSplit over the network would also be the first GoldenEye LiveSplit integration and the first VR GoldenEye ruleset. That is a chance to settle VR speedrun questions no VR board has written down yet: refresh rate, locomotion and playspace.
