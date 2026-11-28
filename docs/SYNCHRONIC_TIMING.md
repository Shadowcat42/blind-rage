# Synchronized QTE timing (research notes)

Findings from the title-update capture: `BCInterpTrackSynchronic` and `BCGFxUI_Synchronic` scripts, the native input-window functions (read as PowerPC), and one logged Episode 1 run. They describe the game's own behavior, not a design for narration. Barrage (mash) tracks were not traced.

## Timing model (Normal type)

Each key has a perfect moment K, its `StartTime` in the track's `Tracks` array. The track object has defaults the Episode 1 finale does not override: `StartTime` 1.1 s, `RankTimeA` 0.1, `RankTimeB` 0.3, `RankTimeC` 0.5, `RankTimeOver` 0.1. `GetTimeValue` accumulates (switch fall-through): A = 0.1, B = 0.4, C = 0.9, Over = 0.2.

| Time | What happens |
| --- | --- |
| K - 1.1 s | `IsStartTrack` (native: `Now >= key - StartTime`) is true, so `StartSynchronicTime` starts that key's HUD element. |
| K - 0.9 s | `IsInputOk` (native: `Now >= key - C` and not past `key + Over`) turns true. `UpdateNormal` drives the ring: `(K - Now) / C`, 1.0 down to 0.0. |
| K +/- 0.1 s | `bShowTiming` is set and a rumble pulse starts the first time. |
| K + 0.2 s | Window closes. `IsInputEnd` ends the track. |

`GetResult`, by press time relative to K: early side, within 0.1 s is 3, within 0.4 s is 2, within 0.9 s is 1; late side, within 0.1 s is 3, within 0.2 s is 1. Otherwise 0. The button is hard-coded to button ID 2 (Y on the controller).

Chaining: a later key's `IsActiveTrack` requires the previous key to be judged. Timing out also judges it (`EndSynchronicTime` sets `bJudge`), so the next key's window opens when the previous one ends. Each key's HUD element is tagged (track name plus index), so prompts for consecutive keys overlap. A press plays `SE_UI_synchronic_Cue`, calls `ShowButton` and ends that element with a rank.

## Check against a logged run

The Episode 1 finale has three keys about 0.65 s apart. The mod's log showed the prompt event 203 ms before the window event (the model predicts 200 ms: 1.1 s minus 0.9 s). All three window ends fell within about 30 ms of K + 0.2 s, which is the timeout time, so none of those presses may have registered in time. The log does not record presses.

## Displayed grades

The grade is shown on screen. Both a press (`BCInterpTrackSynchronic.ShowButton`) and a timeout (`SetResult`) end the key through `BCGFxUI_Synchronic.EndSynchronic(SyncTag, Result)` (`Result` at frame +0xc). It maps Result 1/2/3 to rank 0/1/2 and calls the movie's ActionScript `End(tag, rank)`; a press also calls `Push(tag, hitCnt, rank)`. In the `Huds_SF` Synchronic movie both start `mcInst_Rank` for rank 0 or higher, and `GetTextRank` looks up `BCGame.Synchronic.SynchronicRankC/B/A` with fallbacks GOOD, GREAT and EXCELLENT. A miss (Result 0) shows no grade. Result 3 also counts `AddSynchronicImpact_Excellent` in the save.

## Narration and count-in

The prompt ("Timing: Triangle.") is read when the HUD element appears, 1.1 s before the key. A count-in then plays: three beeps at 525 Hz and a fourth at 1050 Hz, 0.33 s apart (onset to onset), each 50 ms long, at the `audio_cue_volume` level. The fourth beep starts on the key's perfect time, so the first falls at K - 0.99 s, before the input window opens (K - 0.9 s); press on the fourth. The spacing is the widest that fits: the mod's first clock reading arrives a frame after the prompt (logged 1.038-1.055 s before K), and the first beep needs room for the audio queue after it. There is no spoken "now" for a tap key.

