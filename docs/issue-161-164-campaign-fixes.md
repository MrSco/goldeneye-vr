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

## #164 retest: works once, then stuck after reload (2026-10-13)

At 02:11 UTC, after the maintainer asked the reporter to try the newly
published v0.4.21, GknightPR reported: "Jungle worked once, but I re loaded
the level and still stuck, same for control ,caverns and cradle."
[Latest report](https://github.com/MrSco/goldeneye-vr/issues/164#issuecomment-6136684271).
The reporter did not restate the installed version or confirm that both
headsets had updated. Treat the clearance patch as unaccepted; #164 is open.
The earlier Natalya stall was explicitly in co-op, not solo.

Code inspection found that the placement solver recomputes reservations on
each call and the stage loader resets the enabled-slot cache on every load.
This does not establish the runtime cause or exclude other reload state.
The native spawn fixture passes but supplies model bounds and room membership;
it does not run a mission reload, the intro or real headset movement.

Branch `investigate/164-coop-reload`, based on released v0.4.21 (`8683dab`),
adds a rate-limited `coop-move:` diagnostic to failed local co-op movement.
It records mission, slot, camera mode, source/destination, floor tile/room,
player radius/height, line and volume results, floor-locus results and the
blocking prop's type, model/character ID and pad. Volume `-2` means the line
failed and the volume check was skipped; `-1` means volume passed. The
movement decisions and their short-circuit order are preserved.

Use both headsets on this diagnostic build. Start Jungle, attempt to walk,
abort and restart it in the same session, then attempt to walk again. Repeat
Control, Caverns and Cradle; record which headset/slot is stuck and whether
Natalya moves on the host. Capture `gevr.log` from each headset immediately
after the stuck attempt (also available in the app's existing log report).
The diagnostic build is not a claimed fix or a new release.
