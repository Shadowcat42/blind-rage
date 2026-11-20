# Gameplay cue research

Offline investigation, 2026-11-19. No gameplay accessibility is implemented by this document. No game input, live process inspection, or production-code changes were used for this investigation.

## Findings and confidence

The game exposes shared subtitle, instructional-message, and button-prompt interfaces. A small number of shared cue readers appears feasible; individual scene scripts should not be necessary. Reflection establishes the data layouts below, and inspection of saved script metadata establishes the listed call relationships. Actual gameplay coverage, visibility, event order, and latency still require validation.

Evidence came from the saved original-disc title capture and `research/title-update/title/`. Both contain loaded class/function metadata even though their gameplay UI is inactive. Compared 459 selected class, structure, enumeration, and function records, including 1,170 property descriptors: all selected property offsets, sizes, enumeration members, and script-function execution pointers matched. Seven native interpolation helper execution addresses moved by 0x10 in the update. This comparison covers the selected metadata, not all game functionality.

All addresses below are guest addresses unless explicitly marked host RVA. Heap object addresses from individual captures are deliberately omitted. Resolve objects and functions by class/owner/name, and use the existing build profiles for native hooks. Guest data is big endian; offsets are hexadecimal. FString and TArray headers occupy 12 bytes; FName occupies 8 bytes. Packed booleans need their masks, not whole-word truth tests.

## Shared delivery routes

Verified saved-script call relationships:

- `BCInterpTrackInstSubtitle.StartSubtitle` calls the HUD subtitle interface. `BCHUD.StartSubtitle` calls `BCGFxMovieText.StartText`; its text startup path reaches `StartupText`, then `CallStartText`, then native `GFxObject.ActionScriptVoid`.
- `BCHUD.StartGFxText` exposes a general text-data interface. `BCInterpTrackGFxText.StartText` calls that interface. `BCGFxMovieText.StartupText` resolves text, style, and position before calling `CallStartText`.
- `BCHUD.StartShowButton` selects a button movie and calls `BCGFxMovieButton.Create`; `Create` calls `CallCreate`, which calls native `GFxObject.ActionScriptVoid`.
- `BCGFxUI_Advertise.AddAdvertise` reaches `SetAdvertise`, then `CallCreate`, which also calls `GFxObject.ActionScriptVoid`. Its reflected categories include action instructions, camera information, missions, gauges, health, burst, and unlimited mode. This is a strong tutorial/message candidate, but no actual tutorial was captured.
- `BCGFxUI_TalkDemo.CallAddText` and `BCGFxUI_IntervalDrama.CallAddText` are additional text-bearing interfaces. They should be included in coverage validation, not assumed to be ordinary subtitles.

The game-specific methods above are **script functions**, with the common execution pointer `0x82260d78`. They do not each have their own native entry point to detour. `BCHUD.SetSubtitleText` is especially misleading: its saved script body is only three bytes and supplies no substantive subtitle handling. Hooking its name alone would miss the verified route.

Candidate common native boundaries, mapped in both installed executables:

| Boundary | Guest address, both builds | Disc host RVA | Updated host RVA |
| --- | --- | --- | --- |
| Generic script execution | `0x82260d78` | `0x0466e5a0` | `0x03aa6770` |
| `GFxObject.ActionScriptVoid` | `0x8289f6e0` | `0x030822c0` | `0x0348bb10` |
| `GFxMoviePlayer.ActionScriptVoid` | `0x8289c048` | `0x0406ec00` | `0x03078010` |

These are research candidates, not installed or validated hooks. Native function parameter/frame decoding and filtering by caller or movie still need validation. Hooking only `ProcessEvent` is not established to catch internal script calls.

## Subtitles and general text

`BCHUD.HudTextMovie` is at `+0x49c`. `BCGFxMovieText` has `SubtitleComp` at `+0x1f8`, `LetterBoxMC` at `+0x1fc`, and `bShowSubtitle` at `+0x200` (mask `0x80000000` in the saved metadata). It has texture and sound arrays, but no reflected persistent array of active text records. Capturing text at delivery is therefore a better candidate than expecting an always-current UObject subtitle string.

`BCHUD.StartSubtitle` takes FString `Text` at parameter offset `0x00` and `SoundCue* PlayVoice` at `0x0c`. `StopSubtitle` takes FString `Text`; saved call relationships show it reaches `EndTextType` and `EndText`. `BCGFxMovieText` also exposes `ClearText`, `EndText`, and `EndTextType`, providing candidate cancellation boundaries.

