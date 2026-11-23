# Tactical Workshop and loadout presets

The prototype uses `GEVRPRESETS1` for Workshop builds. Keep the existing file format so players can move builds between the game and the Windows Loadout Editor. Each record identifies the root weapon by its catalog ID and lists the part assigned to each mount path. A dash identifies an empty mount. The codec leaves compatibility and named-preset policy to the shared gear catalog.

This draft starts with the native preset codec. The Workshop and mission loadout screen will call the shared gear library. That port also needs to keep mission kit selection available, restrict preset selection to the current weapon, and apply the selected assembly to the player's own starting firearm.

Run `python port/tests/test_tactical_presets.py` to check legacy parsing, writing, and malformed inputs. Pass a bundled preset file as an optional argument to check existing data with the same test.
