# Walking beacon

Walking sections advance through Kismet touch triggers (`SeqEvent_Touch` on a
trigger volume) rather than on-screen markers. Episode 4's first playable section
(`BC-S030_L01_PLY_01`) has three: two play scenery animations, the third fires
`GameEvent_Proceed`. A survey of every playable package found touch triggers in
nine sections; they advance the episode, spawn enemies, play scenery or open a
path, set camera targets or stream levels. None damages the player. The scripts
store no order between them.

## Behaviour

In melee mode (`PDEMode` 1), outside aimed fire, cinematics, pause and menus, the
beacon points to the nearest enabled touch trigger that has not fired
(`TriggerCount` 0), route triggers first (below). When it fires, the next nearest
becomes the target. While any
live enemy pawn (a different `TeamType`) exists, the beacon is silent, so it
neither competes with combat nor leads away from a fight; some triggers spawn
enemies mid-walk.

- A sine tone (the shooting beacon is a triangle wave, the melee beacon a
  lower square wave), at a steady pitch: 525 Hz while the trigger is within 90 degrees of the camera's
  facing, 440 Hz when it is behind.
- Stereo: the side, relative to the camera; fully left or right at 90 degrees and
  beyond.
- Pulse rate: slow beyond 3000 units, quickening as the horizontal distance
  shrinks, continuous within about 270 units.

### Route triggers first

A survey of every playable package, DLC included, found 35 touch triggers. Those
that move the episode on, spawn enemies or set counters fire once
(`MaxTriggerCount` 1, the default). Those that can fire any number of times
(`MaxTriggerCount` 0) change camera angles, play scenery or NPC reactions, stream
levels or switch other triggers. Episode 12's opening street
(`BC-S130_L03_PLY_01`) switches such triggers on behind the player as they walk
on (they play the girl's reactions), and nearest-first guidance used to turn the
player around and lead them in circles.

So the beacon prefers route triggers: one-shot triggers, and a repeatable trigger
that switches on (`SeqAct_Toggle` input Turn On or Toggle) a one-shot trigger that
is still off and unfired. Episode 10's `BC-S100_L01_PLY_02` needs that exception:
its repeatable `Touch_9` switches on the one-shot trigger that spawns the next
enemies. Other repeatable triggers are used only when no route trigger is
waiting. The route is still trigger by trigger in straight lines; on Episode 12's
street it becomes the two scenery triggers and then the exit.

The fields are looked up by name in the touch event's class
(`SequenceEvent.MaxTriggerCount`, `SequenceOp.OutputLinks`/`EventLinks` and their
structs' `PropertiesSize`) and logged once as `WALK_FIELDS`; if any is missing,
every trigger counts as a route trigger, as before. Links through hubs are not
followed (none of the surveyed gates uses one).

### Burst guides

Some doors open with Burst rather than a trigger. Episode 9's last door
(`BC-S080_L05_PLY_01`) has no touch triggers: once the enemies are dead and the
Mantra gauge is full, a `BCSeqAct_BurstGuide` stores the door as
`BCPlayerController.BurstGuideTargetActor`. The HUD's `TickBurstTarget` shows the
R2 prompt only once that target is in sight and near (or the player is inside a
linked burst area); until then sighted players follow an on-screen arrow
(`UpdateBurstGuide`). While a guide target is set, the beacon points at it instead
of any trigger, with the same tones, and the log adds `burst_guide` to
`WALK_TARGET`. The field's offset is looked up by name in the controller's class.
The radius the game requires is not set by that level and was not found in the
class defaults.

`walk_beacon=1` (default) enables it; `audio_cue_volume` sets its loudness. The log
records `WALK_TARGET distance= bearing= remaining=` when the target changes.

## Data

The trigger list is rebuilt every two seconds (and when the world changes) by a
bounded walk of the Kismet sequences of the persistent level and every visible
streamed level, nested sequences included: `WorldInfo.StreamingLevels` (+0x334),
`LevelStreaming.LoadedLevel` (+0x44) and `bIsVisible` (+0x60), `Level.GameSequences`
(+0xa0, native, found in the saved title-update snapshot), `Sequence.SequenceObjects`
(+0xb0). Each player tick reads the triggers' state (`SequenceEvent.Originator`
+0xb8, `TriggerCount` +0xc4, `bEnabled` +0xd0) and the volume's `Location`.

## Known limitation: no route around obstacles

The beacon points in a straight line to its target. Where the route winds, the
line can cross walls, rocks or drops, and the player has to find a way around
(Episode 10's flashback: two dumps showed the player sliding along, then stopped
against, scenery while the beacon aimed correctly). F5 dumps list the position,
triggers and blocking volumes near the line at such spots.

A route finder that maps the level with the game's collision traces was built
and tested offline (branch `walk-pathfinder`), but it cannot trace yet: native
engine functions, `Actor.Trace` among them, do not run when the mod calls them
through `ProcessEvent` (in Episode 4, `Object.Sqrt(16)` returned 0 and a trace
changed no bytes), while script functions do. Tracing through the Engine script
`Weapon.CalcWeaponFire` on the default Weapon object crashed the game. The next
step is to read how the recomp's compiled `ProcessEvent` handles native functions.

## Not yet verified

- Live behaviour, including whether "nearest first" matches each section's
  intended route. Where a section's triggers are not laid out in walking order the
  beacon may lead to a later one first.
- The volume's `Location` is its brush origin, assumed to lie near its centre.
- Offsets are from the title-update build; the disc build has not been compared.
