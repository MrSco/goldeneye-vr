# Crash 71b515e8: null-base Bean wound lookup on Dam

Report: Entropy159, Quest 3, v0.5.1, commit `f1dae9c`, built
2026-10-14 20:44. Crash: `2026-10-15T03:21:11Z`, or 2026-10-14
21:21:11 -0600 on the headset. The player's note says it crashed partway
through the first level. The log confirms Dam (stage 33, `UsetupdamZ`), loaded
at 21:20:09, about 62 seconds before the fault.

Both attachments are base64-encoded; decoded diagnostic data remains outside
the repository.

## Comparison with 53f092b3

This is the same failing wound-rendering check as
[53f092b3](crash-53f092b3-bean-wounds.md), with a different invalid lookup result.

| Evidence | 53f092b3 | 71b515e8 |
|---|---|---|
| Stage | Facility | Dam |
| Process / SDLThread | 6228 / 6274 | 19659 / 19700 |
| Signal | SIGSEGV / SEGV_MAPERR | SIGSEGV / SEGV_MAPERR |
| Fault address / x0 | `0x05007830` | `0x8` |
| First three library offsets | `0x2636e8`, `0x25ee40`, `0x25bbe4` | identical |
| First exported frame | `gevrBeanRenderGdl + 540` | identical |

Both have native build ID `10abf026bb4a588b3690c42d5bddc081a22f4576`.
The 256-byte code snapshots around PC are identical. At the fault, the
instruction is `ldr x9, [x0]`, reading the vertex pointer from the runtime-data
record returned by `modelGetNodeRwData`. A preceding null check accepts `0x8`.
The remaining native render stack also matches, through `drawjointlist`,
`chrRenderProp`, `chrpropsRenderPass`, `bgLevelRender`, `lvlRender` and
`bossMainloop`. No matching archived ELF was used; identification of the two
unnamed frames uses the captured code and production source as described in
the earlier diagnosis.

The captured wound slot at `0x6f13109780` belongs to
`Model *0xb400006f06299ce8` (x22), with serial 2. Its second wound, selected
by x19 = 1, retains node `0xb400006f062aa270`, vertex copy
`0xb400006f0624acf8`, matrix 0, alpha 131 and kinds 3 (head and body).
The model's runtime data is `0xb400006f062ab620`. This again identifies a
stored head wound at the failed vertex-copy check.

The null-base result is consistent with the same shared-head parent problem:
using a different body's head-placeholder index can retrieve a null head-data
base, then adding the stained node's nonzero word index yields a small nonzero
pointer. The full parent chain is not captured, so its exact intervening
values cannot be reconstructed from this tombstone alone.

## Coverage and validation

The existing fix for 53f092b3 already addresses this path: retain the original
instance's runtime-data record at hit time, avoid following shared head
parents when drawing wounds, and clear wounds on model-slot and stage reset.
This report requires no additional production change.

The regression now also supplies a null head base and a two-word record offset.
`python port/tests/test_bean_wounds.py --verify-baseline` reproduces both
original faults, at `0x05007830` and `0x8`, in `beanWoundsInAnchor`. The fixed
production code passes both cases under ASan/UBSan, preserving independent
vertex-copy expiry, head/body tint and model-slot reuse cleanup.

Android APK and device validation remain unverified. On Quest, replay Dam with
Bean models, shoot guards in the head and body while other guards are visible,
let corpses fade, and restart the mission. Verify that wounds stay with their
guard, reused models start clean, and neither crash recurs.
