# Changes after v0.4.10

GE-X now replaces PP7 and silenced PP7 alongside KF7. Both pistols have GE-X
fire and screen reload animations, independent gun/support/reload calibration,
and physical magazines in stereo VR. Insertion reuses GE-X's slide-ready motion
without moving tracked hands. Cupping favors support; removing a magazine
requires a fresh underside grip and pull. Five matching PP7 textures can use
existing HD packs. PP7's magazine hand now keeps the tracked wrist and forearm
orientation with GE-X's reload finger curl, correcting the inverted grip.
The final grip/fit iteration was smoke-tested on Quest 3 and accepted by the user;
the broader acceptance matrix remains documented in the roadmap.

Gun fit's X cycle now includes **Held magazine**: sticks move the magazine
independently of the hand and watch, with separate saved PP7 and KF7 fits.
PP7's supporting hand stays fixed to the pistol when the off controller turns.
Held-magazine mode is available without the physical-reload setting and draws
a preview. PP7 support fit now saves pitch, yaw and roll, adjusted by holding
the gun-hand grip while moving the sticks. Reload grab-point fitting recognizes
the GE-X PP7's magazine too.
Magazine-well fit now adjusts the insertion target separately, with sticks or
the off trigger at the preview tip. PP7's default is the handle-bottom entrance
instead of the fully seated tip near the slide. Magazine previews and held
magazine hands suppress the legacy off-hand fallback arm.

See the [weapon roadmap](../gex-weapon-roadmap.md) for the reusable registry,
inspection tools and later reload families. See [v0.4.10](v0.4.10.md) for smoother
movement at walls and curbs, fewer render stalls and shared co-op mission gadgets,
[v0.4.9](v0.4.9.md) for smooth VR locomotion and fitted muzzles, and
[v0.4.8](v0.4.8.md) for settings persistence and co-op spawn unclogging.
