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

These addresses are build-specific observations, not portable APIs. No game instructions, generated source, binaries, or captured memory are included here. Offline research tools and captures remain ignored.

The executable's static registration records associate each guest address with its native function. Inspection of those native functions confirms `PPCContext` offsets: first argument r3 at `0`, r4 at `0x20`, r5 at `0x28`, r6 at `0x30`, and LR at `0x100`. This agrees with the observed argument flow in both assignment functions. The source adapter records these offsets rather than importing an unverified SDK context definition.

## Implemented foundation

- Pinned MinHook v1.3.4 download, build, and packaged license notices.
- A native detour owner that restores only its own targets. Synthetic tests verify direct calls, indirect calls, original-function calls, disable, reenable, and removal.
- Allocation-free shared ActionScript assignment observations: filter narrated semantic fields, look up the existing hash member, and compare normalized typed values after assignment. Synthetic tests cover integer cursors, double values, unchanged values, padding, irrelevant animation counters, and cyclic hash chains.
- `build.ps1 -Test -NoInstall` builds and runs offline tests without invoking the installer. This option still writes build outputs under `dist`; use it only when those outputs are not loaded by the game.

The running DLL does not yet use the new foundation. Full event-source integration and live validation remain required; no performance or coverage claim follows from these unit tests alone.

## Required before replacing polling

- Resolve guest addresses to exact host entry points in this installed build; verify the actual runtime register ABI instead of assuming the current upstream SDK matches it.
- Verify interception of both direct and indirect calls, preserving original behavior and register state.
- Cover member assignment, raw assignment, text/visibility updates, movie creation/open/close, and startup state transitions.
- Coalesce actual mutations and read only at a stable boundary. An unconditional per-frame reader would still be polling.
- Keep Prism calls on the speech worker. Replace periodic hotkey/focus checks with Windows notifications and event waits.
- Retain the shared reader and all current narration semantics, including choices on dialog entry, settings values, tooltips, collections totals, rediscovery, and focus return.
- Validate with synthetic tests, captured-state replay, and user-driven live navigation. Offline disassembly alone does not prove event coverage.
