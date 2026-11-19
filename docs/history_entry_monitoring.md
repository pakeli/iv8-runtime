# History global-entry monitoring fix

Date: `2026-11-15`. The user reports that start_access_trace followed by
eval("window.history"), stop and text output produces no event. Reproduced on
the combined release implementation: the global get history callback returned
the History wrapper without entering tracing, while length/state/method accesses
already passed through the History HostObject tracing paths.

The scoped fix adds a Window/history/get event with safe value or synchronous
exception completion to the existing native getter. It preserves getter shape,
receiver rules, wrapper identity, History state/URL behavior and lifecycle.
watch_apis remains call-only; reading history is not a method hit. The entry
event is object_type=window, while History child members remain type=history.

Five regression cases cover the exact stopped/text reproduction, parent/child
attribution, filtering, native descriptor/receiver behavior and generation/stop.
The pre-fix candidate fails those five cases. Local macOS arm64/Python 3.13 acceptance passes **94 focused** and full
**2552 passed, 2 skipped**; the prior combined release remains
the latest release-verified package until this fix is separately released.

Scope is this reported History entry. Other unmapped pre-existing root bindings
such as navigator/document/screen and the Location parent accessors remain
separate paths; this fix must not be described as V8-wide/global tracing.

## Local artifact and reproduction

The fixed trace text is `GET window.history -> <HostObject>`. The Window parent
get is separate from the `history.length` child get, and stopping before reading
retains the recorded event. Metadata/negative/focused/full logs are in ignored
`data/history-entry-monitoring-2026-11-15/`. The isolated Python 3.13 wheel has
SHA-256 `7824358da8d46ef2fd1243e8bf7705dc1f667dcd246105c43350f5c0970054cb`.
Source commit/tree and file/log hashes are recorded in `source-receipt.json`
below that directory. No shared SDK install, remote push or release matrix is
performed in this local bug-fix follow-up.
