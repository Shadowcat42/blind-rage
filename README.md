# Blind Rage

A blind-accessibility mod for **Asura's Wrath**, starting with spoken menus and expanding toward gameplay accessibility.

This is an experimental, independently developed mod. **Users must obtain the recomp separately and supply their own game data.** This repository contains only mod source, tests, configuration, and build/documentation files.

## Compatibility

Currently targets the **Mark2 disc build 2026.11.01.1627** of the [PC recompilation](https://github.com/omar2300/asuras-wrath-pc). The launcher verifies executable, runtime, and renderer fingerprints. Other builds need separate validation.

Windows x64 is required. Start NVDA before the mod to use its voice; [Prism](https://github.com/ethindp/prism) provides other speech backends and fallback engines.

## Install and run

1. Install the supported recomp and your game data separately.
2. Extract a Blind Rage mod package into the installation root. It adds `BlindRage.exe` and files under `mods/accessibility/dist`.
3. Run **BlindRage.exe**. The ordinary recomp launcher does not load the mod.

The existing `AsurasWrathAccessible.exe` launcher remains usable in the development installation. The recomp updater can change binaries and make the mod incompatible; Blind Rage does not update or replace them.

| Shortcut while the game has focus | Action |
| --- | --- |
| Ctrl+Alt+F1 | Test speech |
| Ctrl+Alt+F2 | Repeat current item |
| Ctrl+Alt+F3 | Stop speech |
| Ctrl+Alt+F5 | Toggle narration |
| Ctrl+Alt+F8 | Save local UI diagnostics |

Configure virtual-key codes in `mods/accessibility/dist/accessibility.ini`; 0 disables a chord. Restart to reload the file. On this experimental branch, Windows hotkeys are registered while the game has focus and released when it loses focus. A conflicting shortcut may be unavailable; named launcher commands remain usable.

## Current behavior

One shared reader handles live Scaleform menus, settings rows, collection entries and common dialogs. It reads actual selection state, labels, values, effect descriptions, and unlock requirements. Confirmations should announce the prompt and starting choice, then only the selected choice when navigating. Acquired collection totals are entry-only.

This experimental branch replaces recurring menu and registry polling with shared native hooks for ActionScript assignment, movie open/close, text changes, display flag changes, and title status assignments. Windows notifications handle foreground changes and shortcuts. The speech worker waits between events; a mutation starts bounded settling checks before narration. It reads bounded guest-memory snapshots and does not count navigation keys. Live event coverage is still being validated; see [hook research](docs/HOOK_RESEARCH.md).

The latest initial-selection fix passes synthetic and private snapshot tests and still needs live confirmation. Coverage remains under active testing. Custom key binding, pause menus, QTEs, and combat are not claimed as fully accessible. See the [roadmap](docs/ROADMAP.md) for planned gameplay work.

## Build from source

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

Logs and timestamped UI dumps stay locally under the installation's `userdata/accessibility`. Dumps can contain game memory and must not be committed or automatically uploaded. Optional private replay uses `dist/reader_test.exe research/snapshot-name`; snapshots are not required for CI and are excluded from Git.

Each game process starts with a fresh mod log and removes prior `dump-<number>.txt` and `.pages` captures from that directory. Copy captures you want to retain elsewhere before restarting. Restarting the mod worker within the same game process preserves its session files. Unrelated files and subdirectories are retained.

`SPEAK [backend]` records every utterance accepted by Prism, including repeats, speech tests, and narration status messages. Failed requests include their text; stop requests are recorded too. This records Blind Rage's output, rather than NVDA speech from other applications, and cannot prove that an utterance finished before being interrupted.

Keep new support in the shared reader and narration pipeline where possible. Add synthetic regressions for observed failures. Never include game/recomp code, extracted assets, saves, or memory captures in contributions.

## Licensing

The license for Blind Rage's original code is **not yet selected**. Dependencies retain their own terms; see [third-party notices](THIRD_PARTY_NOTICES.md). No rights to Asura's Wrath or its recompilation are granted here.
