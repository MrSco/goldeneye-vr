# Single-player AI ally (#194)

In Play > Game rules, enable **AI ally (Natalya)** before starting a mission.
Off by default. Applies to the twenty original GoldenEye solo missions, in VR
or flat presentation. Online sessions, split-screen, title/ending stages and
the optional hack campaigns ignore it. It works alongside Fast reinforcements.

Choose PP7 (default), silenced PP7, DD44, KF7, D5K, ZMG, AR33, RC-P90,
shotgun or automatic shotgun. Aggression controls engagement distance:
cautious only engages alerted enemies within 600 game units; normal engages
within 1200, aggressive within 1800. She returns to Bond when more than 1000
units away. Damage is 25–400%; health is 25–1000% (100% = 40 NPC damage units).
Invincibility uses the game's NPC invincibility flag. Settings are captured on
mission load; changing them during play takes effect on the next mission.

The companion follows using the game's NPC pathfinder and opens doors through
the normal movement code. She targets visible, armed guards, avoids civilians,
surrendered guards, other allies and named friendly story characters. Her shots
cannot hurt Bond or friendly NPCs. Bond can hurt her unless invincibility is on.
She spawns once after the intro, retries if the starting position is obstructed
or there are too few free character slots, and does not respawn after death.

This is an additional NPC. Jungle, Bunker II, Train and Control retain their
own Natalya and objectives; the companion does not replace her or satisfy an
escort objective. No stage flags, mission character IDs or imported action
blocks are used. The new character uses the game's allocated ID and stage
pool. Stage reload clears the pointer and configuration; an identity check
prevents a recycled corpse slot from being mistaken for the companion.

Implementation: `src/game/gevr_ally.c`, reset from `initguards.c`, spawn from
`chrpropTick`, controller called in place of script AI for this NPC only.
Normal NPC action/animation, collision, shooting and damage code still runs.
The NPC damage multiplier is scoped to her hits. Jungle's existing ally scan
excludes the companion. No network protocol changes.

## Validation

- `python3 port/tests/test_ally.py`: runs the production controller against a
  synthetic stage. Covers solo/campaign/camera gating, retries, one spawn per
  mission, reload and reused slots, preference snapshots and bounds, weapons,
  durability, neutral/player filtering, sight checks, aggression, follow/stop,
  cinematic suspension and damage isolation.
- `python3 port/tests/test_ally_ui.py`: production Game rules widgets and INI
  save/load snippets, with value bounds, online/hack disablement, scrolling
  and footer bounds at the launcher font size.
- `python3 port/tests/test_campaign_items.py`: existing campaign regressions,
  including an added check that scripted Natalya ignores the companion.
- `python3 port/tests/test_reinforcements.py`: body retention and optional
  reinforcements at NTSC/PAL timing. Linux requires pre-including the compiler's
  system `stddef.h` because the existing include shim masks glibc's partial
  includes; no game or test assertions were changed for that workaround.

Android `assembleDebug testDebugUnitTest` passed (30 JVM tests, zero failures
or errors). The debug APK signature verifies. Existing launcher UI checks pass.

Headset play is still required: Dam start/reload and weapon choices; following
through doors, stairs and narrow passages; Jungle with both Natalyas; Bunker II,
Train, Control, Facility/Archives civilians; Fast reinforcements with retained
bodies; damage/invincibility, death, mission endings and performance. No ROM or
Quest is available in the cloud environment. Do not treat synthetic tests as
mission-completion or headset confirmation.
