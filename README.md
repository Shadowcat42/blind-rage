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

The old `AsurasWrathAccessible.exe` compatibility launcher can be deleted. Current builds and installations provide only `BlindRage.exe`. The recomp updater can change binaries and make the mod incompatible; Blind Rage does not update or replace them.

When an option asks to restart the game, an active Blind Rage session now restarts through the mod launcher automatically. It preserves the selected disc/title-update build and game arguments, waits for the old process to exit, and applies the normal compatibility checks before loading the mod again. This requires the current DLL and launcher together. A game already started without the mod still needs to be launched through Blind Rage once.

| Shortcut while the game has focus | Action |
| --- | --- |
| Ctrl+Alt+F1 | Test speech |
| Back/Select during gameplay | Read Health/Burst status; cutscene skipping is preserved |
| Square (PlayStation) / X (Xbox), on results | Repeat the current results summary or battle-point breakdown |
| Ctrl+Alt+F2 | Repeat current item |
| Ctrl+Alt+F3 | Stop speech |
| Ctrl+Alt+F4 | Read current Health/Burst status (gameplay prototype) |
| Ctrl+Alt+F5 | Toggle narration |
| Ctrl+Alt+F8 | Save local UI diagnostics |

Configure virtual-key codes in `mods/accessibility/accessibility.ini`; 0 disables a chord. Restart to reload the file. On this experimental branch, Windows hotkeys are registered while the game has focus and released when it loses focus. A conflicting shortcut may be unavailable; named launcher commands remain usable.

## Current behavior

One shared reader handles live Scaleform menus, settings rows, collection entries and common dialogs. It reads actual selection state, labels, values, effect descriptions, and unlock requirements. Confirmations should announce the prompt and starting choice, then only the selected choice when navigating. Acquired collection totals are entry-only.

Collection grids resolve their selected record from the list cursor and page, including Achievements, CG Art, Illustrations, and Interludes & Movies. Grid position distinguishes locked entries with identical hidden labels. Gauges retain their selected-name/effect narration without added position announcements.

This experimental branch replaces recurring menu and registry polling with shared native hooks for ActionScript assignment, movie open/close, text changes, display flag changes, and title status assignments. Windows notifications handle foreground changes and shortcuts. The speech worker waits between events; a mutation starts bounded settling checks before narration. It reads bounded guest-memory snapshots and does not count navigation keys. Live event coverage is still being validated; see [hook research](docs/HOOK_RESEARCH.md).

The menu implementation has passed synthetic, private replay, and user-led live checks. Custom key binding, pause menus, QTEs, and combat are not claimed as fully accessible. See the [roadmap](docs/ROADMAP.md) for planned gameplay work.

The `experiment/gameplay-text-reader` branch adds subtitle/tutorial narration and the first Episode 1 gameplay prototype. It follows the recomp's effective Xbox, PlayStation or keyboard display and current remapped button labels. Subtitle and tutorial narration default to enabled, including existing installations without new settings. Add `[gameplay]` settings `subtitles=0` or `tutorials=0` to `mods/accessibility/accessibility.ini` to disable either. Text and status speech queue by default (`interrupt=0`); urgent new QTE instructions may interrupt (`qte_interrupt=1`). Disappearing cues do not stop NVDA.

Shared QTE readers cover resolved presses, mashes, holds, grouped stick commands and rotations, plus the separate Synchronic input window. Health/Burst status is available on Back/Select during active gameplay, with low-health warnings and native Burst-button readiness. This follows the recomp's Back binding (default Tab on keyboard), preserves the game's original handler and stays silent in cutscenes or pause/menu screens. Set `status_button=0` to disable it; Ctrl+Alt+F4 remains an alternative. The optional shooting aid widens the game's acquisition cone while retaining its eligibility, range, occlusion, lock limits, homing and manual firing. Defaults are `qtes=1`, `status_warnings=1`, `shooting_assist=1` and `shooting_cone_degrees=45`; changes take effect after restarting the mod/game.

This build has offline validation and is ready for the first user-led gameplay test. Live coverage, speech latency and complete Episode 1 playability have not yet been established. Start the mod before entering gameplay. See the [test instructions](docs/EPISODE_1_TESTING.md), [implementation/evidence](docs/EPISODE_1_GROUNDWORK.md), [text-reader limits](docs/GAMEPLAY_TEXT_READER.md) and [QTE research](docs/QTE_INPUT_RESEARCH.md). Image-only text, other conversation/interlude routes, later combat assistance and encounter-specific exceptions remain future work.

## Build from source

Run `./format.ps1` to format the mod's C++ source and tests, or `./format.ps1 -Check` for a non-editing check. The repository's clang-format style uses the formatter already supplied by the pinned compiler toolchain. CI checks formatting; dependencies and private research are excluded.

For the repeating lock-on sound investigation, see [bounded trace instructions](docs/LOCK_TRACE.md). Diagnostics and the candidate stable-cone repair default to off; the repair is gated on live evidence.

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

Logs and timestamped UI dumps stay locally under the installation's `userdata/accessibility`. Dumps can contain game memory and must not be committed or automatically uploaded. Optional private replay uses `dist/reader_test.exe research/snapshot-name`, or `dist/reader_test.exe --profile=updated research/snapshot-name` for title-update captures. Snapshots are not required for CI and are excluded from Git.

Each game process starts with a fresh mod log and removes prior `dump-<number>.txt` and `.pages` captures from that directory. Copy captures you want to retain elsewhere before restarting. Restarting the mod worker within the same game process preserves its session files. Unrelated files and subdirectories are retained.

`SPEAK [backend]` records every utterance accepted by Prism, including repeats, speech tests, and narration status messages. Failed requests include their text; stop requests are recorded too. This records Blind Rage's output, rather than NVDA speech from other applications, and cannot prove that an utterance finished before being interrupted.

Keep new support in the shared reader and narration pipeline where possible. Add synthetic regressions for observed failures. Never include game/recomp code, extracted assets, saves, or memory captures in contributions.

## Licensing

The license for Blind Rage's original code is **not yet selected**. Dependencies retain their own terms; see [third-party notices](THIRD_PARTY_NOTICES.md). No rights to Asura's Wrath or its recompilation are granted here.
