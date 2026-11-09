GoldenEye VR for Windows (flat screen and PC VR, experimental)
==============================================================

The same game as GoldenEye VR on the Meta Quest, on a monitor (keyboard and
mouse, or a gamepad) or in a PC VR headset.

No game data is included. You need your own GoldenEye 007 (USA) cartridge
dump.

Getting started
---------------
1. Unzip this folder anywhere you can write to (not Program Files).
2. Put your ROM in the folder, next to GoldenEyeVR.exe. Any name ending in
   .z64 works; the launcher finds it. Or choose it with the launcher's
   "Choose ROM file..." button.
3. Flat screen: run GoldenEyeVR.exe. The launcher opens; press START.
   PC VR: see below.

Settings (goldeneye-vr.ini, goldeneye.ini), saves (eeprom.bin) and logs
(vr_debug.txt) are kept in this folder.

The launcher's Mods tab works as on the Quest: HD texture packs download
into texture-packs\, and XBLA / Bean is read from your own copy of
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
  Alt+Enter      fullscreen

Gamepad:
  Left stick     walk and strafe           Right stick   look and turn
  Right trigger  fire                      Left trigger, RB   aim
  A, X           next weapon (A)           B, Y      use / reload (B)
  LB             L                         Start     pause
  D-pad          the C buttons

PC VR
-----
GoldenEyeVR-VR.exe is the Quest's VR game on a PC VR headset, through
OpenXR. It needs openxr_loader.dll beside it (included).
- Play VR.cmd: plays through the active OpenXR runtime, whichever your VR
  software has set: Meta Quest Link (Air Link or a cable; set Meta Horizon
  Link as the OpenXR runtime in its settings), Virtual Desktop, or SteamVR
  when SteamVR is the active runtime. Connect the headset first; until the
  runtime reports a headset the game waits in a small window.
- Play SteamVR.cmd: plays through SteamVR for this launch only, whatever
  runtime is active (sets XR_RUNTIME_JSON for the game). It looks for SteamVR
  in the standard Steam library; if yours is elsewhere, set XR_RUNTIME_JSON
  to its steamxr_win64.json yourself and use Play VR.cmd.
The launcher, settings and saves are the flat game's, in this folder; the
PC window mirrors the headset. The controls are the Quest's, for Touch
controllers (the README on GitHub lists them all):
  Right trigger  fire                      Right grip   aim / zoom
  Left stick     walk and strafe           Right stick  turn
  A / X          next weapon / left-hand item (hold: the weapon wheel)
  B / Y          use, reload               Left stick click  crouch
  Menu           pause, Bond's watch
  Hold the right stick click (1 s)                 screen / stereo play
  One grip + hold the right stick click            Project Bean models on/off
  Both grips + hold the right stick click          the in-game panel
  Both stick clicks  recenter              Swing a hand at a guard  melee

Online
------
As on the Quest: LAN games, joining by address, the internet lobby, the
friends list (no Discord linking on PC), mod downloads, crash reports and
updates (the launcher offers a new release and replaces this folder's files
with its own; your ROM, settings and saves stay).

Troubleshooting
---------------
- A black window or an OpenGL error: update the graphics driver. The game
  needs OpenGL with the GL_OVR_multiview extension (NVIDIA and AMD drivers
  have it).
- Starting from a command prompt shows the log as it runs. Useful options:
  --no-launcher (straight into the game), --boot-stage N (a level).

Licences are in LICENSE, CREDITS.md and licenses/.

GoldenEye VR is a non-commercial fan project. It is not affiliated with,
endorsed by or sponsored by Nintendo, Rare, Microsoft, MGM, Danjaq or Eon
Productions. GoldenEye 007, James Bond and 007 are trademarks of their
respective owners. No copyrighted game content is distributed with this
project; you must supply a ROM of a cartridge you own.
