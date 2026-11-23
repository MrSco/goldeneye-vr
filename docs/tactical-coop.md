# Tactical co-op content and gear

The launcher and dedicated server will require the same campaign and installed Tactical pack for everyone in a Tactical lobby. The retail cartridge alone can match between two installs with different Tactical gear catalogs.

The connection's existing content block can advertise an external campaign pack identity. The host checks it before admitting the joining player. Pack-dependent campaigns must supply a nonzero identity before hosting or joining and reset it when switching campaigns. Regular sessions send the original two entries. The pack fingerprint is a compatibility check on the installed data.

This draft starts with the native handshake and its regression checks. The subsequent adapter will supply the manifest fingerprint and synchronize starting gun assemblies, guard outfits, magazines and item ownership. Those changes need multiplayer checks for drop and pickup, competing claims, reconnects and late-join snapshots on Windows, Quest and the dedicated server.
