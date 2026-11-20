# Blind Rage roadmap

## Current prototype

- Injected x64 mod with Prism speech, NVDA preference, and speech-engine fallback.
- Shared Scaleform menu, settings-row, collection, and dialog decoding.
- Compatibility restricted to one verified Mark2 disc build.
- Synthetic tests and optional local snapshot regressions; no game data in the repo.

## Next work

- Continue validating initial selections, menu entry/return transitions, tooltips, confirmations, and custom controls.
- Validate the experimental shared event hooks against every currently supported menu, dialog, transition, and focus-return path, including idle overhead.
- Expand into gameplay: investigate QTE prompts/timing, combat state, target/direction feedback, and actionable audio cues. Test each capability with blind players before claiming coverage.
- Add versioned adapters and fingerprints for additional recomp versions.

Gameplay support is a project goal, not a current feature. Keep source, builds, and releases independent of game/recomp distribution.
