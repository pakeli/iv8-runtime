# BOM-02 — related-app inspection and vibration capability

Authorized on `2026-11-19` after BOM-01 acceptance. The user approves continuing
the BOM-first plan, with further Symbol.iterator monitoring deferred and local
macOS wheels validated before Linux/Windows builds.

## Page-host scope

- Add native Navigator.prototype.getInstalledRelatedApps in secure contexts,
  using the Page's existing secure-context determination at generation setup.
  The single top-level Page has no installed related applications or manifest
  retrieval backend. Calls return a fresh Promise resolving to a fresh empty
  array. Preserve browser-derived method shape, receiver validation and Promise
  rejection channels; extra arguments are ignored without conversion.
  Empty-result delivery is a native host task on the existing Page task queue,
  after the current microtask checkpoint. `tick`, `advance`, `run_until_idle`, or
  `run_timers` followed by `run_jobs` deliver it; `run_jobs` alone does not run
  a host task. Its private queue entry cannot be canceled by clearTimeout or
  clearInterval. The existing generation teardown releases its callback and
  resolver. Public JSContext/Page method signatures remain unchanged.
  The legacy `run_timers` pump still runs only its queue; callers requiring
  browser-style microtask-before-task ordering use `tick` or the other facade
  pumps. This slice does not silently change the existing independent pumps.
- Add native Navigator.prototype.vibrate with the reference browser's arity,
  receiver rules and numeric/sequence argument conversion. Conversion preserves
  the reference's user-code invocation counts and order, including failures. The Page
  has no trusted activation or vibration hardware, so supported calls report the
  reference's inactive-profile result without a device operation.

Capture Chrome version, origins and execution flags with offline fixtures before
freezing conversion behavior. Browser implementation differences from the
abstract IDL, including overload resolution, numeric Clamp and iterator failure
behavior, follow that reference. Iteration used to convert arguments does not
expand the deferred Symbol.iterator monitoring scope.

Keep identity, defining prototypes, native source and descriptor flags. Secure
exposure must reset correctly across secure/insecure reloads and Page isolation;
user changes to public isSecureContext must not grant an API or affect native
receiver validation. No M1 change, manifest download, OS app enumeration, trusted
input injection or device backend is added.

For a non-callable Symbol.toPrimitive hook, the binding creates a prefixed native
TypeError without re-reading the original object. Its internal diagnostic holder
normalizes the ordinary-object label to the captured Chrome profile. Modified
prototype metadata and other original receiver classes can render different
object labels in that generated message; universal diagnostic-text equivalence
is not claimed. User-thrown exceptions retain their original identity/message,
and diagnostic rendering must not execute extra getters or Proxy traps.

## Observation and verification

Both APIs participate in existing access tracing and watch_apis. Record actual
references/calls, safe arguments and synchronous result/exception completion.
Promise rejection is asynchronous settlement, not a synchronous thrown entry.
Argument getters, conversion and iterator reentry must retain user attribution;
diagnostics must not trigger getters or traps. Existing filters, capacity,
debug/prod and reload semantics remain applicable.

Example with the deterministic Page pump:

```python
import iv8

with iv8.Page() as page:
    page.load("<html><body></body></html>", "https://example.test/")
    page.eval("globalThis.appCount = -1; navigator.getInstalledRelatedApps()"
              ".then(apps => { appCount = apps.length; });")
    page.tick()
    assert page.eval("appCount") == 0
    assert page.eval("navigator.vibrate([50, 20, 50])") is False
```

Run existing pinned offline scenarios with the accepted Mac wheel before this
batch. Record real script blockers separately from regression expectation and
server acceptance. Add browser-derived tests for shape, valid/invalid receivers,
security exposure, conversion effects/errors, fresh results, tracing and
lifecycle. Build/install/test Mac Python 3.11 first, then 3.12–3.14, before
dispatching other platform builds. Keep handoff documents and actual counts in
sync; local acceptance does not imply a completed release matrix.
