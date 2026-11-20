# BOM-03 legacy quota browser reference

Captured on **2026-11-20** using fresh temporary profiles in **Google Chrome
157.0.8103.80**, macOS arm64. The three JSON records cover a secure HTTP loopback
document, an insecure HTTP hostname mapped to loopback, and an opaque about:blank
document. Each records exact time, flags, requests and the probe SHA-256.
Profile paths are normalized. The local CDP pipe waits for actual browser task
completion; there is no virtual-time shortcut or business-site access.

The two Navigator getters return the same stable DeprecatedStorageQuota. Its
shared prototype owns two native methods and a readonly configurable tag, with
no own constructor or exposed global DeprecatedStorageQuota. The required error
type is the constructible DOMError with native name/message prototype getters.
Prototype keys, native source/arity/constructability, receiver errors and
Reflect.construct's single NewTarget.prototype read are captured.

The nonopaque empty profiles report usage zero and quota 10 GiB. These captured
values match the runtime's existing deterministic StorageManager profile; the
runtime does not derive quota from the host disk. requestQuota reports min of
the WebIDL-converted unsigned-long-long request and available quota, and does
not allocate space. Validation, conversions and original thrown values precede
task delivery. A strict callback receives undefined as this.

For the opaque origin, query failure is an asynchronous NotSupportedError;
request failure invokes its AbortError callback synchronously. Both pass native
DOMError objects. The optional-null-error case records a bounded timeout as
evidence of no callback; offline tests drain the available task queue rather
than waiting for that browser timer.

Raw capture scripts and logs live under ignored `data/bom-03-2026-11-20/`.
The final captures exit successfully and preserve the exact committed probe
bytes; initial capture cleanup errors are separate from those final records.
