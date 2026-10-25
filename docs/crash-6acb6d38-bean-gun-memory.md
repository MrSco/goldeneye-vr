# Crash 6acb6d38: retained Bean weapon geometry

Quest 3, v0.5.14, build `f7109317`, built 2026-10-23 20:42. The player reports
Train multiplayer slowing down and crashing after using the rocket launcher.
The attachment is base64-encoded gzip; the decoded personal log remains outside
the repository.

## Evidence and diagnosis

The exit is real. The app logcat includes the crash summary even though no
separate tombstone was attached. SDLThread 6995 in pid 6955 receives SIGSEGV,
code SI_TKILL, at 01:13:33 local time (+0100). The watchdog explicitly sends
that signal after eight seconds without a retrace. This is not a recorded
invalid-address memory fault. The exit-history entry is 00:13:48Z, about 15
seconds after the native signal. Build ID:
`edafa50ffd7a72cc7dba6e75e4eac7c585e8049f`.

The diagnostic stack at 01:13:32 is in `__freeParam` / `alEnvmixerPull` /
`alAudioFrame`. At the fatal signal the thread has reached `gevrBeanBeginFrame`
from `dynGetMasterDisplayList`. These samples show movement between two parts
of the frame loop, not a demonstrated endless loop in either function.

The missed lead was resource growth before the watchdog:

- Bean, Community Edition and ragdolls are enabled. The log contains 796
  first-person gun registrations, including **307 launcher loads**. Each
  launcher reports 4,266 triangles, four textures and 16 registered draw nodes.
- In the final Train stage alone, from 01:08:07, there are **340 gun loads,
  including 130 launchers**, before termination five minutes later. Weapon
  buffers are reloaded on switches and respawns, including bots' hands.
- Audio submissions have gaps of 1.91, 1.09 and 1.82 seconds near 01:13:00,
  then 3.74 seconds at 01:13:22 and 7.71 seconds at 01:13:32.
- The native crash summary records `Vm-Swap-Kb: 696564` and
  `Rss-Hwm-Kb: 902976`. This is substantial memory pressure and accompanies
  progressively longer stalls.

`gevrBeanModelApply` rebuilds the fitted geometry every time the native gun
buffer is loaded. The launcher also builds a spent variant, without the round
in its tube. Lists and vertices are appended to `s_alloc`; retained meshes add
their own vertex/index/run storage and backend handles to `s_rigidMeshes`.

`gevrBeanForgetModel` and `gevrBeanForgetModelStorage` used to remove only the
swap/lookup entries. **Neither released the geometry or meshes.** Every weapon
switch kept another obsolete copy until the entire level reset. The fixed-size
registries count allocations rather than bytes, so memory can grow well beyond
a gigabyte before those limits stop registration.

## Production-path reproduction

`python3 port/tests/test_bean_gun_lifetime.py --verify-baseline` calls the real
model registration, emitter, mesh builder, forget paths, frame lifecycle and
reset. Only mesh decoding and external engine/backend services are stubbed.
It supplies an authored 4,266-triangle mesh with round-tagged triangles, so both
loaded and spent launcher paths run. No ROM or XBLA files are required.

The fixture tracks requested live heap bytes, including retained mesh copies:

| Launcher loads in one buffer | Unchanged main | Fixed |
| --- | ---: | ---: |
| 1 | 6,016,440 | 6,016,464 |
| 21 | 112,445,240 | 21,980,856 |
| 301 | 1,602,448,440 | 21,980,856 |

The original code retains eight more allocations and two more meshes per
reload. The fix settles at 32 allocations and eight meshes: the current load
and three recently retired generations. After forgetting the weapon and aging
the pending frames, its registered allocations and meshes return to zero.

These are measured synthetic-mesh values, not measurements of the player's
actual launcher assets. They establish the production retention defect and its
scale. Together with the reported reload counts, swapping and stalls, it is a
concrete explanation for the watchdog termination; a Quest replay remains
necessary to confirm the full reported scenario.

## Repair

Each first-person gun registration owns a group of ordinary allocations and
retained meshes. Swap entries count references to their group. Forgetting by
header, or overwriting native file storage, retires the group once its last
entry is removed. Both APIs can be called during the same load without
retiring it twice.

Collection waits for **three frame beginnings** after retirement, matching the
existing two-frame arena/retained-cut-mesh lifetime. This preserves display
lists and XR redraws that still refer to the preceding frames. Collection first
calls `gevrStaticMeshFree` to release backend handles and retained mesh buffers,
then frees the emitted lists/vertices and compacts the registries. Current guns
and level-owned scenery are preserved. A level reset frees everything,
including pending retirement metadata.

The repair is scoped to first-person gun registrations. Character skins, cut
caps and stage scenery keep their existing ownership. No version or network
protocol change is needed.

## Existing-fix check and validation

Fetched main is `bdeb44dc`. The header/storage forget functions are identical
on all other current remote branches, including the heads of PRs #220–224, and
none releases weapon geometry. PR #223 clears cut-border caches between levels;
PR #222 repairs music buffers. Neither addresses weapon reload retention.

The focused suite runs under ASan/UBSan and checks 301 repeated launcher loads,
owner/storage/unsupported-model invalidation, readable old lists through both
preceding frames, backend teardown, frame-counter wrap, 16 active hand buffers
switching between two weapons, unrelated persistent scenery, and repeated level
reset with live and retired groups. Unchanged main fails the memory-growth
check. The full Bean suite also passes under ASan/UBSan; it uses the existing
GCC pointer-warning compatibility wrapper and disables leak detection for its
older fixture allocations. The focused suite checks that its tracked live
allocation bytes reach zero after reset.

The Android debug APK builds and passes signature verification; all 31 JVM tests
pass after a forced fresh run. No Quest or ROM is available here. The reported
`f7109317` commit is absent from the current remote;
the latest published release is v0.5.13. No matching APK/ELF was supplied, so
unnamed instructions have not been symbolized using a different build.
