# First Episode 1 test

This build is ready for the first user-led test after offline implementation checks. Actual speech timing and gameplay coverage remain to be established. No live testing is required to complete the preliminary groundwork, and no testing is expected until the user is ready.

## Before starting

1. Start NVDA, then launch `BlindRage.exe` before entering gameplay. Disc and title-update profiles are both supported; automatic selection follows the existing installation setting.
2. Keep the recomp's preferred button-icon setting. Blind Rage follows its effective Xbox, PlayStation or keyboard display. Controller icon changes which require a recomp restart still require that restart.
3. Default gameplay options are enabled: subtitles, tutorials, QTEs, health warnings and wider shooting acquisition. Existing INI files use defaults for missing keys. Edit `mods/accessibility/accessibility.ini` and restart to change them.
4. Ctrl+Alt+F2 repeats the current menu, otherwise an active QTE/timing cue, otherwise current text. Ctrl+Alt+F3 explicitly stops speech; Ctrl+Alt+F5 toggles narration. Back/Select reads Health/Burst during gameplay and retains the game's cinematic-skip behavior. Keyboard users use the recomp's Back binding (default Tab). Holding or releasing it should not repeat the readout. It should stay silent in cutscenes and pause/menu screens; the original game action still runs. Set `[gameplay] status_button=0` to disable the added readout.
5. When convenient, verify one option that requests a game restart. After accepting, expect the normal Blind Rage startup announcement and menu narration in the replacement game. The selected build and game arguments should be retained. This can wait until there is no unsaved progress to lose.
6. On the results screen, Square (PlayStation) / X (Xbox) repeats the full current rank/score summary and selected menu choice. On its expanded point-breakdown page it repeats the breakdown instead. Holding the button should not repeatedly speak, and it adds no repeat action during gameplay. In Episode Menu > Play/Record, the same button repeats only the episode introduction. Native input still passes through. This controller route has offline tests; live delivery needs verification when convenient.

## Short calibration, then an independent attempt

First confirm that a real subtitle/tutorial reads and its button label matches the selected display. Then check the first ordinary press/mash prompt and the first timing cue. Timing should name the button in preparation and say its equivalent “now” only in the game's accepted window. It may need a latency adjustment after this test; it is not yet an Excellent-rank aid.

For stick prompts, expect an explicit stick and direction on a controller. Simultaneous opposing tilts should be spoken as one combined request. Keyboard mode should name the bound keys; mouse-enabled rotation uses the mouse, while mouse-disabled rotation gives a circular key sequence. A release-required timed hold should explicitly say to release; an animation-controlled hold says to keep holding.

During rail shooting, use the game's normal lock/acquisition and firing controls. The aid expands its acquisition cone and retains the game's own locking sounds, timing and caps. It does not fire or aim the controller for the player. Back/Select reads the current displayed Health/Burst percentages; Ctrl+Alt+F4 remains an alternative. A full Burst meter alone is not a readiness announcement; the native usable Burst button triggers that notice.

After these first checks, attempt the episode independently if the cues are usable. The intended first-test success is progress without live button coaching, not a perfect rank or first-try completion. There is no need to stop at every prompt for the agent. The user controls the game throughout; the agent can inspect saved logs afterward.

## Reporting a problem

Note the action/screen and what speech was missing, late, repeated or incorrect. If practical, press Ctrl+Alt+F8, then retain `userdata/accessibility/accessibility.log` before restarting: startup clears the previous session's diagnostics. These files can contain gameplay text/memory, so keep them local unless intentionally shared.

Useful log markers are `GAMEPLAY_METADATA`, `QTE_ICON`, `QTE_STATE`, `TIMING_STATE`, `GAME_QTE`, `GAME_STATUS`, `GAMEPLAY_STATS` and `SPEAK`. A `GAME_CUE_RESET` means a stale or overflowed batch was discarded; retrying a prompt/new lifecycle can restore it. Unsupported or malformed cues deliberately remain silent rather than invent a button.

Later work can calibrate latency, refine wording/earcons with other players, add target selection/manual aiming feedback, expand arena combat and handle encounter-specific hazards. Those are separate iterations after this groundwork.
