# QTE input research

Offline investigation, 2026-11-19. This extends `GAMEPLAY_CUE_RESEARCH.md` with decoded input-judging and prompt-presentation behavior. No live process access, game input, injection, or production changes were used. No gameplay narration is implemented by this document.

## Evidence and limits

The saved updated-build title capture contains the shared QTE class metadata and script bodies. A local research decoder resolves property/function references and intrinsic operators from that same capture. The relevant selected judge, movie, mapping, and Synchronic functions decoded to their script ends without unsupported-token failures. Presentation is cross-checked against the input-judging branches, rather than inferred from enum names or icon strings alone. Earlier disc/updated metadata comparison in `GAMEPLAY_CUE_RESEARCH.md` found matching selected layouts; the deeper behavior trace here uses the updated capture.

Local inspection also sampled nine encounter packages and four cinematic packages. It identified an authored mash request with an action-button command and a required count of 15, plus an ordinary action-button request. Those examples establish real authored requests, but do not establish coverage of every QTE type. No runtime timing, controller behavior, or rendered icon was tested. All packages, script material, extraction tools, and detailed traces remain ignored under `research/qte-inputs/` or the parent's existing ignored research directories. This document contains original analysis and interface metadata only; no story or boss identities are needed for the conclusions.

## Shared command and presentation routes

`BCSeqAct_InputDivergence.RequestInputJudge` copies its `InputSetting`, resolves its `InputButtons` into a `RequestList`, builds `BCButtonCommandInfo.Buttons`, and calls `BCPlayerController.RequestInuptJudge` (the spelling in the game). The controller routes the request into `BCInputJudgeSystem`. The returned identifier distinguishes one judging request from another.

`RequestButtonCommandList` replaces random command categories with concrete commands before populating the request list and before creating icons. A reader should use resolved requests or final movie data; reading the original asset could announce “random button” instead of the button actually required.

`RequestButtonIcon` copies the resolved command, command mode, hold time, time limit, position, hidden flag, and generated name into `BCMovieButtonData`, then calls `BCHUD.StartShowButton`. The shared HUD chooses a button movie and calls `BCGFxMovieButton.Create`. That function resolves a `ButtonTag`, calls `CallCreate`, and stores a full `BCButtonData` record. The presentation calls converge on `GFxObject.ActionScriptVoid`, as documented previously.

`BCGFxMovieButton.Create` passes these arguments to `CallCreate`: resolved button tag, generated name tag, `HoldTime * 30`, and the initial timer. These are presentation arguments; the semantic command mode is retained in the movie's `Buttons` data. `CallCreate` alone does not identify every judging distinction.

## Command modes: actual judge behavior

All modes share a request containing a list of commands. A request is not necessarily one physical button.

| Value | Reflected mode | Verified judging behavior |
| --- | --- | --- |
| 0 | `BCBCT_OrderPush` | Processes the command at `JudgeIdx`, advances after success, and can reveal later entries. Rotation commands have a dedicated branch. |
| 1 | `BCBCT_HittingRepeat` | Counts pressed transitions or completed stick rotations. Each requested entry is compared with the required `InputSetting.Value`; the request succeeds when all required entries meet their counts. |
| 2 | `BCBCT_LongPush` | Accumulates held time per requested command. Release handling differs with `InputSetting.Value`; one variant waits for release after the duration. A reader must not assume all holds finish simply by reaching a duration. |
| 3 | `BCBCT_PushSimultaneous` | Tests all requested entries together. Starts on a pressed transition and maintains active state; success requires the complete set. `InputSetting.Time` bounds the partial-input interval when nonzero. |
| 4 | `BCBCT_ContinuancePush` | Requires continued active input and fails when a required entry is released. It is distinct from an ordinary timed hold. |
| 5 | `BCBCT_SeveralPush` | Maintains visible entries, reveals further entries over time, and counts distinct successful command entries toward a required value. Repeated same-command events in one update are filtered. |

The judging system also has a selection judge. Do not collapse all timed prompts into “press” or all repeated prompts into one generic mash behavior.

### Grouping and paired stick tilts

