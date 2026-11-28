# Melee rapid-fire aiming

Outside rail shooting, holding the Shot button (square by default) fires aimed
shots. Episode 3's tutorial uses this against flying enemies and incoming
projectiles, and lock-on is disabled there. The shooting modes from
[rail shooting](SHOOTING_BEACONS.md) apply here too. Beacons, L1 tracking and the
priority switch have been used live in Episode 3; boosted homing has not been
confirmed live yet.

## What the game does

Verified in the title-update scripts and reflection:

- `BCPawn.AttackType` (+0x454) is 14 during the aimed-fire charge wait and 24
  during rapid fire (`StartAimShotChargeWait`, `StartAimShot`). A quick tap fires a
  single shot (type 22) instead.
- `BCPlayerCombat.AimShotAimingCtrl` moves the cursor (`AimShotAimPoint`,
  controller +0x6b8) with the left stick, as in rail shooting. In melee, a cursor at
  the screen edge also turns the camera, within `AimShotCamParam` limits.
- `SearchSetAimShotHomingTargetActor` picks what a shot homes on: the map's
  projectile cache (`BCMapInfo.ChacheList_BCProjectile`, +0xc0) and every pawn,
  minus the player, the player's own projectiles and joint-struggle pawns. A
  candidate must be in front of the camera (dot > 0.1) and within
  `1 - dot(camera-to-cursor, camera-to-target) < 0.001` of the cursor line, about
  2.56 degrees. The nearest such candidate wins.
- Lasers cannot be shot down: `BCProj_Laser_Base.TakeDamage` is empty. Other
  projectiles lose `Health` and are destroyed.
- While locked on, rapid fire aims at the locked enemy
  (`SetRotationAimShootToFixLockOn`, `IsCanFireAimShotProjectileToTarget`), unless
  Asura is airborne.

## What the mod does

While the player is in aimed fire (types 14 and 24), the player tick copies the
cursor, camera and candidates into the same snapshot rail shooting uses, so the
beacon, L1 tracking and target selection are shared.

- Candidates: enemy projectiles (not the player's, not exploded or temporarily
  destroyed, not the laser family) and live pawns whose `BCPawn.TeamType` differs
  from the player's. Each is marked as a projectile or an enemy.
- Priority: `aim_priority=projectiles` (default) or `enemies`. While any target of
  the preferred kind is reachable, guidance and tracking use only that kind; with
  none left they use the other. D-pad Down switches it during rapid fire and saves
  it ("Projectiles first." / "Enemies first."); the keyboard equivalent is the
  `aimpriority` hotkey (default Slash).
- D-pad Left/Right selects the previous/next target group and says what it is and
  where, for example "Enemy, left, high." or "Projectile, ahead."
- D-pad Up cycles the shooting mode, as in rail shooting.
- The beacon becomes continuous at the game's own homing angle rather than at
  4 degrees.
- Holding L1 steers the cursor onto the selected target in every mode; moving the
  stick takes over. During aimed fire the game's lock-on press
  (`PlayButtonEventPress_LockOn`) is skipped, so L1 does not also lock onto an
  enemy. Lock-on outside aimed fire, and a lock made before firing, are unchanged.
  This is the only place the mod withholds a game input.

### Boosted

Boosted mode widens the homing angle by `boost_homing` (1-10, default 3: about
7.7 degrees, capped at 30). The threshold is a float constant in the function's
own bytecode (`UStruct.Script`, function +0x4c): `EX_FloatConst` (0x1e) at offset
0x388, followed by the big-endian 0.001, then `EX_EndFunctionParms`, in a
1220-byte script. The mod checks those bytes and the script size before writing;
if they differ (another build), boosted leaves melee homing unchanged. The value is
written before each search while boosted, and the original is put back when
another mode is selected or the mod stops. `GAMEPLAY_STATS` in a diagnostics dump
reports `lock_on_skips`, `homing_calls`, `homing_hits` (searches that found a
target) and `homing_patch` (1 widened, 0 original, -1 unrecognized code).

## Melee beacon

Outside rapid fire in melee sections (`PDEMode` 1), a beacon points to an enemy.
The game keeps its current target on the controller's `BCTargetLockControl`
(`BCPlayerController.TargetControl`): `SetTargetInformation` stores `LockedActor`,
and `bFixLock` is set while L1 lock-on holds it. Without a lock,
`UpdateTargetingActor` re-picks `LockedActor` every 0.2 s from enemies within
`LockOnDistance` (500) in line of sight, favouring those in front, and attacks turn
Asura toward it when they start.

- With a target the game picked itself, the beacon points to it with a steady tone:
  attacks will turn to it.
- Locked on with L1, it points to the locked enemy, steady within 500 units and
  pulsing beyond.
- Otherwise it points to the nearest live enemy, always pulsing, faster as the
  distance shrinks (slowest beyond 3000 units).
- A band-limited square wave, an octave below the other beacons: 262.5 Hz ahead,
  220 Hz behind. Stereo shows the side relative to the camera. The walking beacon
  (a sine) is silent while enemies live, and the shooting beacon (a triangle) only
  plays during rapid fire, so they never overlap.
