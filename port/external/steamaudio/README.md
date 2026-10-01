# Steam Audio 4.8.1

Voice-only binaural rendering. Official SDK archive:
https://github.com/ValveSoftware/steam-audio/releases/download/v4.8.1/steamaudio_4.8.1.zip

Archive SHA-256: `4a0aa5ec1176f38f0b0993a37c2259d9e86f27e22d5e24f83ec4c3cb9a1d5449`.
Only the public headers and Android ARM64 library are included. License and
third-party notices are from the matching v4.8.1 source release.

The default HRTF does not initialize at 22,050 Hz. `net_spatial.c` converts
256 game-output samples to a preallocated 512-sample 44,100 Hz SDK frame,
applies one binaural effect per source with bilinear interpolation, then
converts back. Opus remains 16,000 Hz and game output remains 22,050 Hz.
Buffering adds one 256-sample output block (about 11.6 ms). SDK initialization
failure logs once per voice session and keeps directional stereo rendering.
