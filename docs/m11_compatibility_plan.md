# M11 Compatibility Plan

Last reviewed: `2026-08-06`

> **Scope note (`2026-10-27`).** This is the original M11 planning document. The
> compatibility line has since landed and verified `M11-1` through `M11-4`; for the
> current milestone and surface, read `docs/current_status.md` and
> `docs/runtime_surface.md`. The stage judgment below records the state at the time
> of planning.

## 1. Stage judgment

`iv8-runtime` has completed the original M1 embedding kernel and has already grown
through M2-M10 into a layered hosted runtime:

- M1 core: `JSContext`, evaluation, conversion, structured errors, lifecycle,
  isolation;
- M2-M8 first-layer hosted runtime: `Page`, DOM-like tree, scripts, lifecycle,
  forms, URL/text/base64 helpers;
- M9-M10 second-layer monitoring: Inspector attach base and five watch channels.

The latest landed implementation milestone is `M10-2`. The next step should not be
an ad hoc `M10` extension. It should be the first compatibility-first planning stage
for the broader product goal.

## 2. Product goal after M10-2

The project's main goal is now stated as:

> Gradually reproduce the community-edition capability surface of
> `HanZzzzz000/iv8`, then extend beyond it where the repository's own goals justify
> doing so.

This changes the post-M10 framing:

- the primary question is no longer "what small runtime feature can land next?";
- the primary question is "what compatibility gap should be closed next, in what
  order, without collapsing the repository's layered architecture?"

## 3. Target framing

### Community edition is the baseline target

Planning should align against the public/community surface of the target project,
not against private or pro-only capability claims.

### Compatibility does not mean source or API cloning

This repository is not required to copy implementation structure or replicate every
public symbol verbatim. The goal is capability reproduction first, with API-shape
alignment evaluated deliberately rather than assumed.

### Architecture constraints still apply

Compatibility work must preserve the current repository's hard architectural rules:

- one isolate per `JSContext`;
- process-wide V8 initialization separated from context-owned resources;
- thin binding layer;
- explicit milestone boundaries;
- no uncontrolled scope expansion.

## 4. Known overlap with the target direction

The repository already covers substantial foundational ground that is directly
useful for compatibility work:

- isolated JavaScript execution kernel;
- hosted `Page` abstraction;
- partial browser-like globals (`window`, `document`, `navigator`, `location`);
- HTML/script bootstrapping model;
- event target and lifecycle subset;
- DOM tree/query/mutation subset;
- form state/value/default/reset subset;
- URL/text/base64 helpers;
- Inspector attach base;
- watch/monitoring channels.

This means M11 should not start from zero. It should begin with a disciplined gap
survey and then choose the first compatibility slice.

## 5. Gap categories

The exact target-project matrix still needs a focused, source-backed pass, but the
gap categories are already clear enough for planning.

### Likely low-to-medium gap areas

- public API naming/shape differences between this repository and the target
  project;
- missing browser-like convenience methods or aliases around already-existing
  internals;
- broader monitoring coverage building on the existing M9-M10 infrastructure.

### Likely medium gap areas

- more complete DOM/document collections and element behavior;
- broader lifecycle and load semantics;
- broader event semantics and compatibility edge cases;
- closer `page.load`-style behavior and resource integration expectations.

### Likely high-gap or high-risk areas

- full networking model (`fetch`, XHR, WebSocket, navigation, resource loading);
- storage, cookie, and security-policy semantics beyond the current hosted subset;
- richer browser emulation and anti-fingerprinting surfaces;
- compatibility expectations that assume a different public top-level API model.

These categories are planning guidance only. The precise per-feature matrix should
be confirmed as the first execution slice under M11.

## 6. Public API shape question

One of the most important M11 planning questions is whether compatibility should be
pursued primarily through behavior, through API-shape alignment, or both.

### Current repository shape

The current public shape is layered around:

- `JSContext` as the core execution primitive;
- `Page` as the hosted runtime container;
- milestone-by-milestone growth of `Page` methods and JS-visible globals.

### Compatibility risk

If the target project presents a more integrated `page.load` / page-centric user
story or exposes different top-level entry points, then reproducing capability alone
may still leave meaningful migration friction.

### M11 planning stance

M11 should not force an immediate public API rewrite.

Instead, M11 should:

1. inventory the target-facing usage patterns that matter most;
2. identify whether the gap is behavioral, naming-related, or structural;
3. prefer additive or adapter-style convergence before destructive surface changes.

Any proposal to reshape `Page`, add aliases, or freeze a new contract should be a
deliberate sub-decision, not an accidental by-product of a feature patch.

## 7. Decision: what M11 means

M11 is defined as:

> the first compatibility-first milestone line after M10, focused on surveying,
> ranking, and beginning to close the gap between `iv8-runtime` and the target
> community `iv8` capability surface.

M11 therefore begins with planning and gap analysis, not immediate runtime growth.

## 8. Scope of M11

M11 should include:

- a compatibility-oriented framing of the product goal;
- a target-surface gap survey suitable for task slicing;
- an ordered compatibility backlog;
- selection of the first implementation slice (`M11-1`);
- explicit acceptance criteria for that first slice.

## 9. Non-goals of M11

M11 should not, by itself:

- promise full parity with every capability in the target project;
- force a rewrite of the existing layered architecture;
- silently expand into full networking or navigation without explicit approval;
- collapse community-edition and pro-edition goals into one plan;
- freeze a universal `Page` contract without a separate decision.

## 10. Proposed phased rollout

### M11-0: compatibility planning and survey

Deliverables:

- this plan;
- a focused compatibility survey document or matrix;
- a ranked shortlist of candidate first slices.

### M11-1: first compatibility slice

Choose one bounded, testable slice with clear user-visible value. Candidate shapes:

- API-shape convergence around existing `Page` capabilities;
- a missing DOM/document compatibility cluster;
- a load/lifecycle compatibility cluster;
- a monitoring/inspection compatibility cluster if the target project's user story
  depends on it heavily.

### M11-2 and later

Continue through ranked compatibility slices, reassessing after each milestone
rather than locking the entire roadmap up front.

## 11. Test and acceptance strategy

### Planning acceptance

The M11 planning stage is complete when:

- the repository explicitly states that post-M10 work is compatibility-first;
- the next implementation slice is chosen from a documented gap matrix, not by ad
  hoc preference;
- open design questions are recorded instead of buried in chat history.

### Implementation acceptance for the future M11-1 slice

The first code slice under M11 should have:

- a narrow feature boundary;
- focused pytest coverage;
- explicit non-goals;
- no unrelated runtime changes;
- status/doc updates after landing.

## 12. Risks

- **Target drift risk**: the target project may evolve, so compatibility planning
  must periodically re-check the public surface before major work.
- **Surface-shape risk**: behavior parity may be easier than API-shape parity.
- **Scope creep risk**: "compatibility" can be abused to justify unbounded browser
  emulation.
- **Architecture risk**: compatibility shortcuts could damage the current layered
  model if not reviewed carefully.
- **Validation risk**: a stale local skeleton install can obscure whether runtime
  progress is real.

## 13. Open decisions after this plan

- Which concrete feature cluster becomes `M11-1`?
- Should a dedicated compatibility matrix document be maintained long-term?
- Should public API-shape convergence become an explicit tracked workstream?
- At what point, if any, should Windows release parity re-enter the main plan?

## 14. Recommended immediate next step

After this plan is accepted, the next task should be:

> produce a small, source-backed compatibility survey and choose the first
> implementation slice as `M11-1`.

That is the correct handoff point for Claude to begin code-adjacent work later.
