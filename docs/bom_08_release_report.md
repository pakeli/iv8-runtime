# BOM-04..BOM-08 release validation

Date: `2026-11-26`. **All 12 selected lanes and all 12 wheels verified.**
Four local macOS arm64 lanes passed first on `2026-11-26`; the subsequent hosted
Linux x64 and Windows x64 matrices pass on the exact same runtime source on
their first attempts.

## Fixed source

- Runtime/build source: `3cc59b336676b5ce430f668646861eca5e359f8a`.
- Runtime tree: `0fe758ea9a74679774280988b22597b4713a1c8e`.
- All 486 frozen code/build/test/checkout-policy inputs match this source
  (`candidate_source_id`: `worktree-sha256:d253dd4442bb73eabc76e33881b06ae107c515ece7efb29357807a80f14514f8`).
- V8 remains **15.0.245.19** /
  `209c9cea0db17d8caf23e9d2c7de08c351609744`. Matching monolith caches are
  reused across local macOS, hosted Linux, hosted Windows, and V8 CI. Native
  extensions/wheels are rebuilt; V8 itself is not rebuilt.
- Stacked commits above `codex/bom-03-quota` (`93ae7a3`):
  - `4a68fbf` (`BOM-04`): secure `Navigator.prototype` methods (`35/70 -> 45/70`),
    constructible `DOMException`, ClearKey EME, `File` brand.
  - `7c88322` (`BOM-05`): `Navigator` permission, lock, session, and
    media-capability objects (`45/70 -> 53/70` secure, `33/70 -> 36/70` insecure).
  - `538a31d` (`BOM-06`): secure `Navigator` device, WebXR, WebGPU, and
    Presentation objects (`53/70 -> 60/70` secure).
  - `3e8c198` (`BOM-07`): remaining `Navigator` members and `DOMRect`/`DOMRectReadOnly`
    geometry (`60/70 -> 70/70` secure, `36/70 -> 38/70` insecure).
  - `3cc59b3` (`BOM-08`): native `Observable`, `Subscriber`, `AbortController`,
    `AbortSignal`, and `EventTarget.prototype.when` (`navigator.connection` `9/9`,
    `screen.orientation` `9/9`, `visualViewport` `14/14`, inherited `window.when`).

## Verified lanes

Every installed wheel passes the same **42-module / 1257-test gate**, followed
by the full suite outside the source tree. Windows has the existing two extra
Unix-only skips. Local macOS receipts and collected wheel artifacts are under
`data/bom-08-2026-11-26/`.

| Platform / execution | Python | Gate | Full suite | Run / artifact or local receipt |
|---|---|---:|---|---|
| macOS arm64 / local first | 3.11 | 1257 passed | 3483 passed, 2 skipped | macos/cp311/verification.json |
| macOS arm64 / local first | 3.12 | 1257 passed | 3483 passed, 2 skipped | macos/cp312/verification.json |
| macOS arm64 / local first | 3.13 | 1257 passed | 3483 passed, 2 skipped | macos/cp313/verification.json |
| macOS arm64 / local first | 3.14 | 1257 passed | 3483 passed, 2 skipped | macos/cp314/verification.json |
| Linux x64 / hosted | 3.11 | 1257 passed | 3483 passed, 2 skipped | 45835448535 / 15125369259 |
| Linux x64 / hosted | 3.12 | 1257 passed | 3483 passed, 2 skipped | same four-wheel archive |
| Linux x64 / hosted | 3.13 | 1257 passed | 3483 passed, 2 skipped | same four-wheel archive |
| Linux x64 / hosted | 3.14 | 1257 passed | 3483 passed, 2 skipped | same four-wheel archive |
| Windows x64 / hosted | 3.11 | 1257 passed | 3481 passed, 4 skipped | 45835458516 / 15125461652 |
| Windows x64 / hosted | 3.12 | 1257 passed | 3481 passed, 4 skipped | 45835458516 / 15125761278 |
| Windows x64 / hosted | 3.13 | 1257 passed | 3481 passed, 4 skipped | 45835458516 / 15126235377 |
| Windows x64 / hosted | 3.14 | 1257 passed | 3481 passed, 4 skipped | 45835458516 / 15125606444 |

