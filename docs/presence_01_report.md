# PRES-01 local acceptance report

Date: `2026-11-15`. Status: **implemented and locally accepted; release
verification pending**. This is the first API-family batch under the user's
presence-first strategy. The latest release/deployment-verified SDK remains
BIND-01. Boundary: [presence_01_bom_events.md](presence_01_bom_events.md).

## Implemented behavior

- Navigator appVersion/deviceMemory/userAgentData/userActivation. Python accepts
  flat or nested configuration; metadata is copied and derived only from caller
  UA/platform or explicit defaults. UAData supports toJSON and asynchronous
  getHighEntropyValues, with fresh frozen brand arrays. Activation remains false
  without trusted input.
- Location ancestorOrigins is a stable, empty DOMStringList in the current
  top-level-only model. Length/item/contains work without parent browsing contexts.
- Event gains phase constants, composed/timeStamp/isTrusted/srcElement,
  cancelBubble/returnValue, initEvent and composedPath. UIEvent supplies the common
  UI fields and initialization. Dispatch tracks/clears its actual path, rejects
  dispatching the same event while active, and prevents initEvent from changing
  an active event. Native load/FileReader/invalid events use the canonical Event
  interface too. The base Event call without new now throws without polluting
  Window.
- Window 125, Document 117 and HTML element 110 reference handler attributes have
  native accessors and shared function/null storage. Synthetic dispatch,
  cancellation, stop propagation and cleanup are covered. Screen gains
  isExtended=false and a functional synthetic onchange handler.
- Console gains eight names. countReset changes counters; timeLog measures the
  Page virtual clock and retains extra data. Count/timer labels use the first
  optional argument, with undefined/default labels sharing a store. Repeated
  time does not reset an active timer. Existing time/timeEnd log format remains.

## Prototype and ownership foundation

| Object/member family | Defining prototype or relationship |
|---|---|
| navigator.userAgentData | NavigatorUAData.prototype; methods shared across instances |
| navigator.userActivation | UserActivation.prototype |
| location.ancestorOrigins | DOMStringList.prototype |
| UI event instances | Event.prototype ← UIEvent.prototype ← MouseEvent/KeyboardEvent/FocusEvent.prototype |
| UI event constructors | Event ← UIEvent ← MouseEvent/KeyboardEvent/FocusEvent |
| CustomEvent / MessageEvent | Direct Event inheritance retained |
| New Event methods/getters | Event.prototype; isTrusted is an own accessor |
| Document handlers | Document.prototype |
| HTML handlers | Element.prototype for eight defining handlers; HTMLElement.prototype for the other 102 |
| Window handlers / Console methods | Own accessors / namespace methods, matching their reference creation paths |

NavigatorUAData/UserActivation/DOMStringList have illegal public constructors.
The existing per-context registry stores the actual Event/UIEvent constructor
and prototype pairs, avoiding a second hierarchy when later interfaces query it.
Nested wrappers keep stable identity within a generation and reset on load.
Handler/counter/timer stores are generation-owned; retained V8 handles release
before isolate disposal. Separate Page instances remain isolated.

## Measured presence change

The unchanged Chrome 157 reference and membership probe give these missing-name
counts. The machine-readable delta includes every added name and all 22 targets:
[presence_01_api_delta.json](presence_01_api_delta.json). No previously present
reference names disappeared.

| Reference object | BIND-01 baseline missing | PRES-01 candidate missing |
|---|---:|---:|
| Window, including interface entries | 985 | 874 |
| Location | 1 | 0 |
| Document | 215 | 98 |
| Navigator | 44 | 40 |
| Screen | 3 | 1 |
| Console | 11 | 3 |
| div sample | 288 | 178 |
| form sample | 293 | 183 |
| input sample | 330 | 220 |
| canvas sample | 300 | 190 |
| Event | 12 | 0 |

These are name-presence counts for the fixed reference, not complete browser
implementation scores. DOM samples inherit the same handlers, so their additions
must not be summed as independent APIs. The original
[inventory](browser_api_inventory.json) remains the BIND-01 shared-SDK baseline.

## Local validation and artifact identity

- Base mainline source: `7be41ab5b107aff2aa411b7b139b350e6b70f43d`; PRES-01 is a
  local source change on top. Final local commit/tree and source file digests are
  recorded in `data/presence-01-2026-11-15/source-receipt.json`.
- Platform: macOS arm64, Python 3.13, real V8; the wheel is installed only in
  `data/presence-01-2026-11-15/cp313/site`.
- Focused Event/environment/handler/History/named/binding checks:
  **447 passed, 1 xfailed**. Full suite: **2417 passed, 2 skipped, 1 xfailed**.
  The new presence test module contributes 41 cases. Logs:
  `cp313/focused-accepted.log` and `cp313/full-accepted.log` below the same data
  directory. The full suite includes existing monitoring/lifecycle regressions.
- Wheel: `iv8-0.0.1-cp313-cp313-macosx_11_0_arm64.whl`, SHA-256
  `4c41a4bb6338f47b051fb45325ea7a07449478904cd7f2aa023e30df960be0dc`.
- Native SHA-256:
  `91a19af8abad1b03ca821b9ac1890d663a688d7f1a697d56c73aff14855eefc8`.
- Presence probe SHA-256:
  `e25097de56865629168f2d48fc55fb48f881f62f540198ab7ad1bd83fdba3060`.
- Real-browser fixtures are committed under `test/fixtures/presence_01/`, with
  Google Chrome 157.0.8103.51, execution conditions and matching probe hashes.
  Captures used fresh isolated headless profiles and loopback HTTP only, with
  background networking disabled and non-loopback DNS blocked. The handler
  capture's exact script was recovered from its retained DOM dump; provenance is
  explicit in the fixture rather than attributing it to a later modified file.
- No hosted release run/artifact IDs exist for PRES-01: the 12-lane release
  matrix was not run. Shared iv8-test remains BIND-01, native SHA-256
  `d29024112409b373a9a714d9dcd9bb532e22716b2164d5d9b4f773c9b94eef89`.

## Remaining limits and next coverage batch

Handler ordering retains the prior listener-then-handler model. Non-callable
objects clear handlers to null, while the captured Chrome stores such objects;
legacy onerror signatures and HTML inline-handler compilation are absent. Events
are synthetic; trusted input, device events, shadow-DOM retargeting and complete
Web IDL field/exception fidelity are not supplied. UIEvent sourceCapabilities is
null and view accepts the current Window or null. Console UI/profiler operations
are explicit no-ops; rich object/format rendering and missing-timer warnings are
outside this batch. UAData values are configuration, not a host-device capture.

Next coverage work is DOM logic/reflection: dataset, Attr/NamedNodeMap,
namespaces, root/normalization/document-position helpers, import/adopt and
parsing, selected as a bounded batch with the same shared-prototype ownership.
Networking, rendering, real devices and cross-document navigation remain
separate execution subsystems. No live business acceptance is claimed.
