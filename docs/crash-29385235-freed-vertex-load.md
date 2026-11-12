# Crash 29385235 / 98b7d915: a vertex load from freed memory

Reports repo issues #19 (29385235) and #20 (98b7d915), 2026-11-12, one
player, two minutes apart: Quest 3, v0.5.16 (976aa580), the Tomorrow Never
Dies 64 campaign on its dam level, stereo, ragdolls on with 24 bodies kept,
blood patch on. Memory was fine (285 MB used, 3.5 GB free). Not explained
yet; the next report will carry the display list (below).

## What the tombstones show

- `gfx_sp_vertex` (`port/fast3d/gfx_pc.cpp`) reading vertex 0's x, called
  from `gfx_run_dl`'s `G_VTX` case in the frame's top-level list, from
  `gfx_run` / `videoSubmitCommands`. SIGSEGV, SEGV_ACCERR, fault address
  `0xb400007c77e1778c` (#20: `0xb400007c6f22ee9c`). The digest printed 0x0
  until reports repo commit 0f707ac (it read the wrong tombstone field).
- The command: `gSPVertex(ptr, 4, 0)` at +0x58a0 of the top-level list
  (#20: +0x4760), after a fresh fixed-point matrix, culling off.
- The pointer: native heap outside the 16 MB game heap (where every frame
  and stage buffer lives), misaligned for a vertex array. Its page had become
  a no-access guard page of another thread's stack, beside a live 10 MB heap
  block: freed and reused (scudo unmaps large frees). A use after free, or a
  command overwritten.
- The vertices loaded just before: a bullet spark quad (255,255,200), in #20
  blood-patch red: the effects pass (sparks, glass shards, debris, wall
  impacts). Every writer of a top-level 4-vertex load found so far points into
  the game heap or a per-frame buffer, and those renderers read the same page
  while building the list, so the command that ran is not what they wrote.
- Guards were dying around both crashes (bodies 7-10 in #19, body 12 in the
  same second as #20's crash).

## The crash-time display list (2026-11-12)

A crash on the thread running the display list now logs, before the
system's report, `crashdl:` lines in logcat (which the next start's report
sends):

- `crashed drawing: fault ..., N lists deep, last vertex load ..., game heap
  ...-...`
- per open list, its start and command index, the latest game tag (`G_NOOP
  0x4745xxxx`) before it, the 40 commands the innermost list ran before the
  crashing one (6 for outer lists), the command itself (`>>>`) and up to 4
  after, with opcode names;
- the segment table;
- `/proc/self/maps` around the fault and around the last vertex load:
  `holds` (the mapping it is in), `below` and `above`, or `unmapped`.

What to read first next time: the `>>>` command's w1 (the bad pointer as the
list holds it), whether the commands before it are a sane effects draw or
garbage (an overwrite), the SETTIMG and MTX addresses before it (whose buffers
they are), and the mapping that holds or neighbours the address.

How it works: gfx_run_dl keeps each open list's start and current command
(one store a command, `GevrDlTrack`); `port/src/crash.c` installs, on
Android, a SIGSEGV/SIGBUS handler that logs (`gevrGfxCrashContext`) and then
calls the handler that was there before it (debuggerd's), so the tombstone is
unchanged. On Windows the existing handler logs the same lines into gevr.log.
Nothing is skipped or recovered.

Traps found testing it on the Quest:
- `crashInit` runs twice in one Quest process (the launcher, then Start). The
  first version saved its own handler as "previous" on the second call and
  looped forever; it now installs once (the log says "renderer context
  handler on", then "already on").
- Returning from the handler and letting the fault happen again was not
  enough; it calls the previous handler directly, as crash reporters chain,
  and falls back to the default action if it is ever entered more than four
  times.
- The handler runs on the small signal stack: crash lines use a static
  buffer (`gevrCrashLog`).

## Testing it

Marker `gevr_crashtest.txt` in the files folder (the PC: beside the exe),
checked every 60 frames: the next top-level vertex load reads a no-access page.
On the Quest the logo screens draw no top-level vertex loads; it fired as the
eye intro began. Checked 2026-11-12 on the Quest 3 (crashdl lines, then
debuggerd's `Fatal signal 11 ... SEGV_ACCERR` and "Tombstone written") and on
Windows (scripted Dam run). A file saying `tkill` instead sends the thread
SIGSEGV as the watchdog does (no crashdl lines; the system's report must
still come). The `tkill` run on the Quest was blocked by a USB debugging
prompt and is not yet checked. A test crash before a reinstall is not offered
as a report (LogReporter skips exits before the install).
