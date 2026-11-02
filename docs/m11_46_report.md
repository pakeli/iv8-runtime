# M11-46 Delivery Report — Document.cookie

Status: **VERIFIED — real V8 green on Linux + Windows + macOS arm64 (`2026-11-02`)**

Branch `m11-46-document-cookie`, verified tree `18f1f4b`.

This is the first slice of the object-oriented common-API completion track.
The track finishes one browser object's most-used surface before moving to the
next; `Document` comes first, starting with `document.cookie`.

## Evidence

- Linux `release-linux` run `41479330002` — cp311 / cp312 / cp313 / cp314,
  each `1725 passed, 2 skipped`.
- Windows `release-windows` run `41479338268` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1725 passed, 2 skipped`.
- macOS arm64 fallback run `41482460847` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1725 passed, 2 skipped`.

`test/test_m11_46_document_cookie.py` ran in every lane (5 items). The full
suite collects 1727 items; 1725 pass and 2 are skipped.

macOS wheel artifacts: `13159394666` (cp311), `13159434857` (cp312),
`13159594415` (cp313), `13158764626` (cp314).

## Scope landed

- `document.cookie` is a writable, access-traced Document property backed by a
  deterministic in-memory jar owned by `PageState`.
- The setter processes the first `name=value` pair, trims ASCII whitespace,
  updates existing names in place, and appends new names. Malformed inputs
  without `=` or with an empty name are ignored.
- `max-age <= 0` and `expires` at or before the Unix epoch delete the named
  entry; attributes are case-insensitive. `path` / `domain` / `secure` /
  `SameSite` are accepted and ignored.
- The jar survives `Page.load()` generations and is isolated per `Page`; it is
  never persisted or shared across processes.
- Non-string assignments use ordinary `ToString` coercion, and conversion
  failures propagate unchanged.
- The getter joins entries in insertion/update order with `"; "`.
- Reads and writes use the existing HostObject getter/setter path, so the access
  trace records `document` / `cookie` / `get` or `set` with normal completion
  metadata. No new Python or JS API was added.

Descriptor behavior follows the framework's native data properties:
`Object.getOwnPropertyDescriptor(document, 'cookie')` materializes the value and
returns `{value, writable: true, enumerable: true, configurable: false}`; the
descriptor read itself invokes the getter (like the other writable HostObject
properties).

## Explicit non-goals

Real HTTP `Cookie` / `Set-Cookie` handling, origin/path/domain/secure/SameSite
filtering, wall-clock expiry, persistence, cookie stores/events, and the rest of
the Document API (`write`, text nodes, ranges, fragments, etc.) remain out of
scope. They are listed in `docs/m11_46_document_cookie.md`.

## Verification notes

- The first verification attempt (`18f1f4b`'s parent tree `f92887c`) failed
  only `test_cookie_is_a_writable_traced_document_property` on all Windows and
  Linux wheel lanes (`1 failed, 1724 passed, 2 skipped`). The test assumed the
  descriptor exposed `get`/`set`; this V8 build materializes native data
  properties into data descriptors and evaluates the getter during the
  descriptor read. `18f1f4b` corrected the assertion and documented the
  descriptor shape. Runtime behavior was intended and unchanged.
- `Uncaught Error: boom` stderr after the pytest summary is a pre-existing test
  artifact and does not affect the exit code.
- The macOS fallback run required building from the cached V8 monolith on
  `macos-release`; the official `macos-15` lane remains queued/unavailable, so
  the release steps ran on macos-14 with deployment target 11.0.

## Outcome

M11-46 is verified and merged to fork `master`. The next slice continues the
Document object (text nodes / `write` and related APIs) before moving on to the
next commonly used object. The previously deferred selector follow-up was never
numbered and remains unassigned.
