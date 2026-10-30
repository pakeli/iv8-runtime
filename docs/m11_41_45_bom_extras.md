# M11-41..M11-45 — BOM viewport / media / device metadata

Status: **IN PROGRESS — implementation and real-V8 verification pending
(`2026-10-30`)**

This batch fills the remaining deterministic BOM metadata that can be served
without layout, network, OS permission UI, background events, or a real storage
backend. `navigator.userAgentData` remains intentionally unimplemented;
callers that need a Chromium identity pass one through `Page(environment=...)`.
There are no new Python APIs or environment keys.

## M11-41 — visual viewport and window flags

Installed once per generation:

- `window.visualViewport` — read-only `{width, height, offsetLeft: 0,
  offsetTop: 0, pageLeft: 0, pageTop: 0, scale: 1}` derived from `innerWidth` /
  `innerHeight`;
- `visualViewport.add/removeEventListener` and `dispatchEvent` are deterministic
  no-ops; `onresize` / `onscroll` start as `null`. There are no zoom, scroll, or
  resize events;
- `window.origin` — the base URL's origin;
- `window.crossOriginIsolated` — `false`;
- `window.closed` — `false`;
- `window.frameElement` — `null` (single top-level context).

## M11-42 — matchMedia

`window.matchMedia(query)` returns a static MediaQueryList-shaped object:

- `media` is the input string and `matches` is evaluated synchronously;
- `addListener` / `removeListener` / `addEventListener` / `removeEventListener` /
  `dispatchEvent` are deterministic no-ops; `onchange` starts as `null`;
- no `MediaQueryListEvent`, dynamic viewport re-evaluation, or `change` delivery.

The bounded evaluator supports media types `all` / `screen` / `print`, comma
lists, `not`, `only`, `and`, and these features:

- `width` / `height` with exact, `min-`, or `max-` prefixes in `px`;
- `orientation: portrait|landscape` (from `innerWidth` / `innerHeight`);
- fixed desktop capabilities: `prefers-color-scheme: light`,
  `prefers-reduced-motion: no-preference`, `hover: hover`, `pointer: fine`,
  `display-mode: browser`, `update: fast`, and `scripting: enabled`.

Unsupported or malformed queries evaluate to `false` rather than throwing.
These values are not configurable in this version.

## M11-43 — navigator.storage

`navigator.storage` is a read-only object:

- `estimate()` resolves to `{usage: 0, quota: 10737418240}`;
- `persisted()` resolves to `false`;
- `persist()` resolves to `false`.

There is no quota accounting, actual persistence, storage bucket, filesystem, or
storage event. Promises settle through the normal `Page.run_jobs()` checkpoint
(or the virtual facade).

## M11-44 — navigator.permissions

`navigator.permissions.query({name})` resolves to a static PermissionStatus:

- supported names: `accelerometer`, `background-sync`, `camera`,
  `clipboard-read`, `clipboard-write`, `geolocation`, `gyroscope`,
  `magnetometer`, `microphone`, `midi`, `notifications`, `persistent-storage`,
  `push`, `screen-wake-lock`, `storage-access`, and `window-management`;
- every supported name reports `state: "prompt"`;
- the status has `onchange: null` and no-op listener methods;
- a missing/invalid descriptor or unknown name rejects with a JS `TypeError`.

There is no permission prompt UI, OS/device access, `PermissionStatus` change
event, or `revoke()`. This surface reports deterministic metadata only.

## M11-45 — legacy navigator metadata

- `navigator.plugins` and `navigator.mimeTypes` are frozen empty arrays;
- `navigator.javaEnabled()` returns `false`.

All three are read-only properties/functions. There is no plugin enumeration,
Java applet support, or a `PluginArray` / `MimeTypeArray` class.

## Tests

`test/test_m11_41_45_bom_extras.py` covers the viewport/window flags,
media-query evaluation and no-op listeners, StorageManager Promise results,
PermissionStatus query/rejection paths, legacy empty metadata, and read-only
behavior.

## Non-goals

- `navigator.userAgentData` / UA-CH;
- real layout, scroll/zoom, visual viewport events, or dynamic media queries;
- permission prompts, device access, clipboard/geolocation/Notification APIs;
- real storage quota/accounting, persistence, IndexedDB, Cache, or buckets;
- `Plugin` / `MimeType` objects, Java applets, or plugin enumeration;
- Python-side BOM APIs or JS→Python host callbacks.

## Verification

Real-V8 Linux + Windows CI will run after implementation. This section will be
replaced with run IDs and per-lane pytest summaries once green.
