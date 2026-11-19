# toString builtin reference

Captured on **2026-11-17 (Asia/Shanghai)** with **Google Chrome
157.0.8103.51**, macOS arm64, a fresh temporary headless profile and a loopback
HTTP document. The exact UTC capture time, browser/OS, flags, origin,
`isSecureContext`, user agent, request list and probe SHA-256 are retained in
[chrome.json](chrome.json). Temporary profile paths are normalized to
`<temporary-profile>`. External DNS is blocked and background networking is
disabled; only the local fixture document and its empty favicon were requested.
No business site or transport fixture is used.

[probe.js](probe.js) runs synchronously in the empty loopback document and puts
its JSON result in `pre#to-string-reference`. The recorded command-line flags
show the capture conditions. Regeneration must preserve the probe bytes and
record the actual browser version, time and request list.

## Shape

The two distinct functions are `Function.prototype.toString` and
`Object.prototype.toString`. Both have:

- name `toString`, length zero and native source
  `function toString() { [native code] }`;
- own keys `length`, `name`, in that order, and no own `prototype`;
- no constructor internal method;
- writable, non-enumerable, configurable data descriptors on their defining
  prototypes, and `Function.prototype` as their function prototype;
- readonly, non-enumerable, configurable own `name`/`length` properties.

The fixture checks constructability through `Reflect.construct(dummy, [],
method)` without executing either method as a constructor.

## Result layout

- `result.shapes[api]` contains the function, defining-property descriptor,
  own name/length descriptors and repeated identity.
- `result.receivers[label][api]` contains an `outcome` and `effects` for one of
  46 independently created receivers.
- `result.forms[api][label][form]` repeats selected receivers through `call`,
  `apply`, `bind` and `Reflect.apply`.
- `result.direct` contains detached calls, direct prototype-member calls and
  natural function/object member calls.
- `result.boundCreation` separates creating a bound method from invoking it;
  binding alone does not read a receiver's tag getter.

Successful outcomes store `returned`; ordinary builtin failures store the
native `name` and `message`. A custom tag getter that throws an object, string
or undefined uses `threwSameSentinel: true` and `sentinelType`, preserving the
observed identity without inspecting or coercing that thrown value. `effects`
records tag getter calls, Proxy get keys, prototype/own-key/descriptor traps,
ordinary conversion-property access and function-name getter access.

## Important receiver observations

- Object toString preserves null and undefined as `[object Null]` and
  `[object Undefined]`. Its detached call returns `[object Undefined]`.
- Function toString rejects non-callable receivers with TypeError and does not
  read their properties. It returns the original source of ordinary functions,
  arrows, async/generator functions and classes. Native functions keep native
  source; bound functions and callable Proxies use anonymous native source.
- A revoked **callable** Proxy still produces anonymous native source through
  Function toString, without invoking traps. Revoked non-callable Proxies are
  rejected by Function toString.
- Object toString rejects revoked Proxies before any Proxy trap. This Chrome
  version's message is `Cannot perform 'Object.prototype.toString' on a proxy
  that has been revoked`; the exact engine-version wording is evidence in the
  fixture, not permission to replace an original thrown value during logging.
- On an ordinary live Proxy, Object toString requests only
  `Symbol.toStringTag`, once. No prototype/own-key/descriptor trap is invoked.
- A string tag, including the empty string, overrides the builtin object tag.
  Non-string tag values, including a boxed String or Symbol, are ignored
  without conversion. Custom tag getters are called once by Object toString;
  Function toString does not call them.
- Poisoned ordinary `toString`, `valueOf`, `constructor` or function `name`
  getters are not read by these original builtin operations.

This reference freezes browser shape and behavior. Runtime tests must also
assert actual monitoring CALL events for direct, detached, cached and bound
invocations, completion/exception attachment and user getter re-entry. Matching
returned strings alone does not establish that the calls were monitored.
Logging must not add getter/Proxy calls or replace the original exception.
