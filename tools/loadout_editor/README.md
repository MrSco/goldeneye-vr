# GoldenEye Tactical Loadout Editor

The Windows editor builds gun presets, creates guard outfits and assigns loadouts to campaign missions. It uses the same native gear catalog and compatibility code as the game. The source lives here. Preview models, catalog data, and compiled tools belong in their respective distributions.

Build the x64 editor on Windows with .NET Framework and a MinGW-w64 compiler. During integration, point `-RuntimeSource` at a compatible Tactical source checkout. That argument can be omitted once the Tactical gear library is in this tree.

```powershell
.\tools\loadout_editor\build.ps1 -OutputDirectory .\build\editor -NativeCompiler x86_64-w64-mingw32-clang -RuntimeSource C:\path\to\tactical-source
python tools/loadout_editor/test_workshop_bridge.py .\build\editor C:\path\to\tactical-data
```

For the current prototype, put the built EXE and DLL beside the Tactical catalog, or set `tactical-game-dir.txt` next to the EXE to point to that game folder. Test edits against a copy of the data. The integrated editor will use the game data directory for user presets and the separate pack directory for stock catalogs and previews.
