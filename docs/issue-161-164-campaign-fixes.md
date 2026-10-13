# Campaign fixes: #161–#164

Implemented on the v0.4.15 baseline. No release or issue closure is implied.
Both co-op headsets should run the fixed build for validation.

- **#161, Surface II:** remote-player hands follow the local hand's exclusion
  for mission throws. Equipping a mine no longer creates a settled world mine
  for the mission script to mistake for an incorrect placement. Guns retain
  their held models; deathmatch mines retain theirs. Actual throws and their
  authoritative settle events use the existing path.
- **#162, Bunker II:** the authoritative collected-object opcode includes the
  host's real inventory and client-held tags regardless of the nearest-player
  script context. Clients report tags used by collect/deposit criteria and
  stage-local collected-object opcodes, preserving exact object identity.
  Duplicate script references consume one entry. All twenty retail missions
  fit the existing eight-tag packet (largest candidate set: five in Silo).
  The document comparison still requires both documents.
- **#163, Jungle:** exclude `PROP_TYPE_VIEWER` bodies from the nearby-character
  enemy scan. VR keeps Bond's model in the character pool during gameplay;
  the old scan could select him or a teammate as Natalya's guard preset.
  Jungle's combat list aims, switches to fire, then waits for attack completion
  or the target's death. Native tests prove the player-selection defect and
  that live guard selection still works. Whether this resolves the reporter's
  exact stall requires solo and co-op headset testing.
- **#164, co-op starts:** anchor the party at the intended first gameplay
  pad; exclude co-op from extra deathmatch ammo-pad selection. The shared
  search tries nearby directions/distances, checks floor and scenery collision
  for a 30-unit radius, avoids ledges/large floor steps, and reserves 65-unit
  separation between teammates. Earlier slots are recomputed deterministically
  rather than depending on player loading order. Late joins use the same
  search against current teammate positions. A failed search logs a diagnostic
  and retains the original safe anchor rather than selecting distant loot.

## Automated evidence

Run from the repository root:

```powershell
python port/tests/test_campaign_items.py
python port/tests/test_campaign_items.py --baseline mines --baseline-ref d048e74
python port/tests/test_campaign_items.py --baseline documents --baseline-ref d048e74
python port/tests/test_campaign_items.py --baseline natalya --baseline-ref d048e74
python port/tests/test_coop_start.py
python tools/gevr_campaign_audit.py <owned-USA-ROM-path>
```

The baseline modes confirm the respective regression fails against `d048e74`.
The tests extract production functions/opcodes rather than duplicate their
rules. Inventory checks cover all four pickup owners and script contexts,
one-document negatives, inventory removal, direct collect/deposit objectives,
duplicate references, packet encoding/decoding and unchanged solo semantics.
Mine checks exercise both hands, all excluded mission throws, trigger states,
weapon transitions, ordinary guns and deathmatch controls.

The spawn harness runs production tile traversal, circle collision, floor
planes and placement search. Its scenery obstacle is a fixture, not a loaded
game model. It checks blocked alternatives, narrow corridors, four-player
separation, loading order and failure without corrupting the anchor.
The owned-ROM audit checks all twenty campaign entrances for two, three and
four players, with start-wrapper results matching the shared solver. All
additional starts were 70–140 units from their entrance in the floor audit.
It writes no ROM bytes and does not validate loaded scenery or ceiling height.

Existing gadget/settlement, tank, co-op collision, soft collision,
reinforcement and co-op spawn replication regressions also pass.

## Reported headset result

2026-10-09: the user tested Jungle in solo and reported Natalya following,
shooting, walking and crouching normally. She subsequently died; the user
attributed that to their own play rather than a stall. This validates the
observed solo movement/combat behavior, not a full mission completion.
Difficulty, location and Bodies stay setting were not supplied. Co-op
behavior and the remaining mission checkpoints are still unverified.

## Headset checks still required

No Quest was connected during implementation. Test at least two headsets and
repeat starts with three/four sessions when available:

1. Surface II: either player equips a mine in either hand without a trigger;
   objectives remain pending. Throw onto the helicopter and onto incorrect
   geometry separately; confirm the appropriate completion/failure.
2. Bunker II: collect both documents on the host, both on a client, then split
   them both ways and in either order. Move the nearest-player script context
   between teammates. A single document must not complete the comparison.
3. Jungle: solo and co-op, approach enemies with Bond/teammates closest to
   Natalya. Confirm she attacks guards and resumes following; include Xenia
   and the final caves, with Bodies stay enabled and disabled.
4. All twenty missions: start with two/four players and verify each can move,
   turn and leave the entrance. Prioritize Train, Control, Caverns and Cradle;
   check scenery, doors, narrow/crouched starts, and ledges. Exercise a late
   join while the party is in a narrow corridor.

Keep issues open until these gameplay results are confirmed.

## #164 follow-up: stuck starts in v0.4.20 (2026-10-12)

The reporter confirmed v0.4.20 for the repeat report: Jungle's first player
cannot move while the second can; Train, Control, Caverns and Cradle still
leave both stuck. The original all-mission audit checked floor geometry,
and its scenery fixture replaced the game's line and volume routines. It
did not establish that loaded scenery was clear on two headsets.

Three defects remained in the shared placement code:

