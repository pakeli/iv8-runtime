# BOM-10 boundary draft: WebCrypto and origin-private storage

Date: `2026-11-27`

Status: **Digest and native virtual directory/handle storage slices are locally
implemented and verified. Key operations, writable streams, file movement and
cross-platform release verification remain pending.** See
`bom_10_digest_report.md` and `bom_10_storage_report.md` for the successive
working-tree candidates, source hashes, actual tests and profile differences.
Secure crypto has 3/3 entry names and StorageManager 4/4; membership is separate
from complete operation support.

## Objective and completion terminology

The inventory's remaining two entry points are `crypto.subtle` (`crypto` baseline
`2/3`, now `3/3` in the digest slice) and `navigator.storage.getDirectory` (`navigator.storage` baseline `3/4`, now `4/4`).
Track three separate measures: entry-point membership, native interface shape,
and functioning operations. Adding these two names must not be reported as a
complete WebCrypto implementation or complete OPFS/Streams support.

New APIs belong to the hosted Page layer. M1's `JSContext` contract, isolate/GIL
ownership, explicit pumping, and disposal rules remain intact. No JS-to-Python
callbacks, automatic event loop, network backend, user file picker, or access to
the host filesystem is introduced by this milestone.

## Browser evidence and observed behavior

The reference is Google Chrome **158.0.8127.88**, macOS arm64, fresh headless
profiles, no user gesture or permission grant, and external DNS disabled.
`test/fixtures/bom_10` contains two exact probe scripts and six receipts: crypto
and storage for a trustworthy loopback HTTP origin, non-loopback HTTP mapped to
the same local server, and opaque `about:blank`. Each receipt records the script
SHA-256, flags, version, origin class, timestamp and request log. Results were
repeated in fresh profiles and match exactly. Capture instructions and omissions
are in the fixture README; evidence summary is `bom_10_reference_report.md`.

Observed requirements include:

- `crypto` is an instance of `Crypto`, with no own keys; `subtle` is a
  getter on `Crypto.prototype`, and repeated reads return the same SubtleCrypto
  instance. `subtle`, `randomUUID`, `SubtleCrypto`, and `CryptoKey` are exposed
  in the secure profile; `subtle`/`randomUUID` and the two latter constructors
  are absent in both captured nonsecure profiles. `Crypto` remains present.
- `SubtleCrypto.prototype` has **17 operations**, in the captured order:
  `decrypt`, `deriveBits`, `deriveKey`, `digest`, `encrypt`, `exportKey`,
  `generateKey`, `importKey`, `sign`, `unwrapKey`, `verify`, `wrapKey`,
  `decapsulateBits`, `decapsulateKey`, `encapsulateBits`, `encapsulateKey`,
  `getPublicKey`. Constructor/operation descriptors and native function shapes
  must come from the receipts, not a historical twelve-method list.
- SHA-1/256/384/512 digest results match independent hashlib vectors for `abc`
  and empty input. TypedArray/DataView offsets are respected; mutation immediately
  after `digest()` does not change the pending operation's input snapshot.
  Digest validation and invalid receivers reject Promises; a throwing algorithm
  getter retains its original exception identity.
- `navigator.storage` is absent in both captured nonsecure profiles. In the
  secure profile it is a stable StorageManager instance with no own keys.
  `getDirectory()` resolves to a FileSystemDirectoryHandle (`name === ""`,
  `kind === "directory"`). Repeated calls return distinct wrappers for the same
  entry: `===` is false and `isSameEntry()` resolves true.
- OPFS root readwrite `queryPermission()` resolves `"granted"` in the captured
  fresh profile. Invalid names reject TypeError; missing entries reject
  NotFoundError; kind conflicts reject TypeMismatchError; non-recursive removal
  of a nonempty directory rejects InvalidModificationError. `resolve()` returns
  relative components, `[]` for self, and `null` outside the directory.
- Chrome's `createWritable()` returns a FileSystemWritableFileStream inheriting
  WritableStream. A private-profile write/close/read of `"hello"` succeeds.
  This discovery is not approval to omit stream semantics or a claim that iv8
  already supports writable streams.

## Implementation sequence and proposed initial execution boundary

### 1. Native parent interfaces and digest

Replace the current flat crypto object with browser-derived Crypto/SubtleCrypto
bindings and security gating. Implement **digest first**, with the four captured
SHA algorithms, exact BufferSource slicing/copy timing, algorithm conversion,
error identity and Promise behavior. Preserve existing random-value behavior
except for explicitly documented browser-shape/error corrections covered by
additional comparisons. Native function descriptors, receiver rejection, stable
identity, `watch_apis`, and access-trace operation/filter behavior need focused
coverage before this slice is accepted.