Hosted runs: [Linux `45835448535`](https://github.com/pakeli/iv8-runtime/actions/runs/45835448535),
[Windows `45835458516`](https://github.com/pakeli/iv8-runtime/actions/runs/45835458516).
Both pass on their first attempts. Linux's checkout and all five Windows
checkouts match `3cc59b336676b5ce430f668646861eca5e359f8a`; matching V8 cache
keys are verified in logs. Engine-build/cache-save and tag-only publish steps
are skipped. Hosted macOS was not dispatched per the macOS-local-first build
rule; macOS's local execution and absence of hosted artifact IDs remain explicit.

## Source CI

Source [push V8 CI `45835443187`](https://github.com/pakeli/iv8-runtime/actions/runs/45835443187)
passes **83 audit checks**, **1257 gate tests**, and full **3483 passed,
2 skipped** against `3cc59b336676b5ce430f668646861eca5e359f8a` / tree
`0fe758ea9a74679774280988b22597b4713a1c8e` (`data/bom-08-2026-11-26/v8_ci/verification.json`).

## Artifacts and integrity

All **five hosted ZIP archives** and **12 wheels** are collected under
`data/bom-08-2026-11-26/wheels/{macos,linux,windows}/` with combined
`wheel-manifest.json` and `SHA256SUMS`:

- `0bbfc996bfac7a8a2b50de0a5e849d5aa32ce22c4b940a584b75614d9c615bf1` — `iv8-0.0.1-cp311-cp311-macosx_11_0_arm64.whl`
- `b282bcf029dfdd77fc231aad70c7b79dcd225c1406bc4d9164abd1e3f52d53f5` — `iv8-0.0.1-cp312-cp312-macosx_11_0_arm64.whl`
- `bde06ecb1338e9cb50a669b17af8c1fe093ed4a25fc781fb4df850af6d1ed71d` — `iv8-0.0.1-cp313-cp313-macosx_11_0_arm64.whl`
- `1747d577f684c6c77bfd1b62f76c1b67388aa3f7b3dca5bcb7d2da662204065c` — `iv8-0.0.1-cp314-cp314-macosx_11_0_arm64.whl`
- `f6ecee7629feb18c378192d8c6148f6486bebb1c584dfeaab9a8581d0c084879` — `iv8-0.0.1-cp311-cp311-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl`
- `17ec45ba424af47fcb0b5bdc1871cc72b95a9c64ab08b3066d76e870b197e51b` — `iv8-0.0.1-cp312-cp312-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl`
- `7af217529fcc6ab9db6f650c61d76d2702eaee10ec646e1b7238b07ebcbc1ca7` — `iv8-0.0.1-cp313-cp313-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl`
- `c5b96db28c6f7dcfcc78e6f2a23c354ca9accb084399199de5e0916b533ad622` — `iv8-0.0.1-cp314-cp314-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl`
- `3c93663181652535131f57618ee50a48b5233dfa9c007cb77dcb6b0069b0c3b4` — `iv8-0.0.1-cp311-cp311-win_amd64.whl`
- `d9f3b82fc4bd2d980504738f395c738bb43f70b4c4192e458b0579d95fb77c94` — `iv8-0.0.1-cp312-cp312-win_amd64.whl`
- `8d558aa032bc9cb09e899f0339cb26cb5e1c28877857e8324f9e84a3121ee3ff` — `iv8-0.0.1-cp313-cp313-win_amd64.whl`
- `bc94ae66fcfa636a88762f18814c3a41ae16ece261e7731faa5f94048bb7c5b3` — `iv8-0.0.1-cp314-cp314-win_amd64.whl`

Linux's 64,539,244-byte artifact ZIP SHA-256 is
`c4693f4bb8073a94181bdc092abb09e148790c4cd774e6523df807d5cfc564bc` (62 ranges,
0 retries); Windows used 44 ranges with 0 retries across its four ZIPs. All
ZIP/wheel CRC, WHEEL tags, RECORD hashes, and Mach-O arm64 / ELF x86_64 / PE
x86_64 binary headers pass. Follow-up documentation commits do not change
runtime, test, build, workflow, or checkout-policy bytes.

## Handoff

`BOM-04` through `BOM-08` complete all 35 remaining `Navigator` members
(`70/70` secure, `38/70` insecure/opaque) and all three remaining BOM subobject
`when` members (`navigator.connection` `9/9`, `screen.orientation` `9/9`,
`visualViewport` `14/14`) against `docs/browser_api_inventory.json`, adding 32
new pytest cases. Each batch was independently reviewed by OpenAI Codex CLI and
verified against headless Chrome `158.0.8127.88`. Shared SDK deployment remains
`BIND-01`; no shared environment installation, PR merge, or version tag
publication is performed by this validation.
