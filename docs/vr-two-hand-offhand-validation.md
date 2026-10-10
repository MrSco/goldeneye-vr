# Supporting a gun in either hand

## Laser scope flicker on XR redraws (2026-10-10)

The user's v0.4.18 Quest 3 capture tested a supported laser in both weapon
slots. The primary-hand hold ran from 10:13:47.387 to 10:13:50.838 with both
controllers tracked. The off-hand hold ran from 10:13:55.411 to 10:14:00.204;
its weapon controller was untracked in the samples from 10:13:55.788 through
10:14:00.188, while the support controller remained tracked. Neither hold
dropped until its support grip was released. The laser is classified as a
handgun, so its hold does not use the filtered two-hand barrel direction.

Fresh game frames reconstructed an untracked controller from its last
play-space position, while XR redraws reused its stale head-relative position.
Moving the head therefore alternated the gun between different poses. Both
paths now reconstruct the same room-fixed position and retain the same steady
orientation through tracking loss. Separately, scope submission reused saved
camera-relative gun axes, locking the lens to the headset between game frames.
The lens now applies the same absolute controller delta as the gun and outer
sight, including the stored supported-gun orientation and unit conversion.

`python port/tests/test_grip_redraw.py` covers both controller roles and
handedness, head translation/rotation, steadying, tracking recovery, invalid
poses, scope/eyepiece alignment, world scales and repeated non-cumulative
redraws. Both regressions fail against the original `HEAD` sources, including
the scope-only check. Existing two-hand, frame-timing and v043 checks pass.
Android `assembleDebug` passed with JDK 20; the debug APK signature verified.
The new behavior still needs a headset retest: repeat the laser in either
hand, move the head with the hands still, briefly occlude the weapon controller,
and release/re-grip each hand. Compare normal sights and scope lenses before
and after tracking recovery. Also check a supported scoped rifle.

The grip code used to inspect only `GUNRIGHT`, controller role 1, and rejected
any equipped `GUNLEFT`. Separately, owning a sniper made `ITEM_FIST` load the
sniper's melee model. Removing that model substitution alone could not enable
supporting an off-hand gun.

The hold now selects the gun slot with a bare opposite hand. Grip acquisition,
barrel proximity, aim, spread, recoil, palm placement and grip feedback use that
slot. Controller roles still pass through the existing handedness mapping.
The support hand is drawn attached to the gun and its trigger, motion melee and
pickup/holster grip gestures are suppressed while supporting it.
With GoldenEye X enabled, either weapon slot draws the rig's own support
mesh and shares that weapon's GE-X support-position and rotation fits. An
off-hand weapon mirrors both gun and support mesh together. The original
GoldenEye gripping hand is used for original models. Support hands do not
request their own aim/crosshair, and their normal or scoped sight is hidden.

Unarmed uses the bare fist while an off-hand gun is equipped or queued. The
loaded melee model is refreshed when that choice changes. Once the off-hand gun
is holstered, owning a sniper selects the original sniper butt again.

## Automated validation

- `python port/tests/test_two_hand.py`: KF7 and rocket launcher in either slot,
  both handedness settings, controller selection, muzzle selection, supported
  aim/recoil, palm and side trim, pistols keeping wrist aim, grip release,
  distance hysteresis, tracking loss, dual weapons, watch/death and remote ticks,
  and replacing an already loaded sniper butt.
- `python port/tests/test_hand_reload.py`: reload ownership and ammo transfers,
  plus suppressing motion melee with a dominant hand supporting an off-hand gun.
- `python port/tests/test_v043.py`: production controller mapping and independent
  reload routing.
- Watch-grip and GoldenEye X model harnesses passed with Linux header wrappers.
- GE-X harness checks support-mesh visibility, absence of the original hand
  fallback, fitted pickup positions and support-fit controls for either slot
  and handedness. Two-hand checks cover the support-hand sight guard.
- Android `assembleDebug` passed; the debug APK signature verified.

## Headset playtest needed

1. Own a sniper, equip a KF7 in the off hand and select Unarmed in the dominant
   hand. Check that the dominant hand is bare. Repeat with the rocket launcher.
2. Bring the bare hand to the fore-end and squeeze its grip. Check feedback,
   support-hand pose, aiming and firing with the weapon hand's trigger.
3. Release the support grip or move the hand away. Check that the hand returns
   to its controller; holster the off-hand gun and check that the sniper butt
   returns. Repeat starting with the butt already loaded before equipping the
   off-hand gun.
4. Repeat in left-handed mode, and with GoldenEye X models enabled. Check the
   support palm, cuff and face culling in the headset. In Gun fit while
   supporting the weapon, check that the readout says SUPPORT FIT (GOLDENEYE X),
   sticks move the correct hand and holding the weapon hand's grip rotates it.
   Check that the gripping hand has no crosshair, with crosshairs enabled and
   while using a scoped weapon. Release the support grip and verify the usual
   independent aim behavior returns.
5. Check a dominant-hand gun with the off hand holstered, then two independently
   equipped guns. Check that the former supports and the latter fires separately.

The combined build c64e34c headset test exposed a legacy support hand and
crosshair on reversed holds. The follow-up extends the GE-X rig path and
fitting to the supported weapon slot, and suppresses aim/sights on its support
hand. Retest these visual changes in-headset.

## Follow-up to the ed74919 headset test

Reversed GE-X holds measured the weapon controller rather than the free
primary hand. The palm getter and compact-weapon pickup points now select the
free controller for either weapon slot. Reload arbitration uses that same
role, so a magazine grip cannot also acquire two-handed support.