`BCGFxTextData` is 0x4c bytes:

| Offset | Field | Type |
| --- | --- | --- |
| `0x00` | `Text` | FString |
| `0x0c` | `TextType` | `EBCGFxTextType`, byte |
| `0x0d`, `0x0e`, `0x0f`, `0x10` | `PosType`, `AlignType`, `SizeType`, `ColorType` | bytes |
| `0x14` | `pos` | Vector2D |
| `0x1c` | `Size` | int |
| `0x20` | `Tex` | texture pointer |
| `0x24`, `0x30` | `TexPath`, `PackageName` | FString |
| `0x3c` | `SE` | sound-cue pointer |
| `0x40` | `bSyncFinishSE` | packed bool |
| `0x44`, `0x48` | `FadeinFrame`, `FadeoutFrame` | int |

`EBCGFxTextType`: 0 `BCGT_None`, 1 `BCGT_Cast`, 2 `BCGT_Telop`, 3 `BCGT_Tex`, 4 `BCGT_Title`, 5 `BCGT_Next`, 6 `BCGT_Subtitle`, 7 `BCGT_NextTitle`, 8 `BCGT_MAX`. This distinguishes subtitles from other screen text without scene-specific rules.

`BCGFxMovieText.CallStartText` receives resolved FString `Tag`, `msg`, `Type`, and `Align` at `0x00`, `0x0c`, `0x18`, and `0x24`, followed by position, size, fade-frame, and color parameters. It is a promising point for resolved text, whereas earlier fields may contain localization identifiers.

`BCSubtitleTrackKey` is 0x30 bytes: `StartTime +0x00`, `PlayRate +0x04`, `EndTime +0x08`, FString `Subtitle +0x0c`, voice pointer `+0x18`, `OffsetVoicePlayTime +0x1c`, `OffsetVoiceAfterTime +0x20`, and FString `Identifier +0x24`. `BCInterpTrackSubtitle.SubtitleTrack` is at `+0x68`, `DefaultTrack +0x74`, and `DefaultStringTag +0xa4`. This supplies authored timing metadata, but the relationship between track time, pause, playback rate, seeking, and actual visible text is untested. Do not schedule speech from these values alone yet.

## Instructional messages

`BCHUD.HudAdvertiseMovie` is at `+0x48c`. `BCGFxUI_Advertise.SetAdvertise` and `CallCreate` take FString icon and text arguments at `0x00` and `0x0c`. The `ShowAdvertise` interface supplies visibility, presentation category, and input/base flags. At movie `+0x1ec`, masks are `bAdvertiseVisible 0x80000000`, `bAdvertiseSystemVisible 0x40000000`, and `bInputNext 0x20000000`.

`EBCAdvertiseSetType`, in numeric order 0–12: `BCAST_None`, `BCAST_Assault`, `BCAST_HeavyAssault`, `BCAST_Shoot`, `BCAST_Jump`, `BCAST_KeySet`, `BCAST_CameraInfo`, `BCAST_Mission`, `BCAST_Gauge`, `BCAST_Health`, `BCAST_Burst`, `BCAST_Unlimited`, `BCAST_MAX`.

`EBCAdvertiseType`, in numeric order 0–7: `BCAT_Default`, `BCAT_Mission`, `BCAT_MissionNone`, `BCAT_CenterTop`, `BCAT_CenterPause`, `BCAT_Box`, `BCAT_All`, `BCAT_MAX`.

These categories establish shared instruction infrastructure. They do not prove that every tutorial, controller glyph, or overlay goes through it. Validate resolved text, markup/glyph substitution, multi-part messages, initial display, and dismissal before narration.

## Button prompts and QTEs

`BCHUD.HudButtonMovies` is a TArray at `+0x4a0`; `HudButtonDummy` is at `+0x494`. `BCGFxMovieButton.Buttons` is a TArray at `+0x1c8`, with 0x60-byte `BCButtonData` elements:

| Offset | Field | Type |
| --- | --- | --- |
| `0x00`, `0x0c` | `ButtonTag`, `NameTag` | FString |
| `0x18` | `bComplete`, `bHold` | masks `0x80000000`, `0x40000000` |
| `0x1c`, `0x20` | `Width`, `Height` | int |
| `0x24` | `Data` | embedded `BCMovieButtonData` |

`BCMovieButtonData` is 0x3c bytes:

