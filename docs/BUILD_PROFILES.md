# Disc and title-update profiles

Both profiles target the installed Mark2 release `2026.11.01.1627`. `src/build_profile.h` holds fingerprints, native hook RVAs, and the reader's build-specific constants. Menu decoding, narration, event coalescing, and Prism output remain shared.

## Selection

- Automatic attachment identifies the running executable within this installation's `bin` or `bin_disc` directory and verifies its complete fingerprint set.
- With no game running, automatic launch follows `title_update=1` or `0` in `userdata/install.txt`. If that file is absent, patch presence supplies the default. An existing but ambiguous installer state requires an explicit build choice.
- `--build updated` and `--build disc` override automatic launch selection. A different build already running must be closed first; `--attach PID` identifies its actual profile.
- `--check` verifies the selected files without launching or injecting. It can check either profile while a game is running.
- The title-update profile additionally verifies the exact `game/default.xexp` SHA-256. No game or patch files are distributed by the mod.

## Verification evidence

The local patch is an XEX delta with source version word `0x00000005` and target version word `0x00000105`. It retains the original image range and applies compressed changes. The descriptor supplies binary patch metadata, rather than source-level intent.

All ten existing hooks have matched counterparts in the updated executable's guest-to-host registration table. Full normalized native function comparisons preserve the relevant register ABI and member offsets; differences include relocated calls, guest instruction addresses, and their compiler-derived constants. Each entry point accepted detour creation, enabling, disabling, and removal in isolated executable mappings without executing game code.

| Hook | Disc guest | Updated guest | Updated native RVA |
| --- | --- | --- | --- |
| Named member assignment | `0x82a4a7f0` | `0x82a4a800` | `0x02438e80` |
| Raw member assignment | `0x82a4ac48` | `0x82a4ac58` | `0x03c91080` |
| Movie start | `0x82898fe8` | same | `0x031cfaa0` |
| Movie close | `0x82899528` | same | `0x02073600` |
| Native text update | `0x82acee78` | `0x82acee88` | `0x025454c0` |
| Transform visibility flags | `0x82a36778` | `0x82a36788` | `0x021d4b90` |
| Script assignment | `0x8224ddc0` | same | `0x03ba5f10` |
| Movie update boundary | `0x82a86f50` | `0x82a86f60` | `0x01c13090` |
| Visibility setter | `0x82a3f1a8` | `0x82a3f1b8` | `0x041a81b0` |
| Sprite visibility setter | `0x82ae30b8` | `0x82ae3180` | `0x0137b5b0` |

Updated live memory confirmed the same reflection globals (`GNames=0x832a9c58`, `GObjects=0x832a9c98`), text-field vtable (`0x8202bcb0`), and reflected `CurrentSystemProcess` offset (`0x1f1`). The Sprite vtable changed from `0x82028718` to `0x82028710`. A title-screen capture replays correctly with the updated layout and the existing disc capture still replays with its original profile.

The combined mod attached to the already-running updated build and logged `BUILD profile=updated`, successful NVDA initialization, title navigation, Options, Sound, Brightness, Extras, movie-tab navigation, and return to the main Extras menu. The user then closed and relaunched through Blind Rage successfully. Runtime and renderer fingerprints are verified separately for each build; shared exported accessors were checked in the updated runtime.

Startup speech exposed a populated title selector between the load-success message and the DLC-check message. The reader now gates ordinary title selections on native `bEnableInput` (`+0x208`, mask `0x40000000`) while preserving modal messages and choices. Reflection confirms the layout and saved script inspection confirms input is disabled during `OnLoadGame`. Replay tests exercise disabled/reenabled title input in both profiles and preserve modal choices. A fresh live updated-build launch (session 1856) verified Press Start, load success, DLC check, and then the title menu, with no premature title announcement. Subsequent Extras/title/Options navigation also emitted speech.

Synthetic profile tests cover exact fingerprint selection, rejection of mixed/modified binaries, installer-state parsing, explicit profile names, and distinct layouts. All private captures and inspection tooling remain ignored.
