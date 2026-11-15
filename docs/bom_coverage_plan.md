# BOM-first coverage queue

Direction changed by the user on `2026-11-15`: prioritize Window, Location,
Navigator and History, then related BOM objects. Further DOM node work and the
previously suggested Web-data batch move behind this queue. Preserve the locally
accepted PRES-01/PRES-02 implementation and its regressions.

## Current measured baseline

Counts use the fixed Chrome 157 inventory plus the PRES-01/PRES-02 deltas, for
the local candidate based on source `4ae7250`, not the shared BIND-01 SDK.

| Target | Reference names | Present | Missing |
|---|---:|---:|---:|
| Window ordinary properties/methods | 128 | 79 | 49 |
| Window on* handlers | 125 | 125 | 0 |
| Location | 15 | 15 | 0 |
| Navigator | 70 | 30 | 40 |
| History | 8 | 8 | 0 |

Window's global interface-entry category is separate: 106 present and 821
missing out of 927. Those entries include DOM, graphics, device and other Web
interfaces; exposing a constructor on Window does not make that API a BOM
priority. The 49 ordinary missing names also need dependency classification.
Presence is separate from functioning behavior: Location navigation and History
traversal currently record offline intents; only the approved same-document
History state/URL updates execute.

## Execution order

1. **Window common foundations.** Start with missing operations that can reuse
   current scheduling, event and error ownership: animation-frame aliases,
   reportError and legacy captureEvents/releaseEvents, after browser comparison.
   Review remaining Window flags/metadata against a real browser. Implement a
   usable bounded contract for each selected member, with native methods on the
   browser-observed defining owner: retain Window own globals and shared
   prototypes for the applicable interfaces. reportError needs observable Window
   error/handler/cancellation behavior; the opt-in async-error diagnostic buffer
   alone is insufficient. Window close/open and new browsing contexts need
   their own behavior boundary; do not restore the previously withdrawn open
   stub just to reduce a name count.
2. **Navigator and existing BOM subobjects.** Select low-dependency capability
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
queue update changes priorities only; it implements no additional APIs and
does not change the latest local acceptance or release/deployment status.
