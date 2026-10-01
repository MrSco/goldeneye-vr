# v0.3.7 ownership, HUD and host-delay followup

Continued in the existing playtest-logging-feedback-731f59 worktree, preserving the
prior v0.3.7 release and its symbols. Local release build only; no push, deployment,
report upload or headset installation. App 0.3.7 / 46 and wire protocol 14 unchanged.

## Changes

- Radar health and armor arcs move down two scaled HUD pixels, keeping their
  independent rotations and clearance. Inspected the latest headset radar and
  launcher screenshots read-only from /sdcard/Oculus/Screenshots.
- The multiplayer launcher reserves the outer help line below its fixed footer.
  A minimum positive child height avoids ImGui's negative-height sizing behavior.
- Actual warmup-to-match transition clears every player's kills, per-victim table,
  suicide/team penalties, score bank, flag count and elimination order, plus departed
  team points. Late-join snapshots and repeated in-progress phase events retain scores.
- Dominant A selector/cycle no longer contains Empty hand. Native Fists remains.
  Off-hand X still offers Holstered. Depletion prefers another usable item, otherwise
  dominant falls back to Fists and off hand to Holstered, changing only that hand.
- Pickups now grant ownership independently of equipment. A second copy stores a
  same-weapon native inventory pair and enables that firearm in both selectors;
  one copy cannot be equipped in both hands. This includes rifles in VR. Copies are
  capped at two, with later pickups granting ammo. New or duplicate guns preserve
  either equipped hand and pending manual selections. Only a first usable firearm
  while dominant is Fists/unarmed automatically equips dominant, after ammo is granted.
  Having another usable owned firearm prevents this exception from overriding a
  deliberate selection of fists. Flag equip and remote-mine detonator remain native.
  Host Off/Doubles/Any restrictions still apply; flat native behavior is retained.
- Multiplayer character selection chooses the closest shared sleeve asset in the
  actual cuff selector for both gun/watch hands. Formal Bond, jungle uniforms,
  snowy uniforms, boiler/science uniforms and other blue sleeves use available
  variants. These are shared Bond hand meshes, not exact character-specific arms.
  Single-player stage sleeves remain unchanged.
- Host hit equalization defaults On, cap 50 ms. Each host hit against a connected
  client queues for min(smoothed target ENet RTT / 2, cap). Cap 0..80 ms and On/Off
  controls live in launcher Host > Match and multiplayer Pause. Settings persist on
  that headset; migration clears queued hits and uses the new host's local preferences.
  Existing queued hits respond to cap reduction or disabling at the next poll.
  Muzzle feedback, aiming and motion are not deferred. Unknown/stale RTT, self damage,
  and unowned world explosions have no added delay. Ping still means RTT to the host;
  the host correctly displays 0 ms. Stats HUD shows the effective delay per target.
- Preallocated 256-entry queue dispatches earliest due first with FIFO ties. When
  full it logs once and dispatches the oldest early rather than overwriting damage.
  Respawn, departure/reuse, round resets/phases, disconnect and migration clear
  stale hits. Incoming reports are processed before host queue dispatch in each poll.
- Non-explosive hit reports remain eligible for 150 ms after death observed by the
  host. Owner movement drives remote death observation rather than delayed copy
  animations; older unsequenced movement is ignored by this hit-life observer, and
  alive movement clears the death timestamp. Explosives thrown before death remain
  dangerous. Client damage forwarding/receiving now also supports warmup practice.

## Limits of the latency plan

The attached plan overstates the existing rejection behavior and claims complete
elimination of host advantage. Prior code did not reject every dead shooter's report.
This change reduces firefight dispatch timing advantage and bounds late reports;
it does not establish full fairness, exact simultaneity or lag compensation.
Protocol 14 has no shot timestamp or hit-report life ID. The 150 ms rule is based on
host-observed death/arrival time, not proof of when a trigger was pulled. Position
rewind is absent and target health remains applied by its owner, as in the prior build.
Queue deadlines run on polls; render cadence and network jitter still matter.

## Automated validation

- 42 native tests pass, executing production serializers, rules, audio adapter,
  hand lists, actual pickup ownership insertion, per-hand requests/depletion,
  actual cuff selector, host hit queue/arbitration and actual loaded-round start.
  Checks include target-specific 10/30/50 ms timing, dynamic caps/toggle, stale RTT,
  overflow with no overwritten shot, mutual lethal dispatch, 150 ms boundary,
  explosive exception, movement ordering/wrap, slot/round/migration invalidation,
  friendly fire, pickup preservation, copy limits, native Fists and score boundaries.
- 8 launcher ImGui checks pass. Footer bounds include the outer help row at three
  heights with scaled fonts, long rosters and overflowing body/status content.
- 6 Worker tests pass; Worker TypeScript check passes. No Worker deployment.
- 10 Android lobby-client and 2 crash-exit-policy tests pass (forced fresh execution).
- assembleRelease succeeds; APK version and v2 signature verified. Packaged native
  build ID matches the preserved unstripped ELF, which includes debug information.
- git diff --check passes. Legacy native compiler warnings remain.

## Release artifact

- APK: `C:/Users/Occor/Documents/other_projects/goldeneye-vr/.claude/worktrees/playtest-logging-feedback-731f59/android/app/build/outputs/symbols/8a640467ed3f0a4a5e2513c903eaafb25d8bb5e5/gevr-v0.3.7-release.apk`
- Bytes: 21564614
- SHA-256: `2442b1e8a5060daab4b30dd2f097f935b20ca2e9f15ff94065aff7fee745432c`
- Native build ID: `8a640467ed3f0a4a5e2513c903eaafb25d8bb5e5`
- Matching unstripped libgevr.so, manifest, automated logs, Android XML results,
  this report and a source snapshot are alongside the APK.

## Headset acceptance still needed

1. Verify both gauges sit lower and follow the radar without bottom overlap;
   launcher Host/Join footer and help line stay visible at normal menu scale.
2. Kill/self-kill in warmup, start a real multiplayer match and confirm all
   individual/team scores are zero. Late join during a scored round must retain it.
3. First firearm from Fists auto-equips dominant only. Rifle/pistol/grenade holdings
   and pending A/X changes survive new/duplicate pickups. Second PP7 unlocks both
   hands, one PP7 cannot be equipped twice, and dominant never offers Empty hand.
   Test Off/Doubles/Any, depletion, handedness, flag and mine detonator exceptions.
4. Compare Bond, scientist, jungle and snow character sleeves in both hands and watch;
   verify single-player stage outfits remain correct.
5. Two or three headsets over the internet: compare equalization On/Off, cap changes,
   Stats per-target delays and repeated simultaneous duels. Confirm immediate muzzle
   flash/audio/haptics; confirm a host killed by an incoming shot can still trade
   with its already queued shot. Check respawn, disconnect, migration and team FF.
   Grenades/mines already thrown must still damage after their owner's death.
6. Continue earlier crash, ammo-box, voice routing, pause tracking and HUD acceptance.
   Automated checks do not establish crash-free multiplayer or eliminate host advantage.
