# Jetstar environment repair — incremental boundary

Authorized `2026-11-14`: repair the confirmed first blocker and the small
Navigator/environment/permission slice, testing the unchanged cached Jetstar
collector after each independently reviewable change. Stage 4 source extraction
and iframe/Intl/graphics/network subsystems remain separate work.

## Validation levels

1. Focused offline browser/API regressions.
2. Original cached collector, fixed HTML/resources/profile, bounded job/timer
   pumps, recording the first failure and implementation/script hashes.
3. Live same-session server result, only when a compatible project-runtime HTTP
   path exists. The old community-0.1.4 runner cannot establish this result.

Passing load, reaching a later error, and receiving a server success are distinct
outcomes. HTTP 201 alone does not establish `_abck` success. Current collector
probes omit upstream session material and do not freeze time/randomness, so they
prove execution blockers rather than final-output equivalence.

## JET-01: bounded same-document History

Baseline `98f5656` fails at the original collector's zero-based column 503099,
reading `pushState` from an undefined receiver. A separate, explicitly modified
diagnostic copy evaluates the original dynamic key once: `history`; its root is
both `window` and `globalThis`. The original source remains unchanged.

The implementation belongs to `browser/window`, with generation-owned state
and the existing native Web IDL binding paths. The M1 kernel/isolate ownership,
GIL/operation guard, transport and monitoring architecture stay intact.

Approved bounded surface:

- Native `window.history` getter, stable instance, shared History prototype and
  illegal public History constructor; reference-derived descriptors, native
  sources, arities, receiver checks and method constructability.
- `length`, `state`, `scrollRestoration`; fresh load has one entry, null state
  and auto restoration. Invalid restoration values are ignored like the reference.
- `pushState`/`replaceState`: required arguments and string conversion ordering,
  serialized clone isolation, cached current state identity, same-origin HTTP(S)
  URL validation, atomic entry updates and synchronized Location/Document URL.
  The ignored title is converted but does not change document.title. These
  operations do not fire navigation/popstate/hashchange events or issue HTTP.
- `back`/`forward`/`go` expose the native reference shape and use the existing
  offline navigation-intent model. Out-of-range traversals are no-ops; in-range
  traversal and reload are recorded without executing them. Session-history
  traversal, popstate/hashchange delivery and cross-document navigation remain
  unimplemented and explicitly tracked as profile gaps.

Existing bounded URL parsing applies. New URL changes are limited to same-origin
HTTP(S); opaque/file URL rewriting is outside this slice. History cloning uses
V8 serialization; shared-memory cloning is explicitly unavailable. Clone failures expose DataCloneError and URL failures expose
SecurityError through the runtime's existing Error-with-DOM-name representation.
A complete DOMException interface/brand is not added by this slice.

Native History state is released before isolate disposal and reset per Page.load;
independent Pages do not share entries or JS handles. No History API is installed
into bare JSContext.

## Reference and evidence

The installed reference binary is now Chrome `157.0.8103.51`. New History
captures use a separate headless profile and loopback HTTP fixture, with version
and execution conditions recorded. Existing Chrome156 site captures remain
unchanged. Evidence lives under ignored `data/jetstar-native-2026-11-14/` and the
user's existing `iv8-test/reverse-records/请求链路.md`.

## JET-01 local verification (`2026-11-14`)

Native syntax/link/import and standalone source compilation passed. The serializer
Delegate uses a no-RTTI/PCH-free source matching the pinned V8 SDK. Chrome-derived
metadata/descriptors and behavior tests report **30 passed**. Full real-V8 tests
report **2120 passed, 2 skipped, 1 xfailed**; **83 audit regressions** pass.

| Checkpoint | Before History | After History |
|---|---|---|
| Original collector hash | `1d9baa8754ebad40dc7d52940bef7ac6ec17710a26231968fc86c99808f13565` | same, unmodified |
| Page.load | TypeError reading pushState, column 503099 | completes |
| Bounded jobs/timers | not reached | asynchronous XHR NotSupportedError at advance_1000, column 51597 |
| Server success | not tested | not tested; separate live data-send approval pending |

The harness promotes asynchronous errors to the first-failure record, preserving
its explicit test boundary. A test-only JS XHR adapter has offline queue/response
checks, an exact-origin/path allowlist, two total POST maximum, no redirects,
worker wall bounds and no persisted Cookie/sensor values. It is not a product
HTTP implementation and changes XHR method source; any live result using it
must carry that qualification. No live request was made by the blocked command.

Status: native release verified; server validation awaits explicit data-send approval.
No server-pass or final _abck root-cause claim.


## Native release verification (`2026-11-14`)

Verified source `dd67e49e0b183da06086fc71d1c4a96f783d1600`, tree
`060be77ce4668b1e1f6ce5aab084b9d310d6b74e`.

| Platform (Python 3.11–3.14) | Run | Artifact IDs | Per-lane result |
|---|---|---|---|
| linux | `43571858718` | `iv8-linux-manylinux2_28-wheels`: `14104354052` | 2120 passed, 2 skipped, 1 xfailed |
| windows | `43571861832` | `iv8-windows-wheel-cp3.11`: `14103494722`, `iv8-windows-wheel-cp3.12`: `14104358699`, `iv8-windows-wheel-cp3.13`: `14104513241`, `iv8-windows-wheel-cp3.14`: `14103669480` | 2120 passed, 2 skipped, 1 xfailed |
| macos | `43571934345` | `iv8-macos-arm64-wheel-cp3.11`: `14105225568`, `iv8-macos-arm64-wheel-cp3.12`: `14104662497`, `iv8-macos-arm64-wheel-cp3.13`: `14105020965`, `iv8-macos-arm64-wheel-cp3.14`: `14105270055` | 2120 passed, 2 skipped, 1 xfailed |

All 12 hosted lanes pass with pinned V8 15.0.245.19. Required V8 checks
`43571708966` and `43571716293` pass. The macOS helper pins both checkouts
to the same implementation source; restore commit
`a525e69738327d2978ed3a2355ee7eb092a2fdc7` restores original workflow blob
`eb730d3e77a17d4d527e67646475385cebb2428c` under a SHA guard.

The release receipt is ignored `data/jetstar-native-2026-11-14/build-verification.json`.
Later handoff commits change documentation only and retain the verified
implementation/build/test sources.

A same-native-build negative control deletes only window.history in a separate
setup script, retaining the original collector bytes. Load again fails with the
original pushState TypeError at column 503099. Thus the parent object's absence
is causally confirmed for the first local execution failure. It does not prove
the cause of any server-side rejection or _abck state. Evidence is
`history-removed-control/result.json`; live Jetstar requests have not run.
