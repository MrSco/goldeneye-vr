GoldenEye VR for Linux (flat screen and PC VR, experimental)
===========================================================

The same game as GoldenEye VR on the Meta Quest, on a monitor (keyboard and
mouse, or a gamepad) or in a PC VR headset.

No game data is included. You need your own GoldenEye 007 (USA) cartridge
dump.

Getting started
---------------
1. Unzip this folder anywhere in your home folder.
2. Put your ROM in the folder, next to GoldenEyeVR. Any name ending in .z64
   works; the launcher finds it. Or choose it with the launcher's
   "Choose ROM file..." button (it uses zenity or kdialog, whichever your
   desktop has).
3. Flat screen: run ./GoldenEyeVR (or double-click it). The launcher opens;
   press START. PC VR: see below.

It runs on a 64-bit x86 Linux desktop (X11, or Wayland through XWayland)
with OpenGL 3.3, PulseAudio or PipeWire, and libcurl (installed on almost
every desktop; on Debian/Ubuntu the package is libcurl4).

Settings (goldeneye-vr.ini, goldeneye.ini), saves (eeprom.bin) and logs
(vr_debug.txt) are kept in this folder.

The launcher's Mods tab works as on the Quest: HD texture packs download
into texture-packs/, and XBLA / Bean is read from your own copy of
GoldenEye XBLA ("Choose copy...": its .7z, .zip or Xbox 360 package; then
"CE..." for the Community Edition's CommunityEditionUpdaterV6.zip). Nothing
of the XBLA release is downloaded.

Controls
--------
Keyboard and mouse (GoldenEye's 1.2 Solitaire style):
  W / S          walk forward / back       A / D     strafe
  Mouse          look (click in the window to capture it, Escape to let go)
  Arrow keys     look and turn
  Left mouse     fire                      Space     fire
  Right mouse    aim                       Z         aim
  Q, mouse wheel next weapon (A)           E, R      use / reload (B)
  F, X           L                         Enter     Start (pause)
  F10            the in-game panel         Alt+Enter fullscreen

Gamepad:
  Left stick     walk and strafe           Right stick   look and turn
  Right trigger  fire                      Left trigger, RB   aim
  A, X           next weapon (A)           B, Y      use / reload (B)
  LB             L                         Start     pause
  D-pad          the C buttons             Back      the in-game panel

PC VR
-----
GoldenEyeVR-VR is the Quest's VR game on a PC VR headset, through OpenXR
over GLX. The OpenXR loader is included (libopenxr_loader.so.1).
- play-vr.sh: plays through the active OpenXR runtime (Monado, WiVRn,
  ALVR, or SteamVR when it is the active runtime).
- play-steamvr.sh: plays through SteamVR for this launch only, whatever
  runtime is active (it sets XR_RUNTIME_JSON to SteamVR's
  steamxr_linux64.json in your Steam library; if yours is elsewhere, set
  XR_RUNTIME_JSON yourself and use play-vr.sh).
The launcher, settings and saves are the flat game's, in this folder. The
controls are the Quest's, for tracked controllers; the README on GitHub
lists them.

Online
------
As on the Quest: LAN games, joining by address, the internet lobby, the
friends list (no Discord linking on PC), mod downloads, crash reports and
updates (the launcher offers a new release and replaces this folder's files
with its own; your ROM, settings and saves stay).

Troubleshooting
---------------
- Starting it from a terminal shows the log as it runs. Useful options:
  --no-launcher (straight into the game), --boot-stage N (a level).
- No sound: the game uses PulseAudio (PipeWire's pulse server works).
- "error while loading shared libraries: libcurl": install libcurl
  (libcurl4 on Debian/Ubuntu, libcurl on Fedora and Arch).

Licences are in LICENSE.txt, CREDITS.md and licenses/.

GoldenEye VR is a non-commercial fan project. It is not affiliated with,
endorsed by or sponsored by Nintendo, Rare, Microsoft, MGM, Danjaq or Eon
Productions. GoldenEye 007, James Bond and 007 are trademarks of their
respective owners. No copyrighted game content is distributed with this
project; you must supply a ROM of a cartridge you own.
