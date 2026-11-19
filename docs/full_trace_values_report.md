# Complete trace strings and arguments

Date: `2026-11-16`. **Implemented and locally accepted; full release verification pending.**
Boundary: [access_trace_text.md](access_trace_text.md), full-capture update.

The user screenshot shows an 84-byte script.src URL cut to the old 64-byte
preview. Capture now retains complete escaped UTF-8 string content for getters,
setter inputs, call parameters and returns, including strings beyond 4096 bytes.
All argument positions are retained. Automatic debug and manual raw/text reads
share the same captured data; prod remains silent. The script/bootstrap bridge
also preserves complete summaries instead of cutting them a second time at 4096
bytes. Filtered/dropped bridge calls keep their origin token without copying
unretained arguments; internal runtime work stays hidden.

New snapshots include `coverage.string_capture = "full"`. The formatter uses
that marker to distinguish new complete values from saved legacy previews.
This matters for a true 67-byte string ending in `...`, whose old raw encoding
collides with the old truncated preview of another 67-byte value. Missing-marker
legacy reports retain their prior explicit truncation/omission rendering; no old
content is reconstructed. Literal ellipses and escaped controls remain literal,
and each event stays one escaped line.

Objects/arrays/functions remain type-only. No extra getter/toJSON/valueOf or
JS/Python callback is introduced. Native API shape, identities, tracing origin,
event capacity and exception metadata limits are unchanged. Full string/argument
retention consumes memory/output proportional to captured values. Native
allocation/diagnostic failures remain best effort; this is not an object snapshot
or an unbounded exception/stack redesign.

## Verification and source evidence

The pinned V8 15.0.245.19 monolith is reused to rebuild the extension/wheel for
local macOS arm64 / Python 3.13. Build/test/hash receipts are stored under ignored
`data/full-trace-values-2026-11-16/`. Final counts and source/CI evidence are
recorded in `source-receipt.json` and the PR metadata after their actual runs. The same focused module list is kept in all
three release workflows. No SDK installation, version tag or PR merge is included.
Full release lanes have not been rerun for this source; previous fixed-source
release/fallback records remain separate.

## Actual local acceptance

- Four changed test modules: **154 passed**, including **six new cases**.
- Related origin/generic-binding checks: **76 passed**.
- All three workflow lists match: **30 modules / 713 passed** on this wheel.
- Full local suite: **2939 passed, 2 skipped**.
- Cases cover the reported script.src GET/SET, full call/return values, long
  Unicode/escaped content, literal ellipses, all argument positions, type-only
  late Proxy/Error arguments, debug/prod, saved legacy reports and the script
  bridge's previously truncated escaped summary text.
- Independent new-wheel checks confirm an 8192-byte script URL, bridge/native
  complete values, filters/capacity/origin restoration and zero Proxy traps.

Local wheel SHA-256: `d8c0b0b08aedae98151ed2f465ee3fc27504cf0fc872c1f1329f75b70acd6061`.
