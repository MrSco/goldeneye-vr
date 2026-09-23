# gepc-ref PORT guard sweep — 2026-09-22

Three defects in one session (D140, D264, and the music fade tick) turned out
to be things gepc-ref had already solved behind `#ifdef PORT` and that had
simply never been carried into this tree. This is the systematic pass.

## Method

`scratchpad/sweep.py` walks every `#ifdef PORT` / `#if defined(PORT)` block in
`../gepc-ref/{src,port}`, collects the finding ids (`D\d+`, `RC\d`, `M-\d+`)
named inside the block or in the six lines above it, and checks whether each
id is mentioned anywhere in our `src/`, `port/`, `assets/`, `docs/`,
`HANDOFF.md` or `STATUS.md`.

```
PORT guard sites in gepc-ref : 399
distinct finding ids         : 187
  cited somewhere in our tree: 35
  NOT cited in our tree      : 152
```

Full machine-readable output: `scratchpad/port_guard_sweep.json`.

## The caveat that matters

**Uncited is not the same as unported.** The sweep matches on the reference's
finding id appearing in our source. Where this port solved the same problem by
a different route and never named the finding, it shows up as a false gap. Two
worked examples, both checked by hand:

- **D67** (struct image_entry field order). The reference reorders the
  bitfields so `dataoffset` occupies the low 24 bits, because its `texLoad()`
  reads `*(s32 *)&entry & 0xFFFFFF`. Our `texLoad()` instead reads
  `g_Textures[n].dataoffset` by name (image.c:2495), which is equivalent and
  arguably better. Not a gap.
- **D85** (`ptr_texture_alloc_start` is real storage, not a pointer). Already
  fixed here, with its own comment at the top of image.c and no mention of
  D85. Not a gap.

So the list below is a **candidate list to triage**, not a defect list. Each
entry needs the same treatment these two got: read the reference's PORT branch,
read our equivalent, decide.

## What was triaged in this pass

Scope chosen to match the open user-visible complaint: gun, hand, HUD,
crosshair and smoke textures rendering wrong.

| Area | Findings | Verdict |
|---|---|---|
| `port/fast3d/gfx_pc.cpp` | D116 D157 D172 D219 D229 D252 M-110 M-157 M-158 | **Nothing to port.** Every one is an env-gated diagnostic probe (`getenv("GE_D172")` and friends), not a fix. Consistent with the renderer already matching the reference for the real fixes. |
| `src/game/image.c` | M-113 M-114 | **Real gap — fixed, see below.** |
| `src/game/image.c`, `image.h` | D67, D85 | Solved differently here; see above. |
| `src/game/image.c` | D66 | Diagnostic only (`GE_D63` oversized-read logging). |
| `src/game/image.c` | D159 | Deliberately declined; see `docs/texture-port-audit.md`. Our renderer reverses the swizzle and the compiled assets need it. |
| `src/game/image.c`, `image_bank.c` | D68 | Not applicable as written: it converts the byte order of a ROM copy of the Globalimagetable segment. This port compiles those tables in natively and does not romCopy them. Related to HANDOFF §22.4/§24 — if the per-stage segment copy is ever ported, D68 comes with it. |

### The one real gap: M-113 / M-114

The decoder and the renderer disagree about the byte order of the texpool, and
the split is keyed on GBI texture size. Checked against **our** importers
rather than assumed:

- `import_texture_rgba16` reads the pool as manually big-endian bytes,
  `(addr[0] << 8) | addr[1]`; `import_texture_ia16` takes `intensity =
  addr[0]`, `alpha = addr[1]`. A native `u16` store on this little-endian host
  is read back byte-swapped, so the decoder must store `bswap16(value)`.
- `import_texture_rgba32` does `PD_BE32()` on a **native** `u32` load, so the
  32-bit path must stay native. Swapping it was the reference's own M-114
  regression (muzzle flash blue, ammo HUD pink).

Our `image.c` had no swap at any of the eleven 16-bit wide-pixel store sites.
Palettes were already correct — image.c:307 writes them explicitly big-endian,
which is why paletted textures decode and the fire/smoke family does not.

Applied as `PORT_PIXEL16` / `PORT_PIXEL32`, matching the reference site for
site: eleven 16-bit stores swapped, nine 32-bit stores left native and wrapped
in the identity macro so the intent is explicit.

Per the reference's ROM census this covers IMAGE_FIRE_0..14 plus texnums
1198-1201 (RGBA16), 2430 (IA16) and 2510-2523 (RGB15). Every ammo, flare,
crosshair and muzzle-flash wide-pixel image is RGBA32 and therefore untouched
— so **this is expected to fix fire and smoke, and not the crosshair or the
washed-out gun.** Those need their own cause.

