# BOM-01 local acceptance report

Date: `2026-11-17`. **Implemented and locally accepted; later Linux/Windows
hosted and macOS local fallback validation passed. Hosted Mac did not execute.** Boundary: `docs/bom_01_capabilities.md`.
The user prioritizes BOM completion and defers further Symbol.iterator monitoring.

## Delivered behavior

- Native `Navigator.prototype.getGamepads` returns a fresh mutable eight-null
  snapshot matching Chrome's captured no-device profile. Array construction does
  not execute inherited index setters.
- Stable native `navigator.scheduling` exposes `Scheduling.isInputPending`.
  Its optional dictionary reads includeContinuous once, preserves getter/Proxy
  failures and returns false without a trusted input queue. Synthetic events and
  pending JavaScript tasks do not constitute trusted input.
- NetworkInformation, ScreenOrientation and VisualViewport use eager shared
  native interface prototypes and EventTarget inheritance. Methods share the
  actual EventTarget.prototype function identities; handler accessors normalize
  non-functions to null. Connection/orientation have onchange; viewport adds
  onscrollend to onresize/onscroll. Explicit synthetic dispatch supports the
  current function-listener, once/passive, cancellation and cleanup behavior.
- `screen.orientation.lock` converts its required enum and returns a rejected
  Promise for unsupported operation, invalid arguments, conversion exceptions
  and invalid receivers. Original user exceptions retain identity. Unlock
  returns undefined; neither operation changes configured screen metadata.
- Connection/orientation wrappers now retain identity within a generation.
  Nested listeners, handlers and cached wrappers are released before isolate
  disposal and reset on reload. Existing structured tracing and call-only watch
  channels cover the selected entries; callback/conversion reentry stays visible.

## Browser evidence and presence delta

`test/fixtures/bom_01/` records fresh-profile **Google Chrome 157.0.8103.79**,
macOS arm64, HTTP loopback secure context, flags, exact UTC time, served request
paths and probe hash. No business page or external device service is used.
Offline tests compare native source/name/arity/constructability, constructor and
prototype ownership, descriptors/key order, identities and completion channels.

The fixed Chrome157 presence inventory is unchanged; the new membership delta
is `docs/bom_01_api_delta.json`:

| Target | Before | After | Still missing |
|---|---:|---:|---|
| Navigator | 30/70 | 32/70 | 38 names |
| navigator.connection | 4/9 | 8/9 | when |
| screen.orientation | 2/9 | 8/9 | when |
| visualViewport | 12/14 | 13/14 | when |

Four interface globals are added: Scheduling, NetworkInformation,
ScreenOrientation and VisualViewport. Name presence does not certify a device,
network or rendering backend. Location/History behavior remains protected by
the combined regression gate.

## Verification

A newly rebuilt **macOS arm64 / CPython 3.11.18** wheel links the unchanged pinned
**V8 15.0.245.19**. It is installed in a new isolated environment; tests execute
outside the source tree. Import origin, linked version, installed Python source,
wheel ABI tag and ZIP CRC are checked.

- New offline tests: **94 passed** in two modules.
- Updated combined release-focused gate: **34 modules / 917 passed**.
- Full suite: **3143 passed, 2 skipped**.
- Independent installed-wheel probes verify constructor GET monitoring, forged
  Event/Proxy rejection with zero extra getters/traps, lock conversion identity
  and Promise channels, and cleanup across 20 reloads with idempotent disposal.

Initial local acceptance used an uncommitted change set based on source
`889cbaacda107bc895d6d3aca20d64c555dd0305` (base tree
`e05edfa69529da22add74a90dc91949ee8b2da27`), subsequently committed as
`6839c9ab4fec026cedf24b4de351c81c6e7d92e6`. Exact local candidate source-file hashes,
tracked-diff hash, native/wheel hashes and test metadata are recorded in
`data/bom-01-2026-11-17/source-receipt.json`; build/install/focused/gate/full logs
are in the same ignored directory. Local validation ID:
`bom-01-2026-11-17/local-macos-cp311`. This initial local validation has no hosted run/artifact ID. Subsequent
release run/artifact identities are recorded in `docs/bom_01_release_report.md`.
All three release gate definitions include both new test modules.

Wheel SHA-256:
`310befdec7f717c53a823321cf4866d8feb5380f583a72f9ed8e445794f69379`.
Native SHA-256:
`f529817f625558dc46b0ee59f4efb01fe67c8d883d6edf29b4f1f738f2f45a8f`.

## Remaining boundaries

No gamepad/device polling, trusted input, automatic network/orientation/viewport
events, orientation control or layout is implemented. The three when methods
depend on Observable and remain absent. Other Navigator device/service families
remain in the BOM queue. Symbol.iterator monitoring is deferred, not complete.

The shared EventTarget's function-listener and handler ordering limits remain;
listener objects and full browser dispatch fidelity need their own slice.
NotSupportedError retains the existing named-Error model rather than complete
DOMException branding. Ordinary event source locations/stacks, Promise
settlement and broader API monitoring gaps remain separate.

The subsequent user request authorizes submitting this batch to PR #32 and
V8-linked release builds. All 12 lanes subsequently pass (eight hosted, four
local Mac fallback); the failed hosted Mac attempt is not marked green. Source,
runs and artifacts are recorded separately in `docs/bom_01_release_report.md`. No version publication
or shared SDK replacement is included; shared deployment remains BIND-01.
