<p align="center">
  <img src="art/jedi_ninja_banner.jpg" alt="Jedi and Ninja Modes for GoldenEye VR" width="100%">
</p>

<h1 align="center">Jedi &amp; Ninja Modes</h1>

<p align="center">
  <b>Lightsabers, Force telekinesis, DL-44 blasters, katanas, and bouncing throwing blades inside GoldenEye 007.</b><br>
  Two mutually exclusive playstyles built to run everywhere GoldenEye VR does: Quest, PCVR, flat-screen PC, iOS, tvOS, visionOS, Android, and dedicated server.
</p>

<p align="center">
  <a href="#jedi-mode">Jedi Mode</a> ·
  <a href="#blasters--the-dl-44">Blasters &amp; DL-44</a> ·
  <a href="#ninja-mode">Ninja Mode</a> ·
  <a href="#controls--wheel">Controls &amp; Wheel</a> ·
  <a href="#menus--configuration">Menus &amp; Options</a> ·
  <a href="#maintainer--integration-notes">Maintainer &amp; Merge Notes</a>
</p>

---

Both modes stay **off by default** so standard missions load untouched. Flip either one on from the Launcher or Pause menu and GoldenEye turns into a deflecting, dismembering, physics-driven corridor brawler (ragdolls, dismemberment, and 24-second body retention switch on automatically). Enable Jedi and Ninja turns off; enable Ninja and Jedi turns off.

<a id="jedi-mode"></a>
## Jedi Mode

<p align="center">
  <img src="art/jedi_mode_showcase.jpg" alt="First-person VR lightsaber deflection, Force Lightning, and telekinetic rocket catch in Facility" width="100%">
</p>

Walk into Facility with an igniting plasma blade in one hand and the Force in the other.

- **Three Saber Styles & Eight Colors:** Run a **Single** blade, a spinning **Double-Bladed** staff (dual emitters on the hilt, twin-disc saber throw), or **Dual Sabers** (full off-hand tracking in VR). Pick from `Blue`, `Green`, `Red`, `Purple`, `Yellow`, `White`, `Orange`, or `Gold`.
- **Aimed Deflection & Perfect Parry:** Hold block to turn aside incoming shots, or catch gunfire right inside the first **0.22s** parry window (`Perfect parry`) for a bright crit-boosted counterbolt (38 damage vs 16) that snaps toward where your headset/gaze is looking in VR — or locks blades in a sparks-flying **Sith Saber Lock** if a Sith swings at the same time.
- **Force Powers on the Off-Hand:**
  - **Force Grab / Choke (`FORCE TK`):** Lock onto a guard (or suspended ragdoll corpse/severed limb) and hoist them into the air. Twist or whip your wrist sideways in VR to slam them across the room or into other guards, or snap downward below waist speed to trigger a radial **Force Slam**.
  - **Force Push & Pull:** Flick forward or tap/release cleanly to blast nearby guards back as live ragdolls — or use Pull/Push (`Catch rockets`) to snag flying RPGs and grenades mid-air and hurl them straight back downrange.
  - **Force Lightning (`LIGHTNING`):** Chain jagged high-voltage arcs to up to **3** nearby guards simultaneously, stunning and frying entire rooms while your saber stays ready in the main hand.
  - **Saber Throw (`THROW`):** Whip the saber hand forward in VR (or double-tap attack/trigger on flat) and guide the spinning plasma blade straight through a hallway of guards before recalling it to your palm.
- **Custom Stereo Synth Audio & Blade Trails:** A dedicated 22,050 Hz stereo DSP mixer generates dual-detuned lightsaber hum, Doppler swing whooshes, ignition snap-hiss, deflections, metallic clashes, Force sub-bass drones, lightning crackle, and blade spin overhead — paired with 7-sample motion ribbons and plasma wall-scorch sparks when dragging a hot tip across concrete.
- **Sith Enemies & Jedi Ally:** Spawn red-saber Sith NPCs (off by default, `Random` or `Frequent`, about a third wielding double-bladed staffs) at Normal, Giant, Tiny, or random heights — even inside standard GoldenEye missions via `Sith in normal`. Flip on **Jedi ally** to arm your AI wingman with a saber and Force powers too.

<a id="blasters--the-dl-44"></a>
## Blasters & the DL-44

<p align="center">
  <img src="art/dl44_blaster_showcase.jpg" alt="Custom Star Wars DL-44 Heavy Blaster Pistol beside a lightsaber hilt and Facility security keycard" width="100%">
</p>

