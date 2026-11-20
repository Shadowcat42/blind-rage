# Preliminary subtitle and tutorial reader

Branch: `experiment/gameplay-text-reader`. This is a preliminary implementation with offline validation. Live testing is deferred at the user's request; it is not an acceptance gate for this first implementation. Full gameplay accessibility is not claimed.

## Delivery and narration

Two native ActionScript delivery hooks observe `GFxObject.ActionScriptVoid` and `GFxMoviePlayer.ActionScriptVoid`. Both are fingerprinted per build. Their complete normalized native functions match across the installed disc/updated executables (134 and 170 instructions respectively); isolated mappings accept both detours without executing game functions.

Native inspection of the common argument marshaller verifies FFrame offsets: caller UFunction at `+0x10`, caller object at `+0x14`, bytecode pointer at `+0x18`, and locals at `+0x1c`. The marshaller uses caller property offsets to read those locals. The observer filters the exact shared presentation owner/function names; it does not hook every script execution or scan gameplay objects every frame.

- `BCGFxMovieText.CallStartText` supplies resolved tag, message, and text type. `CallEnd`, `CallEndType`, and `CallClear` invalidate active narration records. Text is captured after the game's localization and subtitle-display checks. This also includes non-subtitle text styles delivered through that same movie.
- `BCGFxUI_Advertise.CallCreate` supplies localized instructions after action-button remapping. Lines remain staged until `CallStart`; `CallEnd` and `CallClear` end their lifecycle. Literal button glyphs such as `<A>` and `<Y>` become spoken button names before markup cleanup.
- `CallSetBox` marks tutorials whose wording is stored inside the movie. After actual UI events, the worker reads the movie's visible MainMC tree through the existing reader. This path is provisional until a real box tutorial is captured.

Hooks copy bounded UTF-16 data before the original call can release or alter script locals, then publish after the original returns. Strings require consistent counts/capacities and termination; invalid Unicode does not become partial speech. No speech occurs on the game thread. The bounded queue holds 64 events and reports overflow; the worker discards an overflowed batch and stale events rather than narrating incomplete lifecycle state.

The worker deduplicates active tag/text identities, coalesces replacements, and removes dismissed events before handing them to Prism. Identical text can be announced again after its prior lifecycle ends. Movie index validation prevents retained text from surviving object-slot reuse or a detected movie closure. Existing menu decoding and its event source remain separate from these content events, sharing the same speech worker and backend.

## Preferences and speech behavior

Both subtitles and tutorials default to enabled, as requested. Existing INI files are preserved; absent gameplay settings use these defaults. The optional settings are:

```ini
[gameplay]
subtitles=1
tutorials=1
interrupt=0
```

`subtitles` covers the MovieText delivery route, including its other screen-text styles. `tutorials` covers Advertise. `interrupt=0` queues gameplay announcements without intentionally cancelling unrelated screen-reader speech. Changing it to 1 permits interruption when a new gameplay announcement is spoken. Cue dismissal never issues a global speech-stop call. Explicit stop/mute/shutdown commands retain their prior behavior.

Repeat reads the current menu, or the latest active gameplay text when no menu is active. Focus/mute rules apply to gameplay speech. Accepted gameplay announcements are logged as `GAME_TEXT`, and successful backend calls retain the existing `SPEAK` logging.

## Offline checks and known limits

Synthetic tests exercise copied local ownership, UTF-16 and malformed-string handling, subtitle replacement/dismissal/reentry, prepared versus displayed tutorials, glyph preservation, visible-box deduplication, movie identity reuse, preferences, bounded overflow, native hook interception, original return-register behavior, metadata-cache invalidation, capture shutdown, and noninterrupting speech requests. The native adapter test reserves synthetic guest address space and commits only fixture pages; no game files or real speech are used.

Live text delivery, visibility, event order, latency, localization, and control-remap behavior still need a user-led gameplay session. No game input or focus changes were used to prepare this implementation. Start the mod before entering gameplay: it does not reconstruct an already-running subtitle from history.

Image-only text and icon-only QTE prompts are not decoded. Separate TalkDemo/IntervalDrama conversation routes remain research candidates; their `AddText` calls are not assumed to mean immediate display, which could otherwise announce preloaded dialogue. The box fallback depends on native visible text and cannot recover words baked into textures or unsupported glyphs.

Once noninterrupting speech has been handed to an external backend, the mod cannot selectively retract that utterance without risking cancellation of unrelated speech. Rapid dialogue can therefore build a backend speech backlog. Pending mod events are bounded and invalidated, but this prototype does not claim to solve backend queue ownership or every pause/skip timing case.

QTE findings, including misleading rotation enum names and request-based simultaneous grouping, are documented in `QTE_INPUT_RESEARCH.md`; no QTE narration is implemented here.
