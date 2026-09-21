# Rare Logo Audio + Quit Handoff

**Repo:** `goldeneye-vr` (Quest / OpenGL ES port)
**Date:** 2026-09-21 (rewritten — the original diagnosis was wrong)
**Status:** Root cause found and fixed. **Every SFX in the game was playing the
wrong bank entry** — sound ids were off by one. Awaiting the user's ears.

Reference clip (ground truth): https://youtu.be/OvGZVjbezeE?t=8
Local WAV (capture): `007 GoldenEye - N64 - INTRO ONLY-RARE cymbals.wav`

---

## 1. Root cause: sound ids lost their 1-based bias on LP64

GoldenEye's sound ids are **1-based** indices into `ALInstrument::soundArray`.
The retail code never writes `- 1`; it encodes the bias **in a struct offset**:

| struct | `soundArray` offset (N64, 4-byte ptrs) | offset (LP64, 8-byte ptrs) |
|---|---|---|
| `ALInstrument` (what `alBnkfNew` / `preprocessALBankFile` lay out) | 16 | 16 |
| `ALInstrumentAlt_s` (what `sndPlaySfx` reads through) | **12** | **16** |

On the N64 the alternate view starts one 4-byte pointer earlier, so
`alt->soundArray[id]` lands on standard entry `id - 1`. With 8-byte pointers
the alternate struct's 12-byte header **pads out to 16**, the gap disappears,
and every sound id silently became 0-based — **every SFX played its
neighbour in the bank.**

`BIG_CLANK_SFX = 261` was also reading one past the end (`soundCount = 261`,
valid entries 0..260).

### The fix

`SND_SOUND_INDEX_BIAS` in `src/snd.h`, applied at the single lookup in
`src/snd.c` (`sndPlaySfx`). It derives the correction from the two layouts via
`offsetof`, so it evaluates to **0 on the N64** (byte-identical to the decomp)
and **1** wherever alignment padding has eaten the gap. That one call site is
the only place that indexes `soundArray` by sound id.

---

## 2. What the Rare logo actually is

Three copies of **one** cymbal sample (wave base `0x00be2a0`, len 12168),
a semitone apart, staggered, each quieter — that stacking *is* the rising
swell. Each layer has a 598 ms attack envelope and `fx = 32`.

| id | name | entry | vol | keyBase | pitch | starts at |
|----|------|-------|-----|---------|-------|-----------|
| 258 | `RARELOGO_SFX` | 257 | 100 | 55 | 0.7492 | 0 ms |
| 259 | `RARELOGO_QUIET_SFX` | 258 | 60 | 56 | 0.7937 | 467 ms |
| 260 | `RARELOGO_FAINT_SFX` | 259 | 30 | 57 | 0.8409 | 933 ms |

The names are the giveaway: **RARELOGO / QUIET / FAINT → 100 / 60 / 30**.
`front.c` calls `sndPlaySfx(..., RARELOGO_SFX, 0)` only; the keymap chains the
rest.

**Before the fix** the port played entry 258 (vol 60) at 467 ms plus **entry
260** — an unrelated sound, different wave (`0x00c1228`, len 6274), no attack
envelope, vol 127, `fx = 72` — slammed at full volume at t=0. Two of the three
cymbal layers, including the loudest one that opens the swell, never played.

Verified on device (`tools/gevr_boot_test.ps1`), log now reads:

```
sfx chain: id=258 entry=257 vol=100 velMax=0  delay_us=0      pitch=0.7492 fx=32 next=259
sfx chain: id=259 entry=258 vol=60  velMax=14 delay_us=466662 pitch=0.7937 fx=32 next=260
sfx chain: id=260 entry=259 vol=30  velMax=28 delay_us=933324 pitch=0.8409 fx=32 next=0
```

---

## 3. Evidence

> Reference material cited below (`artifacts/`, the N64 capture WAV) lives in
> the original working folder `GEVR-OpenGLES/`, not in this repository — it is
> debug evidence and copyrighted audio, kept out of the public tree.

Independent ROM parse + VADPCM decode of `sfxctl` @ `0x002EBDE0` /
`sfxtbl` @ `0x002F19A0`, then an offline mix of both chains, compared against
the N64 capture by energy above 2 kHz (each normalised to its own peak):

| t (s) | reference | fixed chain | old port chain |
|------|-----------|-------------|----------------|
| 0.00 | 0.02 | 0.02 | **0.29** |
| 0.24 | 0.12 | 0.43 | 0.12 |
| 0.48 | 0.56 | 0.92 | **0.08** |
| 0.72 | **0.93** | 0.66 | 0.47 |
| 0.96 | 0.73 | 0.72 | **0.96** |
| 1.44 | 0.40 | 0.34 | 0.31 |

The reference and the fixed chain both **start from silence and rise
monotonically**. The old chain fires a transient at t=0, *dips*, then peaks
half a second late — which is exactly the user's "almost like wrong order".

Artifacts: `artifacts/rare_fixed_chain_257_258_259.wav` vs
`artifacts/rare_current_port_chain.wav` (offline mixes, dry, no FX).

