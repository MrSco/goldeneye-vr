# Touch controllers on the watch's Controls page

The watch's **Control** page (pause in single player, both play modes) used to draw
the N64 controller from the ROM. It now draws the Meta Quest Touch controllers in
your hands, each over a list of its controls:

```
   [left controller]        [right controller]
 Trigger  Fire / -        Trigger  Fire
 Grip     Grab            Grip     Aim / use
 Stick    Move            Stick    Turn
 Y        Action          B        Action
 X        Hand item       A        Weapon
 Menu     Pause
```

- **What each control does** follows the play mode (stereo or screen), left-handed
  mode, Swap sticks and what the off hand holds, from the bindings in
  `port/src/input.c`. In stereo the off hand's trigger reads **Fire** while that
  hand holds something, **-** while it's empty (it does nothing then), and
  **Detonate** while GoldenEye X's remote mines are out. The grips follow
  the VR settings too, and change as soon as you return from the watch's
  VR page:

  | Grip | Line |
  |---|---|
  | Screen play, either hand | Aim |
  | Stereo, the hand holds a throwable and Motion throwing is on | Throw |
  | Stereo, gun hand | Aim / use with Grip use on, else Aim |
  | Stereo, off hand holding a gun | Sight |
  | Stereo, off hand empty | Grab with any grip gesture on (holster, use, pickup, mine re-grab, body slots), else Hold gun |

  The lists are laid out for the widest line any of them can show (measured
  in the watch font), so none runs off the page or into the other list. A
  pressed trigger, grip, stick or button moves on the model, as it does in the
  hand, and lights green. Its line turns white, the same way the N64 page's
  labels light up.
- **Which controllers** are drawn depends on the headset: Quest 1, Quest 2, Touch
  Plus (Quest 3 and 3S) or Touch Pro. The runtime's headset name decides, then
  Android's model; the controllers' interaction profile overrides both when it
  names them. To pick a set by hand, use **VR settings → Controls →
  Controllers** on the watch, or `TouchControllers=` in `goldeneye-vr.ini`:
  0 Auto, 1 Quest, 2 Quest 2, 3 Touch Plus, 4 Touch Pro. The log line
  `touch: drawing ... controllers` shows the choice.
- As the N64 controller did, the pair spins and tilts with the stick while the
  page's **Controller** row is selected. It also fades in with the page.

## Code

| File | What |
|---|---|
| `src/game/gevr_touch.c` | Picks the model, moves the parts, lights the vertices each frame, builds one display list per side (16-vertex `G_VTX` loads and `G_TRI4`s), and draws the lists of controls. |
| `src/game/options.c` `draw_watch_controller` | Under `GEVR`, draws the Touch pair and its lists instead of the N64 controller. |
| `port/include/gevr_touch_model.h` | The data's types. |
| `port/src/gevr_touch_model_data.c` | **Generated**: 1,600 triangles a controller, vertex colours, part pivots. |
| `port/vr/vr_input.cpp` `gevrTouchPadState`, `gevrTouchProfileName` | Each physical controller's trigger, grip, stick and buttons, and the interaction profile. |
| `port/vr/vr_openxr.cpp` `gevrHmdName` | The runtime's headset name. |

## Making the data

The source is Meta's **Oculus controller art** (v1.8). The models are rigged
with Meta's controller bones (`b_trigger_front`, `b_trigger_grip`,
`b_thumbstick`, `b_button_*`), so each moving part and its pivot comes from the
model itself.

1. Extract the zip. Long folder names are fine: the tools open the files by
   their long-path form.
2. Export each kind with Blender 4.1 or later (5.2 was used). Three of the
   four models are ASCII FBX, which Blender can't import, so
   `tools/blender/gevr_fbx_ascii.py` reads them.

   ```
   blender -b --factory-startup -P tools/blender/gevr_touch_export.py -- <art> build/touch/quest1.json quest1
   ```

   Run it again with `quest2`, `plus` and `pro` in place of `quest1`.
3. Generate the C data, plus optional preview renders of the page:

   ```
   python tools/gevr_touch_model_gen.py --art <art> --json build/touch --preview build/touch/preview
   ```

   The previews follow the game's camera (`options.c`: lookat, 45° tilt,
   `guPerspective`) and the runtime's lighting, so layout changes can be
   tried on the PC. `--layout units,xoffset,raise,yaw,tilt` tries a layout
   without writing C. The light in `gevr_touch.c` `gevrTouchLightSide` and in
   the generator's `light()` must stay the same.

Budgets: the parts keep up to 120 triangles (trigger, stick), 90 (grip) or
56 (buttons), and the body takes the rest of 1,600. Each controller is about
55 of the page's 320 pixels wide.

## Licence

The attribution file in Meta's art zip allows using the images "solely for
referring to the corresponding product in your video game or VR experience",
and its manuals for users. It forbids implying a partnership,
sponsorship or endorsement, and it doesn't cover Meta's names, trademarks or
logos. The page uses the models only to show the controllers. The generated
data carries vertex colours and geometry, with no logos or textures.
