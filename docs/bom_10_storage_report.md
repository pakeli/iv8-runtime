# BOM-10 virtual storage implementation and local verification

Later combined release verification (`2026-11-28`) passes all 12 selected lanes
at fixed source d475309. See `docs/bom_10_release_report.md`; local-only results
and original pending notes below describe the earlier development snapshot.

Date: `2026-11-28`

Status: **Directory/handle storage slice implemented and locally verified on
macOS arm64 / Python 3.13. Cross-platform release validation is pending.**
Branch: `codex/bom-10-crypto-storage`; base commit
`a476387cc83e802dfa2d2e90a6872ba4775ce7d2`, with uncommitted digest and storage
work. Exact runtime/test/build/workflow hashes are in
`data/bom-10-storage-2026-11-28/verification-manifest.json`; there is no invented
commit/tree ID for the working tree. Latest cross-platform verified remains
BOM-08 and the shared SDK remains BIND-01.

## Delivered behavior

- Native secure-only StorageManager, FileSystemHandle,
  FileSystemDirectoryHandle and FileSystemFileHandle; prototype inheritance,
  method order/descriptors/native source, Promise receiver errors and synchronous
  iterator receiver errors follow Chrome 158 reference receipts.
- `navigator.storage` retains stable identity. `estimate` preserves the offline
  `usage=0`, `quota=10737418240` values; `persisted` and `persist` return false.
  `getDirectory` creates distinct root wrappers with common root/path identity.
- Virtual directory/file creation and lookup, removal including recursive/root
  removal, relative resolution, isSameEntry, granted permission methods and
  snapshot async entries/keys/values. Names use browser-derived USVString and
  NUL behavior; no host pathname or filesystem operation is used.
- Pure C++ Page-owned origin partitions preserve same-origin reload contents,
  isolate other origins and Pages, and clear at disposal. Old JS wrappers still
  invalidate with their ContextState generation. Handles locate current entries
  by root/path so a deleted/recreated file can be reached through an old handle,
  matching the new browser evidence.
- Handles and iterators use weak-wrapper ephemeral ownership. Retained wrappers
  preserve their data; wrapper GC does not erase tree entries. A bound iterator
  next method remains usable across GC; dropping it releases the native object.
- Internal File snapshot factory uses cached native prototype/private bytes,
  without invoking replaced globals or inherited setters. Current files are
  empty; `.txt` MIME is `text/plain`, other extensions are the explicit empty-MIME
  profile, and `lastModified` is deterministic zero.
- Native `createWritable` and FileHandle `move` reject the explicit unsupported
  directory profile. Streams and sync access handles are not installed.
- Storage watch paths and access-trace classification, identity, method GET/CALL
  folding, prototype reads, user overrides and invalid receivers remain covered.

Both final inventory entry points now exist: secure Crypto **3/3** and
StorageManager **4/4**. This closes the recorded non-DOM BOM *name* gaps; it does
not mean full WebCrypto, OPFS, stream or browser behavior is implemented.

## Verification

Fresh installed V8-linked wheel:

- Storage/reference/trace/Blob focused suite: **96 passed**.
- Full offline `test/`: **3604 passed, 2 skipped**, no exclusions.
- Three new `storage_tree` reference receipts for secure loopback, non-loopback
  HTTP and opaque about:blank; **3/3 fresh-profile repeated result payloads match**.
  Exact scripts/version/arguments/source hashes are included in each receipt.
- Tests cover canonical hostname/default-port reload, cross-origin and Page
  isolation, disposed/retained handles, identity after recreation, USV/NUL names,
  prototype pollution resistance, internal File construction, explicit unsupported
  operations, async iterator shape/membership and GC.
- Independent review checked native lifetime/ownership and conversion order;
  bound-next/GC/store-retention behavior was also verified independently.
- Local regression revealed a brittle hard-coded bootstrap source array length:
  changing a comment padded the script with NUL bytes and disabled matchMedia.
  The BOM bootstrap declaration is now unsized and installation uses its actual
  string length. Original matchMedia and full-suite regressions pass.
- Iterator bootstrap compilation/execution releases the GIL, preserving the
  kernel threading boundary even for fixed internal script text.
- All three release entry points now have identical **46-module** focused gates.
  This is wiring validation, not execution of their remote release lanes.

Artifact:
`data/bom-10-storage-2026-11-28/dist/iv8-0.0.1-cp313-cp313-macosx_11_0_arm64.whl`.
Wheel SHA-256: `cd00aa21aa17c1c9bf3a239198fcfca9a7c60932c59a2c6c7915ac261fa158a7`.
Native SHA-256: `4e24a2d90770cc702952776d4ea00c116703f3aa8b86eeb1200e9236e8356cbe`.
Build, focused/full logs and JUnit XML are next to the verification manifest.
The previously verified digest backend/license configuration is unchanged.

## Deliberate limits

- Canonicalization uses the existing bounded URL parser. Nonstandard IP aliases,
  IDNA/full WHATWG URL equivalence, non-HTTP(S) storage and browser-wide sharing
  are not claimed; unsupported origins reject rather than share an opaque root.
- Iterators snapshot membership at creation and use deterministic UTF-16 order.
  Reference comparisons normalize platform-dependent enumeration order while
  retaining explicit mutation/membership checks.
- File snapshots retain the repository's existing bounded File/Blob descriptor
  and toStringTag profile. This slice does not claim complete browser File shape.
- No writes/streams/move, host filesystem, quota accounting, prompts, worker
  filesystem access, persistent browser-profile storage or auto event loop.
- The other 16 SubtleCrypto operations remain explicit unsupported profiles.
- Other Python/platform release lanes have not been run for this candidate.
  Complete local macOS 3.11–3.14 before hosted Linux/Windows per the existing
  release policy, and update verification records only from actual results.
