# Shooting modes and manual cluster beacons

This is the first manual-aiming prototype for native HeavyShoot sections, including
Episode 1's opening shooting encounter. Live audio and gameplay calibration are
still required. It does not implement synchronized-QTE sounds or arena navigation.

## Modes

| `shooting_mode` | Behavior |
| --- | --- |
| `off` | No aim assistance. The game's own lock cone and controls only. |
| `beacons` | Audio guidance for the selected target cluster. The game's cone is unchanged. |
| `boosted` | Beacons, plus the game's lock cone scaled by `boost_point_blank` and `boost_max_range`. |
| `tracking` | Beacons, plus cursor steering toward unlocked targets while L1 is held. |

All three assisted modes play beacons. Older values still work: `native` means `off`
and `autoaim` means `boosted`; configuration synchronization rewrites them.

### Boosted cone

The game's lock limit is `clamp(Min + (Max - Min) * (1 - distance / 3000), Min, Max)`,
with `LockOnDegMinVal` applying at maximum range and `LockOnDegMaxVal` at point blank.
Boosted mode multiplies each by its own setting (1-10; defaults 2 and 3), so the
cone stays a cone that is wider near and far, instead of a flat angle filling the
screen. The stability repair still caps only the added assistance where native
retention would drop the lock, and never narrows a native value. The retired
`shooting_cone_degrees` and `stable_lock_cone` keys are removed on synchronization.

### Tracking

The lock cone's axis runs from the camera through the on-screen cursor
(`AimShotAimPoint`). The game moves that cursor in `AimShotAimingCtrl` from the
stick values `PlayerInput.RawJoyUp/RawJoyRight`, applying its own speed and screen
frame. While L1 is held in tracking mode, the mod supplies a steering value for
that one cursor update and restores the real stick afterwards. It aims at the
nearest unlocked target in the beacon's selected cluster, so the cursor goes where
the beacon points and D-pad Left/Right retargets both; only when that cluster has
nothing left does it take the nearest target overall. It keeps its current target
while that stays eligible, eases
steers in a straight line at the game's full cursor speed (a target locks the moment
the cursor enters its cone, as Episode 1's lock time is 0), tapers only in the last
1.5 degrees, settles inside 0.3 degrees and stops at a full volley.
Any stick deflection beyond 0.2 takes priority and suspends steering. L1's own
lock-on returns immediately in shooting mode (PDE mode 4), which Episode 1's rail
sections use, and the mod never blocks the button. `tracking_strength` (0-100)
sets the steering speed as a share of the game's cursor speed (default 100).
Tracking uses the game's own cone; it does not need the boosted cone.

### Reachable targets

`AimShotAimingCtrl` clamps the cursor to the screen's title-safe frame
(`GetSafeFramePoint`: `TitleSafeZone.MaxPercentX/Y`, shifted for a letterboxed
HUD), and in shooting mode the camera does not turn, so the cursor can never leave
that frame. Lock candidates, however, extend to the camera retention limit of
(FOV + 5) / 2 degrees in every direction. In the Episode 1 captures the frame is
85% of 1280x720 with a horizontal FOV near 59 degrees, so the cursor reaches about
25 degrees left/right but only about 15 degrees up/down. Each candidate is projected
with the game's own `BCGameInfo.CoordTransformParam` matrix (the one its native
`WorldLoc2ScreenLoc` uses; its row-vector convention was checked against two
gameplay captures). Beacons and tracking skip targets outside the frame. Without
projection data every target counts as reachable, as before.

`CheckLockOnShotTarget` also rejects targets beyond `LockOnShotLockDistance`
(controller +0x7c8, when positive) and targets whose line-of-sight trace first
hits something other than their owner. Guidance treats a target beyond the lock
distance as unreachable, and when the game declines a candidate that is inside its
angle limit, guidance and tracking skip that target for 1.5 seconds instead of
holding the cursor on something that cannot lock.

New installation/package defaults are in `config/accessibility.ini` and choose
beacons. The runtime reads only `mods/accessibility/accessibility.ini`. When an
existing file has no explicit mode, `shooting_assist=1` migrates to `boosted` and
`shooting_assist=0` to `off`. An explicit mode takes precedence.

## Controller operation

During active shooting gameplay:

- D-pad Up cycles **off → beacons → boosted → tracking → off**, announces the
  mode, and saves it for the next launch.
- In tracking mode, hold L1 to steer onto unlocked targets; move the stick to take over.
- D-pad Left/Right chooses the previous/next visible unlocked cluster.
- Keyboard and launcher equivalents: Ctrl+Alt+F9 (mode), F10 (previous cluster), F12
  (next cluster); INI `[hotkeys]` keys `aimmode`, `aimprev`, `aimnext`, and
  `BlindRage.exe --command aimmode|aimprev|aimnext`.
- Back/Select continues to read Health/Burst; no modifier is needed for aiming
  commands. Original handlers run once and are never consumed by the mod.

