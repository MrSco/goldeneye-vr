# Tactical campaign and asset pack

Tactical uses the same retail ROM as GoldenEye with its own campaign identifier and `eeprom-tactical.bin` progression file. The campaign selector displays **Tactical (not installed)** until the asset manifest and required gear catalogs are present. A direct start with an incomplete pack falls back to GoldenEye.

The Mods archive extracts into `texture-packs/tactical` in the game data directory. Its root contains `tactical.manifest`, `files.sha256`, `mods/tactical/`, `loadout-models/`, and `tactical-editor-catalogue.tsv`. The three manifest lines are `GEVRTACTICAL1`, a pack version, and the lowercase SHA-256 of `files.sha256`. The checksum list covers the payload files. The quick launch check verifies manifest syntax and required files. The packaging script checks each payload checksum.

For an integration build, build the archive from an existing Tactical asset install with `python tools/package_tactical_assets.py <install-directory> <output.zip> --version <version>`, then extract it into the path above. The eventual Mods listing will reference the approved asset release.

This draft covers campaign selection, the retail layout and save separation. Tactical gameplay, Workshop interactions, network synchronization and the Mods download registration are tracked in the follow-up implementation. Co-op selection stays disabled for Tactical while those changes are in review.

Run `python port/tests/test_tactical_pack.py` for the native pack and layout regression.
