# Gameplay systems reference

Verified in the title-update build's UnrealScript, the embedded menu/HUD ActionScript
(Scaleform movies in the game packages) and Episode 1-2 package data, unless a
statement is marked as an inference. Offsets are for the title-update build.

## Gameplay modes

The player pawn's `PDEMode` (`EBCPDEMode`) selects the gameplay style. Kismet actions
`BCSeqAct_ChangePDEMode*` call `BCPawn.ChangePDE(n)`.

| Mode | Name | Used for |
| --- | --- | --- |
| 0 | Default | |
| 1 | Playable | third-person melee (Episode 2) |
| 2 | Tokkou | charge/rush sections |
| 3 | Missile counter | |
| 4 | Shooting | rail shooting (Episode 1) |
| 5 | Jump move | |
| 6 | 2D battle | |
| 7 | Falling battle | |

Packages: episodes are `BC-S000` (Episode 1), `BC-S010` (Episode 2) and so on.
`_PDE_` packages are shooting sections (`BCSeqAct_ChangePDEModeShooting`, controller
`BCPlayerController_HeavyShoot`); `_PLY_` packages are playable melee
(`BCSeqAct_ChangePDEModePlayable`, base `BCPlayerController` in state
`BCPlayerCombat`); `_STE_` packages are cutscene/QTE scenes, including synchronic
tracks; `_TBC_01` shows To Be Continued and grants the episode's Extras unlocks;
`BC-EndCredit_S0x0` holds the interlude.

## Scoring

`BCResultInfo.PlayJudge`: battle points are, per attack type, points per damaging hit
times the number of hits, plus Unlimited-mode bonuses, minus penalties. Hits are
counted by `IncActionCount_AddType` from enemy `BCTakeDamageCore` and aircraft
`TakeDamage`. Values from the live result object in captures (not overridden in
Episode 1 packages): Burst 1000, Assault 2, Charge strike 10, Dive attack 10, Heavy
assault 20, Shoot (attack types 22/24) 10, HeavyShoot (13 in rail sections) 5,
Reaction snatch 100, Finish attack 50, cutscene inputs 100; each hit taken -1000.

## Rail shooting (mode 4)

See `SHOOTING_BEACONS.md` and `LOCK_TRACE.md` for detail. In brief: the lock cone's
axis runs from the camera through the cursor (`AimShotAimPoint`), clamped to the
title-safe frame; the limit is `clamp(Min + (Max - Min) * (1 - distance / 3000))`
(Episode 1: 3.6 far, 50 near); `CheckLockOnShotTarget` also needs
`LockOnShotLockDistance` and a line-of-sight trace to the target's owner; lock time
is 0 in Episode 1. Lock levels (`LockOnLevelParams`): level 0 at 1 lock, 1 at 4,
2 at 8 (Episode 1's maximum); the level rises one step every 0.15 s while not
firing. Only level 2 adds explosion damage (`G_EX`, radius 800), so full volleys
score far more.

## Melee (mode 1)

### Buttons

Actions map to face buttons by control type (`BCSaveParam.ActionButton2GameButton`;
game buttons 0 A, 1 X, 2 Y, 3 B on the Xbox layout):

| Action | Type A | Type B | Type C | Event |
| --- | --- | --- | --- | --- |
| Assault (light) | B | X | X | `PlayButtonEvent*_Assault` |
| Heavy assault | Y | Y | Y | `PlayButtonEvent*_Swing` |
| Shot | X | B | A | `PlayButtonEvent*_Throw` |
| Jump | A | A | B | `PlayButtonEventPress_Jump` |

Fixed: L1 lock-on, L2 `ExciteCore`, R1 evasion, R2 `MantraTrigger`, R3 camera reset.

### Attacks

- Light attack: the combo starts on release (`ProcessAssaultInput(1)` →
  `StartAttackPC`, combo system attack type 1 `COMBO`).
- Holding the light attack for `DiveAttackButtomRepeatTime` (0.2 s): on the ground
  a charge strike (type 19, `StartChargeAssault`); in the air a dive attack (type
  12 `GROUNDCRASH`, `StartDiveAttack`).
- Heavy attack: `StartSwing` (type 13 `SWING`), or the finishing attack when one is
  available (below).
- Shot: `StartThrowAssault` (type 22 `THROWASSAULT`) on release.
- Evasion (R1): `StartEvasion` toward the left stick, or forward without input;
  `NextEvasionInvalidTime` 0.7 s; the animation grants a brief invincibility window
  (`BCAnimNotify_EvasionInvncible`).
- No separate dash-attack input was found. Inference: attacks close distance by
  moving toward the current target (`BCPlayerCombat.RootMotionOfPawnExtracted` reads
  the target), not traced in detail.
- Attacks, grabs, finishers, Burst and aimed shots turn Asura to the current target
  when they start (`SetRotation2Target`). During combo follow-ups the left stick
  steers instead unless locked on (`AdvComboRotRevise`).

### Automatic targeting

`BCTargetLockControl.UpdateTargetingActor` re-picks a target every 0.2 s (`bAuto`):
enemies within `LockOnDistance` (500) in line of sight, excluding allies and dying
or dead enemies, scored by `(1 - distance / 500) * dot(facing, direction)`; a target
component of priority 3 wins outright (`GetBestLockTarget`).

### L1 lock-on

`PlayButtonEventPress_LockOn` works only in modes 1, 6 and 7 and when the level
allows it (`bActionLockOn`). It searches `FixLockOnSearchDistance` (5000) for the
nearest on-screen enemy, else the nearest off-screen one within the lock-on camera's
pitch limit, then fixes the target and switches to the lock-on camera. L1 again
releases it. The right stick left/right switches to the next enemy in that
direction by camera yaw. The lock drops (`IsCanFixationLockOnRelease`) beyond
`FixLockOnDistance` (20000), when the enemy dies or has been dying over 0.7 s, when
Asura dies or is in a grab clutch, when grabbing that enemy, outside modes 1/7,
in a cutscene, or (if the level enables masking checks) after moving 700 units with
the enemy hidden. On a drop it immediately tries the next nearest enemy. Episode 2
does not override these distances; its second playable section forces the lock-on
camera (`BCSeqAct_ChangeCamModeLockOn`).

### Finishing attack

`UpdateFinishAttackTargetList` looks for an enemy within `FinAtkTargetSearchDist`
(500) that accepts a finisher: small enemies (`FinishAttackPawnType` 0) when knocked
`Down` (not special move 2) or in special move 35; larger types have their own rules.
While Asura is on the ground and can swing, `Tick_FinishAttackButton` shows a HUD
button over that enemy (`BCHUD.StartShowButton`, group `FinishAttack`, button type
34, icon `Finish_Y`), and the heavy-attack press starts `StartFinishAttack` instead
of a swing. The mod's presentation hooks already receive this icon (`QTE_ICON
... input=Finish_Y`) but do not narrate it.

### Reaction snatch

Assault, heavy and shot presses first call `TrialSnatchInput(1/2/3)`. A HUD prompt
`SnatchButton_0` (seen as `input=Y4`) marks the window; this is the counter worth
100 points per hit. Also received but not narrated yet.

### Damage

No player invincibility was found in Episode 2 data: the only `bInvincible` values
are the evasion window and one enemy spawner (`BCSeqAct_AIFactory`).
