# Dam speedboat

Solo Dam. Retail left the hull (`PspeedboatZ`), an unused pad on the north
jetty, and the island across the reservoir, and never the ride. This is that
ride, the same check as GEVR PC vr456.9.

The hull is **1.75×** the retail prop scale (larger than stock, not double).
It sits just off the north jetty, nosed at the island pad: the standable pad
farthest across the level, and the unused pad on this side of the water when
one is as far out as the docks.

- **Board:** from the jetty, reach over the hull's edge and grip, or press **B**
  standing within about 3.5 m of it.
- **Ride:** the left (move) stick drives: up goes ahead, left and right steer, in
  VR and on the 2D screen. Walking does not carry you off the deck. The hull
  floats a tenth of its height in the water and stops at the jetty and the docks.
- **Step off:** grip (or **B**) once the hull is near the island landing or back
  at the jetty. Mid-crossing you stay aboard. With **Grip to use** on (Gestures),
  B is left to its own job and only the grip boards and steps off.
- **Hull lost:** the hull is a normal prop (1000 health). If it is destroyed
  or removed while you ride, you land back on the north jetty.

Multiplayer does not spawn it, nor a Goldfinger 64 or Tomorrow Never Dies
campaign (their own levels sit in Dam's slot). The island's floor only covers part of the
ground there, which is how the stage was left; the landing is that pad.

On the retail Dam the picker takes jetty pad 238 (unused, at the waterline)
and an island pad about 305 m across: pads 79-81 stand on a patch of floor with
no tile link to the rest of the stage, 259-268 on the island's hut.
`GEVR_GE_ROM=<ge.z64> python port/tests/test_speedboat.py` checks that choice
on the player's own ROM.
