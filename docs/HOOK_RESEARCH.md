# Event-driven reader investigation

Branch: `experiment/event-driven-reader`. Implementation and live coverage are not yet complete.

## Verified offline observations

The fingerprinted Mark2 disc executable contains recompiled Scaleform and Unreal UI functions. Runtime exports expose function lookup and replacement, but replacement of a dispatch-table entry alone cannot intercept direct native calls between recompiled functions. A host-function detour must be evaluated for complete interception.

Captured menu clips share ActionScript object primary vtable `0x820285ac` and a secondary interface at object offset `0x10`, vtable `0x82028558`. The secondary interface's slot `0x0c` reaches `0x82a6e640`, which invokes display-specific handling and then the common assignment implementation `0x82a4a7f0`. The latter resolves a named member in a hash table, handles dynamic properties and read-only flags, and copies the assigned ActionScript value into the resolved member. This is a concrete mutation-hook candidate, rather than a movie-frame timer.

Other observed entry points:

| Guest address | Evidence / candidate role |
| --- | --- |
| `0x828835e8` | Reflected GFxMoviePlayer Advance wrapper; calls `0x8289bd50` (host RVA `0x02a5bee0`) |
| `0x82884988` | Reflected SetVariableString wrapper; virtual dispatch reaches `0x8289aea8` in captured movie objects |
| `0x82886e18` | Reflected GFxObject SetText wrapper; calls `0x8289d868` |
| `0x82a4a7f0` | Common named-member assignment; host RVA `0x029b5050` |
| `0x82a6e5f8` | Secondary ActionScript interface slot `0x28`; calls display-specific handling then raw assignment `0x82a4ac48` (host RVA `0x045f72b0`) |
| `0x82898fe8` | Movie Start virtual method; host RVA `0x01a647d0` |
| `0x82899528` | Movie Close implementation; host RVA `0x0212c6c0` |
| `0x82acee78` | Native text field's shared text-update implementation; host RVA `0x03b841c0` |
| `0x82a36778` | Recomputes display flag bit 2 from the display transform, which the existing reader uses to exclude hidden panels; host RVA `0x02b146b0` |
| `0x8224ddc0` | Generic Unreal script assignment handler, VM table opcode `0x0f`; host RVA `0x0040ce60`. Observes the known title status byte only when the title movie is the assignment target. |
| `0x82a86f50` | Scaleform movie root update, vtable `0x82028c10` slot `0x94`; host RVA `0x008071c0`. Flushes only the display objects changed since the previous boundary. |
| `0x82a3f1a8` | Native visibility setter, flag bit 0; host RVA `0x0164bd00` |
| `0x82ae30b8` | Sprite visibility override, flag bit 0; host RVA `0x01ce3ce0` |

These addresses are build-specific observations, not portable APIs. No game instructions, generated source, binaries, or captured memory are included here. Offline research tools and captures remain ignored.

The executable's static registration records associate each guest address with its native function. Inspection of those native functions confirms `PPCContext` offsets: first argument r3 at `0`, r4 at `0x20`, r5 at `0x28`, r6 at `0x30`, and LR at `0x100`. This agrees with the observed argument flow in both assignment functions. The source adapter records these offsets rather than importing an unverified SDK context definition.

## Implemented foundation

- Pinned MinHook v1.3.4 download, build, and packaged license notices.
- A native detour owner that restores only its own targets. Synthetic tests verify direct calls, indirect calls, original-function calls, disable, reenable, and removal.
- Allocation-free shared ActionScript assignment observations: filter narrated semantic fields, look up the existing hash member, and compare normalized typed values after assignment. Synthetic tests cover integer cursors, double values, unchanged values, padding, irrelevant animation counters, and cyclic hash chains.
- `build.ps1 -Test -NoInstall` builds and runs offline tests without invoking the installer. This option still writes build outputs under `dist`; use it only when those outputs are not loaded by the game.

The experimental DLL now uses ten shared native hooks. The movie-update callback only flushes queued member and display mutations; it does not run the reader or scan the UI every frame. Values or flags changed and restored before the boundary produce no event. Repeated normal/raw writes capture the first value once. The speech worker uses Windows event/message waits, foreground notifications, and registered shortcuts. Relevant mutations request a read after a short coalescing delay, followed by bounded confirmation checks. Full discovery follows movie lifecycle changes, initial capture, foreground return, and explicit diagnostics; cursor/value changes reuse validated movie handles. A menu clearing is delayed to avoid treating transient panel construction as a final empty screen.

Hook storage and trampolines have process lifetime. Shutdown stops capture and speech, retaining the pass-through hooks because an already-running callback may still be using its trampoline. Reinitialization reuses them. The injected DLL must remain loaded until process exit.