Deflecting invisible hitscan bullets is mostly guesswork. Turning **Blasters** on (`On` by default whenever Jedi mode is active) fixes that:

- **NPCs & Turrets Fire Laser Bolts:** Enemy guards (including shotguns and rifles) and ceiling/drone autoguns fire GoldenEye's native glowing `ITEM_LASER` energy bolts from the muzzle across the full room trajectory with laser audio — making incoming fire clean to read, dodge, or bat back at whoever pulled the trigger. Rocket and grenade launchers remain rockets and grenades so **Catch rockets** still shines.
- **Default DL-44 Heavy Blaster on Spawn:** Every Jedi mission with Blasters on starts with the `ITEM_LASER` sidearm unlocked and equipped in your right hand, waiting on the 4th Jedi wheel slot (`BLASTER`). When drawn, GoldenEye's Moonraker mesh swaps out for a custom-sculpted 3D **DL-44 Heavy Blaster Pistol** — walnut Mauser C96 broomhandle grip, lanyard cap, gunmetal receiver with exposed hammer and brass side greeble, ribbed barrel cooling fins, flared slotted MG42 flash hider with recoil kick & muzzle flash, and a right-offset Hensoldt sniper scope (`Left-handed` flips the offset cleanly) firing GoldenEye's real laser bolts.
- **Smart Pickup Rules (`Pick up guns` off by default):** In both Jedi and Ninja modes, dropped enemy firearms, knives, launchers, grenades, mines, and ammo boxes are ignored by default so your inventory stays pure — whether walking over drops, grabbing with a VR hand, or Force-Pulling objects across the room. **Keycards, keys, body armour, watch lasers, mission gadgets (decoders, cameras, bugs, plastiques, blackboxes, tapes), and Egypt's mission Golden Gun** still pick up without interruption. Flip **Pick up guns** on anytime if you want every dropped gun back.

<a id="ninja-mode"></a>
## Ninja Mode

<p align="center">
  <img src="art/ninja_mode_showcase.jpg" alt="First-person VR POV ninja katana bullet-slice, octagonal buckler deflection, bouncing boomerang, and Shadow-Step smoke" width="100%">
</p>

No Force crutches — just steel, momentum, smoke, and timing.

- **Six Sword Loadouts:** Choose between **Katana**, **Gold Katana**, faster close-quarters **Short** / **Gold Short** wakizashi, or **Dual Short** / **Dual Gold** for two-handed slashing in VR and flat screen, leaving sweeping steel motion ribbons and directional ragdoll cuts on lethal strikes.
- **Corridor-Ricocheting Boomerangs:** Pick **Shuriken**, **Wing**, **Gold Shuriken**, or **Gold Wing**, whip your throwing hand (or switch to `BOOMERANG` on the wheel / double-tap attack) and bank spinning blades around hallway corners using up to **2** shallow-angle wall bounces before they curve back into your hand.
- **Buckler Shield & Bullet-Slice:** Select `SHIELD` on the wheel (`Ninja shield`) to block and deflect incoming rounds off an off-hand black-and-gold octagonal shield — or go full aggression with **Bullet slice**, severing incoming bullets mid-swing whenever your blade cuts across the shot.
- **Shadow-Step Dash:** Trigger the `SHADOW` slot on the wheel or double-tap your off-hand button (`LT` / right-click) for a burst of grey tactical smoke clouds and a rapid speed surge to close distance or vanish around a corner (`Shadow-Step`).
- **Evil Ninjas:** Populate maps with sword-and-boomerang Ninja adversaries (and optionally spawn them in normal missions via `Ninjas in normal`).

<a id="controls--wheel"></a>
## Controls & Quick Wheel

| Action | Stereo VR (Tracked Hands) | Gamepad / Mouse & Keyboard / Touch |
|---|---|---|
| **Slash / Heavy Swing** | Swing primary (or dual off-hand) controller past speed threshold | Tap `RT` / Left Click / Touch Fire |
| **Block / Deflect / Shield** | Hold primary trigger (`RT`) or off-hand trigger for Ninja Shield | Hold `RT` / Left Click (`Parry` inside first `0.22s`) |
| **Throw Saber / Boomerang** | Whip weapon hand forward and release trigger (or use wheel slot) | Double-tap `RT` / Left Click |
| **Force Power / Shadow-Step** | Hold off-hand trigger (`LT`), pull wrist toward chest for Pull/Choke, snap down for Slam | Hold or tap `LT` / Right Click (double-tap `LT` for `Shadow-Step`) |
| **Mode Wheel** | Press `X` / left thumbstick click (or controller wheel binding) | Hold `LB` / `Q` / Touch wheel |