| Offset | Field | Type |
| --- | --- | --- |
| `0x00` | `ButtonType` | `EBCGameButtonCommand`, byte |
| `0x01`, `0x02` | `PosType`, `ButtonIconType` | bytes |
| `0x03` | `ButtonPushType` | `EBCButtonCommandType`, byte |
| `0x04`, `0x0c` | `NameTag`, `GroupTag` | FName |
| `0x14`, `0x18`, `0x1c` | `EnableTime`, `TimeCount`, `HoldTime` | float |
| `0x20` | `ScaleSize` | float |
| `0x24` | `bHidden`, `bUseSuccessSE`, `bUseFailureSE` | masks `0x80000000`, `0x40000000`, `0x20000000` |
| `0x28` | `Position` | Vector |
| `0x34` | `Target` | object pointer |
| `0x38` | `bShowFrameOut` | mask `0x80000000` |

`EBCButtonCommandType`: 0 `BCBCT_OrderPush`, 1 `BCBCT_HittingRepeat`, 2 `BCBCT_LongPush`, 3 `BCBCT_PushSimultaneous`, 4 `BCBCT_ContinuancePush`, 5 `BCBCT_SeveralPush`, 6 `BCBCT_MAX`.

`EBCGameButtonCommand` values and exact reflected identifiers:

| Values | Members, in order |
| --- | --- |
| 0–6 | `BCGBC_None`, `BCGBC_A`, `BCGBC_X`, `BCGBC_Y`, `BCGBC_B`, `BCGBC_Start`, `BCGBC_Back` |
| 7–10 | `BCGBC_LeftTrigger`, `BCGBC_RightTrigger`, `BCGBC_LeftBumper`, `BCGBC_RightBumper` |
| 11–14 | `BCGBC_DPad_Up`, `BCGBC_DPad_Left`, `BCGBC_DPad_Down`, `BCGBC_DPad_Right` |
| 15–16 | `BCGBC_LeftStick_Push`, `BCGBC_RightStick_Push` |
| 17–20 | `BCGBC_LeftStick_Up`, `BCGBC_LeftStick_Down`, `BCGBC_LeftStick_Left`, `BCGBC_LeftStick_Right` |
| 21–24 | `BCGBC_RightStick_Up`, `BCGBC_RightStick_Down`, `BCGBC_RightStick_Left`, `BCGBC_RightStick_Right` |
| 25–29 | `BCGBC_Random_ABXY`, `BCGBC_Random_LRButton`, `BCGBC_Random_LRTrigger`, `BCGBC_Random_LSRS`, `BCGBC_Random_All` |
| 30–35 | `BCGBC_Assult`, `BCGBC_HeavyAssult`, `BCGBC_Jump`, `BCGBC_Shot`, `BCGBC_FinishAttack`, `BCGBC_Burst` |
| 36–41 | `BCGBC_Stick_Clockwise`, `BCGBC_Stick_ReverseClockwise`, `BCGBC_LS_Clockwise`, `BCGBC_LS_ReverseClockwise`, `BCGBC_RS_Clockwise`, `BCGBC_RS_ReverseClockwise` |
| 42–43 | `BCGBC_RightDown`, `BCGBC_MAX` |

Do not cast this enumeration directly to `EBCGameButtons`: that separate physical-button enumeration starts with A at 0 and orders triggers/bumpers differently. `BCTypes.ConvartButtonCommandToButtonId`, `BCSaveParam.ActionButton2GameButton`, `StaticActionButton2GameButton`, and `BCGFxMovieButton.GetButtonTag` are relevant conversion interfaces. Input remapping, action-based commands, random commands, and the final glyph/tag need live correlation before spoken button names are trusted. The spelling `Assult` above comes from identifiers.

Lifecycle interfaces, verified by reflection:

- `StartShowButton(ButtonData)` and `Create(Data)` create records. `CallCreate` receives resolved FString button/name tags, float `MaxCount`, and float `Timer`.
- `SetActive(NameTag, bActive)` provides an activation boundary distinct from creation.
- `SetPush`, `SetHold`, `SetRelease`, `SetRotation`, and `SetRotationPct` distinguish changes in input/prompt state.
- `SetSuccess`, `SetFailure`, `SetFinish`, and `OnFinished` provide completion candidates. `Finish`, `FinishAll`, `FinishGruop`, and `FinishAtCommandType` provide individual/group/type cleanup interfaces. The group method's reflected spelling is `FinishGruop`.
- `BCHUD.StopShowButton` supplies `bSuccess`, command `Type`, `NameTag`, `bHide`, and `bNoJudge`. Cancellation must not automatically be called failure; those flags need correlation.
- `UpdateTimer(Idx, DeltaTime)` and `CallUpdateTimer` exist. The units and exact meaning of `EnableTime`, `TimeCount`, `HoldTime`, and the rendered timer remain unverified; field names alone are insufficient to set deadlines.

