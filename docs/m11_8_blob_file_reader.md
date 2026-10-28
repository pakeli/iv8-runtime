# M11-8 — Blob / File / FileReader (manual microtask delivery)

M11-8 is the eighth implementation slice under the compatibility-first M11
line. It adds immutable binary value objects (`Blob`, `File`) and a minimal
`FileReader` whose asynchronous completion uses the existing `Page.run_jobs()`
microtask checkpoint. It adds no Python API, no new pump, no network access,
and no object-URL surface.

The slice was selected from `docs/m11_compatibility_backlog.md` (rank 4:
`Blob` / `File` / `FileReader`), batched with M11-7.

## `Blob`

**`new Blob(parts?, options?)`**

- `parts` — when present it must be a JS `Array` (any other non-`undefined` /
  non-`null` value throws `TypeError`). Each element is appended in order:
  - a `Blob` (including `File`) → its bytes;
  - an `ArrayBuffer` → a byte copy;
  - an `ArrayBufferView` (TypedArray / DataView) → a byte copy of the view;
  - anything else → `String(element)` encoded as UTF-8.
- `options.type` — `String()` converted, ASCII-lowercased; default `""`.
- Own read-only fields: **`size`** (byte count) and **`type`**.
- **`slice(start?, end?, contentType?)`** — returns a new `Blob` over the
  selected byte range. Negative offsets are relative to the end; values are
  clamped; a missing `end` means the end of the blob. `contentType` defaults
  to `""` and is ASCII-lowercased when provided.
- **`text()`** — returns a `Promise` resolving to the bytes decoded as UTF-8
  (lenient replacement, same as `TextDecoder`).
- **`arrayBuffer()`** — returns a `Promise` resolving to a fresh `ArrayBuffer`
  copy.
- Promises settle immediately; their reactions (`.then`) run only when
  `Page.run_jobs()` drains the microtask queue.

## `File`

**`new File(parts, name, options?)`**

- Inherits `Blob.prototype`, so a `File` is `instanceof Blob` and supports
  `size` / `type` / `slice` / `text` / `arrayBuffer`.
- **`name`** — `String(name)`; missing / `undefined` → `""`.
- **`lastModified`** — truncated number; default `0` (deterministic — this
  runtime has no wall clock in its scheduling model).
- `options.type` follows the `Blob` rule.

## `FileReader`

**`new FileReader()`** — an event target with:

- **`readyState`** — `0` EMPTY, `1` LOADING, `2` DONE (own writable field
  updated by the reader).
- **`result`** — `null` until a read completes; a string for `readAsText`,
  an `ArrayBuffer` for `readAsArrayBuffer`.
- **`error`** — always `null` in this slice (there is no I/O error path).
- **`addEventListener` / `removeEventListener` / `dispatchEvent`** — the same
  single-target semantics as the existing `EventTargetHost` (no bubbling).
- **`readAsText(blob, encoding?)`** — `blob` must be a `Blob` / `File`
  (`TypeError` otherwise). `encoding` is accepted but always interpreted as
  UTF-8. Sets `readyState = 1`, dispatches `loadstart` synchronously, copies
  the bytes, and schedules one delivery on the microtask queue.
- **`readAsArrayBuffer(blob)`** — same state machine; the result is an
  `ArrayBuffer` copy.
- **`abort()`** — while a read is pending: cancels the delivery, clears
  `result`, sets `readyState = 2`, and dispatches `abort` then `loadend`
  synchronously. With no pending read it is a no-op.
- Calling a read while `readyState === 1` throws a JS `TypeError`
  (`FileReader is already reading`).
- On `Page.run_jobs()`: the pending read completes — `readyState = 2`,
  `result` is set, and `load` then `loadend` are dispatched in that order.
  `run_timers()` / `run_animation_frame()` / `run_idle_callbacks()` never
  deliver a read.
- `loadstart` / `load` / `loadend` / `abort` are plain `Event`-shaped values
  (`type`, `target`, `currentTarget`) built by the native dispatcher; no
  `onload` / `onerror` properties.

## Lifetime

- Blob/File bytes live in a per-instance private slot (GC-managed).
- FileReader state lives in the current generation (`DocumentHost`), including
  listener handles and the pending read; `Page.load()` discards pending reads
  and listeners. There is no cross-generation FileReader.
- `Page.run_jobs()` after `dispose()` still raises `JSContextDisposedError`.

## Non-goals

M11-8 deliberately does **not** add:

- network or filesystem access; `FileReader` reads only in-memory blobs;
- `readAsDataURL` / `readAsBinaryString`, progress events, `on*` handlers,
  bubbling FileReader events, or a `DOMException` error model;
- `URL.createObjectURL` / `revokeObjectURL`, streams, `Blob.stream()`,
  `Blob.bytes()`, or File System Access APIs;
- a Python API, a new pump, or a fifth scheduler;
- non-UTF-8 encodings, BOM stripping, or a `TextDecoderStream`.

## Files changed

- `include/iv8/page_state.h` — FileReader state ownership if needed;
- `src/core/page_state.cpp` — Blob/File constructors, private-slot byte
  storage, slice/text/arrayBuffer, FileReader state and microtask delivery;
- `test/test_blob_file_reader.py` — focused acceptance tests;
- `python/iv8/__init__.py` — module-docstring paragraph;
- docs: this boundary note, `docs/m11_8_report.md` (created at verification),
  `docs/m11_compatibility_backlog.md`, `docs/dom_bom_gaps.md`,
  `docs/current_status.md`, `docs/next_tasks.md`, `docs/decision_log.md`, and
  `docs/runtime_surface.md`.

`JSContext`, the native `ContextState` API, the four pumps, and the existing
DOM/event surfaces are unchanged.

## Verification

Status: **verified on real V8 (Linux + Windows), `2026-10-28`.**

- Focused tests: `test/test_blob_file_reader.py` (22 items).
- Final verified commit `35c85fe`; Linux run `40661574145` (cp311–cp314) and
  Windows run `40661580891` (Python 3.11–3.14), every wheel lane
  `1352 passed, 2 skipped` (the five-slice batch total).
- `test_filereader_read_while_loading_throws` used a top-level `return` and was
  fixed to an IIFE in `35c85fe`. See `docs/m11_8_report.md`.
