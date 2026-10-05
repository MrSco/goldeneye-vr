# Body retention and Fast reinforcements

In v0.4.7, **Bodies stay** controls corpse visuals. It keeps the newest 12,
24 or 48 bodies without changing the original reinforcement rules.
**Fast reinforcements** deliberately increases enemy pressure: alerted
guards can call additional reinforcements while earlier ones are still
alive. It is off by default and works with every body count, including Off.

Both settings are in **Play → Game rules...** and the watch's **VR
settings → Game rules**. **Original rules** in the launcher turns both off.
They apply to solo play; network and split-screen use the original rules.
Only guards whose existing AI permits calling reinforcements are affected.
This does not turn every enemy or fixed mission spawn into a spawner.

## Report and shared cause

The report described a dramatic increase in enemy spawning with 48 bodies
on Archives. The cause was in shared corpse handling and reinforcement AI,
so the correction applies across levels and to all retention counts.

Previously, `AI_IFChrDoesNotExist` immediately treated a retained corpse
as absent, skipping the original fade delay (90 NTSC ticks, 75 PAL ticks).
However, ordinary character lookup still found that corpse under its
script ID. Reinforcement guards normally use their caller's ID plus 10000.
`AI_TRYCloningChr` could spawn a replacement but could not assign that ID
because the retained corpse still occupied it. Subsequent AI checks kept
seeing the old corpse as absent, allowing additional replacements while
the earlier ones were alive. Retaining more corpses prolonged this state.

## Default behavior

Retained bodies have a separate clock matching the original fade delay.
Before it expires, script lookups still find the dead guard. Afterwards,
the visible corpse remains but its script ID is retired, matching normal
cleanup. The replacement can acquire the caller's clone ID, so later
checks recognize a living reinforcement and suppress another spawn.

Reducing retention or running short of character slots releases the oldest
bodies. If their original fade time has not expired, fading resumes from
that elapsed time. Once an ID is retired, changing the retention setting
does not restore it. Corpses still yield slots for real spawns.

## Optional difficulty behavior

Fast reinforcements explicitly allows the existing AI's “my clone does
not exist” check to request another guard even while a reinforcement is
alive. Existing alert/noise conditions and spawn failures still apply.
Each new guard becomes the caller's tracked clone; the previous one keeps
its unique generated ID. This preserves the enemy-surge effect without
making subsequent script commands address an old corpse or the wrong
replacement. Specific-guard mission checks keep their normal behavior.

Turning the option off stops allowing additional living reinforcements.
Guards already spawned remain in the mission. Body visuals and the
default corpse-removal timing remain independent of this setting.

## Validation

Run `python port/tests/test_reinforcements.py --verify-baseline`. It compiles
the production corpse ticks, character lookup functions, existence AI
opcode and cloning AI opcode against a small stub world with the actual
character and AI types. It verifies:

- Original NTSC/PAL fade timing with body counts Off/12/24/48, including
  paused time and the tick before expiry.
- Repeated replacement generations acquire the tracked clone ID while
  old bodies remain visible; a living replacement blocks another spawn.
- Disabling retention, capacity eviction and slot pressure resume fading
  without recapturing bodies or restoring expired IDs.
- Fast mode permits multiple living reinforcements at every body count,
  keeps their IDs unique, and tracks the newest one.
- Turning Fast mode off, network play and split-screen retain the normal
  living-reinforcement check; specific-guard mission checks are unaffected.
- The pre-fix source fails both the premature-removal and clone-ID checks.

`python port/tests/test_vr_display.py` also verifies the default Off value
and independent persistence with every body count on Quest and desktop.

Headset acceptance is pending: compare spawning on Archives and other
missions with each body count and Fast mode on/off, change the settings
mid-mission, test sustained combat and slot pressure, and verify mission
script progress (including Frigate hostages).