Synthetic end-to-end tests exercise each hook's notification path, unchanged values/text, irrelevant display bits, temporary flag toggles, title status changes, original return-register behavior, and capture shutdown. An isolated mapping of the actual executable also accepted creation, enable, disable, and removal of all ten detours; no game functions were executed for that check.

The first live pass confirmed Press Start, DLC status, title navigation, dialog prompts/choices, Extras entry, and Gauges selections. It exposed excessive intermediate display events, a choice selector narrated before its dialog opened, and silent Extras returns. Corrections now coalesce display events and honor explicit visibility (bit 0) in addition to the degenerate transform flag (bit 1). Closed dialog choice selectors remain pending until their prompt opens. These corrections pass synthetic and captured-state regressions and require another live pass. Overall coverage and idle overhead are not yet proven.

The second pass confirmed load-success messages, Difficulty, Sound values, and several other Options transitions. It showed that assignment notifications, rather than display notifications, dominated the Extras traffic: normal and raw setters repeatedly touch members whose settled values are unchanged. Those writes now coalesce at the same movie-update boundary. Two user-confirmed captures distinguish an open Gauges panel from its retained data after returning to Extras: the collection panel's `m_input` switches from true to false. Collection decoding now honors that shared focus marker, matching the existing settings-panel policy. The return capture selects GAUGES in the six-item parent list; the active capture selects Avenger with its effect and one acquired total. These fixes pass offline replay and await live validation.

The third pass confirmed the Gauges-to-Extras return with both user feedback and emitted `SPEAK [NVDA]` records. Startup cleanup removed 19 old diagnostic files without errors. Assignment notifications fell sharply after coalescing. Idle activity still needs a controlled measurement on a confirmed screen; genuine background mutations may continue to request reads.

User testing identified silent Achievements, CG Art, Illustrations, and Movies. Their active panels use `m_info.m_items` plus an `m_instList` control with `m_cursor`, `m_pageNow`, and `m_pageItemMax`, instead of the explicit `m_param` record used by Gauges. One shared record resolver now supports both layouts. All four active captures decode; an additional Achievements capture verifies cursor movement within a page. Page offset, integer cursors, partial pages, and invalid coordinates have synthetic regressions. Page-field changes are included in mutation notifications. Selected-record identity is part of deduplication, and grid positions distinguish otherwise identical locked entries. This extension still requires a live pass.

A subsequent live tab-switch capture corrected the initial page-relative assumption: the movie list uses an absolute `m_cursor` (15 on page index 1 with 15 items per page). The resolver now uses that absolute index and validates it against the current page, waiting out incoherent transition snapshots. A second-tab capture and synthetic regression cover the exact case. The earlier twelve-Right Achievements capture remained on page zero and did not establish cross-page semantics.

During a user-confirmed idle Gauges check, counter samples at ticks 38246703 and 38272250 recorded 15 reader updates over 25.547 seconds (0.59 per second), compared with about 766 updates for the former 30 Hz loop. There were no new speech announcements or periodic registry refreshes in that interval. Seventeen coalesced property-change notifications occurred; the game still mutates some watched state while idle. These figures measure worker reader updates. Total game CPU cost has not been benchmarked.

The New Game difficulty-to-confirmation transition briefly exposed a dormant title selector. A temporary external read-only watcher confirmed that the title controller retained `m_gameDiffHelp=true` and `m_input=false` across the gap. Ordinary `m_input` flags alone cannot determine ownership on every controller, so the adapter uses that explicit title prompt phase to suppress its background selector and text fallback. Dialog selectors retain their own lifecycle. A changed question also starts a new narration context when the game reuses a dialog object, preserving the prompt independently of gap duration. Synthetic tests and a captured handoff simulation cover the suppression without adding a general return delay. The temporary watcher has been stopped.

## Required before replacing polling

- Resolve guest addresses to exact host entry points in this installed build; verify the actual runtime register ABI instead of assuming the current upstream SDK matches it.
- Verify interception of both direct and indirect calls, preserving original behavior and register state.
- Cover member assignment, raw assignment, text/visibility updates, movie creation/open/close, and startup state transitions.
- Coalesce actual mutations and read only at a stable boundary. An unconditional per-frame reader would still be polling.
- Keep Prism calls on the speech worker. Replace periodic hotkey/focus checks with Windows notifications and event waits.
- Retain the shared reader and all current narration semantics, including choices on dialog entry, settings values, tooltips, collections totals, rediscovery, and focus return.
- Validate with synthetic tests, captured-state replay, and user-driven live navigation. Offline disassembly alone does not prove event coverage.
