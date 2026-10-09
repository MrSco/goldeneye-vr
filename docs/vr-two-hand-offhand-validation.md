# Supporting a gun in either hand

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
