# Eight-player lobby and watch controller display

User screenshots from 2026-10-09 showed only five of eight lobby rows and a
partial N64 controller shell on the watch's Controls page.

The hosting lobby now uses two columns above the roster: mode and stage/
player count on the left, weapons and session code/connected count on the
right. Custom weapons open an Edit popup with all four gun choices. The
roster keeps the same readable font, character, team, state, ping and host
removal controls; smaller Kick buttons and less vertical cell padding allow
all eight rows to fit. Existing kick confirmation and ownership checks stay
in place. The joiner's roster also uses the tighter row spacing.

The watch controller uses depth testing and its own projection. Its draw
path did not clear depth inherited from the world scene. A port-only depth
clear now precedes the first model, preserving colour and depth testing
between the model's parts, animated buttons, and a second controller. This
addresses a path that can hide the shell while nearer grips remain visible;
the user's screenshot still needs comparison on the headset after updating.

Validation:

- `port/tests/test_launcher_ui.py`: actual launcher functions and bundled
  ImGui at Quest font/style scale, eight maximum-length player names,
  deathmatch/team/co-op scenarios, standard/custom sets; verifies horizontal
  and vertical fit. Existing Match/Comfort/reset/ownership checks also pass.
- `port/tests/test_watch_controller.py`: production Controls render dispatch
  starts with stale world depth; verifies one clear before the first model
  for opaque/fading, single/dual controller paths, with no clear between
  models.

Headset checks: view all eight lobby entries, open Custom > Edit, use the
team selector, and confirm Kick still opens confirmation. In solo, open the
watch's Controls page and rotate the model; verify the complete shell and
buttons are visible as the watch opens/closes.