Known limitation carried over deliberately: `texShrinkNonPaletted()` blends
16-bit pixels as native `u16`, so mip levels generated from a swapped pool
blend wrong. gepc-ref does not guard it either. Not invented around.

## Remaining candidates, by reference file

Highest counts first. Findings named in gepc-ref PORT guards with no mention
in this tree.

| reference file | n | findings |
|---|---|---|
| src/game/bondview2.c | 31 | D56 D90 D141 D146 D155 D160 D173 D177 D193 D250 M-107 M-141 M-143..M-145 M-154 M-154b M-162 M-163 M-167 M-169 M-170 M-172..M-174 M-183 M-185 M-187..M-190 |
| src/game/model.c | 29 | D43 D45 D51 D52 D53 D56 D59 D99 D100 D101 D156 D173 D193 D218 D249 D311 M-174..M-187 M-189 M-192 |
| src/game/chr.c | 19 | D43 D45 D120 D173 D193 M-80 M-154 M-159..M-163 M-165..M-169 M-172 M-173 |
| src/game/bg.c | 18 | D43 D50 D69 D77 D79 D80 D85 D91 D104 D106 D128 D135 D154 D236 D271 D312 D313 M-30 |
| src/game/front.c | 12 | D50 D63 D64 D65 D143 D146 D164 D178 D221 D250 M-109 M-148 |
| src/bondtypes.h | 10 | D43 D45 D52 D53 D69 D78 D79 D88 D151 D157 |
| src/snd.c | 10 | D147 D152 D202 D285 D322 M-65..M-67 M-70 M-71 |
| src/game/propobj.c | 9 | D52 D135 D202 D207 D218 D222 D318 M-65 M-71 |
| src/game/chraction.c | 6 | D193 D209 D309 D318 M-178 M-187 |
| src/game/objecthandler_2.c | 6 | D43 D45 D48 D49 D50 D69 |
| src/game/stan.c | 6 | D79 D88 D89 D90 D177 D253 |

The tail (1–5 findings each) is in the JSON: boss.c, chrai.c, file2.c,
frametiming.c, language.c, loadobjectmodel.c, libultra/audio/event.c, fr.c,
bondview.h, gunfire.c, ramromreplay.c, textrelated.c, title.c, ramrom.c,
str.c, bondconstants.h, initanitable.c, lv.c, ob.c, tex.c, snd.h, and a dozen
single-site files.

## Suggested order for the next pass

1. **src/snd.c (10)** — D147 D152 D202 D285 D322 M-65..M-71. Audio is the one
   area of this port with no triage at all, and the music fade tick showed
   there are whole mechanisms simply not wired up. Cheap to check.
2. **src/game/model.c (29) and src/bondtypes.h (10)** — D43 D45 D51 D52 D53
   are the pointer-growth/rwdata-layout family that D140 belongs to. This
   session hit two of them the hard way. Likely the densest seam of real
   defects, and it covers the remaining model rendering complaints.
3. **src/game/bg.c (18)** — D69 D79 D85 D91 D128 D135 D154 are room geometry
   and display-list conversion. Relevant if level geometry or lighting is
   still wrong.
4. **src/game/bondview2.c (31) and chr.c (19)** — mostly M-series, which in
   the reference are late-session behavioural findings rather than layout
   fixes. Lower yield per entry, and many will already be handled here.

Do not batch these. Each one needs the reference branch and our equivalent read
side by side, because half-porting a fix — taking the change without the
scheme it belongs to — caused two separate regressions earlier in this same
session (HANDOFF §22.1 and §23).


## Second pass — 2026-09-22: model.c, snd.c, and what they led to

### model.c (29 uncited)

| Finding | Verdict |
|---|---|
| D43/D45 (PROMOTE32 of collision `LinkedTo`) | Solved differently: our promote pass leaves `LinkedTo` a segment-5 offset and resolves it where read. |
| D52 (word-indexed rwdata pool) | Already here (`u32 *datas`), uncited. |
| D59 (vtxallocator full pointer) | Already here, typed. |
| D92 (`unka0` function pointer) | Already here, as a side table plus flag. |
| D99 (`animflipfunc` s32) | Already here, widened to `void *`. |
| D101 (`sp1C` stash of `arg2->Parent`) | Already here (`ModelNode *parent`). |
| D56 / D57 (slot fallbacks) | Already here, with a warning log. |
| **D53.2 (ModelSlot is a whole Model)** | **Ported.** Ours covered only the first eight fields; model.c's own unka0 note records a write past it reaching the tank record. Static-asserted. |
| D156 / D311 (NaN root-motion guard) | Not ported: cutscene-only symptom not seen here. Candidate if cutscene hangs appear. |
| D173, D193, D218, D243, D249, M-17x..M-19x | Diagnostics or cutscene speed clamps; not ported. |

