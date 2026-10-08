# Crash 65a366e6: stale portals on the title

Report: v0.4.11, commit `f27ff36`, Quest 2, anonymous player. The attachment
is base64-encoded gzip. Its log and tombstone were treated as diagnostic
evidence. The decoded files remain outside the repository.

## Evidence

At 2026-10-07 23:54:48 local time (`2026-10-08T03:54:49Z`), SDLThread receives
SIGSEGV / SEGV_MAPERR, fault address `0x5a15b1dc`:

```
gevrSndPathStageLoaded + 296
bossMainloop + 820
bossEntry + 20
pd_main + 964
```

Native build ID: `55e848163211d922ab408d35e979bcda44c57867`.

The last game lines are `stage: unloaded stage text data` and
`stage: switching to 90` (the title). There is no `sound: N portals` line
after that switch. The previous stage was Statue (22), loaded at 23:46:42,
which logged `sound: 43 portals` and a background of 3344 cartridge bytes.

The faulting instruction loads a byte through a portal-point pointer whose
stored value is `0xb40000005a15b1dc`. The stage heap still holds Statue's
16-byte portal records (point blobs 0x34 bytes apart, real room ids), but
every third stored pointer has lost the `0x76` address byte
(`0xb4000076…` became `0xb4000000…`). Register x19 is still 43.

Earlier returns to the title the same session did log a portal count, and
it was the previous stage's count: 94, then 101, then 101 again, then 75.
Those tables happened to survive the title allocations. Statue's table sits
near the start of the stage bank, so the title's player and display
allocations land on it.

## Cause

`mempResetBank(MEMPOOL_STAGE)` only rewinds the allocator. It does not
clear `g_BgPortals`. The title skips `load_bg_file`, so the pointer keeps
addressing the stage that was just freed. `bossMainloop` then allocates
from that bank and calls `gevrSndPathStageLoaded`, which walks
`g_BgPortals`. `gevrSndPathStageLoaded` already returns when the pointer
is NULL. A real stage sets the pointer again inside `load_bg_file` before
that walk.

## Change

Immediately after `mempResetBank(MEMPOOL_STAGE)`, `g_BgPortals` is set to
NULL. The title's sound-path setup is then a no-op. The next stage load
builds its paths from the background it just loaded.

Protocol and release version are unchanged.

## Validation

`port/tests/sndpath_native.c` clears the portal table, checks that
`gevrSndPathStageLoaded` leaves the path count at 0 and distances fall
back to a straight line, then loads the following stage and checks that
the two-doorway paths are built again. The existing room, volume, and
spatial checks in that file still pass. The fixture returned 0.
