Jolt C interface from [Perfect Dark CE](https://github.com/Chesluk-Industries/Perfect-Dark-CE),
Stable tag, commit `e33826da88e929e782e22af1a62278e2de1d5cb6` (2026-10-13).
MIT notice: [../../licenses/perfect-dark-ce.txt](../../licenses/perfect-dark-ce.txt).

The backend is kept separate from GoldenEye's compiler definitions. Local
changes: 48 ragdoll slots (the maximum Bodies stay option), two worker threads
on machines with more than two cores. Units are centimetres, Y up.
GoldenEye integration lives in `port/src/gevr_ragdoll*`, not this directory.
Jolt v5.6.0 is pinned by commit in `cmake/physics.cmake`; its MIT license is
also shipped in the APK.
