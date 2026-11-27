(async () => {
  'use strict';

  // Discovery/reference probe only. No network, random-key generation, or DOM
  // access. The returned Promise and __bom10CryptoResult contain the same JSON.
  const errorSummary = error => {
    if (error === null || (typeof error !== 'object' && typeof error !== 'function')) {
      return {type: typeof error, value: String(error)};
    }
    return {
      name: typeof error.name === 'string' ? error.name : null,
      message: typeof error.message === 'string' ? error.message : null,
      code: typeof error.code === 'number' ? error.code : null,
      tag: Object.prototype.toString.call(error),
      domException: typeof DOMException === 'function' && error instanceof DOMException,
    };
  };
  const hex = buffer => Array.from(new Uint8Array(buffer), byte => byte.toString(16).padStart(2, '0')).join('');
  const valueSummary = value => {
    if (value === undefined) return {type: 'undefined'};
    if (value === null) return {type: 'null'};
    if (typeof value === 'bigint' || typeof value === 'symbol') {
      return {type: typeof value, value: String(value)};
    }
    if (typeof value !== 'object' && typeof value !== 'function') {
      return {type: typeof value, value};
    }
    if (value instanceof ArrayBuffer) {
      return {type: 'ArrayBuffer', byteLength: value.byteLength, hex: hex(value)};
    }
    return {type: typeof value, tag: Object.prototype.toString.call(value)};
  };
  const functionShape = fn => {
    let constructable;
    try {
      // Test [[Construct]] without invoking the web operation itself.
      Reflect.construct(function Empty() {}, [], fn);
      constructable = true;
    } catch (_) {
      constructable = false;
    }
    return {
      name: fn.name,
      length: fn.length,
      source: Function.prototype.toString.call(fn),
      ownPrototype: Object.hasOwn(fn, 'prototype'),
      constructable,
      inheritsFunctionPrototype: Object.getPrototypeOf(fn) === Function.prototype,
    };
  };
  const descriptor = (object, key) => {
    const item = Object.getOwnPropertyDescriptor(object, key);
    if (!item) return null;
    const base = {enumerable: item.enumerable, configurable: item.configurable};
    return 'value' in item
      ? {...base, kind: 'data', writable: item.writable,
         value: typeof item.value === 'function' ? functionShape(item.value) : valueSummary(item.value)}
      : {...base, kind: 'accessor',
         get: item.get ? functionShape(item.get) : null,
         set: item.set ? functionShape(item.set) : null};
  };
  const ownDescriptors = object => Object.fromEntries(
    Reflect.ownKeys(object).map(key => [String(key), descriptor(object, key)])
  );
  const ownerOf = (object, key) => {
    let depth = 0;
    while (object !== null) {
      if (Object.hasOwn(object, key)) return {depth, descriptor: descriptor(object, key)};
      object = Object.getPrototypeOf(object);
      depth++;
    }
    return null;
  };
  const sync = (operation, project = valueSummary, original = undefined) => {
    try {
      return {mode: 'return', outcome: 'returned', value: project(operation())};
    } catch (error) {
      return {mode: 'throw', outcome: 'threw', error: errorSummary(error),
        ...(original === undefined ? {} : {sameError: error === original})};
    }
  };
  const asynchronous = async (operation, project = valueSummary, original = undefined) => {
    let result;
    try {
      result = operation();
    } catch (error) {
      return {mode: 'throw', outcome: 'threw', error: errorSummary(error),
        ...(original === undefined ? {} : {sameError: error === original})};
    }
    if (!(result instanceof Promise)) {
      return {mode: 'return', outcome: 'returned', value: project(result)};
    }
    let timeout;
    const settled = result.then(
      value => ({mode: 'promise', outcome: 'fulfilled', value: project(value)}),
      error => ({mode: 'promise', outcome: 'rejected', error: errorSummary(error),
        ...(original === undefined ? {} : {sameError: error === original})})
    );
    try {
      return await Promise.race([
        settled,
        new Promise(resolve => {
          timeout = setTimeout(() => resolve({mode: 'promise', outcome: 'timeout'}), 5000);
        }),
      ]);
    } finally {
      clearTimeout(timeout);
    }
  };

  const out = {
    secure: Boolean(globalThis.isSecureContext),
    globals: {},
    interfaces: {},
    instances: {},
    constructorCalls: {},
    receivers: {},
    missingArguments: {},
    digest: {},
    keys: {},
  };
  globalThis.__bom10CryptoResult = {status: 'pending'};
  try {
    for (const name of ['crypto', 'Crypto', 'SubtleCrypto', 'CryptoKey']) {
      out.globals[name] = {present: name in globalThis, owner: ownerOf(globalThis, name)};
    }
    for (const name of ['Crypto', 'SubtleCrypto', 'CryptoKey']) {
      const constructor = globalThis[name];
      if (typeof constructor !== 'function') continue;
      const prototype = constructor.prototype;
      const parent = Object.getPrototypeOf(prototype);
      out.interfaces[name] = {
        constructor: functionShape(constructor),
        constructorKeys: Reflect.ownKeys(constructor).map(String),
        constructorMembers: ownDescriptors(constructor),
        prototypeKeys: Reflect.ownKeys(prototype).map(String),
        prototypeMembers: ownDescriptors(prototype),
        parentIsObjectPrototype: parent === Object.prototype,
        parentTag: parent === null ? null : Object.prototype.toString.call(parent),
      };
      out.constructorCalls[name] = {
        call: sync(() => Reflect.apply(constructor, undefined, [])),
        construct: sync(() => Reflect.construct(constructor, [])),
      };
      for (const key of Reflect.ownKeys(prototype)) {
        const item = Object.getOwnPropertyDescriptor(prototype, key);
        if (typeof key !== 'string' || key === 'constructor') continue;
        if (item.get) out.receivers[name + '.get ' + key] = sync(() => item.get.call({}));
        if (item.set) out.receivers[name + '.set ' + key] = sync(() => item.set.call({}, undefined));
      }
    }

    const cryptoObject = globalThis.crypto;
    if (cryptoObject && typeof cryptoObject === 'object') {
      out.instances.crypto = {
        tag: Object.prototype.toString.call(cryptoObject),
        ownKeys: Reflect.ownKeys(cryptoObject).map(String),
        instanceOfCrypto: typeof Crypto === 'function' && cryptoObject instanceof Crypto,
        prototypeIsCryptoPrototype: typeof Crypto === 'function' && Object.getPrototypeOf(cryptoObject) === Crypto.prototype,
        sameObject: cryptoObject === globalThis.crypto,
        subtlePresent: 'subtle' in cryptoObject,
        subtleOwner: ownerOf(cryptoObject, 'subtle'),
        randomUUIDPresent: 'randomUUID' in cryptoObject,
      };
    }
    const subtle = cryptoObject && cryptoObject.subtle;
    if (subtle && typeof subtle === 'object') {
      out.instances.subtle = {
        tag: Object.prototype.toString.call(subtle),
        ownKeys: Reflect.ownKeys(subtle).map(String),
        instanceOfSubtleCrypto: typeof SubtleCrypto === 'function' && subtle instanceof SubtleCrypto,
        prototypeIsSubtleCryptoPrototype: typeof SubtleCrypto === 'function' && Object.getPrototypeOf(subtle) === SubtleCrypto.prototype,
        sameObject: subtle === cryptoObject.subtle,
      };
      const subtlePrototype = Object.getPrototypeOf(subtle);
      for (const name of Object.getOwnPropertyNames(subtlePrototype)) {
        const member = Object.getOwnPropertyDescriptor(subtlePrototype, name);
        if (name === 'constructor' || typeof member.value !== 'function') continue;
        // Invalid receiver/missing required arguments never performs key work.
        out.receivers['SubtleCrypto.' + name] = await asynchronous(() => member.value.call({}));
        out.missingArguments[name] = await asynchronous(() => member.value.call(subtle));
      }

      if (typeof subtle.digest === 'function') {
        const abc = new Uint8Array([97, 98, 99]);
        for (const algorithm of ['SHA-1', 'SHA-256', 'SHA-384', 'SHA-512']) {
          out.digest[algorithm] = {
            abc: await asynchronous(() => subtle.digest(algorithm, abc)),
            empty: await asynchronous(() => subtle.digest({name: algorithm}, new Uint8Array(0))),
          };
        }
        const offsetBytes = new Uint8Array([255, 97, 98, 99, 254]);
        out.digest.typedArrayOffset = await asynchronous(() => subtle.digest('SHA-256', offsetBytes.subarray(1, 4)));
        out.digest.dataViewOffset = await asynchronous(() => subtle.digest('SHA-256', new DataView(offsetBytes.buffer, 1, 3)));
        out.digest.arrayBuffer = await asynchronous(() => subtle.digest('SHA-256', abc.buffer));
        out.digest.snapshotBeforeMutation = await asynchronous(() => {
          const bytes = new Uint8Array([97, 98, 99]);
          const pending = subtle.digest('SHA-256', bytes);
          bytes.fill(0);
          return pending;
        });
        out.digest.lowercaseAlgorithm = await asynchronous(() => subtle.digest('sha-256', abc));
        out.digest.extraArgument = await asynchronous(() => subtle.digest('SHA-256', abc, {unused: true}));

        const badDigestCases = {
          missingData: () => subtle.digest('SHA-256'),
          undefinedAlgorithm: () => subtle.digest(undefined, abc),
          nullAlgorithm: () => subtle.digest(null, abc),
          emptyAlgorithm: () => subtle.digest({}, abc),
          unknownAlgorithm: () => subtle.digest('BOM10-UNKNOWN', abc),
          algorithmForOtherOperation: () => subtle.digest('AES-GCM', abc),
          undefinedData: () => subtle.digest('SHA-256', undefined),
          nullData: () => subtle.digest('SHA-256', null),
          stringData: () => subtle.digest('SHA-256', 'abc'),
          arrayData: () => subtle.digest('SHA-256', [97, 98, 99]),
          objectData: () => subtle.digest('SHA-256', {}),
          symbolAlgorithm: () => subtle.digest(Symbol('algorithm'), abc),
          invalidReceiverWithValidArgs: () => Reflect.apply(subtle.digest, {}, ['SHA-256', abc]),
          nullReceiverWithValidArgs: () => Reflect.apply(subtle.digest, null, ['SHA-256', abc]),
        };
        for (const [name, operation] of Object.entries(badDigestCases)) {
          out.digest[name] = await asynchronous(operation);
        }
        const getterError = new RangeError('bom10 algorithm getter');
        out.digest.throwingAlgorithmGetter = await asynchronous(
          () => subtle.digest({get name() { throw getterError; }}, abc), valueSummary, getterError);
        const reads = [];
        out.digest.algorithmConversion = await asynchronous(() => subtle.digest({
          get name() {
            reads.push('name');
            return {toString() { reads.push('toString'); return 'SHA-256'; }};
          },
        }, abc));
        out.digest.algorithmConversionReads = reads;
        if (typeof ArrayBuffer.prototype.transfer === 'function') {
          const detached = new ArrayBuffer(3);
          detached.transfer();
          out.digest.detachedBuffer = await asynchronous(() => subtle.digest('SHA-256', detached));
        } else {
          out.digest.detachedBuffer = {available: false};
        }
        if (typeof SharedArrayBuffer === 'function') {
          out.digest.sharedBuffer = await asynchronous(() => subtle.digest('SHA-256', new SharedArrayBuffer(3)));
        } else {
          out.digest.sharedBuffer = {available: false};
        }
      }

      // Discovery beyond digest: fixed raw bytes only, no random-key generation.
      if (typeof subtle.importKey === 'function') {
        const keySummary = key => ({
          tag: Object.prototype.toString.call(key),
          ownKeys: Reflect.ownKeys(key).map(String),
          instanceOfCryptoKey: typeof CryptoKey === 'function' && key instanceof CryptoKey,
          prototypeIsCryptoKeyPrototype: typeof CryptoKey === 'function' && Object.getPrototypeOf(key) === CryptoKey.prototype,
          type: key.type,
          extractable: key.extractable,
          algorithm: key.algorithm,
          usages: key.usages,
          sameAlgorithm: key.algorithm === key.algorithm,
          sameUsages: key.usages === key.usages,
          algorithmFrozen: Object.isFrozen(key.algorithm),
          usagesFrozen: Object.isFrozen(key.usages),
        });
        const raw = new Uint8Array(16).fill(7);
        out.keys.aesGcm = await asynchronous(
          () => subtle.importKey('raw', raw, {name: 'AES-GCM'}, true, ['encrypt', 'decrypt']), keySummary);
        out.keys.hmac = await asynchronous(
          () => subtle.importKey('raw', raw, {name: 'HMAC', hash: 'SHA-256'}, false, ['sign', 'verify']), keySummary);
        out.keys.invalidRawLength = await asynchronous(
          () => subtle.importKey('raw', new Uint8Array(3), 'AES-GCM', true, ['encrypt']), keySummary);
        out.keys.invalidUsage = await asynchronous(
          () => subtle.importKey('raw', raw, 'AES-GCM', true, ['sign']), keySummary);
      }
    }
    globalThis.__bom10CryptoResult = out;
    return out;
  } catch (error) {
    const failure = {status: 'probe-failed', error: errorSummary(error), partial: out};
    globalThis.__bom10CryptoResult = failure;
    return failure;
  }
})()
