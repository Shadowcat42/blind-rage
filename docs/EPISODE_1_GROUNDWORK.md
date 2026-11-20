# Episode 1 groundwork

The requested endpoint is an implementation ready for a user-led Episode 1 test. Live calibration and later iteration can follow separately. Do not treat the pre-existing subtitle prototype or one passing cue test as completion of this scope.

## Required implementation

- [x] Preserve the event-driven menu reader and existing subtitle/tutorial preferences.
- [x] Add a shared label adapter following the recomp's applied Xbox/PlayStation choice and effective keyboard-prompt flag; obtain keyboard names from the runtime, including binding/context changes. Details: `INPUT_PROMPTS.md`.
- [x] Add a requested Health/Burst readout and low-health warning with hysteresis.
- [x] Identify native usable Burst-button creation separately from a full meter; announce readiness through the shared label pipeline.
- [x] Implement shared active QTE narration with judging-request grouping, command modes, resolved inputs, hidden/future/completed filtering and lifecycle invalidation.
- [x] Implement the separate Synchronic timing path against the actual input window, rather than treating result feedback or an arbitrary ring fraction as the cue.
- [x] Extend prompt labels to stick/direction/rotation QTEs, including actual keyboard equivalents and remapped game actions.
- [x] Permit urgent new QTE cues to interrupt through the existing NVDA/Prism path. Dismissal must never blanket-stop speech.
- [x] Add optional wider native shooting acquisition while preserving eligibility, line of sight, range, lock limits, homing and manual firing.
- [x] Verify added hook targets/layouts for both supported builds, run relevant offline tests, build/install/package the complete test artifact, and document the user-led validation sequence and known limits.

The requested first shooting aid expands native acquisition. Target snapping/cycling, audio-guided manual aiming, dedicated earcons and later encounter/hazard work remain later tasks. Do not substitute those for this implementation or add them as completion gates. The user will navigate the game; implementation and offline checks must not focus it or send input.

## Health and Burst evidence

The existing fingerprinted ActionScript delivery hooks expose `BCGFxUI_Gauge.CallSetHealth`, `CallSetNextHealth`, `CallSetBurst`, `CallSetNextBurst` and `CallShowGauge`. Decoding `TickGauge` shows that the **Next** setters receive the current player's health and mantra percentages on each tick; they are not a forecast of future damage. The visible flag is `GaugeVisible` at movie `+0x1e0`, with reflected `BCGaugeFlagData.bNow` mask `0x80000000`. Copying this flag with each value also permits state recovery after queue resynchronization.

These are HUD percentages. The game clamps its HUD health value to at least one percent and very small Burst values to zero. The readout follows those displayed values; it is not an exact hit-point or death-state API. Closed, unreadable or reused movies cannot supply active status. Requested status remains available from live gauge values when the game hides the gauge display; visibility still gates automatic low-health warnings. Back/Select requests status during active gameplay; Ctrl+Alt+F4 (`[hotkeys] status=115`) remains an alternative.

Both supported builds' `BCPlayerInput.HandleButtonInput_Back` act only on a pressed event (0), outside loading, with a controlled pawn and an unpaused controller in cinematic mode. That action attempts demo skipping or shows invalid-skip feedback. The normal gameplay branch does nothing; the only other reflected Back handler belongs to the debug camera and is empty. The mod observes the normal handler, always executes the original, and adds status only for a press with a noncinematic, unpaused controller, no active HUD menu scene and an open gameplay gauge. A user-led gameplay test exposed that the game can keep this gauge hidden while its value updates remain live, so display visibility must not disable an explicit status request. It rechecks context after the original call and again before speech; changed/reused input, controller or gauge identities invalidate the request. It never consumes input, changes the skip permission, synthesizes a key or starts an input-polling loop. The feature works independently of QTE narration and can be disabled with `[gameplay] status_button=0`. The additional reflected context fields match across both builds (1,069 properties compared without differences).

`BCPlayerController.Tick_ProcessBurst` calls `CheckVisibleBurstButton` and passes its result to `SetVisible_BurstButton`. The checks include cinematic mode and the controller/pawn's Burst eligibility, not just meter fullness. A usable native Burst button has command 35 and a generated `BurstButton` name tag. Authored input-divergence requests use their own tags and are excluded from this readiness channel. Button active/visible flags and success/failure/finish/destruction calls invalidate readiness.

The mod copies presentation values on the calling game thread and queues speech work elsewhere. Unchanged percentages and button flags are coalesced before waking the worker. A bounded overflow or stale batch resets local state and the setter cache; subsequent setter events republish current values. There is no new timer or recurring registry scan. Low-health warnings default to 25 percent and rearm above 35; `low_health_percent` changes the threshold and `[gameplay] status_warnings=0` disables automatic status speech. Requested status remains available. Status speech queues without interrupting NVDA.

Synthetic checks exercise the native detour's gauge delivery/coalescing, visibility, unreadable/NaN/out-of-range values, object reuse, low-health hysteresis, full-versus-usable Burst, ready-button disable/reactivation, cancellation before narration, copied payloads and effective input labels. These establish offline behavior; they do not establish live gameplay coverage or timing.

## QTE state and event boundaries

The shared script interpreter entry (`UObject.ProcessInternal`, guest `0x82260d78`) supplies FFrame/this/return data through the same translated-function ABI as the existing hooks. Its full normalized native functions match between both builds: 656 instructions. An early name-ID filter excludes unrelated script calls using the frame fields the original interpreter itself reads. It does not perform a process-memory query, object scan, lock or speech call for unrelated names. Selected owner/function metadata is cached with identity checks. Function-name discovery runs once after metadata becomes available.

