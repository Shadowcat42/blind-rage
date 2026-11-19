# Third-party components

Blind Rage's original-code license is not yet selected. No rights to the game or recompilation are granted by this repository.

## Prism v0.18.3

The build downloads the unmodified Windows x64 Prism SDK from the official release and verifies its SHA-256 digest. Source checkouts do not contain Prism binaries or headers. Mod packages include the unmodified `prism.dll`, upstream NOTICE, and dependency licenses.

- Project and matching source: https://github.com/ethindp/prism/tree/v0.18.3
- Release: https://github.com/ethindp/prism/releases/tag/v0.18.3
- License: Mozilla Public License 2.0. Prism and its covered files remain under their upstream terms, irrespective of the mod's original-code license.

See `mods/accessibility/dist/licenses` in an installed package for the exact upstream notices. Prism's NOTICE describes its dependencies and its permission to distribute NVDA RPC definitions under MPL-2.0.

## LLVM-MinGW

The build downloads the official LLVM-MinGW 20261117 toolchain, with a pinned SHA-256 digest. Build tools are not part of the source repository or mod package. The toolchain's LLVM license notice is included with packaged binaries.

- Project: https://github.com/mstorsjo/llvm-mingw
- Release: https://github.com/mstorsjo/llvm-mingw/releases/tag/20261117

## External game and recompilation

Users must obtain the supported recomp separately and provide their own game data. Neither is downloaded by Blind Rage's bootstrap script, included in this repository, or included in mod packages.

The mod uses runtime interfaces and reads guest memory. Original game code, extracted assets, runtime binaries, generated recompilation code, private memory snapshots, and saves are excluded.
