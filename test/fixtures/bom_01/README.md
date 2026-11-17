# BOM-01 browser reference

Captured on **2026-11-17**, Google Chrome **157.0.8103.79**, macOS arm64,
using a fresh headless profile and an empty HTTP loopback secure-context
document. The JSON records the exact UTC capture time, origin, browser flags,
probe SHA-256 and served request paths. Background networking is disabled and
non-loopback DNS is blocked. No user profile, external page, gamepad or native
input is used.

`probe.js` records native function shape, descriptors, defining prototypes,
constructor and EventTarget inheritance, property identity, synthetic event
delivery and orientation-lock completion channels. `chrome.json` is the
immutable reference for offline tests. It is not an API implementation or a
claim that every member in the captured prototypes has been implemented.

The empty-device `getGamepads()` result in this Chrome build is a fresh mutable
array of **eight null slots**. Scheduling has no pending native input;
`isInputPending` still reads and converts its optional options dictionary.
`screen.orientation.lock` rejects a Promise for unsupported operation, argument
conversion errors and an invalid receiver; `unlock` returns undefined.

NetworkInformation, ScreenOrientation and VisualViewport share EventTarget's
native methods and accept synthetic dispatch. Their handler slots normalize
non-functions to null. The reference records once-listener removal, handler
receiver/target/currentTarget, event phase, cancellation and post-dispatch
cleanup. Device-generated events, orientation control, layout and trusted input
remain outside this offline batch.

The current runtime's shared EventTarget retains its separately documented
function-listener and event-handler ordering boundaries. BOM-01 uses the same
store and dispatch machinery; the native Event check added to the three BOM
entry points does not redefine older EventTarget entry points.
