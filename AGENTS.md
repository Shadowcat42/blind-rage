# Blind Rage development

- This repository contains only the accessibility mod. The surrounding directory may be a user's game installation; do not initialize Git above this repository or add surrounding files.
- Never commit or upload game assets, game/recomp/runtime binaries, generated game code, memory snapshots, saves, profiles, local logs, credentials, or downloaded dependencies. Keep captures under ignored `research/`.
- Commit ordinary changes directly on main. Use a separate branch only for work that risks breaking other features, such as large refactors.
- Use explicit file lists for staging and release packaging. Inspect staged files and archive contents before publishing.
- CI must build from a standalone checkout without the game. Synthetic tests run with `build.ps1 -Test`; private snapshot replay is optional and local only.
- Format original C++ source and tests with `./format.ps1`; verify with `./format.ps1 -Check`. Use the pinned clang-format and checked-in style. Exclude dependencies and private research from formatting.
- Preserve shared UI/gameplay readers and one narration pipeline. Support common data/control forms rather than navigation scripts for individual screens or bosses. Gameplay support is a prototype with offline checks; live coverage must be established separately.
- Work independently with the running game through the mod's own commands (for example `BlindRage.exe --command dump`), memory reads, logs and offline tests. Coordinate with the user only when they say they are doing something else on the computer, or when a task needs mouse, keyboard or focus control of the game that no command can do.
- Retain upstream notices when packaging Prism. The original-code license is undecided; do not add a license without the owner's choice.
