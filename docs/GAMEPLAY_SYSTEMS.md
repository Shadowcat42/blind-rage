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
`TakeDamage`; counts are capped (a capture shows 999 lock-on shots).

Points come from the episode's `BCMapInfo.ResultArchetypeNormal` (a `BCResultParam`).
Episodes 1 and 2 override only the rank thresholds, so the point values are the class
defaults (`Default__BCResultParam`): Burst 1000, Attack 2, Dash Attack 10, Dive Attack
10, Heavy Attack 20, Shot 1, Lock-On Shot 5, Reaction snatch 30, Special (finishing)
Attack 50, cutscene inputs 100, Restart -1000. Both episodes' result screens agree:
shots 1 point each, lock-on shots 5. The `GameParam.Result.ResultDefault_*` objects
hold different values (Shot 10, snatch 100) but are not what these episodes use.

Attack type to category (`BCPawn_Enemy.BCTakeDamageCore`): 1 combo → Attack, 19 charge
strike → Dash Attack, 12 → Dive Attack, 13 swing → Lock-On Shot under
`BCPlayerController_HeavyShoot` and Heavy Attack otherwise, 22/24 → Shot, 26 →
Special Attack; reaction-snatch damage counts as Reaction snatch.

Penalties: `SubBattlePoint_Damage` returns immediately in this build, so hits taken
cost no points. The -1000 is per restart after a Game Over
(`BCGFxUI_GameOver.OnRestart` → `IncActionCount_SubType`).

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

Buttons use the same controller code as melee. The heavy-attack button (triangle by
default) fires the lock-on shots: `BCPlayerController_HeavyShoot.StartSwing` launches
one swing per `LaunchCountPerMotion` locked targets, attack type 13. The shot button
(square) is rapid fire, as in melee (below). Episode 1's tutorials say the same:
`<Shoot> Rapid Fire`, `<HAssault> Lock-On Fire`.

## Melee (mode 1)

### Buttons

Actions map to face buttons by control type (`BCSaveParam.ActionButton2GameButton`;
game buttons 0 A, 1 X, 2 Y, 3 B on the Xbox layout). Type A is the default
(`InitSystemParam` → `SetKeyConfigType(0)`):

| Action | Type A | Type B | Type C | Event |
| --- | --- | --- | --- | --- |
| Assault (light) | B (circle) | X (square) | X (square) | `PlayButtonEvent*_Assault` |
| Heavy assault | Y (triangle) | Y | Y | `PlayButtonEvent*_Swing` |
| Shot | X (square) | B (circle) | A (cross) | `PlayButtonEvent*_Throw` |
| Jump | A (cross) | A | B | `PlayButtonEventPress_Jump` |

Fixed: L1 lock-on, L2 `ExciteCore`, R1 evasion, R2 `MantraTrigger`, R3 camera reset
(`BCPlayerInput.InitializeButtonInputHandlers`: slot 7 → `HandleButtonInput_L1` →
`PlayButtonEventPress_LockOn`; the mod's L1 tracking reads the same slot 7, and
holding physical L1 drives it). Open question: Episode 3's tutorial text reads
`<RB> Lock On`, which disagrees with the script. Not yet checked in game.

### Attacks

- Light attack: the combo starts on release (`ProcessAssaultInput(1)` →
  `StartAttackPC`, combo system attack type 1 `COMBO`).
- Holding the light attack for `DiveAttackButtomRepeatTime` (0.2 s): on the ground
  the Dash Attack (type 19 `CHARGESTRIKE`, `StartChargeAssault`; tutorial `Hold
  <Assault> to perform a Dash Attack`); in the air the Dive Attack (type 12
  `GROUNDCRASH`, `StartDiveAttack`; tutorial `After pressing <Jump>, hold <Assault>`).
  These tutorials are in Episode 3 (`BC-S020_L01_PLY_01`); Episode 2's only tutorial
  is `S010_Assault` (attack and combo).
- Heavy attack: `StartSwing` (type 13 `SWING`), or the finishing attack when one is
  available (below).
- Shot: holding the shot button is rapid fire. `BCPlayerInput.InputCheckTick` passes
  the shot button's hold state to `PlayButtonEventTick_ChargeAimShot`, which starts
  `StartAimShotChargeWait` (type 14) and then `StartAimShot` (type 24); projectiles
  fired in type 14 become type 24, and releasing ends the aimed fire
  (`ProcessThrowInput`, `EndAimShotExec`). The left stick moves the reticle during
  rapid fire (tutorial `S020_ShootDescript`). A tap fires `StartThrowAssault` (type 22)
  on release. Types 22 and 24 both count as Shot.
- Evasion (R1): `StartEvasion` toward the left stick, or forward without input;
  `NextEvasionInvalidTime` 0.7 s; the animation grants a brief invincibility window
  (`BCAnimNotify_EvasionInvncible`).
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
34, icon `Finish_Y`) and sets it active; other downed enemies get the same icon set
inactive as guides. The heavy-attack press then starts `StartFinishAttack` instead of
a swing. The game calls it a Special Attack (result screen, tutorial). The mod says
"Special attack, triangle." when an icon becomes active, once per activation.

### Reaction snatch

Assault, heavy and shot presses first call `TrialSnatchInput(1/2/3)`.
`ShowReactionSnatchButton` creates a HUD prompt `SnatchButton_N` for the window
(button types 30-33 and 10 or 35, shown as e.g. `Y4`) and removes it when the window
ends. The tutorial calls it a counterattack. The mod says "Counter, triangle." (with
the icon's button) once per window.

The button comes from the enemy animation's `BCAnimNotify_CounterGrabHit`
(`SnatchButtonType`, `EBCSnatchButton`): 1 light attack, 2 heavy attack, 3 shot,
4 jump, 6 R2 (`MantraTrigger`); type 5 (HUD button 10) is not identified. The press
must match (`TrialSnatchInput`). `GetDisplayButton`: on Normal or Hard, a notify with
`AddRandomSnatchButtonType` picks one of the alternates or the base type at random
(`Rand(Length + 1)`); Easy always uses the base type.

In the package data every counter's base type is heavy attack. Episodes 1 and 2
(`ray01`, `jzo01`, `jzo02`, `mhr01`) have no alternates, and Episode 2's log shows
`SnatchButton_0` with icon `Y4`. Some counters in Episodes 3, 5 and 7 add light attack
as an alternate, so on Normal or Hard they can ask for circle. The mod reads the
icon the game shows; a light-attack counter has not yet been seen in a log.

### Damage

Episodes 2 and 3 never lower Asura's health: their player controller archetypes
(`010_PDE01_P.Asura.PDE01_P_controller`, `020_...`) set `bAlwaysHealthMax`, and
`BCPawn_Player.SetHealthVal` then sets health to `HealthMax`. Hits still land and
play reactions (`BCPawn.TakeDamageModifyApplyDamage` subtracts, then `SetHealthVal`
restores). No other episode package sets it. Other paths that block damage: the
evasion window, `IsInvincibleWithReact` (`DefaultInvincible`, hit reaction but no
loss), and the debug god-mode flags, which only keep health at 1 or more. The player's
damage is also scaled by the gauge skin's `DamageRate` (`SetupParamCorrect_UISkin`).
