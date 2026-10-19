# Crash 3706d27a: Silo master display list over the 512 KiB minimum

Report: v0.5.6, commit `120e37d6`, Quest 2, native build ID
`a99b48495890101ce377747ee2b90e3c85003351`. The exit record is
`crash-native(5)` at 2026-10-19T03:06:24Z, and a tombstone is attached. The
`low-memory(3)` record at 2026-10-18T21:08:18Z is from an earlier session.

## Evidence

The previous game run ends with the allocator's own check:

```
FATAL: dyn: stage 20 buffer 0 invalid master display list: used 541920 bytes,
capacity 524288 bytes, end 0xb400006efd58a6b0, base 0xb400006efd5061d0
```

The tombstone and the logcat both show a SIGABRT on SDLThread, from `abort`
through `sysFatalError`, `dynGetFreeGfx2` and `bossMainloop`. That is the
completed-list check added for crash 4bde84c2. It stopped the game on the
first frame whose commands ran past the buffer, before that list was
submitted.

Stage 20 is Silo, whose cartridge budget is `-mgfx70`. That scales to 140 KiB,
so the 512 KiB minimum (`GEVR_DYN_GFX_MIN`) set the capacity. The player had
Project Bean on (Bean characters, props and rooms), `BodiesStay=24`,
detailed GE-X guns and the watch display. The peak log shows the minimum was
already tight when the stage loaded:

| Time | Silo peak gfx | Aux |
| --- | --- | --- |
| 21:03:36 (first frame) | 439,696 (WARNING, 84%) | 128,224 |
| 21:03:41 | 458,528 | 128,224 |
| 21:04:34 | 510,864 | 156,672 |
| 21:06:24 | 541,920, fatal | 160,256 |

23 guards died in Silo during that time (ragdoll bodies 28 to 50), and the
peak grew as the retained bodies built up. The stage pool still had 8,682,756
bytes free after Silo loaded. Memory was not the limit; the minimum was.

`src/game/dyn.c` is unchanged from v0.5.6 through v0.5.9, so the current
release has the same limit.

## Change

`GEVR_DYN_GFX_MIN` goes from 512 KiB to 1 MiB per buffer half. Silo's failing
list fits with about 48% to spare. Budgets that are already larger still win,
and so does the player-count scaling. Every stage now uses 1 MiB more of the
stage pool (two halves, 512 KiB more each). Silo had 8.7 MB free and
Bunker 8.9 MB. The game heap is 16 MiB.

The check still runs only after the list is built, as noted in crash
4bde84c2. If a list ever exceeds 1 MiB, the game stops with the same message,
not with corrupted frame data.

## Verification

- `port/tests/dyn_native.c` `observed_lists` now also builds Silo's logged
  541,920-byte list in both buffer halves and checks the guard bytes and the
  aux data. Built against the v0.5.9 `dyn.c`, it stops with the device's exact
  message: `stage 20 buffer 0 invalid master display list: used 541920 bytes,
  capacity 524288 bytes`.
- The capacity, reload, larger-budget and 80% warning checks were updated for
  the 1 MiB minimum. All seven groups pass.
- Not yet run on a headset. Acceptance: Silo on Quest 2 with Project Bean,
  `BodiesStay=24` or 48 and sustained combat, checking that the
  `dyn: stage 20 peak gfx` lines stay well under 1048576.
