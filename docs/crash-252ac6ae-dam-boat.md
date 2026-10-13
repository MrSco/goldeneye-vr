# Crash 252ac6ae: removed Dam speedboat

Report: v0.4.22, commit `909918e`, built 2026-10-12 23:49, Quest 3.
Crash time: `2026-10-13T15:39:22Z`. Both attached files contain base64;
the decoded app log and tombstone remain outside the repository.

## Evidence

The protobuf tombstone and app logcat agree on SIGSEGV / SEGV_MAPERR at
address `0x50`, with `x0 = 0`:

```
chrpropDeregisterRooms + 12
setupUpdateObjectRoomPosition + 60
0x49aaf0 (no exported function name)
MoveBond + 360
bondviewMovePlayerUpdateViewport + 880
lvlViewMoveTick + 284
bossMainloop + 1796
```

Native build ID: `b7285ba6b88a52d11dcad01be5a1af9c3fadcab7`.
No matching archived ELF was available; the unnamed frame was not symbolized
against a different build.

The player loaded Dam at local time 17:33:58 and boarded its north-jetty boat
at 17:38:45, 37 seconds before the crash. The tombstone's memory dump around
`x20 = 0xb400007d0b8c0350` identifies the object passed to the room updater.
Using the production 64-bit object layout:

| Field | Offset | Captured value |
|---|---:|---:|
| type | 3 | 3 (`PROPDEF_PROP`) |
| model ID | 4 | `0x122` (`PROP_SPEEDBOAT`) |
| state | 2 | `0x80` (`PROPSTATE_DESTROYED`) |
| flags2 | 12 | `0x20100` (boat's no-fall and tile-room flags) |
| prop | 16 | NULL |
| runtime flags | 108 | `0x10004`, including `RUNTIMEBITFLAG_REMOVE` |
| maxdamage | 128 | 4.0 |
| damage | 132 | 0.0 |

Thus the failed room update belongs to a destroyed, detached speedboat.
`objFree` clears the object's prop after releasing it, but leaves the prop
slot's old `obj` pointer until that slot is reused. The boat driver retained
only the prop pointer and considered a non-NULL `prop->obj` sufficient to
continue moving it. The next room update dereferenced `obj->prop == NULL`.

The boat constructor also zeroed the health threshold (`damage`) and never
initialized it. `objInitWithModelDef` does not supply that value. This allowed
the first damaging hit to destroy it; normal prop initialization uses 1000.

The supplied settings have body slots off and the log runs at 72 Hz. This
crash occurred in the existing release, before the current tracking and
slot-preview changes. It is not evidence of a Bean/120 Hz rendering fault.

## Fix included in the current batch

- Initialize the boat with the normal 1000 health threshold. It remains
  destructible; invincibility is not introduced.
- Retain the original stage-owned object alongside its prop. Check both
  sides of their association, prop type, destruction/removal state and model
  presence before boarding, driving, applying the seat or reporting a ride.
  Invalid ownership clears both references and ends the ride. Reusing the
  old prop slot cannot make an unrelated object become the boat.
- The shared room updater returns for NULL or detached objects. This guards
  the reported null dereference without allowing the boat to keep moving or
  seating its rider after removal.

## Validation

`python3 port/tests/test_dam_boat_lifecycle.py --verify-baseline` runs the full
production boat code and room updater with synthetic engine state under
ASan/UBSan. It verifies normal creation/boarding/driving, health initialization,
destruction and removal, a cleared prop, a reused prop slot, a lost model,
broken ownership, every public ride entrance, stage reset and detached room
updates. The baseline source reproduces the null dereference in
`chrpropDeregisterRooms` after boat removal; the fixed source passes.

Existing speedboat arithmetic, soft-collision and co-op collision regression
scripts pass. The speedboat test's real-ROM pad check is skipped because no
ROM is supplied. Debug and optimized release builds pass with the combined
tracking, body-slot, Bean-dispatch and crash changes.

No headset is connected. Device acceptance: board at the north jetty, cross
and disembark normally; destroy the hull while beside it and while riding,
including mid-crossing; verify that the ride ends without a crash, an invisible
boat carrying the player or movement of a subsequently spawned prop. Losing
the boat over water puts the player back on the north jetty; check movement
and collision there. Reload Dam and repeat
using both B boarding and the VR hull-grab gesture.