The reader observes `BCSeqAct_InputDivergence` request/icon operations, the shared activation check and `BCInputJudgeSystem` updates/end/destruction. It copies bounded request and judge arrays on the game thread and associates them by identifier. Actual judge mode, value and hold duration determine press/mash/hold/simultaneous/continuous/several behavior. Ordered requests only expose `JudgeIdx`. Hidden, unshown, successful, disabled, ended and paused entries cannot produce instructions. One simultaneous request is one combined instruction, not separate speech calls which interrupt each other.

The judge's `InputInfo.bShow` is not a universal visibility flag: request initialization sets only the first entry for several modes, while their icon method creates the whole visible set. The implementation therefore combines the action's `RequestList` display flags with actual movie create/visible/active/completion calls, using judge success/current-index state for eligibility. It waits for all currently visible members of a group before narrating it. Final icon tags supply the resolved physical controls, including randomized commands and action remapping.

Animation-driven prompts use the same group/narration model through three common forms: `BCSeqAct_AnimCrossFadeButtonRep`, `BCSeqAct_AnimFadeByButtonRep` and `BCSeqAct_AnimFadeByHoldTime`. Their enable/success/output flags and button lists determine current input. The hold movie's cosmetic one-second count is not treated as a real completion deadline; narration says to keep holding until the game completes it.

Selected actions stage copied presentation and semantic events through nested calls, publishing at the enclosing action return. The worker merges both queues by sequence number and applies the full available lifecycle before speech. A source action's observed presentation revision also prevents retained state from authorizing a newer reused icon. Movie identities/classes/visibility are revalidated. No FString pointer survives its native callback, and cue disappearance never invokes backend stop. Queue overflow/stale batches fail closed; normal active-state changes are coalesced before waking the worker.

## Synchronic timing

The second added entry observes the native `BCInterpTrackSynchronic.IsInputOk` return used inside `UpdateNormal`/`UpdateBarrage`. Disc guest `0x82950c70` and updated guest `0x82950c80` match as 447 normalized native instructions. Its return is copied after the original call; the enclosing script action then publishes the track's final state. If input was consumed/judged in that same call, the unsent cue is invalidated. Tracks wait for predecessor judgment, and their visible movie tag must match the current lifecycle.

The reader provides a timing-button preparation and announces the physical Y equivalent when the actual accepted input window opens. Barrage tracks request mashing. This is an accepted-window cue, not a claim of an Excellent-rank timing aid. Speech latency, the best cue lead and actual player results require live calibration. Timing-ring fractions and result-feedback calls are not used as guessed input windows.

## Native shooting acquisition

Only the shared `BCPlayerController_HeavyShoot.CheckLockOnShotTarget` execution receives a scoped change. Its `LockOnDegMinVal`/`MaxVal` fields at `+0x7d4/+0x7d8` are raised to at least the configured angle while the original function runs, then restored. A legitimate game-side change during the call is retained. The observed defaults are approximately 3.6/14 degrees; the prototype defaults to 45. `shooting_assist=0` disables it, and `shooting_cone_degrees` accepts 1–89.

Decoded game behavior still performs its original candidate/range exclusion, occlusion/owner checks, acquisition timer, native counts/caps and projectile handling. No target position, player input, firing, damage, vulnerability or phase flag is changed. Existing acquired volleys retain their native component homing. Expanding acquisition does not prove every targetable part is currently vulnerable; later encounter exceptions remain possible.

## Verification and limits

The completed build passed all 13 game-free test suites through `build.ps1 -Test -NoInstall`. It was installed locally and both launcher `--check` profiles passed against the actual supported executables. The installed launcher matches the build. The ZIP's explicit allowlist, mod DLLs and project-default configuration were verified; it contains no game data, recomp/runtime binaries, captures, logs or test executables. The preliminary implementation is ready for the user-led sequence in `EPISODE_1_TESTING.md`.

Both added native entries accepted detour creation/enable/removal in separately mapped copies of each supported executable, without executing game functions. A comparison of 344 relevant reflected properties across saved disc/updated captures found matching offsets, sizes and boolean masks. Native synthetic tests exercise original return behavior, metadata changes, request generations, animation-only prompts, actual predicate returns, same-call completion, shutdown, action-batch staging and scoped angle restoration. Pure readers cover grouping, hidden/future/completed entries, keyboard/PlayStation mapping and timing lifecycles. Existing menu, text, speech and profile tests remain part of the full build.

Live delivery and coverage are deliberately not claimed by these offline checks. Start the mod before a prompt is created; attaching midway through an existing cue does not reconstruct its presentation history. Oversized/malformed/unsupported requests and stale/overflowed batches remain silent instead of guessing controls. Current bounds are 32 commands per request, 32 tracked groups and 64 events per producer batch. If a batch is discarded, an already displayed cue may require a retry/new lifecycle to recover. There are no new recurring timers or registry scans; active game methods supply observations, with changed snapshots waking the speech worker.

`QTE_ICON`, `QTE_STATE`, `TIMING_STATE` and `GAME_QTE` log delivery/state/narration transitions. `SPEAK` still records actual backend calls. Ctrl+Alt+F8 adds `GAMEPLAY_STATS` for captured state, capture failures and widened target checks. These records stay local and restart cleanup still applies. Use `EPISODE_1_TESTING.md` for the user-led test; no live navigation, game input or focus changes were used to prepare this build.
