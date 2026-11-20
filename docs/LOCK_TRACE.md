# Lock-on sound investigation

The acquisition/removal loop is a hypothesis until a live trace establishes it. The existing log confirms configuration and narration, but does not record native sound requests or individual lock decisions. No sound is muted by this implementation.

## Capture

Add the following to `mods/accessibility/accessibility.ini`, then start a fresh session through Blind Rage:

```ini
[diagnostics]
lock_trace=1
```

Capture starts at the first active, unpaused, noncinematic shooting-controller tick. It stops after 30 seconds or 8,192 attempted records. A separate 512-record queue reports overflow without resetting narration. `userdata/accessibility/lock-trace.jsonl` is limited to 2 MiB, overwritten rather than appended, and removed by startup diagnostic cleanup. The game thread only copies bounded data; the speech worker resolves metadata names and writes the file. No timer, key listener, speech or sound is added.

Copy this file and the session's `accessibility.log` somewhere under ignored `research/` before restarting. Capture the same section with assistance off and on at the selected 20-degree setting, using normal user navigation. Preserve and restore the user's INI choices. Testing can wait until the user is available; no autonomous navigation is required.

The JSONL session header identifies the profile, process, configured angle (zero means aid off), and candidate-fix setting. Records carry controller/target identities and shooting-frame IDs. Acquisition `result` means a newly completed lock; `accepted` also identifies a returned target. Removal `result` means the native predicate requested removal. `angular_reached` distinguishes its actual camera-check path from earlier eligibility returns. Removal directions/location are copied from the native function's locals after its camera-view call; FOV is the cached value used by its predicate. Marker records describe actual calls from the HUD's marker method, and sound records describe nested native UI sound requests, not proof that audio was heard.

```powershell
python scripts/analyze_lock_trace.py path/to/assisted.jsonl --baseline path/to/aid-off.jsonl
```

The analyzer requires at least three consecutive frames of the same identity completing acquisition, adding a marker, requesting a sound, and being removed by the angular check above its limit. It rejects incomplete ordering, object reuse and non-angular rejection as proof of that loop. Missing footers and dropped records are reported. An absence of confirmed sequences does not rule out the issue. Baseline and assisted captures are reported separately; their encounters are not assumed identical.

## Candidate repair: disabled pending evidence

`[gameplay] stable_lock_cone=0` is the default. Do not enable it until the trace confirms the proposed mechanism. If the trace instead points elsewhere, revise the repair rather than guess another cone value or suppress sound requests.

With `stable_lock_cone=1` and shooting assistance enabled, the candidate keeps the native retention predicate and caps temporary acquisition angles before acquisition runs. The cap is `max(0, (FOV + 5) / 2 - aim/camera axis offset - 0.5 degrees)`. Both proposed limits are capped, including a native 50-degree near-target maximum when necessary. Original fields are restored afterward, retaining legitimate game-side changes. No acquired-result post-filter can undo counters/events already emitted by native acquisition, so the cap acts before those side effects.

The candidate requires finite camera geometry, matching acquisition/cache origins within 0.001 world units and a verified inherited `Camera.GetCameraViewPoint` implementation. A camera class overriding that method is unsupported and falls back to unmodified native acquisition for that call; the trace reports `geometry_supported=0`. Invalid geometry also skips assistance for that call. `cap_applied` records whether the candidate was actually active. The small angular reserve addresses boundary rounding/jitter; it does not extend retention beyond the native view limits.

## Verification

The new camera, HUD and function-local layouts match across saved disc/title-update captures: 455 relevant reflected properties compared without differences. `Camera.GetCameraViewPoint` reads the same cached location/rotation used by acquisition. Native synthetic callbacks exercise nested frame, acquisition, marker, sound and removal ordering and original-return preservation. Tests cover trace limits/overflow, JSON output, camera geometry, scoped restoration, assistance-off behavior, results/QTE coexistence and analyzer false positives. These establish offline implementation behavior only.

After evidence confirms the cause, compare the candidate in the same live section. Under stable geometry, the confirmed repeated sequence must disappear while eligible locks, normal sound requests and manual firing remain usable. A later encounter with different camera behavior requires its own evidence; no boss-specific navigation or vulnerability change is introduced.