### Separate timing-prompt path

`BCInterpTrackSynchronic` and `BCGFxUI_Synchronic` form another timing interface; QTE coverage must not assume every prompt belongs to `BCGFxMovieButton`.

`BCSynchronicTrackKey` is 0x18 bytes: byte `Type +0x00`, int `AchieveCnt +0x04`, floats `StartTime +0x08` and `EndTime +0x0c`, int `InputCnt +0x10`, then packed flags at `+0x14`: `bJudge 0x80000000`, `bActive 0x40000000`, `bShowTiming 0x20000000`, `bVibration 0x10000000`. `EBCSynchronicType` is 0 `BCSYT_Normal`, 1 `BCSYT_Barrage`, 2 `BCSYT_MAX`.

The track has `Tracks +0x8c`, `ActiveTrackIndex +0xb0`, `bEnableInput +0xbc`, and native `IsInputStart`, `IsInputOk`, and `IsInputEnd` helpers. HUD/UI interfaces include `StartSynchronic`, `EndSynchronic`, `ShowSynchronicButton`, and `SetSynchronicTimingTime`; the timing setter passes a float named `Pct`. This is strong evidence for explicit timing state, but its relation to the visual ring and input window is not yet measured.

## Recommended implementation direction (not yet verified)

Reuse the existing shared narration pipeline and Prism worker. Existing native text, visibility, ActionScript-assignment, and movie-lifecycle notifications can wake gameplay readers when visible data changes. They do not themselves add HUD interpretation, and a text-only reader cannot recover icon-only button identity.

First evaluate a filtered `GFxObject.ActionScriptVoid` observer: several verified routes converge there, and it sees resolved presentation calls. Establish how to identify the caller/movie and copy the relevant arguments safely before installing it. If that loses gameplay semantics, evaluate filtered generic script execution or a narrower underlying dispatch boundary. Avoid unfiltered logging of every script invocation.

Use a bounded cue-event queue with copied text/identity, generation/group key, activation, and cancellation state. Do not speak on the game thread or retain raw UObject/FString pointers across their lifetime. A returned UI state may omit a short-lived prompt; QTEs should preserve transitions and prioritize active button identity over subtitle backlog. Ordinary menu stabilization delays should not be applied to time-critical prompts. Measure actual delivery-to-speech latency before choosing any cue policy; a brief non-speech timing cue may be needed alongside spoken button identity. No timing extension or gameplay automation is proposed here.

Deduplicate by cue identity and lifecycle, not text forever: repeated identical instructions may be separate events. Stop stale queued speech on dismissal, skip, replacement, retry, and level transition. Validate visibility and active state before announcing preloaded records. Respect the user's choice of whether voiced dialogue subtitles should be read.

## Next validation session

No extra live capture is needed while the parent completes menu/build testing. For gameplay work, coordinate a user-led session and capture bounded events plus targeted snapshots:

1. One voiced subtitle entering, changing, and clearing; also pause, skip, replay, and toggle subtitle visibility. Correlate resolved `CallStartText.msg`, category/tag, and actual display timing.
2. The first ordinary tutorial/message overlay, before and after dismissal. Identify its owner, text, glyph replacement, multi-part behavior, and whether it uses Advertise, MovieText, TalkDemo, or another route.
3. One simple button prompt at creation, activation, input, and removal. Capture the active button movie and `Buttons` array, resolved tag, command enumeration, timer fields, and success/cancel flags.
4. Repeat for a hold, repeated presses, simultaneous inputs, stick direction/rotation, and one timing-ring/Synchronic prompt. Record a bounded timestamped trace; a single paused snapshot cannot establish a short input window.
5. Repeat one mapped action after a control remap and one randomized prompt. Verify actual required input against both the command and rendered tag before announcing it.
6. Check a retry/level transition for stale cues, then measure idle callback/event activity and QTE latency. Validate representative cases on both builds; matching reflection metadata is not a substitute for runtime testing.

All binary captures, disassembly, local extraction tools, and detailed traces remain ignored under `research/gameplay-cues/`. This document contains only interface identifiers, layout metadata, conclusions, and validation steps.