**Wheel Layouts**
- **Jedi Wheel (4 slots):** `FORCE TK` · `LIGHTNING` · `THROW` · `BLASTER` *(shows `GUNS` when `Blasters` is off)*
- **Ninja Wheel (6 slots):** `KATANA` · `SHORT` · `DUAL` · `SHIELD` · `BOOMERANG` · `SHADOW`

<a id="menus--configuration"></a>
## Menus & Configuration

Jedi and Ninja never run together — flipping one **On** automatically sets the other **Off**. Everything fits in two dedicated menus without cluttering existing tabs:

- **Launcher:** **Rules** -> **Jedi** / **Ninja** tabs *(beside `Game rules`, `AI ally`, `Cheats`)*
- **In-Game Pause Panel:** **Mods and fun** -> **General** | **Jedi** | **Ninja** sub-tabs

<details>
<summary><b>All Jedi Mode Options (15 settings, saved to goldeneye-vr.ini)</b></summary>

| Menu Row | Values | Default | `goldeneye-vr.ini` Key |
|---|---|---|---|
| **Jedi mode** | Off / On *(turns Ninja mode Off, auto-enables Ragdolls + Dismemberment + 24s Body stay)* | Off | `JediNinjaMode` (`0`/`1`) |
| **Saber style** | `Single`, `Double-Bladed`, `Dual Sabers` | `Single` | `SaberStyle` |
| **Saber colour** | `Blue`, `Green`, `Red`, `Purple`, `Yellow`, `White`, `Orange`, `Gold` | `Blue` | `SaberColor` |
| **Saber hand** | `Right`, `Left` | `Right` | `JediHand` |
| **Force powers** | Off / On *(Force Grab/Choke, Pull, Push, Lightning, Slam)* | On | `JediForcePowers` |
| **Catch rockets** | Off / On *(Pull/Push catches & reflects live rockets and grenades)* | On | `JediCatchProjectiles` |
| **Perfect parry** | Off / On *(0.22s timed reflect crits & Sith Saber Locks)* | On | `JnPerfectParry` |
| **Blade trails** | Off / On *(7-sample blade ribbons & wall-scorch sparks)* | On | `JnBladeTrails` |
| **Custom SFX** | Off / On *(22,050 Hz synth hum, Doppler swings, clashes, Force bass)* | On | `JnAudio` |
| **Blasters** | Off / On *(NPC/turret `ITEM_LASER` bolts + default DL-44 sidearm)* | On | `JediBlasters` |
| **Pick up guns** | Off / On *(Allow picking up combat guns/ammo; keys & gadgets stay enabled)* | Off | `JediPickupGuns` |
| **Sith NPCs** | `Off`, `Random` (1), `Frequent` (3) | `Off` | `SithNpc` |
| **Sith size** *(when Sith NPCs on)* | `Normal`, `Random`, `Giant`, `Tiny` | `Normal` | `SithScale` |
| **Sith in normal** *(when Sith NPCs on)* | Off / On *(Allow Sith spawns in normal Bond mode)* | Off | `SithInNormalMode` |
| **Jedi ally** *(when AI Ally enabled)* | Off / On *(Arm your AI ally with a lightsaber & Force powers)* | Off | `AllyJedi` |

</details>

<details>
<summary><b>All Ninja Mode Options (10 settings, saved to goldeneye-vr.ini)</b></summary>

| Menu Row | Values | Default | `goldeneye-vr.ini` Key |
|---|---|---|---|
| **Ninja mode** | Off / On *(turns Jedi mode Off, auto-enables Ragdolls + Dismemberment + 24s Body stay)* | Off | `JediNinjaMode` (`0`/`2`) |
| **Ninja sword** | `Katana`, `Gold Katana`, `Short`, `Gold Short`, `Dual Short`, `Dual Gold` | `Katana` | `NinjaSword` |
| **Boomerang** | `Shuriken`, `Wing`, `Gold Shuriken`, `Gold Wing` | `Shuriken` | `NinjaBoomerang` |
| **Ninja shield** | Off / On *(Off-hand buckler bullet deflection)* | On | `NinjaShield` |
| **Wall ricochet** | Off / On *(Shallow-angle corridor boomerang bounces)* | On | `NinjaRicochet` |
| **Shadow-Step** | Off / On *(Smoke clouds & speed dash via `SHADOW` slot or double-tap `LT`)* | On | `NinjaShadowStep` |
| **Bullet slice** | Off / On *(Cut incoming bullets out of the air mid-swing)* | On | `NinjaBulletSlice` |
| **Pick up guns** | Off / On *(Allow picking up combat guns/ammo; keys & gadgets stay enabled)* | Off | `NinjaPickupGuns` |
| **Evil Ninjas** | `Off`, `Random` (1), `Frequent` (3) | `Off` | `NinjaNpc` |
| **Ninjas in normal** *(when Evil Ninjas on)* | Off / On *(Allow Evil Ninja spawns in normal Bond mode)* | Off | `NinjaInNormalMode` |

