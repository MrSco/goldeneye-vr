# Crash 4bde84c2: Facility frame-buffer overrun

Report: v0.4.6, commit `9fb7be1`, Quest 2. The anonymous player's report
contains the previous game run and an Android crash backtrace, despite its
summary saying that the tombstone is unavailable. The attachment is
base64-encoded gzip. Its settings include `BodiesStay=48`, detailed guns,
the `ge007-hd` texture pack and the live watch display.

## Evidence

At 2026-10-04 20:14:08 EDT, the renderer logs:

```
FATAL: Unknown GBI opcode 0x00 at 0xb400007896b6ba70.
w0 00000000
w1 00000000
```

The backtrace follows `sysFatalError`, `gfx_run_dl`, the display-list lambda
inside `gfx_run`, and submission through `rspGfxTaskStart` and `bossMainloop`.
This is a deliberate SIGABRT after encountering invalid command data.

The saved v0.4.6 symbol archive has the same native build ID as the crash:
`33f87a26378a6058ad7a6b2fe8aec8fb034eae30`. Disassembly shows that `bossMainloop`
passes its final display-list pointer in register x26, which is preserved on
the stereo rendering path used by this crash. That register contains
`0xb400007896b6df50`.

The matching binary's Facility budgets are `-mgfx70 -mvtx50 -ma160`.
Using the logged game-heap base and the binary's 14,160-byte player allocation
gives this frame-pool layout:

| Boundary | Address |
| --- | --- |
| Master buffer 0 | `0xb400007896b19050` |
| Master buffer 1 | `0xb400007896b3c050` |
| End of master buffers / auxiliary buffer 0 | `0xb400007896b5f050` |
| Auxiliary buffer 1 | `0xb400007896b6b850` |

The final command pointer lies 61,184 bytes beyond both master buffers. It
represents either a 347,904-byte list in buffer 0 or a 204,544-byte list in
buffer 1; each half has only 143,360 bytes of capacity. The invalid command
lies 544 bytes inside auxiliary buffer 1.

This strongly supports a master-list overrun corrupting frame data. Model-slot
exhaustion warnings and slowing frame production near the end suggest a
crowded combat scene. Retained bodies are a plausible workload trigger; the
report cannot establish the exact action or body count at the crash.

## Related report 76717e5d

The second Quest 2 report is from the same original release binary and native
build ID, before this fix. The player reports green screen flicker near the
end of Facility. Its base64-encoded text attachment contains another SIGABRT
on the same graphics-submission call stack:

```
FATAL: Unknown GBI opcode 0x80 at 0xb40000791f6072f0.
w0 80000000
w1 80000000
```

The logged game heap is `0xb40000791f589200`. The logged player pointer is
exactly 160 KiB later, confirming the matching Facility allocation layout.
Applying the same binary's player size and budgets gives:

| Boundary | Address |
| --- | --- |
| Master buffer 0 | `0xb40000791f5b4950` |
| Master buffer 1 | `0xb40000791f5d7950` |
| End of master buffers / auxiliary buffer 0 | `0xb40000791f5fa950` |
| Auxiliary buffer 1 | `0xb40000791f607150` |
| Final command pointer (x26) | `0xb40000791f609d50` |

This implies either 349,184 bytes (341 KiB) or 205,824 bytes (201 KiB) of
commands against the same 140 KiB capacity. The final pointer is 62,464 bytes
beyond both master buffers; the invalid opcode lies 416 bytes into auxiliary
buffer 1. This strongly corroborates the same command-buffer overrun.

The settings again have 48 retained bodies, detailed guns and watch status,
with `ge007-hd-ai` rather than the first report's `ge007-hd` texture pack.
A model-slot exhaustion warning occurs 15 seconds before the fatal error.
During the last three seconds, the renderer's decal vertex-span diagnostic
jumps from roughly 2,250 to as much as 44,093,592 view units. These implausible
geometry values are consistent with corrupted frame data and could explain
the flicker; the report does not establish its exact colour mechanism.
There is no logged stage switch after loading Facility, so the player's note
does not demonstrate a crash during stage teardown.

The existing 512 KiB command minimum accommodates both inferred lengths.
The production-code regression now exercises all four inferred lengths from
both reports in both buffer halves and passes. No additional runtime change
was needed; headset confirmation remains outstanding.

## Change

GEVR now allocates at least 512 KiB of command storage and 256 KiB of auxiliary
storage per frame-buffer half, preserving larger stage/player budgets and the
existing host-command-size conversion and extra-player scaling. Facility uses
about 1.13 MiB more of its existing game heap. Body-retention settings remain
unchanged.

All four auxiliary allocation entry points validate sizes and remaining
capacity before advancing the cursor. Typed allocations keep their original
sizes; generic byte allocations retain their 16-byte rounding.

The existing `dynGetFreeGfx2` call in `bossMainloop` validates the completed
master list before swapping buffers and submitting the graphics task. It
reports the stage, buffer, used bytes, capacity and pointers on failure.
This check diagnoses command overflow **after construction**; individual
master-command writes still use unchecked `gdl++`.

Logs include pool addresses/capacities, stage usage peaks, and one warning per
pool when usage first reaches 80% of capacity. Changed peaks are logged at
most once every five seconds, with final peaks logged on the next stage load.
Public function signatures and network protocols are unchanged.

## Verification

- `python port/tests/test_dyn.py`: seven production-code test groups pass,
  covering all four inferred list sizes, overwrite guards, one through eight
  players, larger budgets, reloads, allocation alignment, exact capacity,
  rejected requests, completed-list validation and peak logging.
- The same `observed_lists` regression compiled against the original release
  source fails its overwrite-guard check.
- `python port/tests/test_v043.py`: four watch/input/ammo/presentation groups pass.
- `python port/tests/test_multiplayer.py`: 60 tests pass, including combat
  allocator guards and eight-player behavior.
- The ARM64 release APK builds successfully and passes `apksigner verify`
  with APK Signature Scheme v2. Its native build ID is
  `2463e36ce584fed38c5d90c00875f5728bb7a8ad`.

APK: `android/app/build/outputs/apk/release/app-release.apk` (21,806,240 bytes).
SHA-256: `febbe290d3227e2b855142872c76328ad879a7959a2f124ddb6141b73f632205`.

Only a Quest 3 was connected during this work. No headset installation or
gameplay test was performed. Acceptance still requires sustained Facility
combat on Quest 2 with 48 retained bodies, detailed guns, HD textures and the
watch display, checking the new usage logs and crash behavior.
