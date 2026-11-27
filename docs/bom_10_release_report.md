# BOM-09/BOM-10 release validation

Date: `2026-11-28`.

Status: **All 12 selected release lanes and all 12 wheel artifacts verified.**
Local macOS arm64 Python 3.11–3.14 passed first; hosted Linux x64 and Windows
x64 then passed the same four Python versions on the fixed source below.
Fixed-source V8 CI also passed. Ordinary hosted macOS V8-free CI has separate
runner configuration/queue issues, explicitly recorded below.

## Fixed source and scope

- Implementation commit: `0e5c52addabf4af0511474b6298115a304622678`.
- Final validation source: `d4753090cdc3b4d3a24cf5595bf87702691d02a8`.
- Final source tree: `564aafddd7d8bb2f33042f209b0564ae6fa403af`.
- The final source adds explicit UTF-8 fixture reads for Windows to the
  implementation commit. All final lanes use the final source; old local
  `0e5c52a` receipts are retained separately and do not count toward this matrix.
- All **522** runtime/test/build/workflow inputs are frozen; final source ID is
  `worktree-sha256:a94a8db8b1233a754fa552a15d24d4ac6bc23eab2755495691985ef54aa19a23`.
- V8 stays pinned at **15.0.245.19** /
  `209c9cea0db17d8caf23e9d2c7de08c351609744`; compatible engine caches are reused.
- Native digest backend is the SHA-only static subset of **TF-PSA-Crypto 1.1.2**,
  archive SHA-256
  `799764c15cebd0d8cc161c71f9601009b2992f471828739fb4d72573b87f8298`.

This candidate includes the BOM-09 Performance/Console/Scheduler review fixes,
BOM-10 native digest and bounded virtual directory/handle storage. Runtime
boundaries remain those in `bom_10_digest.md` and `bom_10_storage.md`; writes,
streams, file movement and non-digest key operations are not claimed as working.
The shared SDK remains BIND-01. Manual workflow dispatch does not publish a
version tag or replace a user's installed SDK.

## Selected release matrix

All lanes run the same **46-module / 1375-test gate** before the full offline
suite. Local macOS runs completed before dispatching Linux/Windows.

| Platform / execution | Python | Gate | Full suite | Run / artifact or receipt |
|---|---|---:|---|---|
| macOS arm64 / local | 3.11 | 1375 passed | 3604 passed, 2 skipped | macos/candidate-d475309/cp311/verification.json |
| macOS arm64 / local | 3.12 | 1375 passed | 3604 passed, 2 skipped | macos/candidate-d475309/cp312/verification.json |
| macOS arm64 / local | 3.13 | 1375 passed | 3604 passed, 2 skipped | macos/candidate-d475309/cp313/verification.json |
| macOS arm64 / local | 3.14 | 1375 passed | 3604 passed, 2 skipped | macos/candidate-d475309/cp314/verification.json |
| Linux x64 / hosted | 3.11 | 1375 passed | 3604 passed, 2 skipped | 46039694818 / 15220038505 |
| Linux x64 / hosted | 3.12 | 1375 passed | 3604 passed, 2 skipped | 46039694818 / 15220038505 |
| Linux x64 / hosted | 3.13 | 1375 passed | 3604 passed, 2 skipped | 46039694818 / 15220038505 |
| Linux x64 / hosted | 3.14 | 1375 passed | 3604 passed, 2 skipped | 46039694818 / 15220038505 |
| Windows x64 / hosted | 3.11 | 1375 passed | 3602 passed, 4 skipped | 46039702056 / 15220140474 |
| Windows x64 / hosted | 3.12 | 1375 passed | 3602 passed, 4 skipped | 46039702056 / 15219523752 |
| Windows x64 / hosted | 3.13 | 1375 passed | 3602 passed, 4 skipped | 46039702056 / 15218879221 |
| Windows x64 / hosted | 3.14 | 1375 passed | 3602 passed, 4 skipped | 46039702056 / 15218744079 |

Local evidence root: `data/bom-10-release-2026-11-28/`.
The four local wheels passed CRC, RECORD, ABI/Mach-O, source facade matching,
TF-PSA/V8 license checks, and no-dynamic-crypto linkage checks. Runtime, test,
V8 and crypto input bytes are verified before and after every local lane.

