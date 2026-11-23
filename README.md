# Blind Rage

A blind-accessibility mod for **Asura's Wrath**. It speaks menus, subtitles, tutorials, quick-time events and Health/Burst status, and it adds audio guidance for shooting and walking sections.

This is an independent, experimental mod. **You need the PC recompilation and your own copy of the game data; neither is included.** This repository contains only the mod's source, tests, configuration and documentation.

## Status

- **Controller is the primary way to play.** Xbox and PlayStation button names follow the recomp's own button-icon setting.
- **Keyboard and mouse support is still in progress.** Prompts name your bound keys, and most of the game can be played that way, but some prompts still need work. Some actions also don't respond to the keyboard (for example lock-on target switching; see [known issues](#known-issues)).
- Episodes 1 to 5 have been played with the mod. Later episodes haven't been tested yet, so expect gaps.

## Compatibility

Blind Rage targets the **Mark2 2026.11.01.1627 disc and title-update builds** of the [PC recompilation](https://github.com/omar2300/asuras-wrath-pc). The launcher checks the executable, runtime and renderer fingerprints; for the title-update build it also checks the installed `game/default.xexp` patch. Other recomp builds are refused until they've been checked. **The title update is recommended:** all gameplay support was developed and played on it. The disc build passes the same offline checks (identical game data, and every hooked function matches) and its menus read live, but its gameplay support hasn't been played. The disc build only runs in an installation without the title update: once the update is installed, the disc executable loads its patch and crashes at startup, with or without the mod. The recomp's updater can change these files and make the mod incompatible; Blind Rage never updates or replaces them.

You need Windows x64. Start NVDA before the mod to use its voice; otherwise [Prism](https://github.com/ethindp/prism) picks another speech engine.

## Install and run

1. Install the supported recomp and your game data. The recomp's setup isn't screen-reader accessible yet and currently needs OCR; an issue is open with the recomp.
2. Extract the Blind Rage package into the game's installation folder. It adds `BlindRage.exe` and `mods/accessibility`.
3. Start NVDA, then run **BlindRage.exe**. The recomp's ordinary launcher doesn't load the mod.

BlindRage.exe attaches to a recognized game already running from this installation. Otherwise it starts the game: the title-update build when it's installed, otherwise the disc build. To choose explicitly, run `BlindRage.exe --build disc` or `BlindRage.exe --build updated`. The disc build is refused once the title update is installed, because it can't run with the update's patch. `BlindRage.exe --check` reports which build would be used without starting anything.

When an option asks to restart the game, the mod restarts it through its own launcher with the same build and arguments, so narration continues.

## Controls

### Controller

| Button | Where | Action |
| --- | --- | --- |
| Back / Select | Gameplay | Read Health, Burst and Unlimited status. Cutscene skipping still works. |
| D-pad Up | Rail shooting, melee rapid fire | Cycle shooting assistance: off, beacons, boosted. The choice is saved. |
| D-pad Left / Right | Rail shooting, melee rapid fire | Previous / next target cluster for the beacon |
| D-pad Down | Melee rapid fire | Switch between projectiles first and enemies first. Saved. |
| Hold L1 | Rail shooting, melee rapid fire | Steer the cursor onto targets; moving the stick takes over |
| Right stick Up / Down | Options, Control Settings | Read the selected control type one button at a time |
| Right stick Left / Right | Episode Menu, Record | Browse difficulty records |
| Right stick Up / Down | Episode Menu, Record | Browse overall rank and individual stats |
| Square / X | Results, Record, episode Play | Repeat the summary, selected record or episode introduction |

### Keyboard shortcuts

These work with any controller or keyboard setup.

| Key | Action |
| --- | --- |
| `` ` `` (backtick) | Repeat the current menu item, cue or latest announcement |
| Ctrl+Alt+F5 | Turn narration on or off |
| H | Read Health, Burst and Unlimited status |
| M | Cycle shooting assistance (D-pad Up) |
| , (comma) / . (period) | Previous / next target cluster (D-pad Left / Right) |
| / (slash) | Switch melee aim priority (D-pad Down) |
| F5 | Save a diagnostic dump (see [diagnostics](#diagnostics)) |
| Ctrl+Up / Down / Left / Right | Keyboard right stick: hold during gameplay (camera, lock-on target switching); press in Control Settings and Episode records to browse |

Change shortcuts in the `[hotkeys]` section of `mods/accessibility/accessibility.ini`, for example `status=Ctrl+Alt+F4`. Modifiers are Ctrl, Alt, Shift and Win. Keys include A-Z, 0-9, F1-F24, Numpad0-9 and names such as Space, Enter, PageUp, Comma, Period, Slash or Backtick. `None` turns a shortcut off. Restart the game after editing the file.

Shortcuts are only registered while the game window has focus, so they never affect other programs. While the game has focus, the game itself doesn't receive a key used as a shortcut. If you bind one of these keys in the recomp, change the mod's shortcut. A shortcut another program already holds can't be registered; the log says so.

## What it reads

- **Menus:** the title screen, options and settings rows, episode select, records, results, collections (achievements, art, illustrations, interludes and movies), and confirmations. It reads selections, values, descriptions and unlock requirements, and Control Settings including the Custom key editor.
- **Subtitles and tutorials**, with button names for your current controller or key bindings. Interlude illustration passages are read too.
- **Quick-time events:** presses, mashes, holds, stick directions, rotations, finishers and counters. Synchronic (timed) prompts get a four-beep count-in whose high fourth beep marks the moment to press.
- **Status:** Health and Burst on request, low-health warnings, "Burst ready" and the Unlimited gauge once an episode allows it.
- **Rail shooting and melee rapid fire:** a beacon on the selected target cluster. Stereo position means left/right, pitch means up/down, and pulses speed up as your aim closes in. You still aim and fire; holding L1 steers the cursor. Boosted mode also widens the game's own lock-on cone (rail shooting) or homing angle (melee rapid fire). See [shooting modes](docs/SHOOTING_BEACONS.md) and [melee aiming](docs/MELEE_AIM.md).
- **Walking sections:** a steady beacon toward the nearest area that moves the story on (525 Hz ahead, 440 Hz behind, quicker as you approach, silent during fights). See [walking beacon](docs/WALK_BEACON.md).

Image-only text and some conversation and interlude routes aren't read yet.

## Settings

Settings live in `mods/accessibility/accessibility.ini`. Installing creates it from [the defaults](config/accessibility.ini), which explain each setting. Later installs add new settings and keep your values. The mod never rewrites the file, except to save the shooting mode and aim priority you choose in game. Restart the game after editing it.

| Setting | Default | Meaning |
| --- | --- | --- |
| `subtitles`, `tutorials`, `qtes` | 1 | Read subtitles, tutorials and quick-time events |
| `interrupt` | 0 | 1 lets new gameplay text interrupt the screen reader |
| `qte_interrupt` | 1 | Urgent quick-time prompts may interrupt the screen reader |
| `status_warnings`, `low_health_percent` | 1, 25 | Low-health warning and its threshold |
| `shooting_mode` | beacons | `off`, `beacons` or `boosted` |
| `boost_point_blank`, `boost_max_range` | 2, 3 | Boosted lock-on cone, as multiples of the game's limits (50 degrees at point blank, 3.6 degrees at maximum range in Episode 1) |
| `boost_homing` | 3 | Boosted melee homing, as a multiple of the game's 2.6-degree homing angle |
| `aim_priority` | projectiles | Melee rapid fire tracks `projectiles` or `enemies` first |
| `walk_beacon` | 1 | Walking-section beacon |
| `tracking_strength` | 100 | L1 steering speed as a percentage of the game's cursor speed |
| `audio_cue_volume` | 60 | Volume of the mod's beacons and count-in, 0-100 |

## Known issues

- **Keyboard:** lock-on target switching didn't respond to the arrow keys or the mouse in testing. The recomp's mouse control (`mnk_mouse`, on by default) means the arrow keys don't act as the right stick; the Ctrl+arrow shortcuts are the mod's replacement. Other keyboard prompts may still be worded wrongly.
- **Episode 5:** counter prompts for incoming missiles can come too late to react to.
- **Control Settings:** browsing with the right stick needs a nearly full tilt.
- **Recomp setup:** installing the recomp and game data currently needs OCR.
- Holding L1 once stayed stuck on an unknown target.

## Diagnostics

The log and diagnostic dumps stay on your computer under `userdata/accessibility`. Dumps contain game memory and screen text, so look at them before sharing one. Starting the game clears the previous session's log and dumps, so copy anything you want to keep first.

The log records every spoken line (`SPEAK`), plus gameplay markers such as `GAME_TEXT`, `GAME_QTE`, `GAME_STATUS` and `GAMEPLAY_STATS`. When reporting a problem, say what you were doing and what was missing, late, repeated or wrong, and include the log if you can.

## Building from source

```powershell
./bootstrap.ps1
./build.ps1 -Test
./package.ps1
```

`bootstrap.ps1` downloads checksum-pinned LLVM-MinGW, Prism and MinHook. A standalone checkout needs no game files, and CI runs the synthetic tests without game data. `./build.ps1 -Test -NoInstall` skips installing. When the checkout is an installation's `mods/accessibility` folder, the build installs into that installation; close the game first. To install elsewhere, run `./install.ps1 -GameRoot 'D:\Games\AsurasWrath'`.

`./format.ps1` formats the C++ source and tests with the pinned clang-format; `./format.ps1 -Check` only checks. `package.ps1` builds `artifacts/blind-rage-windows-x64.zip` from an explicit file list.

Developer notes: [build profiles](docs/BUILD_PROFILES.md), [event-driven menu reader](docs/HOOK_RESEARCH.md), [text narration](docs/GAMEPLAY_TEXT_READER.md), [input prompts](docs/INPUT_PROMPTS.md), [gameplay systems](docs/GAMEPLAY_SYSTEMS.md), [Synchronic timing](docs/SYNCHRONIC_TIMING.md), [lock tracing](docs/LOCK_TRACE.md) and the [roadmap](docs/ROADMAP.md). Earlier research: [gameplay cues](docs/GAMEPLAY_CUE_RESEARCH.md), [QTE input](docs/QTE_INPUT_RESEARCH.md) and [Episode 1 groundwork](docs/EPISODE_1_GROUNDWORK.md).

Contributions should keep new support in the shared readers and the single narration pipeline, and add a synthetic test for each fixed failure. Never include game or recomp code, extracted assets, saves or memory captures.

## Licensing

Blind Rage's original code is licensed under the [GNU General Public License version 3](LICENSE) or later, with an [additional permission](LICENSE-EXCEPTION.txt) under section 7 for running it with Asura's Wrath and its PC recompilation. Dependencies keep their own terms; Prism is under the Mozilla Public License 2.0. See [third-party notices](THIRD_PARTY_NOTICES.md). No rights to Asura's Wrath or its recompilation are granted here.
