# M11-35 — BOM metadata completion (window / screen / navigator)

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-30`)**

M11-35 resumes the BOM direction after the selector work: it completes the
deterministic window/screen/navigator metadata surface that page scripts commonly
feature-detect or fingerprint. It adds no navigation, networking, rendering,
dialog UI, or new Python API.

## Window

- **`window.name`** — mutable string, initially `""`. It is Page-level state:
  it survives `load()` generations within one `Page` and resets for a new Page.
  Value coercion follows JS `ToString`; `window.name = 123` reads `"123"`.
- **`window.length`** — read-only `0` (this runtime has no child browsing
  contexts).
- **`window.screenLeft` / `window.screenTop`** — read-only aliases of the
  configured `screenX` / `screenY` values.
- **`window.isSecureContext`** — read-only, derived from the load's base URL:
  `https://` / `wss://` / `file://` are secure; `http://` / `ws://` are secure
  only for loopback hosts; other schemes are insecure. It is recomputed on each
  `load()`.

## Screen

- **`screen.availLeft` / `screen.availTop`** — read-only integers, configured
  independently from width/height. New environment keys: `screenAvailLeft` /
  `screenAvailTop` (flat) and `availLeft` / `availTop` under `screen` (nested).
- **`screen.orientation`** — a fresh read-only object:
  - if `screen.width >= screen.height`: `type = "landscape-primary"`,
    `angle = 0`;
  - otherwise: `type = "portrait-primary"`, `angle = 90`.
  There is no `lock()` / `unlock()`, no change events, and no real display
  integration.

## Navigator

- **`navigator.maxTouchPoints`** — read-only int (default `0`).
- **`navigator.pdfViewerEnabled`** — read-only bool (default `false`; this
  runtime has no PDF viewer).
- **`navigator.doNotTrack`** — read-only string (default `"0"`).
- **`navigator.connection`** — a fresh read-only object with
  `effectiveType` (string, default `"4g"`), `rtt` (int, default `50`),
  `downlink` (float, default `10.0`), and `saveData` (bool, default `false`).
  It is static metadata; there are no NetworkInformation events.
- New flat environment keys: `maxTouchPoints`, `pdfViewerEnabled`,
  `doNotTrack`, `connectionEffectiveType`, `connectionRtt`,
  `connectionDownlink`, `connectionSaveData`. Nested equivalents live under
  `navigator` with the same names.

## Environment

The flat `Page(environment=...)` key set grows from 27 to 36 keys. The nested
`Page.get_defaults()` shape gains the corresponding navigator and screen keys.
All values remain fixed constants supplied by Python; nothing is derived from
the host OS, display, locale, or network. New default values:

```text
maxTouchPoints=0          pdfViewerEnabled=false     doNotTrack="0"
connectionEffectiveType="4g"  connectionRtt=50       connectionDownlink=10.0
connectionSaveData=false  screenAvailLeft=0          screenAvailTop=0
```

## Non-goals

- `navigator.userAgentData` / UA-CH;
- `history` / `location.assign|replace|reload` / navigation;
- `alert` / `confirm` / `prompt` / `window.print` UI;
- `performance.mark` / `measure` / timeline entries;
- `structuredClone`, `scheduler.postTask`;
- permissions / clipboard / geolocation / Notification / media devices;
- fetch/XHR/WebSocket, workers, IndexedDB/Cache;
- `matchMedia`, scroll, layout, rendering, or screen-orientation events;
- a Python-side BOM API; everything remains JS-side through `Page.eval`.

The selector pseudo-class slice planned earlier is deferred; M11-36..M11-40
and M11-41..M11-45 subsequently completed the remaining deterministic BOM
stubs and viewport/media/device metadata, and pseudo-classes remain a later
selector slice.

## Tests

`test/test_m11_35_bom_metadata.py` covers the new window/screen/navigator
surface, persistence, read-only behavior, secure-context derivation, orientation,
flat/nested environment overrides, and the new defaults. `test_host_environment.py`
is extended for the 36-key round-trip and type rules; `test_m11_12_17_batch.py`
updates the nested defaults; `test_m11_26_window_metadata.py` refreshes the
historical `window.name` guard.

## Verification

Verified on real V8 at commit `aa23c57`:

- Linux `release-linux` run `40990275470` — cp311–cp314, each
  `1599 passed, 2 skipped`;
- Windows `release-windows` run `40990279247` — Python 3.11–3.14, each
  `1599 passed, 2 skipped`.

`test/test_m11_35_bom_metadata.py` ran in every lane (12 items); the full suite
collects 1601 items. Both runs were green on the first attempt. See
`docs/m11_35_report.md`.
