# Three-player crash investigation and follow-up

Changes remain local in the existing worktree. No push, deployment, or headset
installation was performed. Headset access was read-only.

## Crash evidence

The attached `gevr-027f179c.txt` contains base64-encoded diagnostic data. Its
decoded peer log reports a SIGSEGV at 20:56:43.219. The connected host's Android
crash buffer reports a SIGSEGV at 20:56:45.124. Both use native build ID
`13efe7db6fed1a9499add6cb947f0f6c6e23fdb3` and crash at:

```
modelUpdateRelationsQuick + 44  (model.c, node opcode dereference)
objTick + 12896               (propobj.c, model relation update)
propsTick + 100
lvlRender
```

The peer's invalid node address is `0xb400007affffffff`; the host's is
`0x48005a1011014d0e`. The matching release symbols were preserved before
rebuilding. This establishes the same corrupted-model traversal failure on
two headsets; the third headset's log was not supplied.

Android denied access to the host's saved `gevr.log` (private app file), and
the release app is not debuggable. Its crash buffer was readable. No report
was uploaded and no app or device permissions were changed.

## Reproduced allocator defects

`vtxstore_reset` reserved 20 bytes per descriptor, the N64 layout. Each ARM64
descriptor is 32 bytes because it contains two pointers. Initialization wrote
past those allocations. `vtxstore_allocate` also read counts through the
second `s16` of an `s32`, yielding zero on the headset's little-endian CPU and
preventing normal splitting into independent vertex batches.

The fix uses `sizeof` and reads the complete counts. A headless harness
compiles the actual production allocator and surrounds each allocation with
guards. The original code fails at reset with a modified guard. With only
the size fixed, the old count read fails independent allocations. With both
fixes, guards and 100 allocation/free/merge cycles pass.

These are confirmed memory-safety and allocation defects in the combat path
and fit the crash evidence. The logs do not contain the earlier write that
corrupted those specific nodes. Repeating the fight on headsets is required
to confirm that this fixes the reported crash completely.

## Other changes

- Proximity: full gain through 4,000 units; `0.1 + 0.9 * (1-t)^2` from 4,000
  to 8,000; 10% floor afterward. At 6,000 the gain is 32.5%. Voice volume
  applies afterward; Couch and spatial positioning remain available.
- Multiplayer pause opens Status on every normal opening. Gameplay uses
  smaller native health/armor arcs at the radar rim, with red on the left
  and blue on the right. Single-player watch behavior is preserved.
- Friendly fire is enabled by default, host controlled and persisted. It
  changes immediately and is carried by lobby, round, late-join, and
  migration serialization. Pending team choices do not change active damage
  eligibility. Self and environmental damage remain enabled.
- Ammo crates use setup-command identities, host physics, reliable shot
  impulse requests, and 20 Hz host state. Position, render origin, rotation,
  movement, and respawn state reach clients and late joiners. Per-round
  epochs reject delayed traffic from a previous round of the same stage.
  Respawn restores the recorded original pose and clears bullet holes and
  retained projectile/vertex state. Recycled dropped-item pool indices are
  deliberately excluded from this setup-crate protocol.
- Host ping remains 0 ms because the displayed measurement is RTT to the
  current host. Clients show their connection RTT, not pairwise player RTT.
- Protocol is 12; all participating headsets need the same build.

## Validation

- 26 native tests passed, using production serializers, state functions,
  allocator and spawn lifecycle, plus the pinned Steam Audio runtime.
- 10 Android lobby-client tests passed; 6 Worker tests and TypeScript passed.
- Local ARM64 release and debug APKs built. Release v2 signature verified.
- Automated checks do not validate rendered headset pixels or live
  three-player physics/voice routing.

Headset acceptance: repeat sustained three-player combat, inspect circular
radar and rim gauges, reopen Status after visiting other pages, test the 10%
voice floor, toggle friendly fire for bullets/explosions, and shoot/pick up
ammo crates from every player's perspective. Test crate respawn position,
removed bullet holes, late joins, same-map round resets, and host migration.
