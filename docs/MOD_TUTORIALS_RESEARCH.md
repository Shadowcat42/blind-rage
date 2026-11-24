# Mod tutorial boxes: feasibility research

Question: can Blind Rage open the game's own tutorial box with its own text, freeze the game while it is shown, and play game sounds (for example the rail-shooting lock tones) during it? Research date 2026-11-23, title-update scripts and executable. A temporary test command proved the approach live; it has been removed, and no player-facing feature is implemented yet.

**Answer: yes, with the game's own code doing almost all of the work.** The one new capability, calling a game script function, worked live (see the last section).

## The tutorial box already supports a pausing type

`BCGFxUI_Advertise` (movie `Huds.Advertise` in `Huds_SF`) is the tutorial box. Its `EBCAdvertiseType` values are `BCAT_Default` (0), `BCAT_Mission` (1), `BCAT_MissionNone` (2), `BCAT_CenterTop` (3), `BCAT_CenterPause` (4), `BCAT_Box` (5) and `BCAT_All` (6).

- `SetAdvertise(Icon, Text)` replaces button tags in `Text` and calls `CallCreate(Icon, Text)`, which runs the movie's ActionScript `Create` with that string. The text is free text.
- `ShowAdvertise(true, 4)` (`BCAT_CenterPause`) uses position `"center"` and turns on input. It calls `OnSetInput(false)` and `SetAdvertisePause(true)`, then `CallStart("center", …)`.
- `SetAdvertisePause(true)` disables the pause menu and calls `BCGFxMovieScene.SetGamePause(true)`, which calls the player controller's `SetPause(true)` and the HUD's `PauseMovie(true)`. That is the same pause the pause menu uses: its scene `Start` calls `SetGamePause(bUsePause)`.
- While the box waits, `BCGFxUI_Advertise.Tick` checks the confirm button (`DecideKeyName`, A or B depending on region). On a press it calls `CallSetComplete` and plays `SE_UI_fix_Cue`.
- The movie's ActionScript implements the center type: `GetTypeMC` handles `'center'`, `SetComplete` ends the center instance, and `Complete`/`SetInput` call back through `ExternalInterface` to `OnComplete`/`OnSetInput`. `OnComplete("center")` with the pause active calls `SetAdvertisePause(false)`, unpausing the game.

The level scripts reach this through `BCSeqAct_Advertise` → `BCHUD.AddAdvertise`/`AddAdvertiseIcon` → `ShowAdvertise`. Episode 1's rail-shooting tutorials (`S000_RapidShoot`, `S000_HeavyShoot`) use `BCAT_CenterTop`, which does not pause. Episode 3 (`BC-S020_L01_PLY_01` to `_05`) uses `BCAT_CenterPause` for its pausing explanations: `S020_DownEnemyInfo`, `S020_OverheatInfo`, `S020_ReactionInfo`, `S020_RecoverDescript`, `S020_CMoveMes`, `S020_ShootDescript`, `S020_RapidShootDescript`, `S020_BurstDescript`, `S020_UnlimitedMaxInfo` and `S020_UnlimitedDescript`. Its objective lines beside them use `BCAT_Mission`. So a mod box takes the same path as the game's own pausing tutorials.

The mod's existing tutorial reader hooks `CallCreate`/`CallStart`, so a box opened this way would be spoken by the normal narration pipeline (and follow `tutorials=1`).

## Lock tones and other game sounds

`BCGFxUI_Hud.ShowLockonMarker` chooses the lock tones:

- `SE_UI.SE_UI_lockon1_Cue` for each new lock, stepping to `SE_UI_lockon1_LV2_Cue` at 4 locks, `_LV3_Cue` at 8 and `_LV4_Cue` at 12.
- `SE_UI.SE_UI_lockon2_Cue` when the lock maximum is reached (`IsLockonMax`).

The previous tone is stopped first (`StopGFxUISound`). Playback is `BCGFxMoviePlayer.PlayGFxUISound(Snd)`, which calls the player controller's `Kismet_ClientPlaySound(Snd, WorldInfo, 1, 1, 0, false, false)`. Separately, `BCPlayerController_HeavyShoot.UpdateLockOnLevel` plays the lock-level step cues (`LockOnLevelParams[level].LevelUpSC`) with `Actor.PlaySound`.

These cues are referenced directly by script bytecode, so they are imports of the script package and stay loaded whenever the game is. The pause menu's confirm and cancel sounds (`SE_UI_fix_Cue`, `SE_UI_cancel_Cue`) use the same `PlayGFxUISound` path while the game is paused by the same `SetPause`. So the lock tones played this way during a paused tutorial box should be audible. That is an inference from the shared path, to be confirmed live. The mod would only ask the game to play its own cues; no audio would be extracted or shipped.

