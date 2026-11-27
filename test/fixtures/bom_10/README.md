# BOM-10 browser reference

Captured on 2026-11-27 with Google Chrome **158.0.8127.88** on macOS arm64.
Each origin class uses a new temporary headless profile, a private DevTools
pipe, no user gesture or permission grant, and external DNS disabled:

- `*-secure.json`: local `http://127.0.0.1:<port>/reference` (trustworthy origin).
- `*-insecure.json`: `http://bom-fixture.invalid:<port>/reference`, resolved only
  to the local fixture server; no secure-context override flag.
- `*-opaque.json`: fresh `about:blank` with opaque origin.

`crypto_probe.js` and `storage_probe.js` are the exact source bytes hashed by
each receipt. Every receipt includes the full browser version, capture time,
arguments, origin class, request log, and unmodified result. Crypto operations
use fixed bytes; no random keys are generated. File operations touch only the
temporary browser profile's origin-private storage. The test directory is
removed by the probe, and the profile is removed by the runner.

Manual recapture (POSIX Python, no third-party Python dependencies):

```sh
python tools/capture_bom_10_reference.py --chrome '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
```

The normal pytest suite does **not** launch Chrome or use a network. It reads
these fixtures, verifies source hashes, and checks digest vectors independently
with Python hashlib. Browser OPFS write/stream and raw-key import results are
discovery evidence for later slices; they do not establish iv8 support.

Limitations: top-level Window contexts only; no worker, cross-origin-isolated
SharedArrayBuffer profile, permission prompt, persistent user profile, OS file
picker, quota exhaustion, key generation, or reload-persistence capture in this
initial reference set. `constructable` describes the JavaScript [[Construct]]
capability without invoking the API; constructor invocation failures are recorded
separately by the crypto probe. OS/browser updates require a deliberate reference
refresh rather than changing expected data to match the runtime.

The subsequent digest implementation adds `digest_edges_probe.js` and three
`digest_edges-{secure,insecure,opaque}.json` receipts (nine receipts in total).
They capture random API DOMExceptions, resizable-buffer rejection, digest
conversion/copy ordering, recognized non-digest algorithm errors, and static
`SubtleCrypto.supports` behavior. Recapture this group with
`python tools/capture_bom_10_reference.py --groups digest_edges`. Runtime tests
compare supported behavior exactly; positive raw key imports remain reference
only, and pre-existing V8 SharedArrayBuffer exposure is documented separately.

The storage slice adds `storage_tree_probe.js` and three `storage_tree-*`
receipts captured on 2026-11-28 (12 receipts total). These avoid writes/streams
and cover native async iterator shape, USV/NUL names, deletion/recreation,
permissions, snapshot membership, and root removal. Enumeration results are
normalized to UTF-16 order for comparison. Recapture via `--groups storage_tree`.
The new runtime compares this group exactly; the original writable stream
discovery remains outside the implemented directory/empty-file profile.
