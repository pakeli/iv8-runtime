# PRES-01 — first presence-first BOM/event batch

Authorized 2026-11-15: start filling the measured API gaps, preserving an explicit
prototype/inheritance design. This batch prioritizes usable offline foundations
and shared interface registration; detailed Web IDL likeness remains a later
pass. It does not extend the M1 kernel.

## Approved batch

- Navigator: appVersion, deviceMemory, userAgentData and userActivation.
  NavigatorUAData, UserActivation and DOMStringList are native HostObjects with
  per-context shared interface prototypes and illegal public constructors.
  UAData methods live on NavigatorUAData.prototype; values derive from explicit
  caller configuration, never from a hidden host-device query. Brands are fresh
  frozen arrays; high-entropy hints return configured values or explicit empty
  defaults. UserActivation remains inactive without trusted input.
- Location ancestorOrigins: a stable empty DOMStringList for the current
  single-top-level context, with length/item/contains and generation isolation.
- Event: four phase constants, composed/timeStamp/isTrusted/srcElement,
  cancelBubble/returnValue, initEvent/composedPath, plus UIEvent with native
  Event → UIEvent → Mouse/Keyboard/Focus inheritance. New shared properties and
  methods belong to prototypes, except the browser's own isTrusted accessor.
  Existing public own fields stay on their current path. Virtual timestamps use
  the existing Page clock. Event dispatch exposes its real current path and
  clears transient state at completion. Base Event now requires new to protect
  its native brand and avoid global-object pollution.
- IDL event handlers: Window 125, Document 117 and HTML element 110 names from
  the reference inventory, with shared native storage, function/null assignment,
  synthetic dispatch and lifecycle cleanup. Element's eight defining handlers
  remain on Element.prototype; the other 102 on HTMLElement.prototype. Existing
  Window own accessors retain their creation path. No inline HTML on* parsing,
  automatic device events, default actions or special legacy onerror signature.
  Non-callable object handler assignments keep the earlier clear-to-null model,
  a declared difference from the new Chrome reference.
- Console: clear/countReset/dirxml/groupCollapsed/profile/profileEnd/timeLog/
  timeStamp; countReset updates the existing counter store, timeLog uses the
  explicit virtual clock. UI clearing/profiling remain no-op without those
  backends. Console's namespace methods remain own methods as in the reference.
- Screen isExtended is false for the existing single-screen profile; onchange
  uses the shared handler store for synthetic dispatch, with no real screen events.

## Ownership and integration

New interface instances cache within one generation and release V8 handles
before isolate disposal. The existing registry is also bound to the actual
Event/UIEvent constructor/prototype pairs so later lookups cannot manufacture a
second prototype hierarchy. Existing History, named access, binding metadata,
listeners and Page configuration remain protected by focused and local full
regressions. No networking, rendering, real-device permission or trusted input
execution is added.

## Validation cadence

Use focused native tests and one full local real-V8 suite for this development
batch, plus the same presence probe to record added names. Preserve the current
release-verified BIND-01 status; this batch is implemented/local-accepted only
until a future grouped release completes its lanes. Do not trigger a full release
or replace the shared SDK merely to inspect this batch.