## How the mod could call these functions

The game's `UObject::ProcessEvent` is guest `0x82260f60`, identified from its code. It checks function flags and probe names, rejects native functions, builds an `FFrame` (node, object, script code, locals), copies `ParmsSize` (`UFunction+0x7e`) parameter bytes and calls `Func` (`+0x88`). The mod already hooks this exact function on both builds (gameplay hook 3; title-update host RVA `0x00e19240`, disc `0x03d3d630`). Its trampoline is therefore available, and the hook runs on the game thread.

Host code shows the recompiled context layout the mod would use: r3 at offset `0x00`, r1 (stack pointer) at `0x10`, r4 at `0x20`, r5 at `0x28`, r6 at `0x30`, and the link register at `0x100`. This matches the mod's existing argument reader. A call would:

1. run inside a game-thread hook, at a quiet moment with no game tutorial on screen;
2. save the context's argument registers, r1 and the link register;
3. lower r1, place the parameter block (and any string data) in that guest stack space, and set r3 = object, r4 = function, r5 = parameters;
4. call the ProcessEvent trampoline, then restore the saved registers before continuing.

Objects come from the reflection reader the mod already uses: the HUD's `HudAdvertiseMovie`, the `UFunction`s by name, their parameter offsets (`UProperty+0x54`), and the `SE_UI` cues.

## Risks and open questions

- **Calling game code is new for the mod.** Everything so far only observes. Register handling, re-entrancy and the guest stack must be right; a mistake crashes the game. This needs a live proof before anything else.
- **String ownership.** `SetAdvertise` reassigns its text when it contains button tags, which frees the old buffer. Passing text from the guest stack is only safe through `CallCreate`, or through `SetAdvertise` with text that has no tags. Otherwise the string must come from the game's allocator.
- **One box at a time.** The game's own tutorials use the same movie. A mod box must wait until no game tutorial is active, and must not clear one. The mod already tracks the box's lifecycle.
- **Layout.** Episode 3's pausing boxes hold a few sentences; much longer mod text might not fit visually. Speech is unaffected.
- **Sound during pause** is inferred from the pause menu's sounds, not yet heard.
- **Both builds.** The functions and hooks involved match on disc and title update (see `BUILD_PROFILES.md`), but the live proof would be on the title update.

## Live test (2026-11-23, title update)

A temporary test command (since removed) resolved `BCGFxUI_Advertise.CallCreate`, `ShowAdvertise`, `SetAdvertisePause`, `PlayGFxUISound`, the `bUsePause` flag and both lock cues by reflection on the worker thread. On the game thread, at the tutorial box's own `Tick` (a `ProcessEvent` call), it ran the game's call, then called `CallCreate("", text)`, `ShowAdvertise(true, 4)` and `PlayGFxUISound(SE_UI_lockon1_Cue)` through the ProcessEvent trampoline. It played `SE_UI_lockon2_Cue` 1.5 seconds later. Running it again while the box was open closed it (`SetAdvertisePause(false)`, `ShowAdvertise(false, 4)`).

Reflection matched the script: `CallCreate` parameters are 24 bytes (`Icon`@0, `Text`@12), `ShowAdvertise` 5 (`bShow`@0, `ShowType`@4), `SetAdvertisePause` 4, `PlayGFxUISound` 8 (`Snd`@0, `ReturnValue`@4), and `bUsePause` is at +0x1c8 with mask 0x80000000.

In eight runs during gameplay there were no failures or crashes. Each time the box's text was delivered through `CallCreate` and spoken by the existing tutorial reader. The box's `Tick` kept running while paused (45 ticks in the 1.5 seconds before the second tone), so the game's confirm check stays active. Seven runs ended with the player's confirm clearing `bUsePause`, which the script only does after unpausing. The log ends during the eighth run with its box still open, after its full lock tone.

The log cannot show what was heard or whether the game visibly froze. Those need the player's report.

The player heard both tones. Recordings compared against the original cue show the full lock tone played during the pause is missing its first 23 ms (silence, then full level), and sounds somewhat quieter. A second temporary command played the same cue the same way with no box. The player found the cut happens only while the game is paused. The cause was not traced. It is an inference that the engine's audio handling during a pause delays a newly started sound by about one frame. A real feature can avoid it by playing demonstration tones just before the box pauses the game or right after it resumes.

## Next steps

- Confirm by ear that the game freezes and both tones are audible while paused.
- Design the real feature: which modes get a box, the wording, a once-per-mode record and a replay key, and waiting until no game tutorial is on screen.
- Allocate strings through the game if text ever needs button tags (`SetAdvertise` path).