- Slot zero bypassed clearance entirely. Every slot now goes through the
  same deterministic search; the original pad is retained when clear, and
  the first player's repaired location is reserved for the other players.
- The line/volume calls passed height bounds `0, 1`, which disable the
  retail routines' vertical filtering. Props above or below the player
  could reject all alternatives. They now test the floor-relative interval
  from 30 to 180 units, with the existing 30-unit radius.
- Retail volume collision tests polygon edges and can report clear when
  the cylinder is wholly inside a large prop. Placement now also tests the
  centre against those loaded footprints. If the entrance itself is inside
  scenery, the path search temporarily ignores only objects/doors containing
  that entrance, restoring their collision before checking the destination.
  Unrelated doors, objects and floor boundaries continue to block placement.

The same solver serves late joins. Its existing search radius, floor-step
checks, teammate separation and failure diagnostic remain in effect. An
exhausted search still leaves the anchor intact; it does not guarantee a
placement for every possible runtime obstruction.

Validation:

- `python port/tests/test_coop_start.py` now extracts the production line,
  volume, polygon-interior and collision-toggle functions, alongside floor
  traversal and placement. Only model bounds and room membership are fixtures.
  Checks include all four slots, overhead/below-floor scenery, a partly
  obstructed first pad, escape from a large enclosing prop, an unrelated
  blocking door, collision restoration, load order and safe search failure.
- `python port/tests/test_coop_start.py --baseline-ref 004d13e` fails on
  v0.4.20: a huge enclosing prop is incorrectly accepted as a clear spawn.
- The owned-USA-ROM floor audit passes for all twenty missions, including
  slot zero and wrapper/solver agreement for every slot. It still does not
  load scenery models or exercise real movement on headsets.
- All 77 multiplayer regressions and the co-op, soft collision and movement
  slide suites pass.
- Android `:app:assembleDebug` succeeds with `JAVA_HOME` explicitly set to
  `C:/Program Files/Java/jdk-20`. The test APK is
  `android/app/build/outputs/apk/debug/app-debug.apk`.

This repairs reproduced placement defects; confirming the reporter's exact
symptom still requires both headsets running the patched build. Prioritize
Jungle, Train, Control, Caverns and Cradle, then Facility's crouched entrance,
the remaining missions and late joins. No APK was installed during this work.

## #164 intro handoff fix (2026-10-12)

The newest v0.4.21 report says P1 cannot move on Dam, Facility, Runway and
Jungle while P2 can; neither can move on Control, Caverns and Cradle. Jungle
worked once and then failed after a reload. Train remains in the original
report, but was not included in that latest retest. P1/P2 does not establish
whether the difference is host role, slot, character or position.

PR #176 identified the intro as a hypothesis and added blocker diagnostics.
This follow-up reproduces a concrete defect in its production player-tick
path: `chrTick` moves the intro body's root position and floor tile, and
`playerTick` copies both into the collision state after the spawn search.
Co-op retains that body after the swirl; solo removes it. Placement clearance
therefore does not establish clearance at the moment co-op gameplay starts.

During the co-op intro, fade and swirl, the animated body's position/tile
now stay separate from gameplay collision. On normal or skipped completion,
the retained body is reconnected to the gameplay position and switched out of
its intro action. Camera/portal positions, collision bounds and previous
position are refreshed, and VR locomotion resets at the handoff. Missions
without a swirl use the same handoff. An owner correction or late-join
placement received during the intro is preserved. Solo, death and scripted
ending cinematics retain their actor-driven collision behavior.

The rate-limited `coop-move:` diagnostics from PR #176 are included so a
remaining failure identifies its actual line/volume/floor or prop blocker.
`coop-intro:` records the owner's gameplay position and tile at the handoff.
No network protocol or release version changes are required.

Validation on this Windows machine:

- `python port/tests/test_coop_intro.py` passes with extracted production
  cinematic ticks, camera advance, body removal, collision bounds and handoff.
  The animation fixture deliberately displaces the visual body and loses its
  tile. Tests cover all four slots, normal/skipped completion, extra passes,
  three consecutive loads, a late-join correction, no-swirl missions and
  solo/scripted/death controls. The same test with `--baseline-ref 8683dab`
  fails when the animation replaces the checked collision position.
- The existing co-op start, collision, spawn replication, items, debrief,
  soft collision, edge slide, campaign items and frame timing suites pass,
  as do all 77 multiplayer regressions and 30 Android release unit tests.
- The owned-USA-ROM audit passes for all twenty mission entrances with
  two/three/four-player floor placements. This is a floor audit, not a
  headset test of loaded scenery, animation assets or movement input.
- `:app:assembleRelease :app:testReleaseUnitTest` passes with `JAVA_HOME`
  explicitly set to `C:/Program Files/Java/jdk-20`.

This fixes the reproduced intro-state defect. The exact reported gameplay
failure remains unconfirmed until both headsets run the patched release APK;
keep #164 open. Prioritize a full intro and a skipped intro on Dam, Facility,
Runway, Jungle, Train, Control, Caverns and Cradle; leave each entrance with
both players, abort/reload each mission at least twice, then swap the host.
Check Jungle Natalya separately and include a join during another player's
intro. Capture `coop-intro:`, `coop-move:` and `spawn:` lines on both headsets.
