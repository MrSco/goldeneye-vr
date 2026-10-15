# Crash 53f092b3: Bean wound lookup through a shared head

Report: Agent 1649, Quest 3, v0.5.1, commit `f1dae9c`, built
2026-10-14 20:44. Crash: `2026-10-15T07:08:47Z`, or 15:08:47 +0800
in the headset log. Both attachments contain base64; their decoded log,
protobuf and memory extracts remain outside the repository.

## Evidence

The protobuf and app logcat agree on process 6228, SDLThread 6274,
SIGSEGV / SEGV_MAPERR at `0x05007830`. This is a memory access fault.
Native build ID: `10abf026bb4a588b3690c42d5bddc081a22f4576`.

[71b515e8](crash-71b515e8-bean-wounds.md) hits the same instruction and render
stack on Dam with a null-base lookup result of `0x8`. The regression covers
both invalid-address variants.

```
0x2636e8 (no exported name)
0x25ee40 (no exported name)
gevrBeanRenderGdl + 540
drawjointlist + 284
chrRenderProp + 564
chrpropsRenderPass + 148
sub_GAME_7F0B3C8C + 1056
sub_GAME_7F0B8D78 + 108
bgLevelRender + 176
lvlRender + 1248
bossMainloop + 1876
```

No matching archived ELF is available. The two unnamed frames are identified
by captured instructions and source behavior, without symbolizing a different
build. The instruction at PC in the protobuf's code snapshot is `ldr x9, [x0]`;
`x0 = 0x05007830`. Immediately before it, a call returns that pointer, followed
by a null check. The following instructions compare the loaded pointer to a
stored pointer, increment a live count, check a matrix index and wound-kind
mask, and transform a local point. This matches `beanWoundsInAnchor`'s runtime
vertex-copy check, called by `beanSkinGpuDraw` from `gevrBeanRenderGdl`.

The captured wound slot at `0x77bdfc3240` belongs to
`Model *0xb4000077ae4abb58` (also x22). Its serial is 1 and its first wound
contains node `0xb4000077ae4dd134`, vertex copy `0xb4000077ae44a688`, matrix 0,
alpha 138, and kinds 3 (head and body). The model's runtime data is
`0xb4000077ae4de434`. The failed check is therefore a recorded head wound,
not display-list interpretation by the graphics driver.

The player loaded Facility (stage 34, `bg_ark`, `LarkE`) at 15:08:05 and
crashed about 42 seconds later. Bean character and head replacements loaded;
the optional blood-puff patch is off. Normal GoldenEye blood stains still
record Bean wounds with that patch off.

## Cause and fix

Wounds retained a model pointer and a stained model-node pointer. Each draw
called `modelGetNodeRwData(model, stained)` again to see whether the stained
vertex copy remained live. That lookup follows the node's parents to find
the head placeholder and its runtime-data index.

Head model nodes are shared. `modelApplyHeadRelations` rewrites a head root's
Parent for the body currently being processed. When another body has since
used the head, the saved wound node can lead to that body's placeholder.
Applying its index to the wounded model's runtime data can interpret unrelated
data as a head runtime-data pointer. A non-null check cannot validate this.

The wound now captures its instance's runtime-data record during the original
hit walk. Drawing compares that record's vertex copy directly, without walking
shared parent links. Wounds are cleared before `modelInit` reinitializes a
slot, and `gevrBeanReset` clears the whole registry before stage-owned records
can be reused. Head/body tint selection and vertex-copy expiry are preserved.

## Validation

`python port/tests/test_bean_wounds.py --verify-baseline` compiles the production
wound code, runtime-data lookup, head-relation updater, matrix-index lookup
and model initializer under ASan/UBSan. Two body definitions have different
head-placeholder indices and share one head. After recording each instance's
wound, the shared Parent belongs to the second body. The fixture places a
segment address in the first body's unrelated data: the original code faults
at the report's exact address, `0x05007830`, in `beanWoundsInAnchor`. The fixed
code passes, retaining independent head and body tint, expiring returned
vertex copies, and clearing reused model slots before runtime data is read.

The full `port/tests/test_bean.py` suite passes under ASan/UBSan, including the
production stage-reset check added to the rendering fixture. The existing
`port/tests/test_bean_joint_draw.py` checks pass for US and EU walkers.
These older runners need a temporary GCC wrapper in this workspace to force
`stddef.h` before the cartridge headers and allow their existing union/record
pointer conversions on GCC 14. Leak detection is disabled for the full suite,
whose arena-allocation stubs deliberately retain frame allocations. The
focused wound regression runs directly with leak detection enabled.

No Android SDK/NDK or headset is available here; an APK build and device
acceptance remain unverified. On Quest, replay Facility with Bean models,
shoot guards in the head and body while other guards are visible, let corpses
fade, and reload the mission. Check that wounds stay on the correct guard and
that reused models start clean.
