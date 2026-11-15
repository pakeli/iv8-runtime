# BIND-01 — remaining candidate native binding restoration

Authorized 2026-11-15: audit the Epub candidate's remaining native binding
metadata fixes against the current mainline and its saved browser references,
restore confirmed gaps, preserve History/named-access acceptance, build a unified
package, then back up and update the shared iv8-test SDK and verify it offline.

## Boundary

- Inspect only the existing Reflect/Object monitoring installation paths,
  performance/Crypto methods and six Event-family constructors in the candidate
  diff. Do not assume a candidate change is still missing in current mainline.
- Use the existing named non-constructable native factory for browser-native
  operations, with reference names/arities/no own prototype. Event constructors
  retain native source, constructability, own prototype and inheritance, with
  reference arity one.
- Preserve captured original delegates, observable results/errors/receivers,
  monitoring on/off and load/reset semantics. Keep named access and bounded
  History, GIL/isolate ownership, queues and random behavior unchanged.
- Preserve the candidate's original Event fixture and regression cases; retain
  its documented Chrome156/macOS/HTTP provenance rather than inventing a new
  capture. Add behavior and combined-feature regressions, and include binding
  checks in every installed-wheel release acceptance lane.
- Complete local and 12-lane release acceptance before updating shared SDK.
  Back up installed package/metadata and identify old/new source, wheel and
  native hashes. Installation acceptance must verify all three feature groups.
- Reuse local cached EPUB resources only. No live HTTP, Cookie upload, new
  session tests, new Web APIs or full-browser/server-pass claims are authorized
  by this slice.

Implementation, all 12 release lanes and the backed-up shared SDK update are
verified. Exact source/tree, per-platform counts, installed identities and
offline acceptance are recorded in `docs/native_binding_restore_report.md`.
