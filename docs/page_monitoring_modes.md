# Page load monitoring modes

Authorized `2026-11-16`: simplify recording and reading browser API access logs
through a `Page.load()` mode. This is a Python `Page` convenience layer over the
existing native access trace; it does not extend the M1 kernel.

## Contract

```python
page.load(html, base_url, scripts=None, resources=None, *, mode=None)
```

- `mode="debug"` starts a fresh, unfiltered access-trace session after the native
  environment is installed and before the first HTML or host-supplied script.
  Readable trace lines are automatically printed to standard output after
  `load`, `eval`, and each manual event-loop pump. No manual start/read is needed.
- `mode="prod"` stops and clears access tracing and disables automatic output.
  It does not enable or disable separately configured watch channels, Inspector,
  console output, or asynchronous-error capture.
- `mode=None` (the default) preserves the current behavior: a fresh Page has
  tracing off; an existing debug session continues across reloads; existing
  manually configured tracing remains manual. The existing positional arguments
  and manual trace APIs remain supported.
- Only `None`, `"debug"`, and `"prod"` are accepted. Other types raise `TypeError`;
  other strings raise `ValueError`. All load arguments are validated before
  replacing the document or changing monitoring state.

Automatic output consumes the trace buffer at each Python execution boundary,
so calling `read_access_trace()` afterwards normally returns no pending events.
The existing manual workflow remains available when a debug mode is not active.
Debug uses `sys.maxsize` as its per-call event ceiling rather than the manual
10,000-event default, and drains between calls. Events are buffered while native
JavaScript is running; printing does not call Python from JavaScript or start a
background thread. Very long individual calls can use substantial memory.

Failed user scripts still emit their recorded accesses and propagate their
original exceptions. A per-Page guard spans execution, automatic output and mode
transitions; overlapping execution, trace controls/reads and disposal reject with
`JSContextBusyError` rather than draining another thread's unfinished entries.
Ordinary output/formatting errors do not replace a JavaScript result or exception;
Python interrupts and process-exit signals retain their normal control behavior.
Each Page controls its own mode and buffer. Internal environment installation is
discarded before the debug session starts, rather than printed as user API use.
The user-origin trace layer excludes native setup, synthetic lifecycle event
creation and state bookkeeping before event/capacity accounting. An empty load
prints nothing; user lifecycle handlers and scripts still produce their actual
API entries. See `docs/access_trace_origins.md` for callback/coercion provenance.

## Example

```python
with iv8.Page() as page:
    page.load("<html><body></body></html>", "https://example.test/", mode="debug")
    page.eval("navigator.userAgent")  # prints the captured GET lines
    page.load("<html><body></body></html>", "https://example.test/", mode="prod")
```

## Scope and acceptance

This mode controls when existing monitoring runs and how its log is delivered.
It does not claim complete browser API observation: the `2026-11-16` audit's
prototype, invalid-receiver, symbol/query and returned-object gaps remain open.
Their evidence is in `data/environment-monitoring-2026-11-16/audit/summary.json`.

Acceptance covers HTML/external/host scripts and lifecycle callbacks, subsequent
evals, every event-loop pump, exception preservation, reload transitions,
validation, manual-API compatibility, per-Page isolation and runs exceeding the
manual default capacity. Local results and artifact identities belong in
`docs/page_monitoring_modes_report.md`. Cross-platform release verification and
shared SDK deployment are separate from local implementation and acceptance.
The subsequent user-requested PR #32 release matrix is verified on source
`aa72e50` in all 12 lanes; see `docs/monitoring_release_report.md`.