Physical magazine pulling, belt pickup, held-hand rendering, insertion guides
and round ownership now work with the gun in either slot. With a lone off-hand
gun, its ammunition is also offered on the primary-hand hip. Native checks
exercise the full reversed reload sequence in both handedness modes, along
with partial magazines, drop refunds, holstered missing magazines, single
rounds and speedloaders. The two-hand harness uses the production palm getter
with a separated weapon controller to catch the original regression.

A separate report described automatic-weapon support-hand jitter without
identifying the weapon, model setting or whether it happened during firing.
The GE-X anchor followed its animated wrist while the weapon could stay fixed;
a native regression reproduced that dependency. It now maps the resting palm
through the current gun frame and rebuilds the complete support arm/fingers
from the resting chain, including the KF7. Checks cover 100 frames of joint
noise, mirrored holds, model fits and actual weapon movement. This fixes the
confirmed animation dependency; the reported headset symptom remains unverified.

For the next headset pass, equip a gun in the off hand with an empty primary
hand. Check two-handed acquisition/release, pull/drop its magazine, take one
from the primary-hand hip and insert it at the highlighted well. Confirm the
GE-X free hand follows the magazine without a second fist overlapping it.
Repeat with a pistol, rifle and single-round weapon, with body slots on/off
and left-handed mode. Fire sustained bursts with a supported KF7 and another
automatic, then compare with idle holding and with original models.

The `be2aaae` headset test confirmed insertion and both waist pickups, but
pulling an off-hand Klobb's magazine still failed. Its grab-fit sideways offset
was not mirrored, while its gun-fit origin was incorrectly mirrored. With
the shipped settings, the resulting grab point was 29.26 cm from the intended
mirrored point. Interaction points now mirror their model basis and saved
grab offsets while keeping the rendered gun origin. KF7's controller-relative
grab offset follows the same handedness rule, and grab fitting stores the
canonical offset so the fit works in either slot. The reload harness now loads
the shipped model fits instead of zeroing them, compares Klobb/ZMG/DD44 grab
points to the production gun matrix and exercises pulling plus saved-fit
round trips in both handedness settings. This regression fails at `be2aaae`
and passes with the correction; the new Klobb pull still needs a headset test.

## Slot category wheel and stick capture

Slot browsing now captures both sticks from hover, rather than waiting for a
sideways scroll. It clears movement buttons and turn input, including vertical
pushes, diagonals and swapped sticks. After leaving the slot both sticks must
return inside the turning dead zone before capture ends. Menus and fitting
keep their own input paths.

The hovered slot supplies its exact grip choices to the existing weapon-wheel
renderer: a wedge per choice, category tint, selected highlight, spinning model
in the middle and the shared VR overlay. It faces the eyes so shoulder slots
remain visible. Left/right scrolling, A/X, holstering and grip/throw ownership
keep their existing actions; the wheel shows the throwable pause prompt until
ready. The original full-category wheel remains available with A/X away from
slots. Native checks cover all four captured axes, release/centre behavior,
disabled slots, category snapshots and shared ring/model routing for 1..24
choices across both hands and frame buffers. Headset readability and preview
animation still require playtesting.

## Looked-at slot wheel and stick pointing

Headset test of the slot wheel: it only answered left/right, and it came up
in front of the eyes whenever an arm hung by the hip or brushed the belt
reaching for a magazine. The hand's own stick now points round the slot
wheel as the held-A wheel's does: clockwise from the top, past half way,
held a quarter wedge (at most 6 degrees) past its edge, the choice kept when
the stick lets go. The wheel, and with it stick capture and A/X stepping,
now needs the head to face the slot as well: within 40 degrees to come up
after 150 ms, 55 to stay (the wheel sits in front of the eyes, so a glance
at it can't drop it). The shoulders are exempt because they can't be seen.
A hand resting in a slot without a look leaves the sticks and A/X to the
player, and a grip still takes the slot's last choice. Native checks cover
pointing in every direction for 1..24 choices, the hold, arming after
centring, release capture, the gaze cone and its hysteresis, and production
polling with an unlooked-at hip slot. Whether 40/55 degrees suits real
looking-down still requires a headset test.
## Valid poses during controller occlusion (2026-10-10, 13:15 session)

The headset logcat records the offhand PP7's two-handed hold starting at
13:15:20.001. At 13:15:20.239 the physical left controller changes from
position to orientation tracking, recovering position at 13:15:26.912.
Its IMU telemetry reports `OutputValid=1.000` while `OutputTracked` falls
to 0.053 over the affected sample window; the right controller stays tracked.
Both visible hands are attached to the held gun, so rejecting its controller
updates freezes both until full tracking returns.

`update_vr_controllers` previously required both VALID and both TRACKED flags
before copying either VIEW or PLAY pose. It now accepts successful, active
action poses with both VALID flags, as OpenXR permits inferred poses during
tracking loss. Actual tracking quality is retained separately. Game-frame
snapshots and XR redraws use the valid updated poses, and supported-rifle aim
continues filtering usable poses. Starting a new support hold still requires
tracked controllers; an existing hold survives occlusion until grip release.
Invalid poses keep their last known location, and inferred velocities do not
trigger motion throw/melee. `[VR_TRACK]` logs state transitions and raw VIEW
and PLAY flags for a headset retest without continuous aim logging.

`python port/tests/test_controller_tracking.py` exercises the actual updater
with 300 changing inferred poses for each controller, missing validity bits,
inactive actions, locate failures, recovery, differing space quality and
velocity gating. `--source-ref v0.4.18` reproduces the original rejection.
Grip/redraw tests now include usable/inferred snapshots; two-hand tests verify
supported aim follows valid inferred motion and retains its direction only
when the pose is unusable. Frame-timing checks pass. The signed release build
passes with JDK 20. Headset retesting remains necessary, particularly when
the runtime's inferred position itself stops changing under occlusion.