> The older `artifacts/rare_snd258_*.wav` / `rare_snd260_*.wav` were decoded
> under the wrong index mapping and at the wrong gain (`rare_snd260_raw.wav`
> is 43 % clipped). Treat them as invalid.

---

## 4. Why the previous pass missed it

The `sfx chain:` log confirmed the chain was **internally consistent** — 258
really does chain to 260 in the bank — which was read as "scheduling is not
the bug". But internal consistency says nothing about whether the *right*
entries were being read. Everything downstream was then reverse-engineered
from the buggy runtime: the old §2 table assigned `RARELOGO_FAINT_SFX` a
vol-127 wet-heavy "bed", and a theory about wet/dry balance, FX character and
envelope models was built on a sound the N64 never plays here.

**Corrected:** there is no "hot wet bed" layer and no inverted order.

---

## 5. Still-good work from the earlier pass (keep)

These were fixed on the way and remain correct and necessary:

- **Quit / leftover music on Quest home** — `audioPause` / `audioResume` /
  `audioShutdown` in `port/src/audio.c`; `MainActivity` pause/resume hooks.
  Confirmed by the user. Don't regress.
- **Launch SIGSEGV in FX delay taps** — `(s32)` casts on delay taps in
  `src/libultrare/audio/reverb.c`; unsigned `ALDelay` offsets wrapped to huge
  indices on LP64.
- **`aPoleFilterImpl`** implemented from mupen `alist_polef` (was a no-op);
  uses a local `h2_scaled` rather than stomping the shared ADPCM book.
- **GeEnv additive envelope** — fixed the music buzzing; Rare's `_getRate` is
  additive, not SM64's exponential.
- `AL_FX_CUSTOM` + `CUSTOM_FX_PARAMS_N`.

## 6. SIMD opt-outs: measured and resolved

`port/src/mixer.c` had disabled NEON ADPCM decode and NEON `aMix` under
`#ifdef GEVR`, added as an attempted fix for this bug. The bug was not there,
so both were A/B'd on the headset with `tools/gevr_mixer_ab` — it compiles
`mixer.c` twice into one arm64 binary and runs both paths over every ADPCM
wave in the sfx and instrument banks (291 waves, 2,120,512 samples, all 13
shift values, npredictors 1 and 4).

| op | agreement | speed | outcome |
|----|-----------|-------|---------|
| `aADPCMdec` | **bit-exact**, 0 differing samples | NEON 2.75x | NEON re-enabled |
| `aMix` | differs by **≤ 1 LSB** on ~29% of samples | NEON 3.0x | left scalar |

The gates are now named knobs (`GEVR_SCALAR_ADPCM`, `GEVR_SCALAR_MIX`) rather
than buried `!defined(GEVR)` tests, so either can be flipped for a re-test.

`aMix` stays scalar because the three candidate formulations all disagree in
the last bit and none is provably the RSP's: scalar does
`(out * 0x7fff + in * gain + 0x4000) >> 15` with one clamp, NEON does
`out + round(in * gain >> 15)` with two saturations, and mupen's `alist_mix`
does `out + (in * gain >> 15)` with one clamp and no rounding. At ≤ 1 LSB
(~-90 dBFS) there is nothing audible to gain.

**Be honest about the magnitude:** the saving is real but small — ADPCM decode
is on the order of 0.2% of one core at full voice count, so this is tidiness
and headroom, not a frame-rate fix. The debug APK builds at -O0, where the
2.75x (measured at -O2) will not show up.

---

## 7. Architecture notes

**Truth sources**
- Game/FX tables: `goldeneye-decomp` (sibling).
- Soft mixer reference: `artifacts/audio-reference/` (getv-style).
- Host mixer: `port/src/mixer.c` (GEVR forks: GeEnv, scalar aMix, polef;
  ADPCM runs NEON again as of 2026-09-21).

**Audio graph (stock libultra)**
Voices → envmixer (dry→MAIN, wet→AUX) → aux bus → `alFxPull` (CUSTOM
delay/chorus/pole) → main mixes AUX→MAIN → interleave → SDL @ 22050 Hz.

**Key files**
- `src/snd.h` — `SND_SOUND_INDEX_BIAS`, `ALInstrumentAlt_s`
- `src/snd.c` — `sndPlaySfx` lookup + `sfx chain:` log
- `src/game/front.c` — `sndPlaySfx(..., RARELOGO_SFX, 0)`
- `port/src/preprocess/segaudio.c` — ROM bank → host layout
- `src/libultra/audio/bnkf.c` — offset → pointer relocation
- `port/src/gevr_audio.c` — ROM offsets for the sfx/instrument banks
- `port/src/mixer.c`, `port/include/mixer.h`
- `port/src/audio.c`, `MainActivity.java` (quit path)

**Boot test:** `.\tools\gevr_boot_test.ps1 -Seconds 24`
Package: `com.gevr.port`

---

## 8. Constraints / policy for agents

- Match **decomp + getv soft mixer**, not Perfect Dark N-Audio.
- Prefer root-cause in the data path over game-side SFX hacks.
- Before theorising about mixer character, **verify the right sample is even
  being played** — parse the ROM bank directly and compare against the
  reference capture.
- Quit path is done; don't regress `audioPause` / MainActivity pause.
