# Force pull, grab and catch (v0.5.15 batch)

Spec agreed with the owner on 2026-11-05, before any code. This builds on the
existing "Grip to hand" gesture (`VrGesturePickup`, GEVR PC vr451) and ports
upstream GEVR PC's force pull and pull-and-disarm. Its `docs/CONTROLS.md`
"Pickups, hands and arms" table marks both Passed (vr457). Only their docs
are public, so the behaviour below comes from those docs and the owner's
answers. The numbers are ours.

Prior art checked: Perfect Dark VR has no weapon pickup or pull (its grip
grab is PD's crate push, `MOVEMODE_GRAB`). Dab's Mod has nothing on VR
pickup. Its b6d83c3 (dropped guns deleted past PD's ±32000 box on GE maps)
matters if a pulled gun travels as a projectile. GoldenEye's own Magnet
watch only widens walk-over pickup from 100 to 350 units while
`MAGNETIC_HUM_SFX` plays (`propobj.c` objTickPlayer, `gunfire.c`). Nothing
moves.

## What the player does

- **Target:** point an empty hand at something within **3.5 m** (the Magnet
  watch's range) and in line of sight. Whatever is nearest the hand's
  pointing ray within a narrow cone (about 15°) is the target. Pointing down
  at small items searches a little wider (GEVR PC).
- **Pull:** squeeze the grip (GEVR PC exactly, no flick). The target flies to
  the hand in about **0.3-0.4 s** with a small upward arc, easing out at the
  palm.
- **Hold or let go:** still gripping when it arrives puts it in the hand.
  Letting go mid-flight sends it to the inventory instead. Nothing is ever
  dropped.
- **Arrival:** a gun is in the hand at once, with no draw (raise) animation,
  ready to fire. Ammo, armour, keys and mission items are collected with the
  game's pickup sound and text.
- **Close grab:** the existing 25 cm Grip to hand stays. It also works on a
  gun still falling from a dying guard (mid-air catch).

### What can be pulled

| Target | Result |
|---|---|
| Gun on the floor, or falling from a guard | In the pulling hand (rules below) |
| Gun GoldenEye would refuse (full ammo, pair already carried) | Flies in and is absorbed: the floor gun is gone, no ammo is gained, and the hand holds the copy you carry |
| Ammo, armour, keys, documents, mission items | Collected on catch |
| Non-gun the game refuses (full ammo, full armour) | **Held item:** follows the hand and drops onto whatever is below when you let go (GEVR PC vr457 "held items") |
| Gun in a living guard's or MP bot's hand | **Pull and disarm**: the guard lets go as when a hit makes him drop his guns (`chraction.c` `triggered_on_shot_hit`), then it flies. Never a human player's gun |
| Live grenade (a guard's, yours, or an opponent's online) | **Catch:** the fuse keeps counting. It stays in the hand while gripped, and letting go throws it with the hand's speed. You get the kill credit. |
| Armed mines, rockets, other explosives | Not pullable. The existing own-mine re-grab stays as it is. |

### Which hand

- Only an **empty** hand pulls (gun hand `ITEM_FIST`, off hand `ITEM_UNARMED`).
  A watch item in the gun hand (the watch laser and the rest fire from the
  watch) counts as empty, so that hand pulls, holds and takes a pulled gun.
- A gun lands in the **pulling hand** when the game's rules allow it
  (`gevrHandItemAllowed`: the gun can go in the off hand, there is a copy for
  each hand, the MP dual-wield setting allows it). If not, it goes to the gun
  hand when that hand is empty. Otherwise it goes to the inventory.

### Feedback (all on, with one "Pull visuals" toggle for glow and line)

- **Target glow:** the targeted item is brightened before the squeeze.
- **Pull line:** a faint line from the palm to the target while pointing.
- **Haptics:** a tick when a target locks, a buzz during the flight and a
  thump on the catch.
- **Magnet hum:** `MAGNETIC_HUM_SFX` plays during the flight, and the watch
  glows as it does in GEVR PC.

### Options (launcher Controls > Gestures and the watch VR page)

The defaults follow GEVR PC:

| Option | Default |
|---|---|
| Force pull | on |
| Pull and disarm | on |
| Auto pickup | on (new: off means walking over a gun leaves it, so you grab or pull it) |
| Grip to hand | on; WIP label dropped once headset-passed |
| Pull visuals | on |

## Online

- **Everywhere**, and others see the gun fly. A new message carries the prop,
  the puller's slot and hand, and the start time. Peers fly the prop to that
  player's tracked wrist (protocol 22 already sends both wrists).
- **The pickup is claimed when the pull starts.** Both outcomes collect it,
  so two players pulling the same gun resolve at the host: the first claim
  wins and the other pull is cancelled.
- Disarm and grenade catches are host-decided, like co-op guards. A client
  sends a request and the host drops the gun or hands over the grenade.
- This is a protocol bump (25 to 26). It goes out with the v0.5.15 release
  only, and test builds of this batch can't join v0.5.14 headsets or
  servers.

## Not changed

Walk-over pickup rules with Auto pickup on (radius, line of sight, the
look-down cutoff, full ammo), the Magnet watch, mine re-grab, hand reload's
belt grabs, body slots, two-handed hold, the hip holster and motion throwing.
In the grip order, the pull comes **last**. Everything that already claims a
grip goes first: GE-X magazines and the belt, two-handed hold, the watch,
body slots, a throwable's wind-up, the hip holster, mine re-grab, the 25 cm
grab, grip use on doors, and the boat and Horde stations.

## Built (2026-11-05)

Single player first, then online (protocol 26, below).

- **Pull:** `src/game/gevr_forcepull.c`, called from `bondview2.c`:
  - The pull is the last branch of `gevrGripGestureTry`, so every other grip
    job wins.
  - `gevrGripGestureTick` calls `gevrPullTick` after `propsTick`.
  - The draw sits beside the body slots' labels.
  - Reset happens at a new stage (`bondview_r.c`).
- **Arithmetic:** `port/src/gevr_pull.c` holds the cone and range score, the
  eased and lifted flight, and the hand rule. It is tested by
  `port/tests/test_force_pull.py`, which also runs it against GoldenEye's own
  math.
- **The real prop flies:** its position is set each tick, its tile is
  walked, and it re-registers its rooms as `objTick` does for a moving
  object. So it draws in its own art set (N64, HD, Bean or GE-X).
- **Targeting:** only props on screen count, nearest to the hand's ray
  (the barrel direction of a held gun).
- **Line of sight:** walking over uses the same rule (`objTickPlayer`): the
  tile walk from the player must be clear of doors and end on the prop's
  tile. A drop still in the air only needs the clear walk.
- **Glow:** the target's light is set to full gold. It eases back on its own
  when the hand moves off. There is also a pulsing halo ring and a line from
  the palm.
- **Instant equip:** `gunfire.c` `g_gevrSwitchInstant[hand]` skips the empty
  hand's lowering and the gun's raise. It clears when the gun is up, or
  after 1 s if no switch came of it.
- **Auto pickup off** applies to guns only (`gevrPullBlocksWalkOver` in
  `objTickPlayer`). Ammo, armour and keys are still picked up by walking
  over them, so a mission can't get stuck.
- **Disarm:** the target is the hand holding his gun, where the head's view
  last drew it (`chrRenderHeldWeapon` notes the hand's matrix, `basemtx`, in
  world space as it draws, since the extra view passes place guards again in
  other cameras' spaces). The gun's own matrices are often not written yet at
  that point (headset log: NaN and 1e38 values), so the hand stands for it. If it
  wasn't drawn in the last 10 ticks, the target is his feet plus 1 m. (The
  first build guessed his position plus 1.1 m, which put the ring and the
  line above his head on Dam.) The squeeze calls
  `propobjSetDropped(gun, DROPTYPE_DEFAULT)` and sets
  `CHRHIDDEN_DROP_HELD_ITEMS`, as `triggered_on_shot_hit` does when a hit
  makes a guard drop his guns. His next tick lets go, and the flight starts
  from there. If he still holds it after 0.5 s, the pull gives up. There is
  no hit-reaction animation.
- **Held items and grenades:**
  - A caught thing follows the hand's point while the grip is held.
  - On release, `sub_GAME_7F03FDA8` gives it a projectile with the hand's
    speed (world units a second / 60 per tick), owned by the player.
  - A grenade's owner bits become the player's, so its blast is credited
    to him.
  - The fuse keeps running in `propobj.c`'s weapon tick.
  - A grenade let go of mid-flight drops where it is, moving as it was.
  - The 25 cm grab (Grip to hand) also takes a live grenade or a refused
    item into an empty hand.
- **Settings:** `GestureForcePull`, `PullDisarm`, `AutoPickup` and
  `PullVisuals` (all 1 by default). `GestureGripToHand` now also defaults
  to 1, but an ini that already saved 0 keeps it off. These are on the
  launcher's Gestures page and the pause panel's VR tab.
- **Tuning:** the numbers are the 350 cm range, the 15° cone (25° pointing
  down), the 0.30-0.40 s flight and the 20 cm lift (`gevr_pull.h`).
- **Watch glow:** a gold disc on the left watch face, pulsing, while a
  pull flies or waits on a guard, and fading over 0.3 s after the catch. It
  sits on the face's tracked frame (`gevrStereoWatchFace` in `bondview2.c`),
  at the watch's size, and fades out as the face turns away from the eye.
  It is off with Pull visuals.

## Built online (2026-11-05, protocol 26)

Protocol 26 adds one message, `NET_MSG_FORCE_PULL` (`net_core.c`, read and
checked as `NetForcePull` in `net_protocol.h`). It goes from the puller's
headset to the host, which relays it to everyone else. Its phases are in
`gevr_pull.h`:

- **FLY:** a flight begins. Each other headset flies its own copy of the
  prop to the puller's tracked palm (`gevr_avatar.c` `gevrNetRemoteHandWorld`,
  from protocol 22's wrists; a flat player or bot: the chest). A setup
  pickup is named by its index. A dropped gun or a grenade lives in each
  headset's own pool, so it is named by item and position, and each peer
  takes its nearest loose copy (1.5 m; a grenade 4 m).
- **Claims:** for a setup pickup the host checks first. If its own pull or
  another player's already has it, the host doesn't relay the FLY and sends
  REFUSED to that player alone; their prop drops where it is. A headset that
  gets the winner's FLY while its own pull has the prop lets it go.
- **HOLD, RELEASE:** a caught grenade or refused item follows the puller's
  palm on every headset, and is let go from the sent point at the sent speed.
  A grenade becomes the catcher's at the catch on every headset (owner bits),
  so the catcher's headset sends its blast (`explosion.c`) and the other
  copies wait for it. The thrown grenade carries the catcher's fuse.
- **DONE:** collected or absorbed. A collected setup pickup goes with its
  own OBJECT_STATE pickup message, as walking over sends it. A dropped gun's
  copies, and an absorbed pickup, are freed on the other headsets. An
  absorbed deathmatch gun that respawns is taken as walking over takes one,
  so its timer starts, instead of being deleted.
- **CANCEL:** the flight was lost. The copies drop where they are.
- **DISARM:** co-op only, not Horde. The guard is named by his host slot
  (`net_coop.c` `netCoopGuardHostSlot`). The host's guard lets go, the copies
  elsewhere do too, and the gun's FLY follows. A copy not found yet is
  searched for each tick for 1.5 s. A player's wait for the guard is 1 s
  online (0.5 s offline).
- **Guards' grenades, rockets and launcher rounds** (co-op and Horde,
  `NET_MSG_COOP_SHOT`): until now they existed only on the host, so a client
  saw just the blast. The host now sends each one once it is airborne (a
  thrown grenade gets its flight on the guard's next tick). The message
  carries the model, item, fuse, the projectile's flags, position, speed,
  rocket thrust, orientation and spin, and the guard's host slot so it
  ignores him. Clients fly a copy that never goes off on its own
  (`explosion.c` asks `gevrCoopGuardCopy`). The host's blast arrives
  instead and removes the nearest copy. A client can catch a guard's
  grenade copy: HOLD makes the host's real grenade follow that player's hand
  and become theirs, and it stops counting as a guard's on every headset
  (`gevrCoopGuardForget`). The catcher's headset then sends its blast.
- **Not covered:** bots are players, not guards, so they can't be disarmed.
  Walking over a dropped gun is still not synced (as before); only a pulled
  one is.

## Headset test sheet

1. **Facility, a gun on the floor 2-3 m ahead.** Point the empty off hand at
   it. It should light up gold with a ring and a line, with a light tick in
   the hand. Squeeze and hold: it flies over in about a third of a second
   and is in the off hand, ready to fire, with no raise.
2. **The same, letting go mid-flight:** it lands in the inventory and no
   hand changes.
3. **A rifle or sniper rifle with the off hand:** it goes to the gun hand if
   that is empty, else to the inventory.
4. **Shoot a guard.** Point at his gun as it falls: it is caught in the air.
5. **Disarm.** Point at a live guard's gun and squeeze: he lets go and it
   flies to you. Switch Disarm off on the Gestures page and he keeps it.
6. **Full ammo.** Pull a gun you are full on: it flies in, vanishes, and
   your own copy is in the hand.
7. **Full armour.** Pull armour: it stays in the hand. Let go and it drops,
   or throws if you swing.
8. **Grenade guards (Surface, Bunker).** Pull or grab a live grenade: it
   stays in the hand. Throw it back and the blast kills are yours. Hold it
   and it goes off in your hand.
9. **Auto pickup off.** Walking over guns leaves them, while ammo and armour
   still collect.
10. **No pull should steal the grip** from the two-handed hold, the reload
    belt, body slots, the holster or doors.
11. **Watch glow:** during any pull, the left watch face glows gold and
    pulses, then fades after the catch. Turn the wrist away and it goes.
12. **Bean Facility frame time** with a target aimed at (the line and the
    rings are blended).

### Online (two headsets, or a headset and a friend)

13. **Others see it fly.** In a deathmatch, pull a floor gun: on the other
    headset it flies to your hand and is gone from the floor.
14. **Both pull one gun.** Squeeze at the same moment: one of you gets it;
    the other's drops where it was.
15. **A dropped gun.** Kill the other player and pull their dropped gun: it
    flies on both headsets and is gone from both.
16. **Grenade catch.** Catch the other player's grenade and throw it back:
    they see it in your hand and flying back, and the kill is yours.
17. **Co-op disarm**, as a client: the host sees the guard let go and the
    gun fly to you.
18. **Co-op guard grenades and rockets, as a client** (Surface, Bunker,
    Silo): you see them fly now, not just the blast. Catch a guard's grenade
    and throw it back: the host sees it in your hand, and the blast is yours.

## Risks to check in the headset

- The off hand pointing at a floor gun while reaching for the two-handed
  hold or the belt: these come first, so the pull should only take a grip
  nothing else claims.
- A pulled prop crossing rooms in flight (props live in rooms), and Dab's
  b6d83c3 deleting projectiles on large GE maps.
- Bean frame time with glow and line on Facility (the decal band and LRZ
  history in `docs/bean-performance-research.md`).
- A grenade exploding in the hand: Bond takes the blast, as in the original.
