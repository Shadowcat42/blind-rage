# Look-around beacon

Some scenes give the right stick a fixed camera to look around with, and react to what the player looks at. Episode 10's hot spring (`BC-S100_STE_A0020-0020`) is one: its prompt is `<RS> Look around` (`S100_LookAround`), and two of its achievements depend on where the player looks. The look-around beacon leads the view to each thing the game reacts to, in turn.

## How the game does it

- The scene's camera is a `BCCameraActorViewCheck`. `BCCameraActor.ViewModifyRotation` turns it with the right stick (`RawJoyLookRight`, `RawJoyLookUp`) within its rotation limits, and it eases back to centre when the stick is released.
- `BCCameraActorViewCheck.Tick`, while `bEnableViewCheck` is set and the camera is the player's view target, checks each entry of `ViewCheckTargetList`. A group counts once the camera's centre line stays within `FOV / 2 * CheckAngleRate` of one of its `Targets` for `StartTriggerTime` seconds. It then fires `BCSeqEvent_CameraViewCheck` with that group's number.
- The hot spring's camera has a 60-degree view and a `CheckAngleRate` of 0.3: a 9-degree cone. Its seven groups (names translated from the level's Japanese):
  0. Doji peeping from the cliff top: six spots, 0.1 s.
  1. A quick glance at a figure (大尊チラ見; the reading is uncertain): three spots, 0.25 s.
  2. Doji diving under the water to hide: seven spots, 0.25 s.
  3. The woman's face, for a while: 1 s.
  4. The woman's chest, for a while: 1 s.
  5. Augus and the woman, for a while: 1 s.
  6. Reset (リセット用).
- The level script turns these into the achievements: `GameEvent_GetTrophy_1` (View of the Valley) follows group 4, and `GameEvent_GetTrophy_2` (the peeping Doji) follows a count of the Doji groups. Exactly which combination the count needs was not traced.
- "Like a Fish" is the drinking choice instead: circle drinks (`S100_Drink`) and counts 1, triangle chugs (`S100_Hdrink`) and counts 2, and a total of 4 fires `GameEvent_GetTrophy_0`.

## Behaviour

While a look-around camera is checking views, the beacon points to the nearest target of a group not yet found, measured from the centre of the view:

- Stereo for left and right, pitch for up and down, like the shooting beacon (a triangle wave).
- Pulses quicken as the view closes in, and the tone is steady once the view is inside the game's cone.
- Holding the view there for the group's time, plus 0.2 s, counts the group as found, and the beacon moves on to the next. Groups named as a reset are skipped.
- It stops when every group is found or the scene ends.

`look_beacon=1` (default) enables it, also in the settings menu; `look_beacon_volume` sets its share of the cue volume. The log records `LOOK_TARGET group= angle= cone= remaining=` when it moves to a new group and `LOOK_FOUND groups= remaining=` when one is found.

## Data

The camera's fields are looked up by name: `Camera.ViewTarget` on the player's camera, and `bEnableViewCheck`, `CheckAngleRate` and `ViewCheckTargetList` on the view target's class. The list's element is `BCViewCheckTargetParams`, reached through the array property's inner struct property (both at `+0x64`, checked by class and struct name); its members (`GroupName`, `Targets`, `StartTriggerTime`) are looked up by name too. The view comes from the player camera's cached view, as for the other beacons.

## Not yet verified

- All of it in a live session, including that the player's camera reports this camera as its view target during the scene, and that the inner struct property sits at `+0x64` (an inference from the bool property's mask at the same offset; a mismatch leaves the beacon silent rather than wrong).
- The beacon counts a group as found by its own timing, not by reading the game's event, so a group could be counted a moment early or late.
- Which hot spring groups the peeping-Doji achievement needs. The beacon leads to all of them except the reset group.
