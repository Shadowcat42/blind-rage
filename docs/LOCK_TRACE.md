# Lock-on sound investigation

The acquisition/removal loop is a hypothesis until a live trace establishes it. The existing log confirms configuration and narration, but does not record native sound requests or individual lock decisions. No sound is muted by this implementation.

## Capture

Add the following to `mods/accessibility/accessibility.ini`, then start a fresh session through Blind Rage:

```ini
[diagnostics]
lock_trace=1
```

Capture starts at the first candidate-acquisition check or lock-level change inside an active, unpaused, noncinematic shooting-controller tick. It does not require a successful acquisition. Rejected candidates (including capped checks) share one global sample per second, so cycling a large set cannot flood a per-target cache. Retained-target checks are sampled once per identity per second using a bounded 64-entry cache. The footer reports these filtered checks separately from queue drops. It stops after 30 seconds of active shooting ticks or 8,192 attempted records. Pauses, menus and gaps longer than 250 ms between ticks do not count; the footer reports `active_ms`. A separate 512-record queue reports overflow without resetting narration. `userdata/accessibility/lock-trace.jsonl` is limited to 2 MiB, overwritten rather than appended, and removed by startup diagnostic cleanup. The game thread only copies bounded data; the speech worker resolves metadata names and writes the file. No timer, key listener, speech or sound is added.

Copy this file and the session's `accessibility.log` somewhere under ignored `research/` before restarting. Capture the same section with assistance off and on at the selected 20-degree setting, using normal user navigation. Preserve and restore the user's INI choices. Testing can wait until the user is available; no autonomous navigation is required.

The JSONL session header identifies the profile, process, configured angle (zero means aid off), and candidate-fix setting. Records carry controller/target identities and shooting-frame IDs. Acquisition `result` means a newly completed lock: the target's `LockOnCnt` rose from below one before the native check to at least one in its returned `ResultInfo`. (`JustLockon` is an out parameter written in the caller's frame, so the native function's own slot cannot report it.) `accepted` also identifies a returned target. Removal `result` means the native predicate requested removal. `angular_reached` distinguishes its actual camera-check path from earlier eligibility returns. Removal directions/location are copied from the native function's locals after its camera-view call; FOV is the cached value used by its predicate. Marker records describe actual calls from the HUD's marker method, and sound records describe UI sound requests throughout the shooting tick, including requests outside marker callbacks, not proof that audio was heard. `level_change` records separately report `UpdateLockOnLevel` advances and `ResetLockOnLevel` resets, with before/after/target/maximum levels. An advancing level carries its configured `LevelUpSC` cue from the verified script sound path. This is state/path evidence for `Actor.PlaySound`, not interception of its native audio dispatch; the analyzer keeps it separate from observed UI sound requests. These reflected fields and the 40-byte level-parameter stride match the saved disc and updated profiles.

```powershell
python scripts/analyze_lock_trace.py path/to/assisted.jsonl --baseline path/to/aid-off.jsonl
```

The analyzer requires at least three consecutive frames of the same identity completing acquisition, adding a marker, requesting a sound, and being removed by the angular check above its limit. It rejects incomplete ordering, object reuse and non-angular rejection as proof of that loop. Missing footers and dropped records are reported. An absence of confirmed sequences does not rule out the issue. Baseline and assisted captures are reported separately; their encounters are not assumed identical.

## Repair: widen only where the game would keep the lock

The live trace confirmed the mechanism (aid at 45 degrees, Episode 1 first shooting section): one target locked, requested the full-lock tone and was removed by the native view-angle check on 105 consecutive ticks, with the target 12.6-24.7 degrees from the aim line but 38.4-41 degrees from camera forward against a 35-degree limit. The native retention predicate (CheckRemoveLockOnTarget) removes a target when its angle from camera forward exceeds (FOV + 5) / 2 degrees.