</details>

---

<a id="maintainer--integration-notes"></a>
## Maintainer & Integration Notes

Written for **MrSco** (and **Claude Code** / automated PR review) so you can audit the branch against `main` (`v0.5.23`, `a9d3338`) and test/merge with minimal friction.

### Why This Should Be Clean to Merge

- **Clean base & additive diff:** Branched directly on `a9d3338` (`v0.5.23`) with only 15 deleted lines across existing code — almost all logic lives inside dedicated files (`port/include/gevr_jedi_ninja.h`, `port/src/gevr_jedi_ninja.c`, `src/game/gevr_jedi_ninja_game.h`, `src/game/gevr_jedi_ninja_game.c`, `port/tests/jedi_ninja_native.c`, `port/tests/test_jedi_ninja.py`).
- **Safe defaults (100% opt-in):** `VrJediNinjaMode = 0`, `VrControllerWheel = 0`, `VrSithNpc = 0`, `VrNinjaNpc = 0`, and `VrAllyJedi = 0`. Vanilla GoldenEye missions, speedrun timing, co-op, Horde, and deathmatch behave identically unless toggled on.
- **No protocol bump or online regressions:** Network protocol stays at `27`. Active multi-player matches gate out Jedi/Ninja mode automatically via `jnModeActive()` (`!netIsActive() || getPlayerCount() == 1`).
- **Zero merge-conflict hotspots touched:** Root `README.md`, `STATUS.md`, `src/game/gevr_ally.c` (`gevrJnAllyThink` dispatches from `gevrJnGameTick` when `VrAllyJedi` is set), and `.github/workflows/` are untouched.
- **Line endings preserved (`* -text` in `.gitattributes`):** Mixed CRLF/LF files (`port/vr/vr_settings.h`, `port/vr/vr_settings.cpp`, `src/game/chraction.c`, `src/game/propobj.c`, `port/src/audio.c`, `src/game/bg.c`, `CMakeLists.txt`) keep their exact original line endings with zero whitespace/renormalization churn.
- **Zero new ROM assets, external dependencies, or shader edits:** First-person blades, shields, boomerangs, and the DL-44 Blaster render via standard Fast3D vertex/matrix display lists; NPC blasters reuse native `ITEM_LASER` beams and sound effects; custom SFX is self-contained pure C11 (`port/src/gevr_jedi_ninja.c`, zero `M_PI` or `rand()` dependency).
- **Cross-platform CMake & headless compatibility:** New files follow `SRC_PORT`/`SRC_GAME` globs (with a 1-line comment bump in `CMakeLists.txt` to refresh the glob). Headless server (`gevr-server`) and flat targets resolve VR/haptic hooks through existing `port/flat/flat_headset.c` symbols and `#ifdef GEVR` guards.
- **Pause & Launcher UI fit preserved:** `GEVR_SEC_COUNT` remains `8` so the Quest in-game tab bar never overflows — Jedi and Ninja menus sit inside `Mods and fun` as a `General | Jedi | Ninja` sub-tab bar and in Launcher `Rules -> Jedi` / `Ninja`, with `port/tests/launcher_frame_native.cpp` (`4 * 14` = 56 pages) and `port/tests/settings_table_stubs.h` updated to match.

### Surgical Hook Map