Hosted runs:
[Linux](https://github.com/pakeli/iv8-runtime/actions/runs/46039694818),
[Windows](https://github.com/pakeli/iv8-runtime/actions/runs/46039702056).

## Source CI and artifact audit

Fixed-source [V8 CI](https://github.com/pakeli/iv8-runtime/actions/runs/46037969479)
passes **83 audit**, **1375 gate**, and **3604 passed, 2 skipped** full tests.
Its engine cache hit and exact source/tree are independently checked in
`data/bom-10-release-2026-11-28/v8_ci-artifact-verification.json`.

The ordinary V8-free [CI run](https://github.com/pakeli/iv8-runtime/actions/runs/46037969494)
is tracked separately from the selected release matrix. Its eight Ubuntu/Windows
V8-free jobs passed. Four macOS 14 jobs were cancelled before any step ran: the
check annotations say `The configuration 'macos-14-arm64-us-default' is not supported`.
At evidence collection, four macOS 13 jobs were queued with no assigned runner
or executed step; no further cause is inferred. Sample job-log endpoints returned HTTP 404 because
no job logs were available. Raw metadata/annotations and the status snapshot are
in `data/bom-10-release-2026-11-28/ci/summary.json`. These jobs are not represented
as passing hosted macOS checks; local macOS is the selected release lane.

## Immutable artifact integrity

All **5 hosted ZIPs** are collected under the evidence root's
`{linux,windows}/artifacts/`; all **12 wheels** are under
`data/bom-10-release-2026-11-28/wheels/{macos,linux,windows}/`.
`verification.json`, `wheel-manifest.json` and `SHA256SUMS` record the complete
per-lane/source/API-digest/native/RECORD/ABI/license evidence. ZIP CRC, wheel
CRC, every RECORD hash, interpreter/platform tags, and Mach-O arm64 / ELF x64 /
PE x64 headers pass. GitHub artifact API digests agree with upload logs and
downloaded ZIP bytes. Linux used 63 byte ranges and Windows 45, with no retries.

- linux artifact `15220038505` (`iv8-linux-manylinux2_28-wheels`): 65039802 bytes; `sha256:7b827584ec0c6bd0f756a8597d03e57d5ad0986a820bf65309c421cee8c79c0d`.
- windows artifact `15218744079` (`iv8-windows-wheel-cp3.14`): 11633117 bytes; `sha256:631bcaa4d6947901303c63deadfed386f3f5999e08f567688da73f8ff441ff69`.
- windows artifact `15218879221` (`iv8-windows-wheel-cp3.13`): 11330156 bytes; `sha256:4ea41ef5cab82293740afae4ac9ea68f340921fb4107e61810b6648aa363233d`.
- windows artifact `15219523752` (`iv8-windows-wheel-cp3.12`): 11329924 bytes; `sha256:4dcb1da65a0342f19737635982af6426fd8d65b3bbac6bed1a92289d2b1804e4`.
- windows artifact `15220140474` (`iv8-windows-wheel-cp3.11`): 11329453 bytes; `sha256:b5c97312274ce7f723292f9fb0672c3069c077644a5386114e6d4e88befbb23f`.

Wheel SHA-256 values:

- `d754ae214926649072881c5de13c39d157ec237e812cbafae0f6ba3a80a8b32d` — `iv8-0.0.1-cp311-cp311-macosx_11_0_arm64.whl`
- `d8fa726b24ee04b608af350d231aaa1dd0b454d48c03dbdd51d2a245c487fb83` — `iv8-0.0.1-cp312-cp312-macosx_11_0_arm64.whl`
- `d04828785335dec4693905018d54d8e1e365f84d46e076391055b95858b0b66e` — `iv8-0.0.1-cp313-cp313-macosx_11_0_arm64.whl`
- `6b1062850d374cdcb0fb02b6149560412a242039c72f742b69c0246e7507d5f8` — `iv8-0.0.1-cp314-cp314-macosx_11_0_arm64.whl`
- `e896252f4f1b27603de5918c213777f0facdb7f237c070beb2cac6c0acccf909` — `iv8-0.0.1-cp311-cp311-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl`
- `016a406b3062deaf5fe8febc444e3c9556ba217b80f23a9611e059cf8839ad3f` — `iv8-0.0.1-cp312-cp312-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl`
- `6f6b8ff201a0d440c1f58ff2899b54d2401035e9659d8fe279aed652c5298ec1` — `iv8-0.0.1-cp313-cp313-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl`
- `6abbc10ead74458e1b345cabf49ea38048e70eae8f1cd244e3d9995191068652` — `iv8-0.0.1-cp314-cp314-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl`
- `bb7657c9d71e6503f6f8114978dd3c87cd9bab50d255107a27db32b2f6d22dee` — `iv8-0.0.1-cp311-cp311-win_amd64.whl`
- `5f944183f5fe6bd969f431cbcc2b070c04a886ccd35508a256beff34ab8bd20b` — `iv8-0.0.1-cp312-cp312-win_amd64.whl`
- `21e86e854fc338f7d7590ef5ceb6bf8be84f2830fc35595617fc89865ad47207` — `iv8-0.0.1-cp313-cp313-win_amd64.whl`
- `b569c7691f3a3a26f3d68f81d7c988d40cf728eb0ef9bb69e1979a324c8125d2` — `iv8-0.0.1-cp314-cp314-win_amd64.whl`

Windows wheels include a delvewheel **1.13.3** loader patch and the single
hash-named MSVCP runtime DLL. The auditor verifies the exact official v2 template,
patch location and version; removing only that validated patch restores the
original Python facade bytes after permitted Windows newline normalization.
The DLL hash/name, PE machine/imports and RECORD are checked. Other facade
changes are rejected. Four negative cases (altered bootstrap/facade, extra DLL,
altered DLL) are rejected; this is not an unconditional __init__.py exclusion.

Bundled license inventories are checked against the fixed platform build scripts
and pinned upstream sources. Windows stages the root V8 LICENSE as LICENSE.v8;
Unix stages the separate LICENSE.v8 plus additional component license files.
These actual platform-specific lists are preserved in the manifest. Every lane
also contains the exact TF-PSA-Crypto LICENSE and crypto_pin.cmake.

## Handoff and remaining work

This is the latest selected cross-platform release-verified bounded runtime
candidate. Distinguish it from the shared BIND-01 SDK and from the unavailable
hosted macOS V8-free checks. Version-tag publication, PR merge and shared SDK
replacement were not performed by this validation task. Future work is the
ordinary CI runner configuration, then separately scoped write/stream/move and
key-operation execution; complete name coverage does not expand runtime scope.
