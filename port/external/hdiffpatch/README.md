The patch half of HDiffPatch (https://github.com/sisong/HDiffPatch, MIT, see
LICENSE), taken at 3b9dca715ca492873bf2c49e22e5d5b7d2a78620. Used to apply the
GoldenEye XBLA Community Edition updater's files.diff, which the player
supplies, to their own imported copy of the release (port/src/gevr_bean_ce.c).
Nothing is changed from upstream.

Copied as a whole from Dab's Mod (port/src/external/hdiffpatch, dabs-mod
aae28c8, MIT; port/licenses/dabs-mod.txt), including its glue, hdpglue.c/.h,
which is Dab's. Its lzma plugin runs over the port's own LZMA SDK in
port/external/lzma (the same 9.20 copy Dab's Mod carries).
