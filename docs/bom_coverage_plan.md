# BOM-first coverage queue

Direction changed by the user on `2026-11-15`: prioritize Window, Location,
Navigator and History, then related BOM objects. Further DOM node work and the
previously suggested Web-data batch move behind this queue. Preserve the locally
accepted PRES-01/PRES-02 implementation and its regressions.

Latest user direction (`2026-11-17`): BOM environment completion takes
priority over further Symbol.iterator monitoring.
BOM-01 completed low-dependency Navigator capabilities and the existing
connection/orientation/viewport event foundations. Its boundary and local
acceptance are tracked in `docs/bom_01_capabilities.md` and
`docs/bom_01_capabilities_report.md`.

BOM-02 is authorized on `2026-11-19`: native related-app inspection and
vibration in the declared offline/inactive profile. Its boundary, local
acceptance and release status are in `docs/bom_02_navigator.md` /
`docs/bom_02_navigator_report.md`. PR #32 is merged as the BOM-01 baseline.

BOM-03 follows on `2026-11-20`: native legacy quota aliases/callbacks plus the
required DOMError dependency. Browser evidence, acceptance and limits are in
`docs/bom_03_legacy_quota.md` / `docs/bom_03_legacy_quota_report.md`.
All 12 selected lanes and wheel artifacts are validated on `2026-11-21`; exact
release evidence is in `docs/bom_03_release_report.md`.

BOM-04 through BOM-08 complete the remaining audited BOM inventory on
`2026-11-26`: secure `Navigator` reaches **70/70** (`38/70` insecure/opaque)
across `BOM-04` (`docs/bom_04_navigator_methods.md`), `BOM-05`
(`docs/bom_05_permission_objects.md`), `BOM-06` (`docs/bom_06_device_objects.md`),
and `BOM-07` (`docs/bom_07_remaining_navigator.md`), and `BOM-08`
(`docs/bom_08_observable_when.md`) adds native `Observable`, `Subscriber`,
`AbortController`, `AbortSignal`, and `EventTarget.prototype.when`, bringing
`navigator.connection` (**9/9**), `screen.orientation` (**9/9**), and
`visualViewport` (**14/14**) to 100% inventory coverage. All 12 selected
Python 3.11–3.14 lanes and 12 wheels are verified on `2026-11-26` in
`docs/bom_08_release_report.md`.

## Current measured baseline

Counts use the fixed Chrome inventory plus PRES-01/PRES-02/PRES-03 and
BOM-01..BOM-08 for the validated candidate (`3cc59b3`), not the shared BIND-01 SDK. PRES-03
evidence and the name/behavior distinction: `docs/presence_03_report.md`.

The subsequent WIN-MON-01 closes scoped monitoring gaps for these entries:
49 global reads, all 22 callable globals and 127 current native shell methods
are checked against actual logs. See `docs/window_monitoring_report.md`. This
does not add API names or backend execution. Its combined release is verified in
`docs/bom_release_report.md`; shared deployment is unchanged.

| Target | Reference names | Present | Missing |
|---|---:|---:|---:|
| Window ordinary properties/methods | 128 | 128 | 0 |
| Window on* handlers | 125 | 125 | 0 |
| Location | 15 | 15 | 0 |
| Navigator (secure context) | 70 | 70 | 0 |
| navigator.connection | 9 | 9 | 0 |
| screen.orientation | 9 | 9 | 0 |
| visualViewport | 14 | 14 | 0 |
| History | 8 | 8 | 0 |

Window's global interface-entry category is separate. The PRES-03 baseline
recorded 106 present and 821 missing out of 927; that is a historical count,
preceding XHR-01 and BOM-01. BOM-01 adds the four explicit global interfaces
listed in `docs/bom_01_api_delta.json`. Those entries include DOM, graphics, device and other Web
interfaces; exposing a constructor on Window does not make that API a BOM
priority. The user explicitly approved all 49 ordinary missing names in PRES-03;
their presence is complete, with backend/fidelity limits recorded separately.
Presence is separate from functioning behavior: Location navigation and History
traversal currently record offline intents; only the approved same-document
History state/URL updates execute.

## Execution order

1. **Window ordinary presence — locally accepted.** PRES-03 adds all 49 names
   with native/type/prototype foundations. Prefixed frames reuse scheduling;
   event/reportError have bounded observable delivery; absent backends fail
   explicitly. The user approves native open presence with unsupported window
   creation. Additional execution/fidelity follows concrete needs rather than
   being claimed by the presence count. The 821 interface entries remain excluded.
2. **Navigator and existing BOM subobjects — BOM-01 locally accepted.** Native
   getGamepads and Scheduling/isInputPending cover the explicit no-device/no-input
   profile; connection/orientation/viewport expose native shared EventTarget
   foundations, with synthetic dispatch, handlers and bounded lock/unlock.
   BOM-02 subsequently adds secure getInstalledRelatedApps and native vibrate
   conversion, leaving 36 Navigator names in the fixed secure inventory and the
   three Observable-dependent when entries; see `docs/bom_02_api_delta.json`.
   BOM-03 then adds webkitTemporaryStorage and restores native shared quota
   behavior, leaving 35 Navigator names in that secure inventory. BOM-04 through
   BOM-07 subsequently complete all 35 remaining Navigator APIs (**70/70** secure,
   **38/70** insecure/opaque) using deterministic fresh-headless-Chrome offline
   profile semantics (no host hardware, OS permission grant, user gesture or
   network I/O). BOM-08 implements native `Observable`, `Subscriber`,
   `AbortController`, `AbortSignal`, and `EventTarget.prototype.when`, closing
   the three remaining `when` entries on `navigator.connection`,
   `screen.orientation`, and `visualViewport`.
3. **Location/History protection and concrete gaps.** Their measured member
   names are already complete. Keep URL components, configured identity,
   same-document state/URL updates and named access in the regression gate.
   Repair observable inconsistencies when evidenced. Executed navigation or
   session-history traversal still require a distinct host-layer contract.
4. **Related BOM families.** Screen/orientation, visualViewport, performance,
   storage and permission surfaces follow according to actual missing names and
   dependencies. Distinguish read/configuration state, synthetic events and
   external execution capabilities in the coverage ledger.
5. **Deferred.** More DOM nodes/collections/traversal/selection/parsing and the
   proposed DOMException/AbortController/Headers/Request/Response/FormData batch
   follow BOM coverage. A small event/error dependency needed by a selected BOM
   API can be included in that API's boundary; it does not restart the DOM queue.

## Development cadence

User-selected build order (`2026-11-17`): local macOS arm64 wheels and installed
Python 3.11–3.14 acceptance come first, then Linux/Windows release builds. Hosted
macOS runner acquisition is supplementary and its infrastructure failures stay
explicit; do not wait for that queue before producing local Mac wheels.

Keep coverage-first delivery and native prototype ownership together. Capture
browser version/conditions, test meaningful read/call/state behavior, protect
existing History/named/DOM/PRES regressions and run one local full suite per
development batch. Group release verification and SDK replacement later. This
original queue update changed priorities only; the subsequent PRES-03 implements
ordinary Window presence as recorded above. Release/deployment status is unchanged.