A simultaneous request can contain left-stick and right-stick direction commands in the same `Buttons` list. This provides a shared mechanism for “left stick down and right stick up together”; a scene-specific script is not structurally necessary to represent it. This conclusion follows from the generic command list and simultaneous judge; a representative live paired-tilt event is still needed.

The correct grouping source is the judging request identifier plus its command list. `InputDivergence.RequestButtonIcon` generates individual display name tags and does not assign `BCMovieButtonData.GroupTag`. Grouping based only on the movie's `GroupTag`, or speaking each arriving icon separately, can therefore lose the relationship between simultaneous commands.

`RequestList.bShow` and per-entry success state matter. Ordered and several-push requests can contain commands not yet shown. Do not read every authored command on request creation: that can disclose later instructions and overwhelm the current action.

## Stick directions and rotation: enum names are misleading

The earlier document preserves exact reflected enum identifiers. Actual rotation dispatch does **not** match a literal interpretation of all those identifiers:

| Command values | Reflected identifiers | Verified judge path | Resolved icon tags |
| --- | --- | --- | --- |
| 36, 37 | `BCGBC_Stick_Clockwise`, `BCGBC_Stick_ReverseClockwise` | Left stick, index 0 | `RRotateL`, `LRotateL` |
| 38, 39 | `BCGBC_LS_Clockwise`, `BCGBC_LS_ReverseClockwise` | `Update_LRStick`, evaluates both stick channels | `RRotateSet`, `LRotateSet` |
| 40, 41 | `BCGBC_RS_Clockwise`, `BCGBC_RS_ReverseClockwise` | Right stick, index 1 | `RRotateR`, `LRotateR` |

The start routine sets clockwise for 36/38/40 and counterclockwise for 37/39/41. Initialization orders the left physical directions as up, right, down, left, and similarly orders the right stick; clockwise increments the index, counterclockwise decrements it. Pressed direction transitions advance progress; an unexpected direction can reset progress. Holding a diagonal is not equivalent to completing a rotation.

**The paired-icon path is not proof that both sticks must complete a circle.** `Update_LRStick` advances each channel separately and sets the shared success flag when an individual channel reaches its sequence length. No “both completed” conjunction occurs in that function. This suggests either stick can satisfy that rotation variant, but it must be checked live before the reader gives the user that instruction. “Both sticks required” would currently be an unsupported claim, despite the paired visual tag.

Ordinary direction commands 17–20 represent left up/down/left/right; 21–24 represent right up/down/left/right. Command 42, named `BCGBC_RightDown`, resolves to `LRightDown` and its judge explicitly checks `LS_DOWN` **and** `LS_RIGHT`: it means left stick diagonally down-right, not right-stick down.

## Resolved tags and control mapping

`BCGFxMovieButton.GetButtonTag` emits:

- Ordinary face/shoulder tags such as `A`, `X`, `Y`, `B`, `LT`, `LB`, `RT`, `RB`, and `BURST`.
- Mash tags such as `Barrage_A`; action-button variants use current configuration.
- Direction tags `LUp`, `LDown`, `LLeft`, `LRight`, and the corresponding `R...` forms.
- Hold direction tags such as `Hold_LUp` and `Hold_RDown`.
- Rotation tags listed above, plus the diagonal `LRightDown`.
- Face-button variants ending in `4` for a presentation icon type. The suffix does not change the physical button.
- A `Finish_` prefix for the finishing action. That prefix is not itself a separate physical button.

The shared getter does not map every enum member to a tag. An empty or unsupported tag must remain unknown; do not invent labels for unhandled values.

Action commands are resolved through current key configuration. Movie `ConfigData` order is action 0 Assault, 1 Heavy Assault, 2 Shot, 3 Jump. `GetConfigTag` calls `BCSaveParam.StaticActionButton2GameButton`, then maps physical IDs 0/1/2/3 to A/X/Y/B. The command enum and physical-button enum have different numbering and trigger/bumper ordering; never cast between them directly. None of this establishes the recomp's keyboard-to-gamepad bindings.

### Tutorial text glyphs

`BCGFxUI_Advertise.Start` fills configuration placeholders `<Assault>`, `<HAssault>`, `<Shoot>`, `<Jump>`. `GetIconTag` applies the current action mapping and returns the literal strings `<A>`, `<X>`, `<Y>`, or `<B>`. `SetAdvertise` substitutes those placeholders before `CallCreate(Icon, Text)`.