The repair is always applied in autoaim mode; there is no separate stable-cone setting. Manual beacons and native mode do not widen acquisition. Before each native acquisition check, the candidate is widened to the configured angle only if its world position is inside that view limit by at least 0.5 degrees. Unsafe candidates keep the unmodified native cone. A live repaired capture also showed seven consecutive lock/drop ticks admitted by that native cone because the aim and camera axes differ. Per the project's policy of preserving base-game behavior, this native case is deliberately left alone. The repair limits only widening introduced by the mod; it does not mute sounds, reject native locks or change retention. Original fields are restored afterward, retaining legitimate game-side changes.

The candidate position is the world origin of its `PrimitiveComponent.LocalToWorld` matrix (reflected +0x90, fourth row). This is a model, not a verified fact, so it is checked at runtime: on every acquisition check where the native function computes its angle, the angle predicted from the component position is compared with the native one (0.25-degree tolerance). The model is used only after three agreements and never after any disagreement. Until then, and permanently after a disagreement, the earlier conservative bound applies: `max(0, (FOV + 5) / 2 - aim/camera axis offset - 0.5)`, which limits added assistance without lowering either native angular threshold. The log line `GAMEPLAY_STATS ... lock_position_matches=N lock_position_mismatches=M` (Ctrl+Alt+F8) reports the check; trace records carry `target_angle`, `view_margin`, `view_gated` and `position_verified`.

The candidate requires finite camera geometry, matching acquisition/cache origins within 0.001 world units and a verified inherited `Camera.GetCameraViewPoint` implementation. A camera class overriding that method is unsupported and falls back to unmodified native acquisition for that call; the trace reports `geometry_supported=0`. `cap_applied` records use of a conservative bound on added assistance; `view_gated` records that an unsafe candidate was left at the original native cone. Missing candidate geometry uses the conservative camera bound when available, without treating an unknown position as an unsafe target.

## Verification

The new camera, HUD and function-local layouts match across saved disc/title-update captures: 455 relevant reflected properties compared without differences. `Camera.GetCameraViewPoint` reads the same cached location/rotation used by acquisition. Native synthetic callbacks exercise nested frame, acquisition, marker, sound and removal ordering and original-return preservation. Tests cover trace limits/overflow, JSON output, camera geometry, scoped restoration, assistance-off behavior, results/QTE coexistence and analyzer false positives. These establish offline implementation behavior only.

After evidence confirms the cause, compare the candidate in the same live section. Repeated sequences admitted only by the widened cone must disappear while eligible locks, normal sound requests and manual firing remain usable. A later encounter with different camera behavior requires its own evidence; no boss-specific navigation or vulnerability change is introduced.

### Repair verification gate

Run `python scripts/analyze_lock_trace.py path/to/repaired.jsonl --baseline path/to/failing.jsonl --verify-repair` after a fresh launch with `shooting_mode=autoaim`, `shooting_cone_degrees=45` and `lock_trace=1`. The baseline argument can point to the preserved failing assisted run; its session header identifies its actual configuration.

The command exits with status 2 when trace checks fail. A pass requires a completed 30-second active window, no queue drops, the repair enabled, recorded repair intervention and widened acquisition, position-model verification without an observed mismatch, ordinary new locks and UI sound requests, and no repeated acquire/sound/angular-removal sequence containing a lock that the native angular test would reject. Native-cone sequences remain visible in the report and do not fail that check; missing angular evidence remains inconclusive. Empty, partial and assistance-off captures cannot pass. The report lists each check and its evidence counts.

This gate does not prove the sound was heard or that firing worked. User confirmation of normal stepped/full-lock feedback, manually fired volleys and absence of mod-induced intrusive repetition remains required. The user confirmed the repair before authorizing the manual-beacon prototype; its separate live validation is documented in `SHOOTING_BEACONS.md`.

## Guidance records

In every shooting mode, a `guide` record is written at most every
250 ms while candidates exist. It counts candidates, unlocked candidates and those
guidance may not use (`unreachable`: outside the cursor frame, beyond
`LockOnShotLockDistance`, or recently declined by the game inside its angle limit),
and places the usable ones `left`/`centre`/`right` and `above`/`below` of the
camera forward direction (5-degree bands). Tracking fields give whether L1 was
held, the chosen target, whether it came from the beacon's cluster
(`tracking_focus`) and its error from the cursor in degrees.
