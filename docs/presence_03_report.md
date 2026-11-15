# PRES-03 Window presence acceptance

Date: `2026-11-15`. **Implemented and locally accepted; release verification
pending.** The user confirms this batch covers the **49 missing ordinary Window
members**, with simple typed exposure first. The separate **821 missing global
interface entries are excluded**. Boundary:
[presence_03_window.md](presence_03_window.md).

## Result

The unchanged Chrome 157 inventory and membership probe confirm all 49 names
were added, no earlier reference names disappeared, and the excluded global
interface-entry count stayed unchanged:

| Window category | Before present / reference | After present / reference | Missing after |
|---|---:|---:|---:|
| Ordinary members | 79 / 128 | **128 / 128** | **0** |
| on* handlers | 125 / 125 | 125 / 125 | 0 |
| Interface entries | 106 / 927 | 106 / 927 | 821 |
| All browser-related reference names | 310 / 1180 | 359 / 1180 | 821 |

Every added name, its exposure/behavior status and all 22 measured targets are
recorded in [presence_03_api_delta.json](presence_03_api_delta.json).
Other targets are unchanged. These are name-presence results, not subsystem
implementation percentages.

## Typed exposure and prototype foundation

- All captured global functions have native names, arities and engine-generated
  native source, without construction or an own prototype. NodeFilter is also
  the captured non-constructable function namespace with its 16 numeric constants.
  GPU usage namespaces have the captured numeric constants.
- CSS/GPU/chrome namespace objects retain their direct Object.prototype
  relationship; instance shells use canonical shared prototypes in the existing
  private interface registry. Hidden constructors are not published as new
  Window interface entries. StyleMedia retains its captured inherited Object
  constructor. EventTarget-derived shells reuse synthetic event delivery.
- Object identities and native handles are generation-owned, stable within a
  Page, independent across Pages and reset on load. Native/namespace methods keep
  an extension point for later implementation. The trace filter supports
  window_shell calls without adding Python callbacks or another engine.
- The captured profile values are PERSISTENT=1, TEMPORARY=0,
  credentialless=false, originAgentCluster=true and offscreenBuffering=true.
  fence is null. window.event is initially undefined **with the property present**.

## Behavior supplied in this batch

| Group | Current behavior |
|---|---|
| Prefixed animation-frame operations | Share the existing scheduler and cancellation handles |
| window.event | Current listener/handler event, nested/error restoration, replaceable property assignment |
| reportError | Synchronous bounded error Event with original error identity and message/location fields |
| CSS.escape | Real UTF-16 identifier escaping, including control/digit/null/surrogate cases |
| EventTarget shells | Synthetic add/remove/dispatch and supported handler slots |
| captureEvents, releaseEvents, close, requestResize | Declared profile no-ops; no window closes or navigation |
| open, find, getSelection, getComputedStyle, fetchLater, when, legacy filesystem operations | Native entry points; explicit synchronous NotSupportedError |
| createImageBitmap, getScreenDetails, queryLocalFonts and three picker methods | Promise rejection with NotSupportedError |
| Remaining subsystem methods/properties | Typed shells with explicit unsupported behavior; no external execution success |

The user explicitly authorizes re-exposing open. It is named, native and
non-constructable; calling it reports unsupported execution. This supersedes the
old absent-open expectation for this candidate, while preserving the earlier
anonymous/constructable binding regression repair. The shared BIND-01 SDK still
has its release-verified absent-open profile.

reportError retains the existing single-Event/false-cancellation onerror model.
Chrome's ErrorEvent interface and five-argument/true-cancellation legacy handler
behavior are not supplied here. Device, graphics/layout, navigation, storage,
Observable and other backend execution remain later work. Namespace submember
exposure is not a claim of complete recursive API implementation.

## Acceptance and artifact identity

- Base source: `63282165d29e46c61b8057846dff24911caf7859`. Final local commit/tree,
  source file hashes and log hashes:
  `data/presence-03-2026-11-15/source-receipt.json`.
- Real V8 on macOS arm64/Python 3.13. **526 focused passed**; the additional
  profile/trace guard group has **28 passed**. Final full suite:
  **2491 passed, 2 skipped**, with no xfailed tests. The three new modules add
  36 cases; the existing open metadata comparison now passes instead of xfail.
- The former absent-open/chrome/trustedTypes guards now enforce the approved
  typed surface. Lifecycle tracing uses normal capacity and checks zero drops;
  the measured load installs 116 internal records plus two eval records. Separate
  bounded-buffer tests still protect drop behavior. No runtime tracing capacity
  or event retention behavior was changed.
- Logs: `data/presence-03-2026-11-15/cp313/focused-accepted.log`,
  `profile-guards-accepted.log` and `full-accepted.log`. Installation is isolated
  under `data/presence-03-2026-11-15/cp313/site`.
- Wheel: `iv8-0.0.1-cp313-cp313-macosx_11_0_arm64.whl`, SHA-256
  `dfeed6b2c4b9d382809d3fdda2871546f06d70f0a7b2d13a93a6cdf76bf34934`.
- Native SHA-256:
  `92f84ac03a7f13bab35c999b704645718a6f1349291c9f706c0fd449bc13a36f`.
- Browser fixtures: `test/fixtures/presence_03_window/`,
  `presence_03_window_errors/` and `presence_03_css_escape/`. All record Chrome
  157.0.8103.51, fresh headless profiles, macOS arm64, loopback-only fixtures,
  background networking disabled and non-loopback DNS blocked. Main capture
  inspects members without invoking system/network backends; error/event and
  CSS captures exercise only bounded local behavior.
- Probe hashes: 49-name
  `11e9caa7641fbb813de9685adf74c67f8f5f6501ce0bde76e2beaf273e5e6dfe`;
  event/error `9bbd8b5db21ff019bd85f9ce89fc41cc57e35f2e31ffdf691efd791d58175ac2`;
  CSS `4721dd4ae5d72d41b7fc1f71c574149d6029665d1a1c8be868333bc2f726deeb`.
  The unchanged inventory probe is
  `e25097de56865629168f2d48fc55fb48f881f62f540198ab7ad1bd83fdba3060`.
- No hosted run/artifact IDs or full release-matrix results exist for this local
  batch. Shared iv8-test remains BIND-01 native SHA-256
  `d29024112409b373a9a714d9dcd9bb532e22716b2164d5d9b4f773c9b94eef89`.

Next BOM presence work is Navigator's 40 missing top-level names and related
subobjects. Further DOM nodes and the excluded Window interface entries remain
deferred under the current user direction.
