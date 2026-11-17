# Existing Symbol.toStringTag reference

Captured on **2026-11-17 (Asia/Shanghai)** with **Google Chrome
157.0.8103.79**, macOS arm64, in a fresh temporary headless profile. This is the
installed Chrome version at this capture; earlier repository fixtures using
157.0.8103.51 are unchanged historical captures.

[chrome.json](chrome.json) records the exact UTC time, browser/OS, origin,
`isSecureContext`, command-line flags, probe SHA-256 and HTTP request list.
Temporary profile paths are normalized to `<temporary-profile>`. The only
requests were the loopback fixture document and its empty favicon. External DNS
is blocked, background networking is disabled, and no business site is contacted.
The probe creates one local MutationObserver record and an unsent XHR; it does
not use XHR transport, external resources or device services.

[probe.js](probe.js) runs synchronously in an empty loopback document and writes
JSON to `pre#symbol-tag-reference`. Regeneration must preserve the probe bytes
and record the actual execution conditions. The JSON uses inline primitive
records/arrays to avoid repeating large prototype tables across objects while
retaining complete ordered owner keys.

## Captured shape

There are **31 target views and 27 distinct defining owners**. They cover
Window/WindowProperties; Document/HTMLDocument; Element/HTMLDivElement;
EventTarget; Navigator/Screen/Location; XHR/Upload; local/session Storage;
HTMLCollection/HTMLAllCollection; NamedNodeMap/DOMStringMap; URL/URLSearchParams;
Blob/File; Attr/MutationRecord; UAData/UserActivation/DOMStringList; and the CSS
namespace.

- `result.objects[label]` records availability, directly read tag value,
  `Object.prototype.toString` result, own/inherited status, repeated-read
  equality, owner key and prototype depth.
- `result.owners[owner]` records the defining descriptor, direct owner read,
  owner toString, full `Reflect.ownKeys` order and Symbol-only order. Owner
  tables are deduplicated. `WindowProperties` denotes
  `Object.getPrototypeOf(Window.prototype)`, not a public constructor.
- All sampled native tags are **data properties**, readonly, non-enumerable,
  configurable, with a string value. No getter/setter descriptor is exposed.
- CSS owns its namespace tag. The other sampled instances inherit their tag
  from the named interface prototype. `document` reads `HTMLDocument`, while
  reading `Document.prototype` directly yields `Document`.
- HTMLCollection and HTMLAllCollection both order `Symbol.toStringTag` before
  `Symbol.iterator`. The probe handles HTMLAllCollection's falsey/IsHTMLDDA
  behavior using explicit null/undefined checks.

Full owner-key lists are browser evidence. They do not assert that every other
member of each large Chrome prototype is already implemented by the runtime.
This fixture supports monitoring existing runtime-owned tag installations; a
Chrome row does not authorize adding a tag, interface or API absent from the
approved runtime profile. Iterator calls and arbitrary user Symbol properties
are separate from this direct tag-read slice.

## Mutation and receiver behavior

`result.overrides` records fourteen representative actual objects. Reflective
assignment of their readonly native tag returns false; strict assignment throws
TypeError and leaves the value unchanged. Explicit own data redefinition to
`OwnOverride` changes the direct tag and object toString consistently. Deleting
that override resumes inherited lookup, or removes the CSS own namespace tag;
original descriptors are restored after each sample.

`result.inherited` uses a child of Navigator.prototype. Inherited reads return
Navigator, readonly assignment cannot create a child property, and an explicit
child definition/deletion shadows then restores inherited lookup. A
`Reflect.get(owner, tag, proxyReceiver)` on the original data tag executes **no
Proxy receiver trap**: monitoring must not introduce receiver inspection.

`result.getter` replaces the prototype tag with a user accessor temporarily.
The direct reflective read, inherited child read and object toString each call
the getter once, with the recorded actual receiver. Throwing accessors preserve
the same original sentinel on direct reads and toString. A non-string tag is
returned unchanged by direct reads and ignored by object toString; its
`toString` is never called. Original descriptor restoration runs in `finally`.

`result.ownerMutation` captures deletion, data redefinition and restoration of
the defining Navigator.prototype tag. Deletion leaves that tag undefined and
object toString falls back to `[object Object]`; redefining it produces
`[object RedefinedTag]`, and restoration recovers the original native value.
These records freeze observable semantics and getter counts. Runtime tests must
separately assert real GET events for approved native tag paths, rather than
using correct object-toString output as proof of monitoring.
