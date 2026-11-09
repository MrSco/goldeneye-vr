GoldenEye VR for Windows (flat screen, experimental)
====================================================

The same game as GoldenEye VR on the Meta Quest, on a monitor: keyboard and
mouse, or a gamepad. PC VR comes later.

No game data is included. You need your own GoldenEye 007 (USA) cartridge
dump.

Getting started
---------------
1. Unzip this folder anywhere you can write to (not Program Files).
2. Put your ROM in the folder, next to GoldenEyeVR.exe. Any name ending in
   .z64 works; the launcher finds it. Or choose it with the launcher's
   "Choose ROM file..." button.
3. Run GoldenEyeVR.exe. The launcher opens; press START.

Settings (goldeneye-vr.ini, goldeneye.ini), saves (eeprom.bin) and logs
(vr_debug.txt) are kept in this folder.

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

Online
------
LAN games and joining by address work. The internet lobby, mod downloads,
crash reports and the friends list are not on PC yet.

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
