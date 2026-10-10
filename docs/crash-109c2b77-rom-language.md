# Crash 109c2b77: invalid language-bank input during startup

Report: v0.4.18, commit `869d9b8`, Oculus Quest 2, October 10, 2026 at
19:25:11 UTC. Both attachments are base64 encoded; the decoded tombstone is
Android protobuf. Diagnostic files remain outside the repository.

## Evidence

The log and protobuf agree on SIGSEGV / SEGV_ACCERR on SDLThread, native build
ID `345a09097379adebbb76ad363341db0f3d7bccee`. The relevant stack is:

```
zlib_inflate_block + 144
zlib_inflate + 84
decompressdata + 76
load_resource + 128
fileIndexLoadToBank + 152
langInit + 256
bossInitMainthreadData + 532
```

This is initial game startup, immediately after ROM and audio-bank binding.
No mission has loaded. The missing desktop GL functions in the startup log
are unrelated to this CPU-side decompression failure.

The log places the game heap at `0xb4000071c5470300` through
`0xb4000071c6470300`. The tombstone's x24 holds the compressed-copy address,
`0xb4000071c646fbe0`, exactly 1,824 bytes before that heap's end. This matches
the manifest's first permanent language bank, LgunE, at ROM offset `0x008ED250`
with stored length 1,824. `langInit` loads that bank first.

The protobuf's memory snapshot around x24 contains `b8 8d` at the copy's start,
where the resource format requires `11 72`. The input base in the globals is
the same address plus two. The failing load reads at offset `0x0200041e` from
that base, reaching `0x71c8470000`, the reported fault address. The read has
escaped a 1,824-byte resource by roughly 32 MiB.

The old `decompressdata` checks header bytes without enforcing the result,
unconditionally skips two bytes, invokes the legacy inflater without a source
length or output capacity, and ignores its error result. The legacy decoder
also omits several DEFLATE validity checks. The launcher and loader previously
checked only cartridge size, magic, name and USA id; those checks cannot detect
damaged or differently laid-out asset data.

The report establishes invalid compressed input in memory and an unbounded
read. It does not establish whether the player's file is damaged, modified,
incompatible with the manifest, or whether an earlier write changed the copy.
No original ROM file or matching unstripped release library was supplied;
function symbols come directly from the log and tombstone.

## Change

- The launcher validates every manifest-listed compressed file before enabling
  Start or accepting a picked ROM. Bad headers, invalid streams, truncated
  streams and files outside the image produce a visible ROM error. Validation
  uses bounded streaming output, with a 16 MiB per-file ceiling matching the
  engine heap. It does not store decoded assets.
- The runtime loader performs the same check before publishing the ROM pointer
  or binding asset segments. Byte-order normalization is shared, and rejects
  undersized images before reading their magic.
- `load_resource` uses the already-linked zlib library through a bounded 1172
  decoder. It checks allocation sizes before forming the compressed-copy
  pointer, snapshots overlapping input, and enforces both compressed length
  and destination capacity. Failed decoding reports the file and sizes and
  never proceeds to model, setup or language-bank conversion with partial data.

This covers resource-file loads. The legacy decoder remains for other callers,
such as background substreams, music and textures; they are outside this
report's stack. Validation is structural, not a checksum/authenticity check.

## Verification

`python3 port/tests/test_romcheck.py --verify-baseline` passes:

- Stored, fixed and dynamic DEFLATE; multiple blocks and outputs larger than
  the validation scratch buffer; ROM padding; exact and insufficient capacity;
  in-place expansion; overwrite sentinels; invalid headers, block types,
  stored lengths, backreferences and truncated input.
- The actual `load_resource` body, checking successful conversion and failure
  before conversion or an oversized compressed copy.
- The actual launcher probe, with a fully synthetic 12 MiB USA image and
  synthetic valid streams at every compressed manifest entry. z64, v64 and
  n64 images pass; damaged startup/later files and malformed streams fail.
- The original v0.4.18 probe accepts the same synthetic image after LgunE's
  header is changed to the report's invalid two-byte prefix. The fixed probe
  rejects it. No original cartridge assets are included in the fixtures.

The existing `test_gex_weapons.py` registry and production model/attachment
checks also pass. Its initial Linux compile hit the existing header wrapper's
missing `wchar_t`; a scoped compiler wrapper pre-includes GCC's own `stddef.h`
without changing repository sources, test assertions or dependency files.

The ARM64 debug APK builds with the configured NDK 25.1.8937393 and passes
`apksigner verify` with Signature Scheme v2. No Quest is attached and no retail
ROM is available here, so headset startup and gameplay remain unverified.
Acceptance requires checking that the affected ROM shows an error in the
launcher, and that a known-good original USA dump starts and loads missions,
text banks and weapon models normally.
