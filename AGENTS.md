# Blind Rage development

- This repository contains only the accessibility mod. The surrounding directory may be a user's game installation; do not initialize Git above this repository or add surrounding files.
- Never commit or upload game assets, game/recomp/runtime binaries, generated game code, memory snapshots, saves, profiles, local logs, credentials, or downloaded dependencies. Keep captures under ignored `research/`.
- Use explicit file lists for staging and release packaging. Inspect staged files and archive contents before publishing.
- CI must build from a standalone checkout without the game. Synthetic tests run with `build.ps1 -Test`; private snapshot replay is optional and local only.
- Format original C++ source and tests with `./format.ps1`; verify with `./format.ps1 -Check`. Use the pinned clang-format and checked-in style. Exclude dependencies and private research from formatting.
- Preserve shared UI/gameplay readers and one narration pipeline. Support common data/control forms rather than navigation scripts for individual screens or bosses. Gameplay support is a prototype with offline checks; live coverage must be established separately.
- The user may be using the computer. Coordinate before controlling or focusing the game. Read-only inspection and offline tests can continue independently.
- Retain upstream notices when packaging Prism. The original-code license is undecided; do not add a license without the owner's choice.
