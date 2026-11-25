(async () => {
  'use strict';
  const out = {secure: isSecureContext};
  const functionShape = fn => {
    let construct;
    try { Reflect.construct(fn, []); construct = 'constructed'; } catch (error) { construct = error.name; }
    return {name: fn.name, length: fn.length, source: Function.prototype.toString.call(fn), prototype: Object.hasOwn(fn, 'prototype'), construct};
  };
  const descriptor = (object, key) => {
    const item = Object.getOwnPropertyDescriptor(object, key); if (!item) return null;
    return {enumerable: item.enumerable, configurable: item.configurable,
      ...('value' in item ? {kind: 'data', writable: item.writable, value: typeof item.value === 'function' ? functionShape(item.value) : (typeof item.value === 'object' && item.value !== null ? '[object]' : item.value)}
        : {kind: 'accessor', get: item.get ? functionShape(item.get) : null, set: item.set ? functionShape(item.set) : null})};
  };
  const proto = DOMException.prototype;
  out.global = descriptor(globalThis, 'DOMException');
  out.constructor = functionShape(DOMException);
  out.constructorParent = Object.getPrototypeOf(DOMException) === Function.prototype ? 'Function.prototype' : String(Object.getPrototypeOf(DOMException).name);
  out.constructorKeys = Reflect.ownKeys(DOMException).map(String);
  out.constructorMembers = Object.fromEntries(Reflect.ownKeys(DOMException).map(key => [String(key), descriptor(DOMException, key)]));
  out.prototypeKeys = Reflect.ownKeys(proto).map(String);
  out.prototypeParentIsError = Object.getPrototypeOf(proto) === Error.prototype;
  out.prototypeMembers = Object.fromEntries(Reflect.ownKeys(proto).map(key => [String(key), descriptor(proto, key)]));
  const summarize = value => ({name: value.name, message: value.message, code: value.code, ownKeys: Reflect.ownKeys(value).map(String),
    tag: Object.prototype.toString.call(value), instanceOfError: value instanceof Error, string: String(value),
    stackType: typeof value.stack, stackOwn: descriptor(value, 'stack') ? descriptor(value, 'stack').kind : null});
  const cases = {
    none: 'new DOMException()', message: "new DOMException('m')", named: "new DOMException('m','NotAllowedError')",
    custom: "new DOMException('m','CustomName')", undefined_args: 'new DOMException(undefined, undefined)',
    null_args: 'new DOMException(null, null)', number_args: 'new DOMException(1, 2)',
    legacy: "new DOMException('m','IndexSizeError')", data_clone: "new DOMException('m','DataCloneError')",
    quota: "new DOMException('m','QuotaExceededError')", security: "new DOMException('m','SecurityError')",
    syntax: "new DOMException('m','SyntaxError')", not_supported: "new DOMException('m','NotSupportedError')",
    abort: "new DOMException('m','AbortError')", timeout: "new DOMException('m','TimeoutError')",
    invalid_state: "new DOMException('m','InvalidStateError')", not_found: "new DOMException('m','NotFoundError')",
    encoding: "new DOMException('m','EncodingError')", operation: "new DOMException('m','OperationError')",
    call: "DOMException('m')", symbol: "new DOMException(Symbol('m'))",
  };
  out.cases = {};
  for (const [name, source] of Object.entries(cases)) {
    try { out.cases[name] = summarize(eval(source)); } catch (error) { out.cases[name] = {error: error.name, message: error.message}; }
  }
  try { out.platform = summarize(await navigator.share({})); } catch (error) { out.platform = summarize(error); }
  out.receivers = {};
  for (const key of ['name', 'message', 'code']) {
    try { Object.getOwnPropertyDescriptor(proto, key).get.call({}); out.receivers[key] = 'returned'; }
    catch (error) { out.receivers[key] = error.name + ': ' + error.message; }
  }
  out.prototypeAccess = {};
  for (const key of ['name', 'message', 'code']) {
    try { out.prototypeAccess[key] = proto[key]; } catch (error) { out.prototypeAccess[key] = error.name + ': ' + error.message; }
  }
  const order = [];
  try { new DOMException({toString() { order.push('message'); return 'm'; }}, {toString() { order.push('name'); return 'n'; }}); } catch (error) {}
  out.conversionOrder = order;
  class Sub extends DOMException { constructor() { super('sub', 'AbortError'); } }
  const sub = new Sub();
  out.subclass = {proto: Object.getPrototypeOf(sub) === Sub.prototype, name: sub.name, code: sub.code, tag: Object.prototype.toString.call(sub)};
  const pre = document.createElement('pre'); pre.id = 'bom-reference'; pre.textContent = JSON.stringify(out); document.body.append(pre);
})();