- Quiet during a counter or special attack, so their animation and sound come
  through. A counter is `PlayCounterGrab` -> `DoSpecialMove(21)`. A special attack
  is `StartFinishAttack` -> `PlayAttack(26)` (`BCATT_FINISHATTACK`), whose
  `InitFinishAttack` does `DoSpecialMove(50)`. The beacon is quiet while
  `BCPawn.SpecialMove` is non-zero or `AttackType` is 26; normal combos keep it.
  The log records `MELEE_MOVE special_move= attack_type=` on each change.

`melee_beacon=1` (default) enables it, also in the settings menu. D-pad Up toggles
it in melee outside rapid fire (the game's own D-pad handlers are empty), and so
does the `aimmode` key; during rapid fire both still cycle the shooting mode. The
log records `MELEE_TARGET distance= bearing= locked= reach=` when the target or
its reach changes. Field offsets are looked up by name in the controller's and
target control's classes.

## Sword waves

Episode 11's second section (`BC-S110_L02_PLY_01`) is a long-range fight with Augus (`BCPawn_ogs02`, `BCAI_EnemyBossOgs02`): far away he only fires waves, and when the player comes within 500 units he dashes off to a far point. His waves (`BCProj_Ogs`) fly straight along the ground at 3000 units per second. The upright blades (`_length` archetypes) come as a fan of seven, straight at the player and 0.2, 0.4 and 0.6 rad to each side; the wide horizontal wave (`_cross`) has nine extra side hit areas (`AddColNum` 9).

In melee, including rapid fire, the mod follows these waves from the map's projectile cache (`src/wave_guide.h`), using their world velocity so the player's own movement doesn't bend the prediction:

- Each wave's hit cylinder is read from the game: each cylinder's `Translation` is turned by the projectile's actual rotation. The blades' cylinders are 400 tall and 50 wide (`CollisionHeight` from `Default__BCProj_Ogs`), so `BCProj_Ogs.Init` rolls them 90 degrees (`bTrans`) and their (-150, -350, 0) offset points back and up: horizontally the cylinder runs along the blade's own path. The wide wave's nine extra hit areas are separate zero-speed projectiles riding along; they are skipped, and the wide wave counts as half its whole width (`AddColNum` areas `AddColRelativeLocY` 250 apart, plus its 250 radius).
- Fan blades: silent while every blade will pass by (further than its radius plus the player's, 60, from the player's centre). When one will hit within 2.5 s, the fire warning's buzz plays fully to one side, toward the wider gap to the next blade (with equal gaps, away from the blade), louder as it nears.
- The wide wave: when its path comes within 1200 units, a 150 ms sweep from 500 to 1000 Hz ends at the moment to jump, 0.6 s before the wave arrives (0.25 s was too late in play). That lead is an estimate (`JumpSpeedZ` 1000; the first 0.13 s of a jump can't be hit, `JumpStartInvalidDamageTime`) to tune in play, as is the wave's half width.

Both start during the wind-up, before the wave exists. Each attack animation fires its waves from a `BCAnimNotify_ShotArcheType` notify, whose `ShotInfo.ProjectileArcheType` names the wave. Augus's fan swing (`atk01`, 1.67 s) fires at 0.729 s; the glow comes at 0.21 s and the swing sound at 0.54 s. His wide swing (`atk02`) fires at 0.746 s. The special's barrage (`Spl01_ed01`/`ed02`, alternating fan and wide) fires 0.142 s into each swing. The mod scans each enemy mesh's `AnimTickArray` once a second for `AnimNodeSequence` nodes whose sequence fires a `BCProj_Ogs`. While such a node plays (`bPlaying`, `NodeTotalWeight` above 0.01), the shot time minus `CurrentTime`, divided by `Rate` times `RateScale`, gives the time until the wave fires. Its shape (speed, width) comes from the archetype.

- During a fan wind-up the buzz plays panned right: the middle blade will be aimed at the player (the swing homes in, `HomingTime` 1 s), and the gaps either side are equal. That aim is an inference from the AI's homing parameters. Once the blades exist, their real gaps decide.
- A wide wind-up schedules the jump from the predicted arrival (time to the shot plus distance over speed). The spawned wave arrives within 0.5 s of that and shares the cue.
- At close range this turns about 0.2 s of warning at 600 units into about 0.95 s.

Both use the fire warning's volume share. The log records `WAVE_SEEN` for each wave (width, cylinder offset), `WAVE_STEP left|right|off`, `WAVE_JUMP in <ms>`, `WAVE_WINDUP` for each wind-up (time to the shot and arrival), `WAVE_NODES` per enemy (wave-firing nodes found) and `WAVE_FIELDS` once (the notify array layout read by reflection).

## Not yet verified

- The melee beacon in a live session, including that the game's automatic target
  matches what attacks actually turn to, and that 500 units is a sensible "in
  reach" while locked on (it is the automatic targeting radius, not a measured
  attack range).

- All of the above in a live session: audibility, guidance, the priority switch,
  L1 tracking and the lock-on skip.
- That allies are excluded by `TeamType` as expected (an inference from the field
  name and the player's own value).
- Flames: no separate flame projectile class was found; fireballs
  (`BCProj_FireBallBase`) take damage. A flame attack that is not a projectile, or
  is an unshootable projectile of another class, would need adding.
- The offsets and bytecode layout were read from the title-update build; the disc
  build has not been compared. The bytecode check keeps boosted safe there, but
  capture offsets are assumed to match.
