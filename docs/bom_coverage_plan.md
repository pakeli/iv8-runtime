# BOM-first coverage queue

Direction changed by the user on `2026-11-15`: prioritize Window, Location,
Navigator and History, then related BOM objects. Further DOM node work and the
previously suggested Web-data batch move behind this queue. Preserve the locally
accepted PRES-01/PRES-02 implementation and its regressions.

## Current measured baseline

Counts use the fixed Chrome 157 inventory plus the PRES-01/PRES-02/PRES-03
deltas for the locally accepted candidate, not the shared BIND-01 SDK. PRES-03
evidence and the name/behavior distinction: `docs/presence_03_report.md`.

The subsequent WIN-MON-01 closes scoped monitoring gaps for these entries:
49 global reads, all 22 callable globals and 127 current native shell methods
are checked against actual logs. See `docs/window_monitoring_report.md`. This
does not add API names or backend execution, and release verification is pending.

| Target | Reference names | Present | Missing |
|---|---:|---:|---:|
| Window ordinary properties/methods | 128 | 128 | 0 |
| Window on* handlers | 125 | 125 | 0 |
| Location | 15 | 15 | 0 |
| Navigator | 70 | 30 | 40 |
| History | 8 | 8 | 0 |

Window's global interface-entry category is separate: 106 present and 821
missing out of 927. Those entries include DOM, graphics, device and other Web
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
2. **Next: Navigator and existing BOM subobjects.** Select low-dependency capability
   inspection first, then connection/orientation/viewport event surfaces and
   configuration-backed metadata. Candidate families include getGamepads and
   scheduling; choose the concrete API set after checking real-browser profile
   behavior. Empty device lists/inactive input must be explicit profile states,
   not claims of device execution. Connection listener/dispatch/onchange gaps can
   reuse EventTarget; its when method depends on Observable and stays deferred.
   Clipboard, geolocation, media/devices, service workers, GPU/XR and
   other execution systems are separate bounded families, not arbitrary empty
   success objects.
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

Keep coverage-first delivery and native prototype ownership together. Capture
browser version/conditions, test meaningful read/call/state behavior, protect
existing History/named/DOM/PRES regressions and run one local full suite per
development batch. Group release verification and SDK replacement later. This
original queue update changed priorities only; the subsequent PRES-03 implements
ordinary Window presence as recorded above. Release/deployment status is unchanged.
