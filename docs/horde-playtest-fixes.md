# Horde two-player playtest fixes (2026-10-17)

This changes ammo, remote-player combat, avatar posing, corpse presentation and
Horde doorway recovery and initial-party admission, plus Bean's articulated
prop/aircraft replacement.
The batch also includes a candidate fix for the separate XBLA Facility
wall/floor flicker report; its exact scene still needs a headset retest.
All peers need protocol 23. No release version is bumped.

## Behavior and causes

- **Ammo:** Breaks granted three ammunition types while enemy guns were marked
  undroppable. Shared death crates now supply owned ammo pools, with a bounded
  24-object pool, 60-second expiry, reliable snapshots and owner grant counters.
  A guard generation drops once; despawn replacements and repeated lethal-hit
  reports cannot produce extra crates. Pickup requests carry the full generation
  ID and epoch, validate distance/doors/floor connectivity, and consume a crate
  once. Drops and counters survive host migration and late-join snapshots.
  Shop ammo refills and repeated weapon purchases are rejected. A new gun gets
  one magazine; the starting PP7 has 42 starting rounds. See the current amounts
  in [Horde mode](horde-mode.md). Special rounds arrive once per four pickups to
  avoid explosive multi-kills returning one explosive per victim.
- **Remote arms:** `playerTick` puts a viewer's prop at eye level, but its nested
  `chrTick` temporarily moves that prop to the animated pelvis. Wrist targets
  now use `field_488.collision_position`, the stable movement/eye anchor.
  Shoulders and elbows already rotate; the incorrect anchor lowered the entire
  reach target. Native bone lengths and the existing full arm solve are kept.
- **Joining-player damage:** Guard muzzle matrices are built in the observer's
  view. Switching to the target player's context before converting those
  matrices previously produced a different world muzzle. The firing context now
  keeps the observer's inverse view. Remote copies' damage-flash timer also
  cannot block hit probability: only the owner's live damage path applies
  immunity. Host targeting still chooses living players and reacts to whoever
  shot the guard; downed players remain excluded.
- **Player two spectating wave one:** The network marks a Horde match in
  progress during map loading. The generic load acknowledgement treated a ready
  launcher party member as a mid-round arrival. The starting party now stays
  active in either load order, preparation waits for both loads, and wave one's
  quota counts both players. New connections use the Horde director's phase:
  preparation and intermission admit players; combat/defeat arrivals spectate.
  A spectator who finishes loading during a break is admitted in that break.
  Duplicate load acknowledgements and migration returns preserve active slots.
- **Bodies:** Physics corpse visibility was cached at simulation time, before
  the current rendered camera/room visibility was established. It is now
  evaluated per rendered view. After capture, native actors stop rebuilding
  duplicate poses, attachments and hit lists while their cleanup timers continue.
  Moving-prop collision selection skips guards and weapons before scanning its
  collider pool. Bodies stay remains **per headset** when ragdolls are enabled;
  the host controls enemy lifecycle, not another player's cosmetic corpse count.
  Without ragdolls, online animated-body retention remains disabled.
- **Doorways:** Horde requests a blocking door within 220 units once per second,
  using native swing/open behavior and avoiding repeated swing changes while it
  opens. Unseen stalled guards get a fresh chase path after six seconds and
  replacement after eighteen; combat positions with a clear route to a living
  player remain intact. This does not teleport enemies through visible doors.
- **Runway plane:** Rigid Bean props can have a hull on a non-root matrix with
  rotor lists encountered first. Body selection now considers the largest
  non-simple near list when no fitted root body exists, and distant body alternatives on that same matrix
  retain the replacement hull. Native propellers remain when no replacement
  rotor geometry exists. The fix applies to articulated rigid props generally.
- **XBLA Facility walls/floors:** A separate report reproduces with AI fill on
  or off. The room decal classifier ignored coincident faces with the same
  image unless their average vertex brightness differed substantially. That
  leaves differently mapped copies, or equal-brightness colour changes, in
  ordinary opaque depth mode. The loader now recognizes changed UV mapping or
  vertex tint on exact, equally wound coincident faces and gives the later
  layer decal depth. Both surfaces remain. Equivalent repeats, adjacent
  triangles and opposite-winding sheets keep their previous classification.
  This is a reproduced renderer failure class, not confirmation that it
  explains every surface in the user's Facility report. The earlier material
  census lists eight coincident non-decal groups there, but contains no UV/tint
  or scene visibility evidence to establish that they match this case.

## Validation and limits

Focused native regressions cover the production Horde adapter and transport,
45 simulated minutes, bounded pickup reuse, shared/idempotent pickup and grants,
closed doors, host takeover, both-player targeting and owner damage packets,
different observer/target cameras, stomach/chest/overhead arm poses with a
pelvis-position prop, and an articulated hull with a distant LOD. The ragdoll
fixture switches visible rooms without a simulation tick before drawing.
The admission regression runs the production launch and load handlers in both
host/client load orders, checks first-wave scaling, malformed/duplicate load
acknowledgements, combat arrivals, break admission and migration returns, and
preserves campaign/drop-in and deathmatch spectator behavior.
The added room-layer regression failed on the old loader and checks changed
UV phase, repeat count, equal-brightness tint, reordered equivalent corners,
shared-edge neighbours and opposite-winding sheets. The emitter check verifies
base-before-paint order and opaque decal depth on the batched float-world path.

Completed checks:

- Android `assembleDebug testDebugUnitTest`, repeated after the final source
  edits: 30 JVM tests, zero failures/errors. Debug APK signature verified.
- `port/tests/test_horde.py`, `test_coop_combat.py`, `test_coop_spawns.py`,
  `test_avatar.py`, `test_bean.py`, and `test_ragdolls.py` passed. The final
  Bean renderer fixture was also rerun after the prop-selection refinement.
- All 34 headless network-core regressions passed after the admission fix,
  including the production launch/load handlers. The Horde suite includes
  the new admission check without requiring the Windows Steam Audio adapter.
- Bean (including the final Facility candidate checks), avatar/combat and
  real-Jolt fixtures ran with address/undefined-behavior
  sanitizers. Linux native fixtures used the environment's GCC compatibility
  include setup; no ROM or headset was attached.

The synthetic map and models do not establish Quest frame time or final visual
fit with the user's ROM/Bean assets. Repeat the original two-headset test with
both players attracting fire and collecting drops, a long 48-body Bean run,
doorway approaches and the Runway plane at near/far distances. Record CPU/GPU
frame time if the body slowdown persists. Ammo quantities and the 24-drop limit
need sustained two/four-player balance testing.
For the XBLA report, revisit the affected Facility walls/floors while moving
and turning, with AI fill off and on. The user's assets and headset are absent
here, so the report remains open for that visual acceptance check.
