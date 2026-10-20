# Co-op mission effects: issue #216

The host already runs guards and background mission AI. Before this change,
clients received guard poses, objectives, alarms, doors, text, cameras and
player combat, but several world effects of those scripts stayed on the host.
Control's two glass breaks are `object_destroy` instructions. Train's end
sequence issues fourteen `object_destroy` instructions, rather than using the
repeated-blasts trigger. Both need source-object state as well as explosion
visuals. The countdown is shared mission state, not the next-round lobby timer.

## Audit source and scope

All 20 retail mission setups were inspected against the production AI handlers.
The inventory below uses [n64decomp/007 at c4356466](https://github.com/n64decomp/007/tree/c4356466796c697dfd298010b9bed261f9ed8c6a/assets/obseg/setup),
with the US variant where the upstream has regional variants. Archives includes
literal byte arrays; the audit tool decodes those with the engine's opcode
lengths. Counts are source occurrences, not executions or confirmed player bugs.
No cartridge/setup data is added to this repository.

Reproduce with an existing checkout of that upstream:

```sh
python3 tools/audit_coop_mission_effects.py /path/to/007/assets/obseg/setup
```

| Mission | Shared effect instructions (source occurrences) |
| --- | --- |
| Dam | countdown: 1, monitor: 32, audio: 4, music: 1, vehicle AI (local): 3 |
| Facility | flags: 3, monitor: 28, audio: 5, music: 1 |
| Runway | move: 1, flags: 2, audio: 1, music: 1, vehicle AI (local): 2 |
| Surface 1 | flags: 3, monitor: 1, audio: 6, music: 1 |
| Bunker 1 | countdown: 1, flags: 1, monitor: 42, blast sequence: 1, audio: 25, music: 1 |
| Silo | countdown: 4, destroy: 1, flags: 1, blast sequence: 1, music: 1 |
| Frigate | destroy: 3, flags: 3, audio: 9, vehicle AI (local): 1 |
| Surface 2 | countdown: 1, destroy: 7, flags: 7, monitor: 1, audio: 7, music: 1, vehicle AI (local): 2 |
| Bunker 2 | flags: 1, audio: 1, music: 2 |
| Statue | countdown: 2, destroy: 7, move: 11, flags: 8, audio: 1, music: 1, vehicle AI (local): 1 |
| Archives | music: 1 |
| Streets | countdown: 4, destroy: 1, move: 2, flags: 1, music: 1, vehicle AI (local): 2 |
| Depot | music: 1 |
| Train | countdown: 1, destroy: 14, flags: 14, gas/fog: 1, audio: 20, music: 1 |
| Jungle | destroy: 3, flags: 3, music: 3 |
| Control | destroy: 2, flags: 13, audio: 3, music: 3 |
| Caverns | audio: 2, music: 3 |
| Cradle | countdown: 1, move: 1, flags: 1, audio: 3, music: 3, vehicle AI (local): 1 |
| Aztec | countdown: 2, destroy: 8, flags: 8, monitor: 4, gas/fog: 1, rocket: 1, music: 1 |
| Egypt | destroy: 2, flags: 8, gas/fog: 1, audio: 5 |

This identifies countdown use in nine missions, plus missing effects beyond the
reported Control/Train pair. The code fix applies through common handlers,
without hard-coding level IDs or duplicating their mission scripts.

## Synchronization

- `COOP_CLOCK` sends the countdown value, running state and mission visibility
  bits, plus gas progression/source and the repeated-blast sequence's state.
  It refreshes four times a second while changing, and every two seconds when
  idle; a join or host change forces a refresh. Watch, pause and damage HUD bits
  remain local. Clients retain blast timing for host migration but do not create
  a second blast sequence.
- `COOP_SCRIPT` names a local mission AI list and instruction offset. The client
  walks its own list to validate the instruction boundary and an explicit
  whitelist, then executes exactly one existing context-free effect handler.
  No received bytecode, branching, inventory changes, spawning, or actor AI is
  executed. Covered effects are destruction, movement to pads, flags/flags2,
  monitor banks, sky changes, rocket launch, script sound channels and music.
  The packets are reliable, host-only and checked against stage/world epoch.
- Durable script effects are retained in last-execution order on every peer.
  A joining player receives this bounded history with the delayed guard roster;
  a new host retains it. Repeated flag/move instructions retain their final
  order. Sounds/music are transient and are not replayed to late joiners.
  The 512-reference bound exceeds the number of durable effect instructions
  found in any audited retail mission; a custom campaign overflowing it logs
  and evicts its oldest effect.
- Glass has an explicit reliable host break event for guard bullets and other
  breaks outside `object_destroy`. Ordinary and tinted glass also participate
  in the existing world snapshot, including panes already freed on the host.
  Duplicate break events do not create shards twice.
- Damaging world blasts are sent by the host with a world-owner flag. A client's
  replayed destruction updates its object without creating a duplicate blast.
  Received world blasts do not remove a nearby host-owned grenade or report
  another hit; the host retains world damage authority. Solo and deathmatch
  ownership follow their existing paths.

The new message/action IDs and explosion flag are additive to protocol 23.
Existing packet sizes stay unchanged. Older builds ignore the new clock/script
messages and glass action, so both headsets need this fix to gain full coverage.

## Remaining limits found by the audit

This is not a claim that every campaign event is fully synchronized:

- Vehicle path/speed and aircraft rotor AI still simulate locally in prop ticks.
  They do not have authoritative transform/AI snapshots like guards or the driven
  tank. This affects the missions marked `vehicle AI (local)` above. The Aztec
  rocket trigger now travels, but a late join does not reconstruct its precise
  in-flight pose from the original launch time.
- Guard gun tracers/wall sparks and flying guard grenades remain the documented
  co-op limits. Glass breaks are now explicit, independent of those visuals.
- Loose-object physics, held objective-item ownership, and per-headset dropped
  guns retain the existing co-op rules. This does not add general object physics
  replication or replay a past soundtrack for someone joining midway through it.
- The upstream inventory covers retail scripts. ROM hacks using additional
  context-dependent opcodes need a separate setup audit; those opcodes are not
  allowed through the script-effect receiver.

## Verification and headset follow-up

`python3 port/tests/test_coop_world.py` executes the production packet readers,
senders, effect handlers, glass removal, snapshot handling and explosion ownership
paths with ASan/UBSan. It checks malformed/stale messages, HUD masks, countdown
start/stop, gas, script instruction boundaries, flags, movement, monitors,
transient audio, durable late-join restoration, bounded history, duplicate glass,
world blast deduplication, nonzero host slot, and solo ownership.

The Android debug build and 31 JVM tests pass. Existing co-op gadget/tank/mine,
combat, intro and debrief/save checks also pass. GCC 14 requires a scoped
`-Wno-error=incompatible-pointer-types` compatibility flag for the existing
legacy C fixtures, and the gadget/tank fixture needs `-lm` on Linux. Neither
requires a tracked source/test workaround.

No Quest or ROM is available in the cloud workspace, so these are code/native
checks, not two-headset mission playthroughs. On two headsets, repeat with host
slot 0 and a migrated/nonzero host, then join/rejoin midway:

1. Control: both scripted glass panes during Natalya's defense; also shoot a
   pane and confirm both visibility and shot collision agree.
2. Train: countdown appears and hides on both; all tagged train objects and
   explosion visuals change once during escape, without consuming a nearby
   host-thrown grenade.
3. Silo and Bunker 1: countdown expiry and repeated blasts; Statue/Streets,
   Cradle and Aztec: the other countdown start/stop paths.
4. Surface 2/Jungle/Frigate: scripted demolition and flag-controlled objects.
   Statue/Runway/Cradle: moved objective objects. Dam/Facility/Bunker 1/Surface
   1/Aztec: changed monitor banks.
5. Egypt: non-damaging gas/fog; Facility gas objects: source/fog progression and
   local damage. Train/Aztec: the instant sky/fog change.
6. Archives/Depot/Caverns and the other listed missions: scripted music and
   sounds while connected; joining midway must not replay all past audio.
