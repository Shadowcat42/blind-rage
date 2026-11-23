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
(`TriggerCount` 0). When it fires, the next nearest becomes the target. While any
live enemy pawn (a different `TeamType`) exists, the beacon is silent, so it
neither competes with combat nor leads away from a fight; some triggers spawn
enemies mid-walk.

- Steady pitch: 525 Hz while the trigger is within 90 degrees of the camera's
  facing, 440 Hz when it is behind.
- Stereo: the side, relative to the camera; fully left or right at 90 degrees and
  beyond.
- Pulse rate: slow beyond 3000 units, quickening as the horizontal distance
  shrinks, continuous within about 270 units.

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

## Not yet verified

- Live behaviour, including whether "nearest first" matches each section's
  intended route. Where a section's triggers are not laid out in walking order the
  beacon may lead to a later one first.
- The volume's `Location` is its brush origin, assumed to lie near its centre.
- Offsets are from the title-update build; the disc build has not been compared.
