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
