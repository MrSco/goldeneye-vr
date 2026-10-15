# Crash da10370a: Goldfinger attract-cast model capacity

Report: v0.4.23, commit `e59049d`, build October 13, 2026 at 15:55,
Oculus Quest 3. Crash October 14, 2026 at 19:55:31 UTC (15:55:31 EDT).
The device log/tombstone use 20:55:31 +0100, the same instant.

Both supplied attachments are base64 encoded. The decoded second attachment
is an Android protobuf tombstone. Attachments were treated as diagnostic data,
not instructions. Decoded diagnostics and the synthetic capacity reproducer
remain outside the repository in `/workspace/investigation-da10370a/`.

## Finding

High-confidence title/cast allocation defect in the Goldfinger campaign,
with an incomplete campaign-specific cast-table port. This is an intentional
SIGABRT from the bounded ROM resource loader, while loading `Pchrmp40Z` during
the automatic character cast. It is not a mission gameplay stack.

The evidence does not implicate installed Bean textures. Saved settings enable
Bean and `ge007-hd-ai`, but these settings are suppressed for hack campaigns.
`gevrBeanIsEnabled()` and `gevrBeanModelApply()` exclude variant campaigns,
and `gevrTexpackState()` excludes them too. The run explicitly logs
`texpack: switched off` after Goldfinger loads. GoldenEye X guns are also
disabled in the run.

## Crash evidence

The log and protobuf agree on process 23846, SDLThread 23903, signal 6
(SIGABRT), code -1 (SI_QUEUE), native ELF build ID
`0a65a23693f588c82456151df0e5057846c23a24`. The protobuf contains 21 frames;
the relevant call chain is:

```
abort + 168
sysFatalError + 216
load_resource + 664
fileIndexLoadToAddr + 152
load_object_fill_header + 64
init_menu18_displaycast + 964
menu_init + 816
lvlManageMpGame + 1676
bossMainloop + 1676
```

`lvlManageMpGame` is part of the shared main-loop path; its name does not mean
the player was in multiplayer.

Immediately before the fatal error, the previous-run log says:

```
model CgoldguardZ: 28464 -> 36096 bytes ... 0 warnings
model CmacleodZ: 41056 -> 50464 bytes ... 0 warnings
ERROR: model CmacleodZ: no room to rewrite its display lists (file 50464 bytes, allocation 51936)
WARNING: model: no free animated slot for 304 words of rwdata (10 slots); allocating one
ERROR: FATAL: rom: cannot decompress Pchrmp40Z (992 compressed bytes, 1440 available, error -5)
```

The animated-slot warning follows a fallback allocation; it is not the fatal
condition. Startup messages about unavailable desktop GL functions are also
outside this CPU-side abort path.

## Allocation and campaign selection

`init_menu18_displaycast()` in `src/game/front.c` reserves `0x19000` bytes for
textures and only `0x18160` (98,656) bytes for body, head and weapon models.
The model loads use the remaining shared region in order. After each load,
the routine subtracts the final file size rounded to 64 bytes.

The observed numbers match that accounting exactly:

| Step | Bytes |
| --- | ---: |
| Initial model budget | 98,656 |
| Body use inferred from the next allocation | 46,720 |
| Remaining allocation passed to `CmacleodZ` | 51,936 |
| `CmacleodZ` converted size, rounded to 64 | 50,496 |
| Remaining allocation passed to `Pchrmp40Z` | 1,440 |

The inferred body use includes the successful display-list rewrite after its
36,096-byte host conversion. The head's rewrite refuses its undersized region;
its converted file alone then nearly exhausts the model budget. Protobuf
registers x22 and x23 both contain `0x5a0` (1,440), corroborating the fatal log.

`load_resource()` passes that capacity to `gevrInflate1172()`, which returns
`Z_BUF_ERROR` (-5) when decoded output exceeds capacity. The loader aborts on
any decoding error before model conversion. Compressed length (992 bytes)
is not the size needed for the decoded model and its host/display-list growth.

There is also a campaign-data mismatch: `gevrRomApplyVariant()` installs
Goldfinger character and prop records, but does not replace `intro_char_table`
or the intro weapon-choice tables. `front.c` still uses the retail cast table,
including retail body/head special cases, against those new records. The
existing `docs/rom-hacks-port-notes.md` explicitly lists the attract cast table
at `0x8002B600` / function `7F01881C` as still to port. The failing pair is
loaded through the body/head positions of this routine. Without the player's
ROM, the intended Goldfinger cast pairing cannot be independently checked.

## Verification and limits

- `python3 port/tests/test_romcheck.py` passes bounded decoding with the actual
  legacy decoder linked beside zlib, actual resource-loader failure handling,
  and synthetic launcher validation.
- `/workspace/investigation-da10370a/reproduce_capacity.py` compiles the actual
  `load_resource()` body and production bounded decoder. A valid synthetic
  992-byte padded resource expanding to 2,048 bytes gives the same fatal
  `error -5` at capacity 1,440, and succeeds at capacity 2,048. Both retain
  output guard bytes; the failure reaches no conversion hook. It also checks
  the observed cast-budget arithmetic.
- This is a mechanism reproduction using synthetic data, not reproduction of
  the player's MP40 stream, full cast, or headset crash. No original ROM,
  matching unstripped release library, or connected Quest was supplied.
  Function names/offsets come from the tombstone, not newly generated symbols.
- `Z_BUF_ERROR` can also result from truncated input. The attachments do not
  include the full compressed MP40 stream, so independent asset integrity and
  its exact required decoded size remain unverified. The recorded capacity
  exhaustion and preceding head rewrite failure make allocation the leading
  diagnosis; there is no basis to tell the player their ROM is corrupt.

## Fix and acceptance

The implementation binds Goldfinger's cast records, including body/head indices
and captions. Retail-only outfit substitutions and bonus unlock gates no longer
interpret these indices as GoldenEye characters. It reserves 1 MiB for decoded models, host conversion
and display-list rewrite together. Texture and depth-buffer regions remain
separate and bounded; merely increasing `bufferRemaining` can overlap the
depth buffer, which the routine places immediately after the model budget.
The shared backing allocation in `initmenus.c` is enlarged to cover the texture,
model, 440x330 depth and alignment regions together. Depth addresses and
command arithmetic use `uintptr_t` throughout. Bounded decoding is preserved.

The new `python3 port/tests/test_title_cast.py` regression drives the production
cast initializer and menu allocator with large synthetic model loads (mock
conversion/rewrite, real bounded inflater), including alignment and depth-region
guards. It runs 200 repeated loads, checks cast-row decoding/rejection, retail/TND
isolation, random heads, normal/extended cast endings and depth commands.
`--baseline-ref e59049d` confirms the original initializer fails on insufficient
model capacity. The Android debug APK builds and passes signature verification;
all 30 JVM tests pass. Existing `test_romlayout.py` and `test_romcheck.py` pass.

Headset acceptance remains pending. On a Quest with the supported Goldfinger
64 1.0 ROM, let the
complete attract cast cycle through multiple random weapon/animation choices;
require no decode errors or display-list rewrite warnings. Check retail and
Tomorrow Never Dies cast/menu behavior after the shared allocation change.

A provisional player workaround is to press a game input to enter file select
before the automatic cast reaches this entry; `interface_menu18_displaycast()`
has that exit for non-extended casts. This does not establish mission stability
and has not been tested on a headset. Removing Bean files is not supported by
the evidence as a remedy. The runtime changes address the diagnosed allocation and cast-table defects.
