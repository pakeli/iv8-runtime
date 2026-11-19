# ENV-MON-01 — observe existing Page API use

Authorized on `2026-11-15` after direct `window.history` and `window.location`
reads were found to produce no access trace. The user requires observation when
running JavaScript uses the browser environment. This extends WIN-MON-01 from
the 49 newly added Window names to existing Page browser API entry paths.

**Current delivery (`2026-11-16`): implemented candidate with known coverage
gaps.** The expanded hooks and local regression suite are included with the
Page debug/prod modes. Local final acceptance is **482 release-focused passed**
and full macOS arm64/Python 3.13 **2772 passed, 2 skipped**. The strict
"every API use" requirement is not complete: direct generic prototype method
references, invalid-receiver entries, some symbols/queries and returned
structures still have reproduced gaps. See `docs/environment_monitoring_report.md`.
Release verification is tracked separately in `docs/monitoring_release_report.md`.

## Required observation

- Record browser global object, function and constructor references, including
  Window aliases, alongside existing child property and method operations.
- Cover the independently installed native callbacks and runtime bootstrap APIs,
  cached method calls, known namespace constants and method references, and the
  dynamic collection and attribute views already supported by Page.
- Preserve identity, descriptors, prototypes, native function shape, coercions,
  results and original thrown values. Use one Window identity per generation.
  Record synchronous property/call completion and safe summaries without reading
  user error getters, coercing values or invoking Proxy traps for diagnostics.
- Keep the monitoring store and its identity tokens owned by the Page generation.
  Do not add callbacks into Python or move browser behavior into the M1 kernel.

## Meaning of API use

The observation point is the runtime-owned browser API boundary. Accessing
`window.location`, reading `location.href`, obtaining a supported method and
calling a cached method are independent operations. Ordinary local variable
reads, pure ECMAScript internals and arbitrary fields of user data are not
browser API entries. Unsupported global string lookups emit a missing get probe.

Window `window` and `self` references are themselves observable; consequently
`window.location` includes `GET window.window` before `GET window.location`.
Known inherited native data members identify their defining prototype because
the pinned V8 property callback exposes Holder, not the actual receiver. Actual
instance field and method callbacks retain their instance identities.

watch_apis stays call-only. Promise calls record their synchronous Promise
return, without following settlement. Engine-rejected readonly writes and
construction that never enters a callback have no fabricated set/call event.
Native data descriptor queries can additionally execute and record a get.
Replacing a native property with user JavaScript ends that native property path.
This is not an engine-wide tracer for every JavaScript variable/property access.

## Acceptance and delivery

Assert actual events, watch hits and monitoring-on/off behavior in focused
offline regressions, then run the complete local real-V8 suite. Historical
History/named-access and native-binding regressions stay in acceptance.
Record actual counts and source/binary hashes in the report after completion.
Cross-platform release verification and shared SDK deployment are separate.
The subsequent `2026-11-16` user request authorizes a PR and V8-linked release
builds for the current candidate, not a shared SDK replacement or a claim that
the remaining coverage gaps are closed. The subsequent PR #32 build is now
verified in all 12 release lanes on source `aa72e50`; deployed SDK remains BIND-01.