These commands require an active HeavyShoot player, no pause/menu, and no cinematic.
Empty D-pad callbacks did not reach either script or event dispatch in live tests.
Commands now observe the game's existing `BCPlayerInput.InputCheckTick` and its
already-updated `ButtonInputList` raw-active states. Rising edges issue commands;
holding, releasing, or holding through a menu/cinematic return does not repeat.
There is no added input timer, OS controller polling or UObject registry scan.
Shooting commands do not require the Health/Burst gauge. Diagnostic `dpad_calls`,
`dpad_presses` and `dpad_accepted` count input ticks, direction press edges and
accepted commands respectively. Live confirmation of this replacement is pending.
The normal four D-pad handlers are empty in the inspected game scripts. Other
direct uses are menu/dialog navigation and development controls; the latter's
shipping-build override was removed separately. Debug camera handlers are excluded.

## Reading the beacon

The beacon uses a band-limited triangle tone to distinguish it from the game's
own lock sound. Five odd harmonics retain the triangle's character without
introducing harmonics above the output sample rate's Nyquist limit.

- Stereo position indicates left/right relative to the current aim.
- Higher pitch means above the aim; lower pitch means below it.
- Pulses become faster as the aim approaches the guided point.

The pulse period decreases from 350 ms at 45 degrees of aiming error to 55 ms
at zero error. Pulses have a 55-ms body with short edge ramps. Below eight degrees
the gaps progressively fill, becoming a continuous tone at four degrees or less.
This indicates close alignment, not confirmation of a native lock. The existing
700-Hz middle pitch, elevation mapping, stereo pan and configured peak gain remain.
The waveform and envelope keep running smoothly as aiming error changes.

Only one cluster is guided. Candidates group within an 8-degree seed radius and
a 25% range span, so widely separated depth layers remain separate even when
they overlap on screen. Range is measured from the camera in native world units.
The initial choice is near the aim, preferring the nearer candidate when directions
differ by at most 1.5 degrees. Automatic focus yields to a newly overlapping target
at least 10% nearer; explicit D-pad selection preserves the chosen depth layer.
Otherwise focus stays with the cluster. Within the
cluster, the beacon points to an actual unlocked member, not an empty midpoint
between targets. It holds that member until acquired or lost, then guides the
remaining members before automatically advancing to another cluster.

At native full-volley capacity, guidance stops so the game's own full-lock sound
can be heard. It resumes after native target consumption by firing, or a complete
lock reset. Targets already locked or awaiting a projectile hit are skipped. The
mod never fires, moves the reticle, chooses damage, modifies vulnerability, or
mutes native lock sounds.

`beacon_volume=30` sets cue loudness on a 0–100 scale; 0 silences it. A restart
applies volume changes. Narration mute and focus loss also silence the beacon.

## Data and lifecycle

The existing interpreter detour observes native shooting ticks and the completed
`SearchLockOnShotTarget` call. It copies bounded candidate identities, component
world positions, camera/aim geometry and current/in-flight lock counts. It uses
the game's native projectile capacity and observes actual target consumption in
`IterateLockOnShotTargetComp` to distinguish firing from an angular lock loss.

There are no recurring UObject scans or input/memory polling timers. A latest-only
mailbox keeps frame data outside the narration queue. The worker performs angular
and depth clustering, retains focus by object identity, and updates
the cue. XAudio2 services the mod's own stereo PCM buffers on a separate thread;
that audio thread never reads game memory or controller input. Missing/stale data
fades silent after 200 ms, plus at most the small queued audio buffer. Pause,
cinematics, menus, unsupported camera geometry and oversized lists also silence
guidance. The candidate cap is 512; an oversized list fails closed rather than
silently dropping targets. Mode switches stop the current diagnostic capture so
its session header cannot describe a different shooting mode.

The camera supplies the view origin, orientation and field of view; the native
aim line supplies the reticle direction. Cues describe target offsets from that
aim, updating as the player aims. The mod does not steer the camera or reticle.
The guide uses enabled native candidates inside the camera retention view. It
does not establish line of sight or a currently vulnerable boss part; the game's
own acquisition, occlusion and damage checks remain authoritative. Guidance is
available while the native lock-on search is running. Camera/position-model
disagreements disable the copied guidance rather than inventing a target.

## Validation

Synthetic checks cover configuration migration, depth-layer separation, overlapping
foreground preference, explicit farther-layer selection, camera-relative range,
cluster focus/advance/cycling,
real-member guidance for sparse clusters, capacity/firing/reset behavior, object
reuse, aim-relative and rolled geometry, bounded native capture, in-flight locks,
pause/menu isolation, original handler execution, native-angle preservation,
stereo synthesis, stale-data silence and invalid floating-point values. Selected
layouts (600 reflected fields) match across the saved disc/update captures.

For the first live pass, enable beacons in Episode 1's opening shooting section.
Check left/right and elevation cues, manually acquire a cluster, fill and fire a
volley, switch clusters, pause, and switch modes. Confirm that native lock sounds
and firing still behave normally. Audio audibility, practical clustering and
guidance latency are not established by the offline tests.