Short leads: a key less than 1.1 s into its scene starts its prompt as soon as the scene starts, with less lead. Episode 11 has three such keys, at 0.87, 1.04 and 1.06 s into their scenes (`BC-S110_STE_A0040-0040`, `B0030-0030`, `B0090-0092`; no track there overrides `StartTime`), and their count-ins lost the first beat. When the time from the first clock reading to the key, less 40 ms for the audio queue, is under 0.99 s, the four beats are spaced evenly over it instead, with the high beat still on the key. A 0.87 s lead gives about 0.28 s spacing. Spacing never goes below 0.15 s (below that the earliest beats are skipped), and it is chosen at the first reading, so later clock readings move the beats without changing the tempo. The log's `COUNT_IN_PLAYED` line records the spacing. The grade the HUD shows is read as "Good.", "Great." or "Excellent." when the key ends.

Scheduling: `UpdateTracks` calls `UpdateNormal(Idx, NowTime)` (Idx at +0, NowTime at +4) on every update once `StartSynchronicTime` has started the key, before the input window opens. The first reading of the key's `StartTime` (+8 in the 24-byte `BCSynchronicTrackKey`) minus `NowTime` is stamped with the steady clock and schedules the beeps; the window opening and the 0.2 s mark reschedule them on the same track clock. A key that waits for its predecessor's judgment starts its count-in only when it becomes active, beeps already due are skipped, and its schedule replaces the previous key's. The beeps are rendered on one continuous sample clock, resynced to the steady clock only after more than 15 ms of drift, and the queue ahead of each block is measured from XAudio2's `SamplesPlayed`. The mod compensates for its own audio queue but not the output device's latency.

The count-in follows the track's own clock (`UpdateNormal`'s NowTime): if it stops advancing for 150 ms the beeps fade out. The first clock reading after such a pause republishes the key with its remaining time, so the count-in reschedules at once and plays every beat still ahead. A beat that already sounded before the pause is not repeated even if the reschedule places it after the resume, and beats rendered while muted are not counted as heard. Episode 3's first synchronic key needs this: a pausing tutorial popup (`BCAT_CenterPause`) freezes the ring between the second and third low beats (the third, 0.33 s before the key, was silenced by the pause in a live log), and after it is dismissed the third low beat and the high beat follow on the game's clock. The beeps follow the `qtes` setting, the mute key and game focus. `COUNT_IN_PLAYED` in the log gives each beat's render time relative to the perfect time (`-` if not rendered). Barrage (mash) keys keep speech ("Prepare to mash", "Mash now"), but no track in the game uses them.

## Not traced

Barrage tracks (none are used), the keyboard mapping of the button, localized grade text (the mod reads the English fallbacks), and whether other episodes override the timing defaults.

## Open design questions

- A single count-in will not fit every sequence: key spacing and press counts vary.
- Overlapping prompts make a stacked audio rhythm hard to follow.
- Speech is too slow and variable for the top rank band (0.2 s wide), so any precise cue probably needs a non-speech sound with latency compensation.

## Survey across the game

Every cooked package was scanned for `BCInterpTrackSynchronic` and the track data of the cutscene (STE) packages was read: 100 track objects with keys, 112 keys in all.

- **Type:** all 112 keys are Normal (tap). None uses the Barrage (mash) type, although the type exists in the scripts. Mashing in the game's ordinary QTEs is a separate system (`BCSeqAct_InputDivergence`).
- **Presses per sequence:** 91 single-key sequences, 6 with two keys, 3 with three.
- **Spacing between consecutive taps:** 12 gaps. Episode 1's finale is the tightest at 0.65 s. The next are 1.33-1.57 s, and the rest 2.66-3.49 s. Prompts for consecutive keys overlap only when the gap is under about 1.3 s (the prompt appears 1.1 s before its key and the previous window closes 0.2 s after its key), so only Episode 1's finale overlaps.
- **Disabled tracks:** four track objects carry `bDisableTrack=true` (in `S010_STE_A0050-0051`, `S030_STE_B0050-0050` and `S180_STE_B0060-0062`). In Unreal Engine 3 that flag normally makes Matinee skip the track; this has not been confirmed in this game's scripts, so treat those as probably not playable.
