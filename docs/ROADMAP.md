# Blind Rage roadmap

## Current prototype

- Injected x64 mod with Prism speech, NVDA preference, and speech-engine fallback.
- Shared Scaleform menu, settings-row, collection, and dialog decoding.
- Verified Mark2 disc and title-update profiles using one shared reader.
- Preliminary subtitle/tutorial delivery observer on `experiment/gameplay-text-reader`; live gameplay validation remains pending.
- First Episode 1 test prototype: effective prompt labels, Health/Burst status, grouped QTEs, native Synchronic window and optional wider shooting acquisition. Offline checks are complete; live calibration and coverage remain to be established. See `EPISODE_1_GROUNDWORK.md` and `EPISODE_1_TESTING.md`.
- Synthetic tests and optional local snapshot regressions; no game data in the repo.

## Next work

- Continue validating initial selections, menu entry/return transitions, tooltips, confirmations, and custom controls.
- Preserve the validated event-driven menu reader while extending gameplay narration.
- Calibrate gameplay speech/timing with the user, then expand combat state, target/direction feedback and actionable audio cues. Test each capability with blind players before claiming coverage.
- Add versioned adapters and fingerprints for additional recomp versions.

Full gameplay accessibility remains a project goal; the text reader is a preliminary implementation. Keep source, builds, and releases independent of game/recomp distribution.
