# Crashes 7b162d90, 919e73ee, and c3ad6b13

v0.4.14, commit `463e22b`, Quest 3. The tombstones are Android protobuf
crash dumps. Each is SIGSEGV / SI_TKILL: the frame watchdog aborted the
game thread after eight seconds with no retrace. Two distinct hangs.

## Same hang: 7b162d90, 919e73ee, and 3e30f876

`7b162d90` is Agent 2673, slot 2, pid 3999, 2026-10-08 21:40:34 EDT.
`919e73ee` is Agent 3060, pid 11199, 2026-10-08 22:40:32 EDT.
`3e30f876` is Agent 3060's manual report of that same session. Its log
matches `919e73ee` through the abort, then the restart at 22:40:48.

Both headsets were co-op followers on Frigate. The mission completed, the
party returned to the title, and each follower logged:

- host screen 13 (mission complete), text `LdestE`
- host screen 10 (briefing), text `LsevxbE` (Surface 2, the next mission)
- host screen 7 (mission select), then no further frames

The stack at five seconds and at the abort is the same:

```
langGetLangBankIndexFromStagenum+0x20
update_menu0A_briefing+0x30
menu_init+0x218
lvlManageMpGame+0x690
bossMainloop+0x614
```

`+0x20` is the retail `while(1)` for a level with no text bank. Mission
select's init sets `briefingpage` to `-1` and the host sends that with
screen 7. The follower applied the host's folder entry, then the briefing's
cleanup indexed that entry and asked for its text bank.

## Separate hang: c3ad6b13

No player name. pid 25873, 2026-10-09 14:33:01 in the headset's zone
(06:33:01Z). Solo Caverns (`stage 39`, `LcaveE`). The intro camera reached
swirl mode at 14:32:53, which is when action music starts. The stack is:

```
nanosleep
sysSleep+0x60
musicTrack2Play+0x78
set_missionstate+0x280
lvlManageMpGame+0x6c4
bossMainloop+0x614
```

`musicTrack2Play` waits until sequence player 2 reports stopped. The stop
is an event on that player's queue. A full queue drops it, so the player
stays `AL_PLAYING` (or `AL_STOPPING` if the follow-up is the one dropped).
Servicing audio inside the wait does not help once the event is gone. The
log's `amDma: reject` lines are sample loads outside the ROM; they are not
this stack.

## Changes

- A follower changes screen without copying the host's mission. That copy
  happens when the new screen is already showing, or on the frame its init
  runs, so the briefing clears the bank it loaded and the next briefing
  still receives the host's folder entry.
- An unset or chapter folder entry does not look up a text bank. An unknown
  level logs and returns `-1`. Load and clear ignore a bank index outside
  the table.
- The three music play waits post the stop again while the player is still
  playing. After 120 retraces (two seconds), `gevrCSPForceStop` stops and frees
  every remaining voice, returns its voice state to the free list, stops
  oscillators, and discards queued and prefetched events from the old track.
  It restarts the player's heartbeat at the current audio sample time before
  reporting `AL_STOPPED`, so the next track can load and reuse all voices.

Protocol and release version are unchanged.

## Validation

`python port/tests/test_crash_hangs.py` compiles the production follow
decision, the briefing-entry check, and the music wait. A follower on the
briefing does not apply mission select. The init frame of the next briefing
does apply. Page `-1`, a chapter row, and an out-of-range page yield no
stage. The music fixture uses the real event queue, stop handler, voice
release/unmap routines, and timeout recovery; only the synthesizer output and
sequence parser are mocked. It checks a dropped initial stop, dropped release
and final-stop events, an already-stopping player, stale queued and prefetched
events, oscillator cleanup, and a stop landing on the last allowed retrace.
Recovery returns all 16 voices and all 64 event slots, and the next track
starts and allocates every voice without an old event freeing one again.
A player already stopped is left alone.
