# Preliminary subtitle and tutorial reader

Branch: `experiment/gameplay-text-reader`. This text reader is part of the first Episode 1 prototype, with offline validation. Live testing is deferred at the user's request; it is not an acceptance gate for the theoretical groundwork. Full gameplay accessibility is not claimed. Gameplay implementation and test instructions are in `EPISODE_1_GROUNDWORK.md` and `EPISODE_1_TESTING.md`.

## Delivery and narration

Two native ActionScript delivery hooks observe `GFxObject.ActionScriptVoid` and `GFxMoviePlayer.ActionScriptVoid`. Both are fingerprinted per build. Their complete normalized native functions match across the installed disc/updated executables (134 and 170 instructions respectively); isolated mappings accept both detours without executing game functions.

Native inspection of the common argument marshaller verifies FFrame offsets: caller UFunction at `+0x10`, caller object at `+0x14`, bytecode pointer at `+0x18`, and locals at `+0x1c`. The marshaller uses caller property offsets to read those locals. The observer filters the exact shared presentation owner/function names; it does not hook every script execution or scan gameplay objects every frame.

- `BCGFxMovieText.CallStartText` supplies resolved tag, message, and text type. `CallEnd`, `CallEndType`, and `CallClear` invalidate active narration records. Text is captured after the game's localization and subtitle-display checks. This also includes non-subtitle text styles delivered through that same movie.
- `BCGFxUI_Advertise.CallCreate` supplies localized instructions after action-button remapping. Lines remain staged until `CallStart`. The movie's ActionScript `Start` displays the staged lines and then empties its queue (`SetupClear`), so each start announces only the lines created since the previous one; Episode 3's mission tutorial depends on this, since its earlier steps are never cleared by `CallClear` (UnrealScript calls it only when information display is turned off). A start without new lines stays quiet. `CallEnd` and `CallClear` end the lifecycle. Interlude illustration passages follow `interrupt` as well. Literal button glyphs such as `<A>` and `<Y>` become spoken button names before markup cleanup.
- `CallSetBox` only selects a gauge picture (`gauge`, `health`, `burst` or `unlimited`; `BCGFxUI_Advertise.AddAdvertise` set types 8-11) that the movie's `Start` shows beside the step. The step's text still arrives through `CallCreate`, and Episode 3's Burst and Unlimited steps create it before `CallSetBox`, so those created lines are spoken with their button names. Only a box step with no created line falls back to the movie's visible MainMC text, where buttons are drawn as images and cannot be read.

The shared label adapter now translates those button glyphs according to the recomp's effective Xbox, PlayStation or keyboard prompt display. Keyboard names come from the runtime's own binding-aware prompt export. See `INPUT_PROMPTS.md` for the verified interface and remaining live checks.

Hooks copy bounded UTF-16 data before the original call can release or alter script locals, then publish after the original returns. Strings require consistent counts/capacities and termination; invalid Unicode does not become partial speech. No speech occurs on the game thread. The bounded queue holds 64 events and reports overflow; the worker discards an overflowed batch and stale events rather than narrating incomplete lifecycle state.

The worker deduplicates active tag/text identities, coalesces replacements, and removes dismissed events before handing them to Prism. Configuration-neutral button tokens survive markup cleanup and resolve at narration/repeat time. Identical text can be announced again after its prior lifecycle ends. Movie index validation prevents retained text from surviving object-slot reuse or a detected movie closure. Existing menu decoding and its event source remain separate from these content events, sharing the same speech worker and backend.

## Preferences and speech behavior

Both subtitles and tutorials default to enabled, as requested. Existing INI files are preserved; absent gameplay settings use these defaults. The optional settings are:

```ini
[gameplay]
subtitles=1
tutorials=1
interrupt=0
```

`subtitles` covers the MovieText delivery route, including its other screen-text styles. `tutorials` covers Advertise. `interrupt=0` queues gameplay announcements without intentionally cancelling unrelated screen-reader speech. Changing it to 1 permits interruption when a new gameplay announcement is spoken. Cue dismissal never issues a global speech-stop call. The mute command still stops current speech.

Repeat reads the current menu, or the latest active gameplay text when no menu is active. Focus/mute rules apply to gameplay speech. Accepted gameplay announcements are logged as `GAME_TEXT`, and successful backend calls retain the existing `SPEAK` logging.

## Offline checks and known limits

Synthetic tests exercise copied local ownership, UTF-16 and malformed-string handling, subtitle replacement/dismissal/reentry, prepared versus displayed tutorials, glyph preservation, visible-box deduplication, movie identity reuse, preferences, bounded overflow, native hook interception, original return-register behavior, metadata-cache invalidation, capture shutdown, and noninterrupting speech requests. The native adapter test reserves synthetic guest address space and commits only fixture pages; no game files or real speech are used.

Live text delivery, visibility, event order, latency, localization, and control-remap behavior still need a user-led gameplay session. No game input or focus changes were used to prepare this implementation. Start the mod before entering gameplay: it does not reconstruct an already-running subtitle from history.

Image-only text is not decoded. Icon-only QTEs use separate typed state/presentation readers described in `EPISODE_1_GROUNDWORK.md`. Interlude illustration passages now use the visible text container and script-owned scene lifecycle through the shared menu readout path; embedded dialogs keep priority. Preloaded `AddText` calls are not narration events. TalkDemo conversation routes remain research candidates. The box fallback depends on native visible text and cannot recover words baked into textures or unsupported glyphs.

Once noninterrupting speech has been handed to an external backend, the mod cannot selectively retract that utterance without risking cancellation of unrelated speech. Rapid dialogue can therefore build a backend speech backlog. Pending mod events are bounded and invalidated, but this prototype does not claim to solve backend queue ownership or every pause/skip timing case.

QTE findings, including misleading rotation enum names and request-based simultaneous grouping, are documented in `QTE_INPUT_RESEARCH.md`; their implementation is tracked in `EPISODE_1_GROUNDWORK.md`.
