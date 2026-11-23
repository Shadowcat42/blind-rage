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

## Not yet verified

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
