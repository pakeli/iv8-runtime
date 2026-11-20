# BOM-03 — native legacy Navigator quota interfaces

Selected on `2026-11-20` under the resumed BOM-first queue. BOM-02 is complete
and PR #33 is ready for review. This continuation covers the missing
webkitTemporaryStorage entry and the directly related existing persistent quota
object; it does not select a new device, network or storage-persistence backend.

## Boundary to implement against browser evidence

- Navigator.webkitTemporaryStorage and webkitPersistentStorage return the same
  stable native DeprecatedStorageQuota per Page generation. Match the reference's
  defining prototype, descriptors, methods and absence/presence of public
  interface objects; callback and numeric conversion follow the captured browser.
- queryUsageAndQuota and requestQuota use the existing deterministic quota
  profile (usage zero, quota 10 GiB), consistent with native StorageManager
  estimate. They do not consult or reserve host disk space. requestQuota reports
  the bounded available amount without allocating persistent storage.
- Replace the old JS bootstrap persistent-quota shim. This intentionally updates
  its distinct-object/flat-tag/own-JS-method shape, Promise-job timing and
  unconditional nonzero-request failure to the approved native browser reference.
  Original History, unrelated storage/permissions behavior and M1 API stay intact.
- Callback delivery uses the existing Page-owned native task infrastructure.
  Respect browser-observed synchronous versus asynchronous error channels and
  callback receivers. Callback exceptions must not alter the API's return.
  Handle state must clear before isolate disposal and on generation reload.
- Include only an error-object dependency actually required by quota callbacks,
  with browser-supported constructor behavior if exposed. No general error API
  batch or Observable/iterator-monitoring work is included.

Chrome 157.0.8103.80 confirms the dependency is constructible DOMError with
readonly native name/message fields. DeprecatedStorageQuota has a shared
prototype but no global interface constructor. Nonopaque callbacks run as tasks;
opaque query failure is deferred NotSupportedError, while opaque requestQuota
invokes its AbortError callback before returning. Match those completion channels.
The configured Page profile supports HTTP(S), WS(S), FTP and file origins as
nonopaque; other base URLs are treated as opaque, consistent with the existing
bounded URL/origin model. No iframe/worker origin inheritance is added.

Numeric and DOMString conversions share the already implemented native primitive
conversion path used by vibrate. Its existing generated-error object-label
limitations under modified prototype metadata remain explicit; original
user-thrown exceptions are preserved. Callback failures use the existing opt-in
async-error buffer, with a storage_quota kind, and do not abort later callbacks.

Capture secure/insecure/opaque cases with fresh Chrome profiles, version, flags
and exact probe hashes before freezing details. The final report must explicitly
separate configured quota values from actual browser disk-dependent values and
record any bounded profile differences. Shared SDK installation and PR merge
remain separate from development and validation.

## Observation and acceptance

Native parent reads, alias identity, methods, returned fields, synchronous
completion and rejected receivers enter the existing access trace. Calls also
enter watch_apis. Conversion/callback user reentry is visible; internal
materialization stays quiet. Preserve original user errors without diagnostic
getter or Proxy-trap invocation. Existing symbolic tag monitoring is reused;
further Symbol.iterator monitoring remains deferred.

Both legacy watch paths select the shared quota object. A method invocation
matches either alias selector; watching both produces one hit for each selector,
while the structured trace contains a single canonical DeprecatedStorageQuota
CALL. This is identity-based alias matching, not reconstruction of call syntax.
The existing DeprecatedStorageQuota.method watch selectors also remain valid.
Standalone run_jobs does not run quota tasks; the normal facade pumps provide
microtask-before-task ordering. Synchronous opaque request failures preserve
their captured timing. DOMError supplies its browser-required constructor and
name/message getters, with UTF-16 values retained exactly.

Focused offline tests must cover actual native shape, identity, alias mutation,
callback validation/order/this, number conversion, opaque-origin failures,
task pumping/cancellation, exceptions, tracing and lifecycle. Update superseded
persistent-quota/tag tests explicitly, then run the regression gate/full suite.
Validate local Mac Python 3.11–3.14 wheels first, then Linux/Windows when releasing.

Release validation completes on `2026-11-21` for source 4cbab17: four retained
local Mac lanes and eight hosted Linux/Windows lanes, with all 12 wheel artifacts
verified. See `docs/bom_03_release_report.md` for exact sources, runs, counts and
artifact IDs. This completion changes no API boundary or deployment permission.
