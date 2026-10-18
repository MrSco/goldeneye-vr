# Tracked multiplayer avatars

Remote living VR players now use the headset and both controllers to pose their
head and arms. This applies to multiplayer and co-op player bodies. It is enabled
automatically when a peer sends valid tracked poses; bots, flat players,
unsupported rigs and dead players retain the existing animations.

Both hands move even when the offhand is empty. The head follows yaw, pitch and
roll independently of the gun. A slower torso heading lets a player glance to
the side without immediately turning their entire body. Walking and sustained
turns bring the torso around; separation is limited to 65 degrees. The upper
body also follows the camera's rise/offset, bounded to 35 cm per axis, while
legs retain the existing walking/crouching/floor placement.

Each arm uses an analytic shoulder–elbow–wrist solve. Its elbow is estimated
outward, downward and back from the torso. Reach is clamped to the character's
own bone lengths rather than stretching the model. Wrist orientation includes
roll and detailed held weapons inherit that wrist. Left-handed weapon roles
are mapped to the physical arms. When a two-handed hold changes the firing
direction, that turn is carried into the weapon wrist while retaining its roll.

Elbows and torso are estimates: a headset and two controllers do not track
them. Very different player/avatar proportions and the palm-to-wrist offset
(initially 4 cm) need headset assessment. A support hand follows its controller;
this first implementation does not constrain its fingers to every rifle's
foregrip or implement finger tracking.

## Transport and loss of tracking

Protocol **23** retains three position/quaternion pairs, a torso heading and validity
bits to the existing movement packet. All players in a match need this build.
The complete packet including its transport header is 253 bytes and fits the
existing 256-byte send buffer. Existing shot rays and combat messages are kept.

- Positions use unscaled world centimetres. Head position is an offset from
  the player's movement position; wrists are offsets from that head. Rotations
  are world-space `x,y,z,w` quaternions. Hands are weapon roles, with a separate
  left-handed flag; raw legacy `gCtrlQuat` values are not used for IK.
- Capture uses the game camera/grip snapshot, including the existing aim
  steadying/recoil. Invalid or lost controllers are flagged independently.
  An unfocused XR session sends no tracked avatar pose.
- Receive validates finite positions, plausible distances and quaternion norms.
  The existing epoch/life and move-tick checks still reject old packets.
- An eight-sample history interpolates offsets and quaternion rotations with a
  50 ms delay on the receiver's monotonic arrival timeline. Movement and shot
  authority retain their existing timing. Each limb holds briefly, then fades
  to animation between 100 and 300 ms after its last valid tracking sample.
- Histories reset for a new life/epoch, reconnect, handedness change, a movement
  jump over 1 m, or a gap over 500 ms. Torso following advances during capture,
  never once per eye or render pass.

## Rig integration and hit testing

[`gevr_avatar.c`](../port/src/gevr_avatar.c) resolves the guard skeleton's actual
nodes by joint ID: left shoulder/elbow/wrist 4/6/8, right 5/7/9 and head 3.
It checks those chains and falls back if the rig differs. It uses the existing
body pose to measure arm lengths and preserves the head's bind orientation.

Wrist targets use the player's stable collision/eye position. During `chrTick`,
the viewer prop temporarily holds the animated pelvis position; using that prop
as the eye anchor lowered hands by the eye-to-pelvis distance, including overhead
poses. Regression poses cover stomach, chest and above-head heights.

The adapter runs after the ordinary character animation and before guns and
hats are attached. A temporary base build establishes torso heading; a further
build supplies solved local joint rotations through `process_02_position`.
This regenerates primary, half-rotation and bend matrices, plus descendants,
using the game's own builders. Native, HD and Bean visuals therefore receive
the same rig pose. Root animation state and callbacks are restored afterward.

The original animated body matrices are retained separately. `chrTestHit` uses
those for body hit detection, then restores the displayed pose. This preserves
existing body hitboxes and combat authority; visible tracked arms can differ
from those hitboxes. Held objects/hats follow their displayed attachments.
Projectile and thrown-object tests (`propobj.c`), the hit-list depth sort and
held-gun hitboxes still read the displayed pose in the drawn pass. IK runs only
in the drawn view pass: the extra per-copy and barrel passes (`lv.c`
`gevrViewPass`) keep the animated pose and allocate nothing.
Death and ragdoll posing continue on their original path.

## Checks

`python3 port/tests/test_avatar.py` compiles the real math/protocol and adapter
under AddressSanitizer/UndefinedBehaviorSanitizer. It exercises reach limits,
coincident targets and poles, 5,000 reach samples, quaternion round trips,
truncated/invalid packets and packet size, interpolation, tracking loss and
history resets. A synthetic guard rig runs the production joint builders and
checks both wrist positions/orientations, auxiliary matrices, left-handed gun
attachments, level scale 1.0/0.2, several headings, capture and collision-pose
retention. No ROM or headset is used by those checks.

Before release, test on two headsets: looking away while pointing, wrist roll,
empty offhand, dual wield, supported rifles, left-handed mode, overhead and
cross-body reach, crouch/stairs, 360-degree and stick turns, recenter,
controller loss, sleep/wake, death/respawn and stage change. Check native/HD/Bean
characters and hats, rendered barrel versus shots, and eight-player frame time.
These visual/device checks have not been performed in the cloud environment.

## Provenance

The arm geometry follows Perfect Dark CE's two-bone wound-clutch solve at
`e985acc0de494b3c50a47ea1fcdc48839607e1b0`, `port/src/ragdoll/motion.c:856–895`.
Torso following adapts the independent body-yaw pattern in Alex-LeTux's
Perfect Dark VR `7739e3b05fc19049535565bb18d5f71865366c1f`.
Their MIT notices remain in the project and APK. See [research](multiplayer-ik-research.md)
and [credits](../CREDITS.md).
