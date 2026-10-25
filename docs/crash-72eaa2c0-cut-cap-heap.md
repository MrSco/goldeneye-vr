# Crash 72eaa2c0: ragdoll list corrupted by a stale cut cap

Report 72eaa2c0, v0.5.13 (bdeb44dc), Quest 3, 2026-10-24 02:24 local. Bean
with the Community Edition, ragdolls and dismemberment on, no campaign mod;
the process had been up 27 hours. The tombstone survived in the logcat crash
buffer (the report's own tombstone was unavailable).

## What faulted

SIGSEGV, fault address `0xb400000000000000`, in `rdRelease` called from
`gevrRagdollReset` from `lvlStageLoad`: a level load releasing ragdoll bodies.
The instruction is `ldr x12, [x8]` at `rdRelease+0x40`: reading `m->next`
while walking a body's private memory list (`RdMemory`, from `rdAlloc`). The
previous node (`x11`) was a valid heap pointer; its `next` was the heap tag
`0xb4` with a zero address.

## Why

A heap pointer `0xb40000XXXXXXXXXX` whose low six bytes were overwritten with
three zero `s16`s. That is `beanCutCaps` (gevr_bean_pose.inc.h), which writes
each rim vertex's `posed[edge].v.ob[0..2]` into a frame buffer sized for the
skin's current vertex count. The edges come from the cut-cap cache, keyed by
skin address and cut mask. Before PR #223 that cache outlived the level reset
that frees the skins, so a later level's skin at a reused address, with the
same cut mask, was given the earlier skin's edges: indices past its vertices,
written past the buffer into the neighbouring heap block. The crash comes at
the next level load, when the corrupted list is walked.

## Fix

- PR #223: the cap cache is cleared at every level reset.
- A cache entry also carries the skin's vertex count and is used only for a
  skin of that size, so a stale entry can no longer index past `posed`
  (gevr_bean_caps.inc.h; gevr_bean_render_test.c checks it without a reset).

Not reproduced on a headset. Symbolized with
`build/release/native-debug-symbols-v0.5.13.zip` (Build ID f8a72cb0...).