| Layer | Files | Role |
|---|---|---|
| **Pure C11 core & synth** | `port/include/gevr_jedi_ninja.h`, `port/src/gevr_jedi_ninja.c` | Trigger tap/hold/double-tap state machine, VR swing/pull/throw heuristics, deflection/parry math, shallow-angle ricochets, NPC melee decisions, 22,050 Hz stereo DSP mixer |
| **Unit test suite** | `port/tests/jedi_ninja_native.c`, `port/tests/test_jedi_ninja.py` | Verifies pure C11 (`-Wall -Werror`) + GoldenEye math (`math_atan2f`/`asinf`/`acosf`) core behavior |
| **Gameplay & Fast3D VFX** | `src/game/gevr_jedi_ninja_game.h`, `src/game/gevr_jedi_ninja_game.c` | Combat loop, telekinesis, Force Slam/Lightning/Catch, 7-sample blade trails, sculpted DL-44 viewmodel, blaster grant (`ITEM_LASER`), `gevrJnCanPickupProp()`, Sith/Ninja/Ally AI |
| **Ragdoll extensions** | `port/include/gevr_ragdoll.h`, `port/src/gevr_ragdoll.c`, `port/tests/ragdoll_native.c` | Directional dismemberment (`gevrRagdollForceCutNear`), body/loose-limb telekinetic grab & throw, 24s retention under `VrJediNinjaMode` |
| **Audio & input hooks** | `port/src/audio.c`, `port/src/input.c` | Mixes `gevrJnAudioMix()` in `audioEndFrame()`; feeds flat/pad/mouse/touch triggers (`gevrJnInputFeedFlat()`), `LB`/`Q` wheel toggle, and `gevrJnSuppressNormalFire()` |
| **Settings & two menus** | `port/include/gevr_settings.h`, `port/src/gevr_settings.c`, `port/vr/vr_settings.h`, `port/vr/vr_settings_defaults.c`, `port/vr/vr_settings.cpp`, `port/vr/gevr_launcher_ui.h`, `port/vr/gevr_settings_ui.h`, `port/vr/vr_launcher.cpp`, `port/tests/settings_table_stubs.h`, `port/tests/launcher_frame_native.cpp` | Mutually exclusive Jedi/Ninja toggles, all configurable options, INI load/save, Launcher `Rules` & Pause `Mods and fun` sub-tabs |
| **Engine & decomp hooks** | `CMakeLists.txt`, `src/game/lv.c`, `src/game/bg.c`, `src/game/initguards.c`, `src/game/bondview2.c`, `src/game/chraction.c`, `src/game/propobj.c`, `src/game/gunfire.c`, `src/game/chrprop.c`, `src/game/gevr_forcepull.c` | Reset/tick/draw entry points, damage & fire interception, VR grip axes helper, wheel `"BLASTER"` / 6-slot Ninja UI, NPC/autogun `ITEM_LASER` blaster bolts & SFX, pickup filtering |

### Steps Already Taken & Verified Locally

- **Pure C11 core check:** `gcc -std=c11 -O2 -Wall -Werror -Iport/include -fsyntax-only port/src/gevr_jedi_ninja.c` passed.
- **Six-platform C syntax sweep (`-fsyntax-only`):** Checked `src/game/gevr_jedi_ninja_game.c` (`-Wall -Werror=implicit-function-declaration`, zero warnings in that file) plus all 14 other touched C files across `pc_flat`, `pcvr_quest`, `server`, `tvos_ios`, `android_mobile`, and `win32` define matrices (using minimal temporary SDL2/JNI/EGL/GLES3 header stubs where host SDKs were unavailable).
- **Local Python & native tests passed:** `test_jedi_ninja.py`, `test_settings_table.py`, `test_ingame_fit.py` (32 sections), `test_launcher_frame.py` (56 pages), `test_launcher_ui.py`, `test_friends_ui.py`, `test_launcher_mods.py`, `test_launcher_loadout.py`, `test_launcher_start_row.py`, `test_pause_ui.py`, `test_ally_ui.py`, `test_ally.py`, `test_body_slot_wheel.py`, `test_force_pull.py`, plus mutual-exclusivity and default assertions for `Jedi mode`, `Ninja mode`, `VrJediBlasters`, `VrJediPickupGuns`, and `VrNinjaPickupGuns`.
- **Clean diff hygiene pass:** Stripped accidental CI workflows and unrelated files, verified zero diff on `src/game/gevr_ally.c`, and restored native CRLF line endings on touched headers/sources.

### Suggested Checks Before Merging Upstream

- Run the full native CMake build/link across target platforms (Quest NDK, Windows flat/PCVR, Linux, Apple platforms, `gevr-server`) — full builds and CI runs were intentionally skipped on the fork branch.
- Run `port/tests/test_ragdolls.py` (skipped locally because `cmake` was unavailable on this machine) alongside your full release test gate.
- Do a quick in-headset / flat playtest pass to tune subjective feel: DL-44 & lightsaber grip offsets (`gevrJnGetGripAxes`), in-game `Mods and fun -> Jedi / Ninja` sub-tab visual layout, and `gevrJnAudioMix()` volume relative to GoldenEye music/SFX.
