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
   support palm, cuff and face culling in the headset.
5. Check a dominant-hand gun with the off hand holstered, then two independently
   equipped guns. Check that the former supports and the latter fires separately.
