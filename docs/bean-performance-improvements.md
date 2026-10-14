# Bean performance: XBLA at 120 Hz

2026-10-13. Branch `codex/bean-performance` (PR #180), based on v0.4.23 / `e59049d`.
Goal (owner, 2026-10-13): XBLA visuals become the main way to play; 120 Hz with no
dropped frames. Research: [Bean performance research](bean-performance-research.md).

## How a 120 Hz frame is spent now

The game ticks at 60 Hz, so every other XR frame is a game frame and the one between
is a redraw (issue #53). Before this PR the game frame did everything after
`xrWaitFrame`: the game's tick and display-list build (~2 ms), then fast3d running
the list and drawing it (~4.5-5 ms), against an 8.3 ms budget.

1. **Early tick** (`gevr_engine_shim.c`, `vr_prepare_next_frame`). When the next XR
   frame is the game's, the pump locates that frame's views, head and controllers
   for its display time (the last frame's plus one period) and hands the game its
   retrace without waiting. `xrWaitFrame`/`xrBeginFrame` come once the game has sent
   its display list. Movement records taken between frames are staged for the next
   frame's trace row.
2. **Replayed eye pass** (`gfx_pc.cpp` `gfx_run`, `gfx_vr_eye_record_only`). In the
   early tick the eye pass is kept, not drawn: ring and mesh draws and depth clears
   skip their GL draw, framebuffer 0 binds no eye image. Inside the game's XR frame
   the redraw replay draws it (`gevr_vr_replay_frame(true)`), with poses located
   again at the begin and the head, both hands and the body corrected to them. That
   is the replay players already see on every other frame at 120 Hz, so it adds no
   new image path; anything missing from it would already flicker on redraws.
   Scope images render inside that frame, after the replay.
3. **Pump timing.** The forecast takes 1 ms more margin than the check after a wait,
   so jitter can't flip a retrace between slots; a wait that still comes back due
   draws that frame from the last game frame and ticks early for the next one,
   instead of running the whole list inside it.
4. The retrace's audio mix runs between XR frames, not inside the game's.
5. First pass, kept: Fast3D inlining in optimized builds (`5d7025c`,
   `GEVR_FAST3D_INLINE`) and cached replay GL bindings (`5886849`).

Not taken over: paused hubs, the native pause overlay, the virtual screen, and
passes the vertex ring can't keep draw inside the frame as before.
`files/gevr_pipeline.txt` selects 0 (old order), 1 (early tick only) or 2 (default);
`tools/perf/quest_profile.py --pipeline` matches and requires the acknowledgement.

## Measured (Quest 3, Dam, XBLA rooms, world and body batching on, 120 Hz)

`quest_profile.py` 60 s traces: stationary, forward, backward, strafe, angled wall
contact, then 14.5 s of full-speed smooth turning. Missed frames per trace:

| Build / scene | Missed | Compositor drops | Fresh in-frame median |
| --- | ---: | ---: | ---: |
| PR first pass, guard post (pad 32), 2 traces | ~145 | 6/s median | 6.5-7.2 ms |
| + early tick (pipeline 1), 4 traces ABBA | ~59 | 2/s | 5.3-5.8 ms |
| + replayed eye pass (pipeline 2), guard post | 6-8 | 0/s | 2.6-3.1 ms |
| pipeline 2, road tunnel (pad 285) | 4-8 | 0/s | 1.5-2.1 ms |
| pipeline 2, sniper scope probe | 23-35 | 0/s | 2.6-3.2 ms |
| pipeline 1, sniper scope probe | 187 | 6/s | 5.7-6.5 ms |

Every remaining miss is during full-speed smooth turning. Motion-to-photon latency
fell from 31.5 to 26-27 ms (poses are relocated for the replay). GPU time is
unchanged (~2.3 ms per eye pass). The first comparisons in this PR were taken with
`quest_profile.py` defaulting both batching switches off, which no player gets;
the defaults now match the game.

What's left: a redraw (2.8 ms) plus the early tick (4.1 ms mean: 2.55 ms record
pass, ~1.5 ms game logic and list build) must end about 1.5 ms before the game
frame's slot, or the runtime hands that frame the next display time. In the
heaviest turning views they don't. Running the game logic before the redraw and
the record pass after it would balance the two slots (~5 ms each), but needs the
controller snapshot and locomotion timeline double-buffered.

## Validation

- Host suites pass (72 of 79 on Windows; the 7 others need ASan or POSIX headers
  MinGW lacks and don't touch these files). New: staged movement records
  (`frame_timing_native.c`); the grip and locomotion fixtures gained the early-pose
  flag.
- Quest: every trace above validated (active frames, phases, acknowledged
  switches); eye-buffer captures of the replayed frames show both eyes, sky, trees,
  decals, hand and gun. Settings and saves restored and verified after each run.
- Headset checks still needed: hands and gun feel, HUD/watch, both scopes and the
  lens, transparency, decals, death/fade, pause in and out, multiplayer.
