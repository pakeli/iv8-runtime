# TAG-MON-01 acceptance report

Date: `2026-11-17`. **Implemented and locally accepted; full release verification pending.**
Boundary: [symbol_to_string_tag_monitoring.md](symbol_to_string_tag_monitoring.md).

## Delivered observation

All existing runtime tag-definition paths now use the established native-data
observer: shared interface/WindowProperties prototypes, HTMLCollection and
HTMLAllCollection prototypes, tagged namespace shells, CSSStyleDeclaration's
prototype and the existing Battery/Quota bootstrap values. Values, data-property
flags, defining owners, ordinary assignment/deletion/redefinition and own-key
order are preserved. This does not add missing tags/APIs or correct unrelated
profile differences. Battery/Quota retain their earlier flat own/nonconfigurable
runtime profile rather than claiming Chrome's inherited prototype profile.

Direct/Reflect.get reads, including objects/prototypes cached before tracing,
record completed GETs on the defining owner. A canonical member
`Symbol.toStringTag` plus optional `member_kind="symbol"` distinguishes the real
well-known Symbol from a string with the same spelling. Text renders
`GET HTMLDocument.prototype[Symbol.toStringTag] -> "HTMLDocument"`. String keys
and other Symbols retain their previous metadata/presentation. CSS's runtime
Proxy delegates this key to the native prototype hook once, and uses a captured
Symbol constant so a user-replaced global Symbol cannot run a diagnostic getter.

Object.prototype.toString delegates under runtime attribution for ordinary native
data lookups. Safe engine/descriptor metadata recognizes native/bound tag
getters/traps needing user attribution without a new JS frame. Ordinary JS
getters/traps naturally add user frames. Registered CSS runtime get traps follow
their engine target for classification, preserving a bound user accessor on that
target. Null/undefined/primitive receivers still reach the original builtin raw;
diagnostic boxing does not replace them. No classifier getter or Proxy trap is
executed and the original tag operation occurs once.

## Verification

New fresh-profile Chrome **157.0.8103.79** evidence covers 31 target views and 27
deduplicated owners, data flags/value, full ordered owner/Symbol keys, inheritance,
overrides/deletion and getter/trap controls. Conditions/probe SHA are in
`test/fixtures/symbol_to_string_tag/`. Browser-derived descriptor/Symbol-order
projections are checked in the new offline module. Full Chrome prototype-member
lists are evidence, not an authorization to add unimplemented APIs.

- **32 new native cases** and **two new pure formatter cases**; new tag/toString/
  text modules pass **167 tests**.
- Covers seven definition paths, direct/Reflect.get, arbitrary reflected receiver,
  cached/inherited/Proxy lookup, metadata without extra getters, original and
  wrapped descriptor materialization, readonly/override/delete, ordinary
  toString quietness, JS/bound/native/CSS callback reentry, original exceptions,
  filters/capacity/stop/reload/Page isolation and debug/prod.
- Independent review and fresh CP311 wheel checks confirm data values/flags/key
  order, single CSS reads, zero diagnostic Proxy traps and fixed bound-CSS
  callback visibility. Installed gate: **32 modules / 823 passed**; full suite:
**3049 passed, 2 skipped**.

The Python **3.11.18 / macOS arm64** wheel is rebuilt against unchanged pinned
**V8 15.0.245.19** and installed into a new isolated environment. Import origin,
linked version, ABI tag/CRC and actual debug output are verified. Build/install/
focused/gate/full logs and source/native/wheel hashes live under ignored
`data/symbol-to-string-tag-2026-11-17/`; see `source-receipt.json`. Required Linux
V8 CI source/run evidence is recorded after push in the receipt and PR metadata.

## Preserved boundaries

A native-data descriptor query may evaluate its native getter. Cached original
GOPD can therefore record GET; existing wrapped queries execute materialization
in runtime scope. User shadows/accessors are not wrapped or forcibly reinstalled.
Value-only redefinition follows V8's native-data behavior. Original flags/results
and user getter counts stay unchanged.

Unknown metadata uses conservative attribution: dynamic handler.get/apply
accessors, Proxy handler metadata or chains beyond the bounded descriptor walk
cannot be resolved without executing user code. For example, a handler.get getter
returning undefined may retain an underlying native tag GET during object
stringification, while its getter still executes exactly once. This extra event
is an explicit remaining noise boundary, preferred to dropping a real native
callback from the same unknown path; there is no universal no-noise claim.

Language-intrinsic tags, missing-Symbol probes, other Symbols/iterator calls,
ordinary user properties and other reflection/array paths remain separate.
The earlier stringifier interposer's Inspector/Proxy-kind and pristine cross-Realm
limits remain unchanged. This updates PR #32; full 12-lane release verification
is pending. No shared SDK replacement, PR merge, version tag, networking or M1
capability is added.

Local wheel SHA-256: `98a21385ef26d55ab61eed2c508b96ccb3f0813e23c7da150f99b778ce80354d`.
