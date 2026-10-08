# Crash bfb06b31: sound queue spin after warmup

Reports `bfb06b31` and `15cb7aaa` are the same Quest 3 session (pid 23241,
player Rivas10, v0.4.12, commit `3a0649d+`). `15cb7aaa` is the manual note
for that crash: "Crashed after end of warm up and start of mp match with
bots". The decoded log and tombstone remain outside the repository.

## Evidence

At 2026-10-08 00:44:17 local time (`2026-10-08T03:44:17Z`), SDLThread is
reported as SIGSEGV / SI_TKILL with no fault address. The program counter
is `mov x20, x1` at `alEvtqNextEvent+24`, which cannot fault. The watchdog
sends that signal with `pthread_kill` after 8 seconds without a retrace.

```
alEvtqNextEvent + 24
sndPlayerVoiceHandler + 76
alAudioFrame + 132
amHandleFrameMessage + 192
gevrAudioFrame + 132
gevrSchedBlockedRecv + 828
osRecvMesg + 92
bossMainloop + 1028
```

Native build ID: `a182885db22b141b1cc6a7362e62753e2bb4e1f6`.

The log matches the note:

- 00:41:46 `net: bots: 5 of 5`, then 00:41:57 `Match launched! Stage: 34`
  (Archive), 6 players, `sound: 109 portals`.
- 00:44:07 the round reloads Archive, `sound: 109 portals`, the bot floor
  graph, and one pump at 00:44:08.
- 00:44:14 `watchdog: no retrace for 5 s`, stack in `alEvtqNextEvent` from
  `alAudioFrame`.
- 00:44:17 `watchdog: hung for 8 s outside the frame wait; aborting`.

## Cause

`sndPlayerVoiceHandler` loops `while (nextDelta == 0)` around
`alEvtqNextEvent`. An empty queue used to set that delta to 0. The sound
queue holds 64 events. A full queue drops posts, including the
self-reposting `AL_SNDP_API_EVT` that carries `frameTime`. Once only
zero-delta events remain, or the queue is empty, the handler never returns
and `alAudioFrame` does not advance. Five bots starting the live round is
enough traffic to fill 64 events. The 256-step guard in `sndHandleEvent`
does not cover this outer loop. The sequence players use the same loop.

## Change

An empty `alEvtqNextEvent` returns one frame (`AL_USEC_PER_FRAME`) instead
of 0. In `sndPlayerVoiceHandler`, a heartbeat that could not be queued is
posted again with `frameTime` once a slot is free, and an empty pop is
treated as that heartbeat. The zero-delta loop stops after 256 events and
returns `frameTime`. `__seqpVoiceHandler` and `__CSPVoiceHandler` use the
same bound.

Protocol and release version are unchanged.

## Validation

`port/tests/sfx_queue_native.c` calls the production queue and
`sndPlayerVoiceHandler`. An empty queue returns one frame. A queue whose
only events have delta 0 returns from the handler. A handler that keeps
reposting a zero-delta event stops inside the 256-step bound and returns
`frameTime`. A full queue that drops the heartbeat posts it again once a
slot frees and still returns. The fixture returned 0.