The remaining 16 SubtleCrypto operations, CryptoKey import/export/generation,
encryption/signing/derivation/wrapping and encapsulation families remain separately
tracked implementation work. Their shape is inventoried by this reference set;
fixed raw AES-GCM/HMAC import observations are discovery evidence only. Each
selected operation must either execute its declared contract or report an
explicitly documented unsupported profile. Empty objects, fake ciphertext,
constant digests, or false success must never stand in for implementations.

**Backend selected for the digest slice:** pinned TF-PSA-Crypto 1.1.2 portable
SHA implementations, statically built with C ABI and shipped license. Version,
source archive hash, local vectors, offline-build behavior and private-upstream
API limitations are documented in `bom_10_digest_backend.md`. Remaining key
families need their own backend/configuration review; no handwritten primitives
or Python callbacks are introduced.

### 2. StorageManager and a bounded in-memory OPFS profile

The StorageManager shim has been replaced with native browser-derived bindings
and secure-context gating. The completed initial execution slice includes: `getDirectory`, root and
child handles, entry identity, directory/file creation, lookup, removal, relative
resolution and async enumeration; `getFile()` can expose an empty native File
before the write slice is implemented. Exact names and shape are retained in the
reference inventory, including methods outside this initial execution slice.

The selected storage model for implementation planning is **Page-owned virtual
storage partitioned by canonical origin**. Same-origin reload retains that Page's
virtual contents; different origins and different Page instances do not share
contents; dispose drops the data. JS wrappers still invalidate on generation
replacement. This is an explicit offline profile choice, not a claim about
Chrome's persistence or cross-tab sharing, which this initial capture does not
measure. The backing store must never map a JS path onto a host filesystem path.

Use existing native DOMException, interface and Promise infrastructure. Reuse
Blob/File's private-byte representation through an internal binary factory;
do not invoke user-replaceable global constructors. File metadata policy, browser-derived name behavior and lifecycle evidence
are frozen in `bom_10_storage.md` and its report. Returned handles must retain their entry
identity without retaining discarded wrappers for the Page's entire lifetime.

### 3. Writes, streams and remaining crypto families

`createWritable`, writer/stream locking, seek/truncate/abort, close/commit
visibility, deletion interactions and binary snapshots require a separate
execution slice. WritableStream foundations do not exist in this repository;
settle their bounded contract before claiming write support. Worker-only sync
access handles, file pickers, host-disk persistence, shared cross-Page storage,
quota pressure, device access and permission prompts remain outside this initial
plan. Additional crypto families also require operation-specific reference
vectors and lifecycle/error coverage before they count as supported.

## Existing contracts to migrate explicitly

Before the digest slice, `src/core/page_state.cpp` installed crypto methods on
a plain object; that shape has now been migrated. The former three plain StorageManager
methods in `src/core/browser/bom/navigator_parts.cpp` have also been replaced
by native StorageManager; affected descriptor/trace tests were migrated. `test/test_access_trace_storage_manager.py` and crypto
monitoring tests preserve those earlier descriptors/owners and trace categories.
Each interface migration must identify the superseded assertions, replace them
with browser-backed expectations, and retain invocation provenance, exception
identity, filters, and monitoring toggle transparency. Do not remove regression
assertions simply because a shape changed.

## Acceptance and release gates

1. Offline reference tests verify script hashes, complete captures, security
   gating, independent digest vectors and stable OPFS observations. These tests
   alone do **not** verify iv8 implements BOM-10.
2. Add runtime comparisons per completed slice using the same secure/insecure/
   opaque inputs, plus exception/coercion ordering, invalid receivers, native
   descriptors, cached identity, tracing and copied-input tests.
3. Exercise Page isolation, same-origin reload policy, invalidation, disposal,
   Python GIL behavior and GC of retained/released keys, handles and buffers.
   Reuse the BOM-09 memory regression protections.
4. Run focused tests before the full offline suite. Record actual counts,
   runtime commit/tree, and remaining unsupported methods in the handoff set.
5. After the selected implementation is complete, validate Python 3.11–3.14
   wheels on local macOS arm64 first, then Linux x64/Windows x64 and V8 CI.
   Keep latest implemented separate from latest cross-platform verified;
   collecting this baseline changes neither release status nor shared SDK.