### Leads from model.c into neighbouring files

- **propobj.c D135 (object bullet-hit parser)** — ported. Fixes "can't shoot
  the padlock": the parser read N64 byte offsets from 16-byte host Gfx and
  never found a triangle. Adapted for this converter's bit-0 address tag.
- **gun.c D45 (gun model buffers)** — ported (previous commit).
- **dyn.c D95 (master display list budget)** — ported.

### snd.c (10 uncited)

| Finding | Verdict |
|---|---|
| D147 / D152 / D285 | Not applicable: they close races with a real preemptible audio thread. Our OS threads are stubs and audio runs from the retrace. |
| **D305 (preemption scan on an empty list)** | **Ported.** A do-while dereferenced `iterState` before its NULL check (fault 0x62) when the 8-voice pool is believed full. Single-threaded, so it applies. |
| D202 / M-65 / M-66 (ownerless looping SFX) | Not ported: a deliberate deviation from N64 behaviour (fading loops that ring until level exit there). Candidate if a stuck door loop is heard. |
| D202 / M-67 / M-70 / M-71, D322 | Diagnostics and telemetry. |

### Not from the sweep, but found on the way

The texture faults were not PORT-guard gaps. They were found by dumping the
importers' output (HANDOFF 31): padded 32-bit rows, 32-bit odd-row swizzle,
tile windows larger than the load, and stale pointers left in the static
image tables across stages — the last one being a fix this branch had and
wrongly removed.


## Third pass — bg.c, bondview2.c, chraction/chrai, frametiming

| File | Ported | Already here / not applicable |
|---|---|---|
| bg.c | D128 (special-portal flag at N64 stride), D271 (non-finite portal bounds only) | D91, D312, D154, D69/D79/D85 handled differently; D104/D63 diagnostics |
| bondview2.c | D177 (movement locus placeholder too small) | D140/D56, D191 present; D146/D160/D173/D193/D243/M-series diagnostics or cutscene work |
| chraction.c | D209 (walk-speed byte alias), D210 (patrol last-seen field) | D309/D318 diagnostics |
| chrai.c | D310 (1-byte PRINT in global AI lists) | D309 diagnostics |
| frametiming.c | D155 (catch-up clamp) | D117/D134/D193 diagnostics |
| gunfire.c | watch controller `(u32)` render_pos cast, knife keyframe `u32` (not in the reference: its dram.c hides them) | — |

Found outside the sweep: weapon timing bytes read in reverse through the
`RecoilSpeed`/`b44` union (the reference carries it too, as open D240), and
an sRGB double-encode on the Quest swapchain.


## Fourth pass — chr.c, front.c

| File | Ported | Already here / not applicable |
|---|---|---|
| chr.c | — | D43/D45 (segment-offset resolve of CollisionRelatedNode); D120 (walk cap for a converter defect ours does not have); D173/D193/M-80 telemetry; D243 M-154..M-169 intro-puppet experiments |
| front.c | M-148 (difficulty/mission text buffers), D221 (folder hit-band pairs), D50 (uninitialised legal_text_ptr) | D164 (ARRAYCOUNT), D178/D143 (gevrRomSwapBriefing in ob.c); D63/D64/D65/D146/D243/D250 diagnostics |
| port/src/input.c | D194/D238 (hold the control style at 1.2 Solitaire) | — |

Found outside the sweep: implicit declarations of functions returning s8,
u16, bool and f32 (joyGetStickY read -80 as 176). The prototype generator
now covers them; see HANDOFF 36.


## Fifth pass — bondtypes.h, propobj.c

| File | Ported | Already here / not applicable |
|---|---|---|
| bondtypes.h | — | D69/D78 (bitfields declared reversed under GEVR), D43/D45 (LinkedTo u32), D52 (u32 *datas), D88 (setup converter widens intro cameras 40→56), D151/D157 (HANDOFF 35 reads the low part of the word) |
| propobj.c | — | D52 (word-indexed at all three sites), D135 (HANDOFF 31); D218/D222 FOV-scale option not present; D202/D207/D318/M-65/M-71 diagnostics |

Found outside the sweep: struct members declared `bool` change size in port
files that include <stdbool.h>; struct player's pause_state sat 16 bytes
earlier from input.c's point of view. See HANDOFF 37.
