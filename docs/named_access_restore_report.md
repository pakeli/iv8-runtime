# NAMED-01 — Window/form named access restoration

Date: 2026-11-15. Latest implemented; local acceptance complete, cross-platform
release verification pending. Boundary: `named_access_restore.md`.

## Cause and implementation

The 2026-11-14 installation of `0f0ab4b` replaced the Epub candidate's native
`e2bbe168...` with `7858e2d0...`. The candidate's named-access patch was still
uncommitted against `6191958`, so it was absent from the new History wheel.
The main branch did not delete a merged named-access implementation. Both
packages were labelled 0.0.1; version equality did not establish feature parity.
Installation receipt: ignored `data/iv8-test-install-2026-11-14/install-record.json`.

The named portion of that patch is now ported into the split source tree:

| Original candidate concern | Current implementation |
|---|---|
| HostObject named resolution/query/set/define/enumerate | `include/iv8/host_object.h`, `src/core/host_object.cpp` |
| Form controls and past names | `browser/host/named_properties.cpp`, `node_hosts.h` |
| Window ID/name resolution | `DocumentHost::window_named_property`, `browser/window/window_named_properties.cpp` |
| Dynamic duplicate collections | `live_collection.cpp` routes window-name/form-name queries |
| Past-name ownership invalidation | `mutation_observer.cpp` prunes before the observer fast return |
| WindowProperties prototype/registry | `web_interface.cpp`, installed per Page generation |

Fallible V8 operations added to prototype registration are checked. Generic
named lookup distinguishes absent names from JS exceptions, preserves complete
UTF-8 keys, and avoids inherited setters while enumerating/reading descriptor
fields. Form duplicate collections filter by the nearest owning form, fixing an
inconsistency in the old candidate; it has a dedicated regression.

History implementation and public Python contracts remain unchanged. The old
candidate's independent Reflect/Object/performance/crypto/Event callback
metadata repairs are not included in this named-access slice. No complete
RadioNodeList, cross-tree form owner algorithm or network behavior is added.

## Reference and regression preservation

The original Chrome156 fixture is byte-identical to the private candidate:
`b177c07fcf4382ed62ad7d24e8ab4602e959ea563018341483298f519f9e8272`.
Its recorded browser is 156.0.8078.80 on macOS/HTTP publication origin.
All original 10 test functions / 18 parameterized cases are retained unchanged
at AST level. Eight integration cases cover History coexistence, collision with
the builtin history global, reload, independent Pages, tracing changes and
nested-form collection ownership.

The pre-repair wheel runs the new named suite with **24 failed, 2 passed**.
The repaired wheel runs **26 named + 30 History = 56 passed**. The coexistence
fixture supplies an absolute form action so this port does not silently change
the existing bounded form-action URL contract.

## Local wheel acceptance

macOS arm64, deployment target 11.0, pinned V8 15.0.245.19. Each wheel is
installed into its own ignored target directory, and pytest runs from `/tmp`
against that installed package, not repository Python sources.

| Python | Focused | Full suite | Wheel SHA-256 |
|---|---|---|---|
| 3.11 | 56 passed | 2340 passed, 2 skipped, 1 xfailed | `6856d0cd503bcc44b12ffb4d4c309716c141dcc2de04d6006424b2225dcb9d30` |
| 3.12 | 56 passed | 2340 passed, 2 skipped, 1 xfailed | `bef9c924ba423e7cb5de1af64f4d43f0f9f907227055f501e30bd98b5635625f` |
| 3.13 | 56 passed | 2340 passed, 2 skipped, 1 xfailed | `42055c65f427c1c6a989cb45856e3705bdc562de648037f580776ea023f16fb4` |
| 3.14 | 56 passed | 2340 passed, 2 skipped, 1 xfailed | `3fcb4ba841377adc1e3e7fc67b7b900e5c5827969804bda4ef6f407b6a95d746` |

Every lane collects 2,343 tests. The two skips remain V8-free-only guards and
the expected failure remains the explicitly unsupported Window.open profile.
The complete member/native compilation and independent source reviews found no
missing named-access hunk or generation-lifetime issue.

Local logs/wheels are in ignored `data/named-access-restore-2026-11-15/`; the
negative-control log is `data/named-access-negative-2026-11-15.log`.

## Release acceptance

Linux wheel script and Windows/macOS release workflows now explicitly run
History and named access on installed wheels before the complete suite. Windows
checks each command's exit code so a later passing command cannot hide focused
failure. Existing release matrix scope remains Linux x64, Windows x64 and macOS
arm64, each CPython 3.11–3.14.

Source/tree, CI runs and artifact IDs will be recorded after those 12 lanes
finish. Until then, do not label NAMED-01 release-verified or replace the latest
verified JET-01 receipt with local-only counts. No shared user SDK replacement
or live business request is performed by this acceptance.
