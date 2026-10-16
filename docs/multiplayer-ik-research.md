# Multiplayer head and arm IK research

Checked 2026-10-16 against GoldenEye VR `07189c99d19aed9dc2ca82c684fc33850ccf952e`.
This is source research and a proposed implementation, not a working IK feature.

## Findings

There is useful prior art, but no ready-to-copy multiplayer VR avatar system in
the sources checked. Perfect Dark CE has an actual two-bone arm solve in its
ragdoll reflexes. Perfect Dark VR has controller-driven first-person wrist and
forearm posing, elbow tuck, and a smoothed torso heading. Combining those ideas
with explicit tracked-pose replication is a practical route for our avatars.

| Project / inspected branch and commit | Verified result | Use here |
| --- | --- | --- |
| [Perfect Dark VR, `port`, `7739e3b`](https://github.com/Alex-LeTux/perfect_dark_VR/tree/7739e3b05fc19049535565bb18d5f71865366c1f) | `bondgun.c` implements `apply_wrist_rot`, `tuck_elbow`, and `vr_wrist_rot`; torso yaw follows head yaw using shortest-angle differences. | Wrist/forearm posing and body-stable elbow direction. This manipulates first-person gun/hand models, not a complete shoulder-to-wrist avatar. |
| [Perfect Dark CE, `port`, `e985acc`](https://github.com/Chesluk-Industries/Perfect-Dark-CE/tree/e985acc0de494b3c50a47ea1fcdc48839607e1b0) | `port/src/ragdoll/motion.c` solves a two-bone arm so the hand can clutch a wound, then drives physics toward the result. | The best mathematical reference for shoulder, elbow, reach clamping, and a preferred bend direction. Extract the geometry, not the ragdoll motors. |
| [Dab's Mod, `dabs-mod`, `3ee8ed0`](https://github.com/DabDavis/perfect-dark-dabs-mod/tree/3ee8ed0cd549b64350848655c43ae1568e449c6d) | Shoulder/elbow/wrist chains and rest/bind transforms in `port/src/gebean.c`. No arm IK solver found in the checked C/C++ sources. | Rig mapping and mesh deformation references; the limb fitting code is not runtime IK. |
| [GEVR, `main`, `a431553`](https://github.com/no6969el/GEVR/tree/a4315530ad535c67a4df65071929d02a7a581aae) | Published docs discuss the PD elbow tuck and explicitly say an IK arm was deferred. The calibration document leaves the IK question open. | Design history, not a verified shipped solver. The published tree does not provide the complete game implementation, so this cannot establish what every release binary does. |

### Exact upstream code

- PD CE: [two-bone clutch solve, motion.c:795–903](https://github.com/Chesluk-Industries/Perfect-Dark-CE/blob/e985acc0de494b3c50a47ea1fcdc48839607e1b0/port/src/ragdoll/motion.c#L795-L903).
  It measures upper/lower arm lengths, clamps shoulder-to-target distance, uses
  the cosine rule to locate the elbow, and prefers an elbow out/down/back.
  [`ragdollAimPart`, line 139](https://github.com/Chesluk-Industries/Perfect-Dark-CE/blob/e985acc0de494b3c50a47ea1fcdc48839607e1b0/port/src/ragdoll/motion.c#L139-L150)
  rotates a bone toward the solved direction. Its target includes a palm offset
  and its output is a physics target, not an exact tracked wrist orientation.
- PD VR: [wrist rotation and elbow tuck, bondgun.c:803–961](https://github.com/Alex-LeTux/perfect_dark_VR/blob/7739e3b05fc19049535565bb18d5f71865366c1f/src/game/bondgun.c#L803-L961).
  Tuck rotates the forearm around a fixed wrist and preserves forearm length;
  it does not enforce a shoulder attachment. The main-hand legacy rotation
  blend is currently zero; tuck is the relevant active correction.
- PD VR: [torso yaw follower, bondgun.c:11063–11079](https://github.com/Alex-LeTux/perfect_dark_VR/blob/7739e3b05fc19049535565bb18d5f71865366c1f/src/game/bondgun.c#L11063-L11079).
  It wraps the angular difference before following the head. Its `0.02` follow
  factor is per tick, so adapt it to elapsed time and keep separate state for
  each remote player.
- Dab: [three-joint limb mapping, gebean.c:6677](https://github.com/DabDavis/perfect-dark-dabs-mod/blob/3ee8ed0cd549b64350848655c43ae1568e449c6d/port/src/gebean.c#L6677-L6687).
- GEVR: [arm posing assessment](https://github.com/no6969el/GEVR/blob/a4315530ad535c67a4df65071929d02a7a581aae/docs/168-THE-PERFECT-DARK-PORT-FEASIBILITY.md#32-elbow-tuck--arm-posing)
  and [open calibration/IK questions](https://github.com/no6969el/GEVR/blob/a4315530ad535c67a4df65071929d02a7a581aae/docs/205-THE-CALIBRATION-PHASE-A-STANDING-DOCUMENT.md).

## What our multiplayer currently has

- [`netplayermove`](../port/src/net/net_protocol.h) carries player position,
  head yaw/pitch, a legacy single-hand position/Euler rotation, and both guns'
  muzzle origins and aim directions. It lacks a complete head orientation,
  separate torso heading, both wrists' full poses, and tracking-valid flags.
- [`netPlayerSyncAfterTick`](../port/src/net/net_player_sync.c) fills the legacy
  hand fields from `gCtrlPos[1]`/`gCtrlQuat[1]`. [`net_core.c`](../port/src/net/net_core.c)
  stores and mirrors them, but no avatar renderer consumes them. These fields
  should not be mistaken for functioning networked arm tracking.
- `netPlayerSyncBeforeTick` deliberately poses the copy from head pitch, not
  barrel pitch/yaw. Its comments record the earlier bug: a lowered gun bent
  the whole torso, and moving a controller twisted the body. IK must control
  the arms independently instead of restoring that coupling.
- The current gun rays support firing independently of where the avatar's
  animated hands appear. A ray does not specify wrist roll or the grip offset,
  and an empty hand still needs a pose even when it has no weapon ray.
- [`chr.c`](../src/game/chr.c) calls `subcalcmatrices`, captures ragdoll poses,
  then builds held weapons and hats. [`gevrHeldGunCompute`](../src/game/gevr_heldgun.c)
  already fits detailed weapons to a supplied hand matrix.
- The existing [`ragdoll rig mapping`](../port/src/gevr_ragdoll_internal.h)
  identifies shoulders, elbows and hands by guard `JointID` (4/6/8 and 5/7/9).
  Resolve the model's actual matrix IDs from its nodes; do not copy PD VR's
  first-person matrix indices 1/2/17/18.

## Proposed first implementation

1. **Replicate poses before solving arms.** Capture head and both physical
   hands from a consistent game-frame snapshot, with positions, normalized
   quaternions, validity, and a torso heading. Define one coordinate system,
   units, quaternion order and hand ordering explicitly. Map physical hands
   to weapon roles once for left-handed mode. Transmit empty hands too.
   The existing camera/grip helpers in [`vr_input.cpp`](../port/vr/vr_input.cpp)
   and world conversions in [`bondview2.c`](../src/game/bondview2.c) provide
   the path; account for `D_800364CC` level scale. Distinguish a held last pose
   from currently valid tracking. Decide whether recoil/steadied aiming belongs
   in the visual wrist pose or in a separate rendered weapon transform.
2. **Display head and hands accurately.** Use the HMD rotation relative to the
   torso for head yaw/pitch/roll; allow a quick glance without immediately
   rotating the whole body. Align the avatar's eye/neck offset to the tracked
   head. Keep animated legs and existing floor placement, and bound the
   resulting neck/torso adjustment for lean and crouch. Interpolate remote
   positions and quaternion rotations on a common timestamp timeline.
3. **Solve each arm with two bones.** Use the animated shoulder anchor and
   calibrated model bone lengths. Convert controller grip to an anatomical
   wrist target using a rig-specific offset. Solve the shoulder/elbow with
   the CE geometry, then apply the tracked wrist orientation with its bind
   correction. Infer elbow direction from the torso and hand side, retaining
   a stable previous bend plane near singularities. Headset plus two controllers
   cannot measure the actual elbows or torso; these are estimated poses.
4. **Apply the pose through the character rig.** Solve after the base animation
   is available and before attachments are built. Prefer feeding solved local
   joint rotations through matrix generation, or explicitly rebuild all
   affected descendants and auxiliary matrices in a second pass.
   [`modelBuildGroupMatrices`](../src/game/model.c) creates half-rotation/bend
   matrices as well as the primary matrix: changing three primary matrices
   alone can tear native/HD/Bean meshes. Include attached head and hat poses.
   Restrict this to living tracked remote players; retain animations for bots,
   unsupported rigs and missing poses, and retain the death/ragdoll path.
5. **Attach guns and handle tracking loss.** Let detailed held guns inherit
   the solved wrist, with the existing model grip fit. For two-handed weapons,
   agree on the supported weapon rotation and support-hand target; two noisy
   raw grips cannot independently constrain a rigid gun. Fade stale or invalid
   limbs back to animation, and reset pose history on respawn, life/epoch
   changes, teleport, recenter and stage changes.

Keep shot origins, damage validation and existing gameplay authority independent
of visual interpolation. Review the rendered muzzle against the transmitted aim
ray, particularly for two-handed aim, so pointing and firing agree visually.
The character matrices also feed hit testing, so an ostensibly visual pose
change must explicitly account for hitboxes and lag compensation.

For transport, extend the actual `netbufWritePlayerMove`/`netbufReadPlayerMove`
path and host relay, not only legacy `NetMsgPlayerState`. Bump protocol version
21 when the wire schema changes. Check the 256-byte send buffer in
`netSendOwnedMove`, packet limits, bot/default initialization and every serializer
user before adding fields. The current receive path rejects old move ticks and
keeps the newest sample; smooth visual arms need a short pose history as well.
Validate finite values, quaternion norms and plausible pose bounds before use.

## Validation for the implementation

- Pure solver checks: reachable/unreachable/coincident targets, nearly straight
  and fully folded arms, parallel pole directions, both sides, preserved lengths
  and no NaNs. Verify wrist orientation independently of arm bend.
- Protocol round-trip and truncated/invalid packet checks; physical left/right
  hands, left-handed weapon mode, empty offhand and bots; enforce buffer limits.
- Two-headset check: point away from head direction, lower gun while looking
  straight, roll wrists, cross hands, reach overhead/behind, dual wield and
  support a rifle. Verify rendered gun/muzzle and remote firing agree.
- Turn through 360 degrees, snap/smooth turn, crouch/lean, use stairs, lose one
  controller, sleep/wake, recenter, die/respawn, change stages, and add packet
  loss/reordering. Check native, HD and Bean characters, hats, and multiple
  camera passes; advance pose state once per simulation step.
- Measure the full eight-player case on Quest. Two analytic arm solves per
  avatar are small; matrix rebuilding, mesh paths and network smoothing still
  require device measurement.

No gameplay code was changed or headset behavior tested for this research.
If adapting upstream code, retain its MIT notices and update provenance in
`CREDITS.md`; existing PD CE/PD VR notices are already present in the tree.
