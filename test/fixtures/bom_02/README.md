# BOM-02 browser reference

Captured on **2026-11-19**, Google Chrome **157.0.8103.80**, macOS arm64,
using isolated temporary profiles. Each JSON records the exact capture time,
origin, browser flags, probe SHA-256 and served fixture requests. The secure
fixture uses HTTP loopback; the insecure fixture maps a non-localhost HTTP
hostname to loopback. External DNS is blocked, background networking is disabled,
and no user browser profile, manifest, installed app or device is used.

Both captures use a local DevTools pipe and wait for the probe's Promise
with a wall-clock bound. This allows the browser's native related-app completion
callbacks to run without racing the dump-dom virtual-time budget. The insecure
capture has no related-app API and captures the synchronous vibration cases.
Raw capture scripts/logs remain under ignored `data/bom-02-2026-11-19/`.

`getInstalledRelatedApps` is a native, non-constructable Navigator prototype
method in the secure context only. Every successful call returns a new Promise
and fresh mutable empty array. Extra arguments are ignored. Invalid receivers
reject a Promise. An inherited Array.prototype.then getter runs after the
current microtask checkpoint, not during the API call.

`vibrate` returns false in this browser's unactivated profile after its required
argument conversions. Real arrays bypass an overridden Symbol.iterator and
convert indices using the live array length. Other objects first undergo
overload iterable detection; a sequence conversion can read Symbol.iterator a
second time. Iterator results read value before done in this pinned Chrome
version, and failed conversion does not invoke iterator.return. The captured
side-effect/error assertions define this implementation profile rather than
assuming generic JavaScript for-of behavior. These conversions do not authorize
new iterator monitoring or a vibration hardware backend.

The runtime uses its existing deterministic Page task queue for related-app
delivery. Facade pumps provide microtask-before-task ordering; standalone pumps
retain their documented separate-queue behavior. There is no wall-clock,
manifest-fetch or operating-system app-enumeration implementation.
