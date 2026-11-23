# Blind Rage

A blind-accessibility mod for **Asura's Wrath**, starting with spoken menus and expanding toward gameplay accessibility.

This is an experimental, independently developed mod. **Users must obtain the recomp separately and supply their own game data.** This repository contains only mod source, tests, configuration, and build/documentation files.

## Compatibility

Targets the **Mark2 2026.11.01.1627 disc and title-update builds** of the [PC recompilation](https://github.com/omar2300/asuras-wrath-pc). Both use one reader with verified build profiles. The launcher checks executable, runtime, and renderer fingerprints; the updated profile also checks the installed `game/default.xexp` patch. Other builds need separate validation.

Windows x64 is required. Start NVDA before the mod to use its voice; [Prism](https://github.com/ethindp/prism) provides other speech backends and fallback engines.

## Install and run

1. Install the supported recomp and your game data separately.
2. Extract a Blind Rage mod package into the installation root. It adds `BlindRage.exe` and files under `mods/accessibility/dist`.
3. Run **BlindRage.exe**. The ordinary recomp launcher does not load the mod.

Automatic mode attaches to a recognized game already running from this installation. Otherwise, it follows the installer's `userdata/install.txt`: an installed title update selects `bin`, and a disc-only installation selects `bin_disc`. An unsupported updated installation reports an error rather than silently using the disc build.

Use `BlindRage.exe --build disc` or `BlindRage.exe --build updated` to choose explicitly. `BlindRage.exe --check` verifies automatic selection without launching or injecting; add `--build disc` or `--build updated` to check either profile. Close a running game before switching builds.

The recomp updater can change binaries and make the mod incompatible; Blind Rage does not update or replace them.

Settings live in `mods/accessibility/accessibility.ini`. Installing creates it from the defaults in `config/accessibility.ini`, and later installs add new settings while keeping your values. The mod never rewrites it at startup.

When an option asks to restart the game, an active Blind Rage session restarts through the mod launcher automatically. It preserves the selected disc/title-update build and game arguments, waits for the old process to exit, and applies the normal compatibility checks before loading the mod again. This requires the current DLL and launcher together. A game already started without the mod still needs to be launched through Blind Rage once.

| Shortcut while the game has focus | Action |
| --- | --- |
| Back/Select during gameplay | Read Health/Burst status; cutscene skipping is preserved |
| D-pad Left / Right, during shooting or melee rapid fire | Select the previous / next target cluster for the beacon |
| D-pad Up, during shooting or melee rapid fire | Cycle shooting assistance: off, beacons, boosted; saves the selected mode |
| D-pad Down, during melee rapid fire | Switch between projectiles first and enemies first; saved |
| Hold L1, during shooting or melee rapid fire | Steer the cursor onto targets in any mode; moving the stick takes over |
| Right stick Left / Right, in Episode Menu -> RECORD | Browse difficulty records |
| Right stick Up / Down, in Episode Menu -> RECORD | Browse overall rank and individual stats; Square/X repeats the selected entry |
| Square (PlayStation) / X (Xbox), on results | Repeat the current results summary or battle-point breakdown |
| Square (PlayStation) / X (Xbox), on episode Play/Record | Repeat the episode introduction |
| Ctrl+Alt+F2 | Repeat current item |
| Ctrl+Alt+F4 | Read current Health/Burst status |
| Ctrl+Alt+F5 | Toggle narration |
| Ctrl+Alt+F8 | Save local UI diagnostics |
| Ctrl+Alt+F9 | Cycle shooting mode (keyboard equivalent of D-pad Up) |
| Ctrl+Alt+F10 / F12 | Previous / next beacon cluster (D-pad Left / Right) |
| Ctrl+Alt+F11 | Switch melee aim priority (D-pad Down) |

Change shortcuts in the `[hotkeys]` section of `mods/accessibility/accessibility.ini` using key names, such as `aimmode=Ctrl+Alt+F9` or `repeat=Ctrl+Shift+Numpad5`. Modifiers are Ctrl, Alt, Shift and Win; keys include A-Z, 0-9, F1-F24, Numpad0-9 and names such as Space, Enter, PageUp, Home, Slash or Backtick. `None` disables a shortcut. Older numeric key codes still work and are converted to names on install. An unrecognized value is logged and the default is used. Restart to reload the file. Windows hotkeys are registered while the game has focus and released when it loses focus. A conflicting shortcut may be unavailable; named launcher commands remain usable.

## Current behavior

One shared reader handles live Scaleform menus, settings rows, collection entries and common dialogs. It reads actual selection state, labels, values, effect descriptions, and unlock requirements. Confirmations should announce the prompt and starting choice, then only the selected choice when navigating. Acquired collection totals are entry-only.

Interlude illustration screens read the currently displayed text and their embedded common dialogs. These follow the scene's own start/end lifecycle; preloaded passages are not announced. This support has captured-state checks and awaits live confirmation.

Collection grids resolve their selected record from the list cursor and page, including Achievements, CG Art, Illustrations, and Interludes & Movies. Grid position distinguishes locked entries with identical hidden labels. Gauges retain their selected-name/effect narration without added position announcements.

Menus are event-driven: shared native hooks for ActionScript assignment, movie open/close, text changes, display flag changes and title status assignments replace polling. Windows notifications handle foreground changes and shortcuts. The speech worker waits between events; a change starts bounded settling checks before narration. It reads bounded guest-memory snapshots and does not count navigation keys. See [hook research](docs/HOOK_RESEARCH.md).

Gameplay narration covers subtitles and tutorials, following the recomp's effective Xbox, PlayStation or keyboard display and current remapped button labels. Add `[gameplay]` settings `subtitles=0` or `tutorials=0` to disable either. Text and status speech queue by default (`interrupt=0`); urgent new QTE instructions may interrupt (`qte_interrupt=1`). Disappearing cues do not stop NVDA.

Shared QTE readers cover resolved presses, mashes, holds, grouped stick commands and rotations, plus the separate Synchronic timing prompts (a four-beep count-in whose high fourth beep marks the moment to press) and the melee Special Attack (finisher) and counter prompts. Health/Burst status is available on Back/Select during active gameplay, with low-health warnings and native Burst-button readiness. This follows the recomp's Back binding (default Tab on keyboard), preserves the game's original handler and stays silent in cutscenes or pause/menu screens. Set `status_button=0` to disable it; Ctrl+Alt+F4 remains an alternative.

Rail-shooting guidance: stereo position points left/right, pitch indicates elevation, and pulses accelerate as aim approaches the selected target cluster. You still aim and fire; holding L1 steers the cursor onto unlocked targets. Native lock feedback is retained, and guidance pauses at full volley capacity. Boosted mode also widens the game's acquisition cone while keeping its eligibility, range, occlusion, lock limits, homing and manual firing. See [shooting modes](docs/SHOOTING_BEACONS.md). The same modes cover melee rapid fire (holding square outside rail shooting), against enemies and incoming projectiles, with a projectiles-first or enemies-first priority; boosted widens the game's homing angle there (`boost_homing=3`). See [melee aiming](docs/MELEE_AIM.md). Defaults are `qtes=1`, `status_warnings=1`, `shooting_mode=beacons`, `boost_point_blank=2`, `boost_max_range=3`, `tracking_strength=100` and `audio_cue_volume=30`. D-pad Up changes shooting mode during active shooting; other INI changes take effect after restarting the game.

Status: menus have passed synthetic, private replay and live checks. Episode 1 (rail shooting) and Episode 2 (melee) have been played with the mod; later episodes have not been tested. Custom key binding and pause menus are not claimed as fully accessible. Start the mod before entering gameplay. See the [test instructions](docs/EPISODE_1_TESTING.md), [implementation/evidence](docs/EPISODE_1_GROUNDWORK.md), [text-reader limits](docs/GAMEPLAY_TEXT_READER.md), [QTE research](docs/QTE_INPUT_RESEARCH.md) and the [roadmap](docs/ROADMAP.md). Image-only text, other conversation/interlude routes, wider combat assistance and encounter-specific exceptions remain future work.

## Build from source

Run `./format.ps1` to format the mod's C++ source and tests, or `./format.ps1 -Check` for a non-editing check. The repository's clang-format style uses the formatter already supplied by the pinned compiler toolchain. CI checks formatting; dependencies and private research are excluded.

[Lock tracing](docs/LOCK_TRACE.md) records rail-shooting lock and guidance decisions for diagnosis; it defaults to off.

```powershell
./bootstrap.ps1
./build.ps1 -Test
./package.ps1
```

Bootstrap downloads checksum-pinned official LLVM-MinGW, Prism, and MinHook dependencies. A standalone checkout needs no game files and writes output inside itself. CI runs synthetic tests without game data. `./build.ps1 -Test -NoInstall` skips the installer; it still replaces files in the local `dist` directory, so close any game using those files first.

Install a locally built mod with:

```powershell
./install.ps1 -GameRoot 'D:\Games\AsurasWrath'
```

When developing directly in an installation's `mods/accessibility` folder, the build also refreshes that installation's launcher. Close the game before rebuilding its loaded DLL.

`package.ps1` creates `artifacts/blind-rage-windows-x64.zip` from an explicit mod-file allowlist. It excludes test tools, build tools, game/recomp files, saves, and snapshots. It does not publish a GitHub release.

## Diagnostics and contributions

Logs and timestamped UI dumps stay locally under the installation's `userdata/accessibility`. Dumps can contain game memory and must not be committed or automatically uploaded. Optional private replay uses `build/tests/reader_test.exe research/snapshot-name`, or `build/tests/reader_test.exe --profile=updated research/snapshot-name` for title-update captures. Snapshots are not required for CI and are excluded from Git. Development test executables and their speech dependency stay under `build/tests`; `dist` contains only the launcher, runtime mod libraries and notices. Packaging removes its temporary staging directory after success or failure.

Each game process starts with a fresh mod log and removes prior `dump-<number>.txt` and `.pages` captures from that directory. Copy captures you want to retain elsewhere before restarting. Restarting the mod worker within the same game process preserves its session files. Unrelated files and subdirectories are retained.

`SPEAK [backend]` records every utterance accepted by Prism, including repeats and narration status messages. Failed requests include their text; stop requests are recorded too. This records Blind Rage's output, rather than NVDA speech from other applications, and cannot prove that an utterance finished before being interrupted.

Keep new support in the shared reader and narration pipeline where possible. Add synthetic regressions for observed failures. Never include game/recomp code, extracted assets, saves, or memory captures in contributions.

## Licensing

The license for Blind Rage's original code is **not yet selected**. Dependencies retain their own terms; see [third-party notices](THIRD_PARTY_NOTICES.md). No rights to Asura's Wrath or its recompilation are granted here.
