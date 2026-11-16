# Environment monitoring candidate report

Date: `2026-11-16`. **Locally tested and release-verified candidate; strict API coverage incomplete.**

Boundary: [environment_monitoring.md](environment_monitoring.md). The delivered
version also includes [Page debug/prod modes](page_monitoring_modes.md).

## Implemented observation paths

The native expansion records existing Window root/function/constructor reads,
cached native and bootstrap API calls, selected namespace constants/references,
independently installed constructors/events/value objects, and dynamic
class-list, attribute and collection entries. A generation-owned store gives
independent native API objects stable trace identities; Window entry paths
share the generation's Window identity. Completion helpers preserve original
values, coercions and thrown identities without executing user getters for
diagnostic formatting. Existing native functions retain their installed shapes.

Nine new ENV-MON modules provide **163 tests**; the separate History parent fix
adds five cases, and the Page monitoring-mode slice adds 57. Updated existing
tests assert the new parent/reference entries rather than treating them as
unexpected noise. The combined installed-wheel release gate includes 23
modules and passes **482 tests** locally. Full real-V8 macOS arm64/Python 3.13
passes **2772 passed, 2 skipped**. The local candidate wheel and source/log
hashes are recorded in `data/page-monitoring-modes-2026-11-16/source-receipt.json`;
the release gate log is `data/monitoring-release-2026-11-16/gate.log`.

## Outstanding strict-coverage failures

Passing those tests does not establish the user's requirement that every
runtime-owned browser API use is recorded. The independent `2026-11-16` audit
cached each target before enabling tracing, ran operations individually without
filters, and compared actual entries with valid-operation controls. It found:

| Path | Observed gap |
|---|---|
| Generic interface prototype methods | Closed for the shared HostObject path by `docs/generic_binding_monitoring_report.md`; direct/inherited defining-prototype references now emit GET |
| Invalid native receivers | Closed for generic method/IDL accessor callbacks by `docs/generic_binding_monitoring_report.md`; independent callback families remain separate |
| Returned browser structures | MutationRecord nine own data fields are now covered by `docs/mutation_record_monitoring_report.md`; independent array length/index, other arrays and value dictionaries remain open |
| Symbol and structural queries | Some `Symbol.toStringTag` / `Symbol.iterator`, `in`, cached query wrappers and non-HostObject query targets are silent |
| Readonly write attempts | Engine-ignored/rejected attempts can have no set event |
| Manual trace retention | The manual 10,000-event default drops later events; native installation can consume that budget if tracing was enabled before load |

Debug mode starts after native installation, drains automatically between
execution calls and uses `sys.maxsize` as its ceiling. This removes that manual
default from the automatic path, while the other API coverage failures remain
open. The subsequent user-origin repair excludes synthetic lifecycle/state entries; see `docs/access_trace_origins_report.md`. FileReader's
unimplemented `onload` handler is a missing API/probe issue rather than evidence
of a missing hook on a working handler.

Reproduction scripts, exact events, source/binary hashes and positive controls:
`data/environment-monitoring-2026-11-16/audit/summary.json`. This report preserves
the gaps as unresolved work; it does not reinterpret them as acceptance waivers.

## Release and deployment

The user separately authorizes submitting this tested candidate and building
the V8-linked wheels. Source, runs and all 12 successful lane results are recorded in
[monitoring_release_report.md](monitoring_release_report.md). The V8 pin and
M1 kernel remain unchanged. Shared SDK installation, version tags and live
business/server acceptance are not part of this release-build request.
