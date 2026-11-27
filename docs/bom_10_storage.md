# BOM-10 StorageManager and virtual filesystem slice

Date: `2026-11-28`

Status: **Implemented and locally verified on macOS arm64 / Python 3.13;
included in the 12-lane release-verified d475309 candidate.** The user requests
continuing after digest with native StorageManager and the bounded memory
filesystem from `bom_10_crypto_storage.md`. Writable streams and key operations
remain separate work.

## Execution boundary

- Expose native StorageManager, FileSystemHandle, FileSystemDirectoryHandle and
  FileSystemFileHandle only in the browser-observed secure profile; gate
  `navigator.storage` accordingly. Preserve stable StorageManager identity and
  the existing offline `estimate` (`usage=0`, fixed quota), `persisted=false`,
  `persist=false` behavior while migrating plain-object descriptors to native
  browser-derived prototypes and invalid-receiver Promise rejection.
- `getDirectory()` resolves a new root wrapper each time, pointing to the same
  entry. Implement directory/file creation and lookup, isSameEntry, resolve,
  removeEntry, handle.remove, and async entries/keys/values. Use reference-native
  shapes and errors, UTF-16 names, user coercion ordering and Promise behavior.
- `queryPermission` / `requestPermission` report granted for this private virtual
  store after browser-derived parameter validation. There are no actual user
  prompts or host-disk permissions.
- `getFile()` returns a standalone native File with copied bytes, original name,
  deterministic `lastModified=0`, and the explicitly supported MIME profile.
  No write API executes in this slice, so created files initially contain zero
  bytes. `.txt`/`.TXT` map to `text/plain`; other names use an empty MIME type
  unless separately captured and documented. File construction must be immune
  to replaced global File/Blob constructors and user prototype setters.
- Expose browser-native `createWritable` and FileHandle `move` methods but reject
  an explicit NotSupportedError profile; full option conversion/stream/rename
  behavior is not claimed. FileSystemWritableFileStream and worker-only
  FileSystemSyncAccessHandle are not installed by this slice.

## Ownership and isolation

The Page owns an origin-partitioned in-memory store with no V8 handles and no
host paths. Canonical HTTP(S) origins fold host/scheme case and default ports;
same-origin reload retains entries, different origins and different Page
instances isolate data, and dispose releases it. Unsupported/opaque origins
cannot alias a shared root. Old JS wrappers invalidate on generation replacement
using existing ContextState disposal, without changing the M1 contract.

Handles are weakly owned by their JS wrappers; retained handles identify an
origin root and UTF-16 path. Browser comparison shows that deleting and then
recreating a file at that path makes an old file handle access the new entry;
missing-path operations reject while identity and permission checks remain
available. Tree parents, when used, are weak links to avoid cycles. GC of
discarded wrappers does not remove live filesystem entries; deleting an entry
does not alter already-created File snapshots. Async iterators keep only their
needed state and support repeated done results. Tree data and wrappers must not
accumulate across reload/disposal.

Directory iterators snapshot membership when created and enumerate in
deterministic UTF-16 key order. Browser comparisons normalize enumeration order
because it is filesystem/platform dependent, and separately check membership
across changes before and after the first `next()`. USVString conversion replaces
unpaired surrogates. The captured Chrome NUL behavior is preserved: the display
name retains its suffix while the backing path truncates at the first NUL; a
leading NUL is rejected. These are virtual path keys, never host paths.

These persistence/isolation rules are the selected offline profile, not claims
that separate Chrome tabs or browser restarts behave the same way. No external
network, OS file picker, real persistence, quota pressure, Worker execution,
automatic event loop, or JS-to-Python callbacks are added.

Origin normalization deliberately follows the existing bounded `resolve_dom_url`
implementation (scheme/host case and default ports). Full WHATWG URL/IDNA and
nonstandard IPv4/IPv6 alias equivalence are not added by this slice; unsupported
or unparsable origins are rejected instead of sharing an opaque root.

## Compatibility migration and acceptance

Use the original storage fixtures for parent/interface shape and the applicable
getDirectory/tree behavior; writable-stream execution from that discovery probe
is reference-only. Add a dedicated tree/iterator/error probe without writes,
and explicitly list profile differences in runtime comparisons.

Migrate former StorageManager own read-only data-property expectations to native
prototype methods. Keep navigator.storage parent reads, watch calls, trace
identity, invalid receiver outcomes, provenance and tracing-toggle transparency
covered. Insecure/opaque Navigator fixtures should be aligned with the existing
browser reference, not changed to match a new implementation accidentally.

Validate exact browser comparisons first, then offline creation/removal,
same-origin reload, canonical origin aliases, cross-origin/Page isolation,
retained/deleted handles, GC counts, native File constructor mutation resistance,
async enumeration and Promise errors. Follow with the full installed-wheel
suite, record actual source/build evidence, and keep cross-platform release
verification pending until all selected lanes execute successfully.

Local acceptance: **96 focused passed**, full **3604 passed, 2 skipped**.
Source/build receipts, known profile differences and review evidence are in
`docs/bom_10_storage_report.md`.

Combined release verification completed on `2026-11-28`: see
`docs/bom_10_release_report.md` for fixed source/tree, 12-lane results, artifacts
and the separate ordinary hosted macOS CI limitation.
