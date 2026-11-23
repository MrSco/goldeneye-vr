# Tactical inventory and interactions

The prototype tracks the chamber, firearm well, hand, and rig position independently. Each carried magazine has a stable ID, compatibility type, capacity and current round count. Moving a magazine preserves its identity and rounds. A chambered round stays with the firearm during a magazine change.

This draft ports the inventory and controller-independent interaction state. The campaign adapter will feed tracked hand intent and body-relative distances into the state machine, and will own world objects, pickup arbitration and rendering. Use the game's existing body slots and tracked input as integration points. Game hooks will be added behind the loaded Tactical campaign check.

Run `python port/tests/test_tactical_inventory.py` to check round conservation, magazine compatibility, chamber behavior, drops and tracking interruption.
