# Facility: green flashes (2026-10-17)

Report: on XBLA (Bean) Facility, walls and floors flicker green. It happens
with AI fill on or off and with SpaceWarp on or off. Evidence: the player's 43 s
video, then headset recordings (`adb shell screenrecord`, both eyes at about
60 fps) on Quest 3 2G0YC1ZF8Q0JQ6.

![Frame before (top) and the flash (bottom): 207, 693, 717, 852, 1018, 1168](facility-fog-flash.jpg)

## What it is

A flash frame is flat RGB (12, 28, 15): Facility's fog colour
0x10, 0x20, 0x10 (`bgfog.c`, and Bean's fog table `0x102010`). Only the
stats overlay, the vignette and the HUD quad survive on it. It is not z-fighting
and not missing room geometry. The eye image reached the compositor holding only
the display list's first draw, the background fill.

**Cause: the 2x multisampled eye image drawn directly.** The eye framebuffer
uses multisampled render-to-texture (`FramebufferTextureMultisampleMultiviewOVR`,
`EyeMsaa=2` by default since v0.5.6; 4x since v0.5.4). A frame whose display
list is drawn straight into it (`gfx_run`'s non-deferred branch) switches
targets for its HUD captures in the middle of the pass. That image came out
holding only its first fill. Recorded frames replayed in one pass
(`gevr_vr_replay_frame`) came out whole.

Measurements: flashes per ~14 s, standing still in the vent at level start.

| Build / switch | Flashes |
| --- | --- |
| v0.5.6, `gevr_pipeline.txt` 1 (every fresh frame drawn directly) | 349-379 (half the frames) |
| replay skips mesh draws / ring draws / reprojection | still flashes on the other half |
| no presentation correction; skip mesh draws in the direct pass | 365-366 |
| `glFinish` before releasing the eye image | 339 |
| detach the eye texture before release | 357 |
| read back one pixel of the eye texture before release | 0 |
| eye MSAA forced to 1 | 0, all frames whole |
| **fix**, pipeline 1 / default / default + SpaceWarp off | **0 / 0 / 0** |

At the default pipeline (2), the early tick normally defers the eye pass and
the replay draws it, which avoids the bug. Frames without an early tick are
drawn directly: the intro's stereo part (intro mode 3 until gameplay; the
player's "intro covering the screen") and frames that miss the early tick. In
the player's log, the green screenshot at 18:48:42 fell in a second with one
such frame. Before the fix, the intro window recorded on the headset had 1 flash.
After the fix it had 0.

## Fix

`gfx_pc.cpp gfx_run`: when the eye image is multisampled
(`vr_eye_multisampled()`, `vr_openxr.cpp`), a frame that was not deferred also
takes the record-then-replay branch. The replay draws the whole image in one
pass; the scopes render after it, as for deferred frames. Pose relocation stays
on deferred frames only. Single-sampled eyes keep the old branches, so pipeline
0/1 stay comparable for profiling.

Tested on the headset, driven over adb: pipeline 1, the default, and the default
with SpaceWarp off. At a CPU level of 0 with turning and walking, 127 frames
missed the early tick, and none flashed full-screen.

## Edge wedges (2026-10-19)

Rarer, smaller fog-colour wedges grow in from the view's edge for a frame or
two during fast turns (frames 717 and 1168 in the player's video). They occur
on replayed frames. With identical 40 s driven runs they appear as often before
the fix above (2) as after (1), and still with eye MSAA off. After v0.5.7 the
player still saw flicker on XBLA Facility with every combination of SpaceWarp,
anti-aliasing and refresh rate, and none with the HD + AI textures.

**Likely cause: chunks culled once, at the record, for cameras the replays
leave.** Bean's opaque world is one mesh drawn by spatial chunk
(`gevr_bean_world.c`, `s_opaque`), and `gevr_mesh_run_visible` drops a chunk
whose box is outside the game frame's camera and its presentation. The kept
pass is then drawn again by every replay until the next game frame, each from a
newer head pose. A chunk just past an edge, or close beside the head, comes
into view on a fast turn or step and is not in the kept draws. The fog fill
shows in its place. GoldenEye's own rooms (the HD + AI textures) cull whole
triangles against the same cameras. They are large and few, and the game
probably holds its frame rate better there, so a replay rarely turns far from
its record.

**Fix:** the record keeps what a camera moved by up to the replays' recent
departure could see. Each replay measures how far its camera is from the kept
pass's presented one (`gevr_mesh_slack_measure`: turn and step, falling by half
in about half a second). The next record culls with that departure plus half,
never under a floor of 5 degrees and 10 cm (`gevr_mesh_slack_begin`,
`gevrMeshBoundsVisibleSlack`). The draws then cull the kept chunks against their
own camera (`gfx_opengl.cpp gevr_eye_mesh_visible`: the replay's reprojection,
or the fresh draw's presentation), so the GPU draws no more than before. Only
the record's CPU sees the extra chunks. Show stats logs `mesh-cull: ...
replays' departure` and `eye-merge: ... chunks left out when drawn`.

`files/gevr_cull_slack.txt` (read every few seconds): `0` goes back to the old record
cull for A/B; `<degrees> <cm>` sets the floor (up to 30 and 100).

Not yet checked on the headset. Tests: `port/tests/test_mesh_replay_cull.py`
(the slack, the departure and the replay's matrix against the shader's).

## Re-testing

`files/gevr_pipeline.txt` 1 makes every fresh frame direct, the fastest repro
on an old build (no head motion needed). `adb shell screenrecord` captures both
eyes; count frames whose colour is mostly 0x102010.
