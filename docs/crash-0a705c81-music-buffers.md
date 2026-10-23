# Crash 0a705c81: music sequence writes into the audio filter

Report: Quest 3, v0.5.13, commit `bdeb44dc`, native build ID
`f8a72cb04ca995ac667dfc6b9350e94da02c2bc3`. The reported exit is
2026-10-23 17:05:41 UTC. Logcat and the protobuf tombstone agree on
2026-10-24 01:05:41 +0800, the same instant. Both attachments arrived as
base64 text; the app log decoded to gzip and the tombstone to protobuf.
Decoded attachments remain outside the repository.

## Evidence

SDLThread receives SIGBUS / BUS_ADRALN with PC `0x0025480725490725`.
The first library frame is `alAudioFrame+248` at relative PC `0x59eb14`,
followed by `amHandleFrameMessage`, `gevrAudioFrame`, and the frame pump.
This is a bad indirect call, not a watchdog abort or an audio queue spin.
The final queue log has zero empty/dropped buffers and a 42,310 us maximum gap.

The instructions captured around LR `0x6ea7fdeb18` show:

```
0x6ea7fdeb0c  ldr x8, [x25, #16]
0x6ea7fdeb10  mov x0, x25
0x6ea7fdeb14  blr x8
```

`x25` and `x0` are `0xb400006e4490e9c0`, the output filter. `x1` is 6
(`AL_FILTER_SET_DRAM`) and `x2` is the PCM destination. This is
`output->setParam(output, AL_FILTER_SET_DRAM, lOutBuf)`, not the earlier
player callback. At filter offset 16 the captured bytes are
`25 07 49 25 07 48 25 07`, exactly the value in `x8`
(`0x0725480725490725`, with the top address tag ignored by the CPU).
The surrounding filter memory is also sequence-like byte patterns, not
valid filter pointers. The driver still points to that filter; the filter
itself was overwritten.

Saved settings select `Campaign=1` (Goldfinger 64). The retained game log
contains `cryptdoor` diagnostics but starts after stage loading. Neither
the original sequence bank nor its size table is attached, so this report
does not establish which sequence or exact length caused the overwrite.
No matching archived ELF was used: function offsets come from the tombstone,
and the call instructions come from its memory capture.

## Cause and fix

The three music players use retail capacities of 6,344, 2,000, and 4,000
bytes. Players 2 and 3 share one allocation. Their load paths compute
`aligned raw length + 64`, then stage the aligned compressed bytes ending
at that position, without comparing it with the allocation size. The
legacy decoder also has no destination capacity. A campaign replacement
can overwrite the next player's live data or the synthesizer allocations.
`alSynNew` allocates `ALSave`, the crashed output filter, immediately after
these sequence buffers. This is a concrete overwrite path consistent with
the captured damage, although the exact reported music file is unavailable.

For GEVR, each player now gets an independent buffer sized for the largest
uncompressed sequence in the active bank. Music loads use the existing
`gevrInflate1172` bounded decoder directly on the resident bank, removing
the compressed staging copy. Invalid track IDs, bank ranges, output sizes,
and failed/truncated decoding stop before the sequence parser is called.
The original non-GEVR path is preserved.

Each capacity is at most 65,536 bytes because bank lengths are u16, so the
three buffers use at most 192 KiB of the existing 1 MiB AL heap. Previously
they used 12,352 aligned bytes. Only track changes decompress; per-frame
synthesis is unchanged.

## Validation

- `python port/tests/test_music_buffers.py --verify-baseline` compiles the
  original `bdeb44dc` music code and its allocation/length initialization,
  using the real AL heap allocator. A synthetic 12,000-byte stored sequence
  loaded through player 2 overwrites the following filter region in the
  original `romCopy`, before decompression. The fixture detects that write
  and exits with the expected status 42. No original crash asset is claimed.
- `python port/tests/test_music_buffers.py` uses the current production
  music file, AL heap allocator, and bounded decoder. It exercises all three
  play paths with sizes around 2,000, 4,000, and 6,344 bytes, plus 12,000,
  32,769, and 65,535. All decoded bytes match; other players' whole buffers
  and the following filter guard survive. Oversized output, short input,
  wrong lengths, invalid IDs, and invalid bank addresses fail before parsing.
- `python port/tests/test_crash_hangs.py` passes, including music stop queue
  recovery and voice reuse.
- `python port/tests/test_romcheck.py` passes its bounded decoder and resource
  regressions.

- Android `assembleDebug testDebugUnitTest` passes (38 tasks executed, 31 JVM tests,
  zero failures/errors). The debug APK passes `apksigner verify`.

Headset acceptance is still needed: repeat the affected Goldfinger 64
mission with action/watch music transitions and confirm continued audio and
no filter corruption. The cloud environment has neither a Quest nor the
player's ROM, so it cannot reproduce that exact gameplay session.