A resolved tutorial reader should translate these known button tokens **before** HTML removal; otherwise a generic tag stripper can silently delete the required input from the instruction. This mapping is already configuration-aware at the resolved presentation boundary.

## Activation, completion, cancellation, and timing

`BCPlayerController.InputJudgeSys` is at `+0x760`. `BCInputJudgeSystem.JudgeInfo` is a TArray at `+0x1c4`; `BCInputJudgeInfo` elements are 0x5c bytes. Important fields are the 0x20-byte request at `+0x00`, input-info array `+0x20`, visible/success counts `+0x2c/+0x30`, ordered index `+0x34`, time-limit flag `+0x38`, remaining limit/time `+0x3c/+0x40`, enabled flag `+0x48`, result `+0x4c`, identifier `+0x50`, and pause flag `+0x58`. Packed booleans require their reflected masks.

`BCButtonCommandSetting` is 0x14 bytes: command mode `+0x00`, required count/value `+0x04`, mode-specific time `+0x08`, maximum shown entries `+0x0c`, and miss-input category `+0x10`. Do not equate this mode-specific time with the separate overall request time limit.

Movie creation, movie visibility, and active input are separate concepts. `Create` can hide the icon immediately; success handling can reveal a hidden icon for feedback. `SetActive` reaches `CallSetActive`, but the semantic enabled state is also represented by the input judge and caller lifecycle. `SetSuccess`, `SetFailure`, and `SetFinish` mark movie records complete, and `OnFinished` later removes records. The movie array can retain completed/animating entries.

`BCGFxMovieButton.UpdateTimer` subtracts game `DeltaTime` from a positive display timer, clamps at zero, and sends a normalized remaining fraction. Its movie creation conversion multiplies hold duration by 30 for the visual hold count. Input judging independently maintains its own time and pause state. Real wall-clock deadlines and time-dilation behavior need live calibration.

The divergence lifecycle ends and destroys its judge request on deactivation and clears its identifier. HUD `StopShowButton` can select all icons, a name, or a command type, and carries success/hide/no-judge flags. Removal must not automatically be called failure. A future reader should invalidate its own queued cues on cancellation without issuing unconditional backend stop calls that cancel unrelated NVDA speech; the user explicitly rejected such cancellation on menu exit.

## Separate Synchronic timing prompts

`BCInterpTrackSynchronic` is a distinct path, not an ordinary `BCGFxMovieButton` request. It supports normal timing and barrage tracks. `IsInputButton` explicitly tests physical button ID 2 through `IsButtonPressedFromButtonID`, then disables its input gate for that update. In the inspected physical enum this is **Y**; it is not the remapped Heavy Assault action.

Normal timing logic starts a HUD cue, tracks active/judged flags, calculates a normalized timing value over a pre-hit interval, and judges the button against timing windows. Barrage logic increments an input count and sends updated hit/result feedback. `ShowButton`/`CallPush` are response/result calls; waiting for those to announce the instruction would be too late. A useful reader must observe the start/active window and the separate timing updates.

The UI `CallStart` and `CallSetTimingTime` routes converge on ActionScript presentation boundaries, but button identity, track mode, and timing semantics are in the track/judge data. Only reading visible text will miss icon-only prompts. The actual window thresholds, timing-ring alignment, speech latency budget, pause/skip behavior, and representative controller results are untested.

## Implications for the first implementation

A shared implementation is plausible: observe the resolved movie delivery calls for ordinary prompt identity, and associate them with enabled judging requests when mode/group semantics are needed. Keep the Synchronic path separate. Use copied cue data with request/generation identity; do not retain live FString pointers or speak on the game thread. Preserve short-lived activation transitions instead of relying solely on a later snapshot.

The next user-led validation should cover a plain press, a mash, a timed hold including release, paired opposing stick tilts, left/right rotations in both directions, the paired rotation variant, a remapped action, a resolved random command, and a Synchronic timing prompt. Record bounded timestamps and observe creation, activation, success, cancellation, and retry. No gameplay input or automation is needed from the agent to prepare that instrumentation.
