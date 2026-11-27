(async () => {
  'use strict';
  const summarizeError = error => (error && typeof error === 'object')
    ? {name: error.name, message: error.message, code: 'code' in error ? error.code : undefined,
       tag: Object.prototype.toString.call(error), domException: typeof DOMException === 'function' && error instanceof DOMException}
    : {thrown: typeof error, value: String(error)};
  const functionShape = fn => {
    let construct;
    try { Reflect.construct(fn, []); construct = 'constructed'; } catch (error) { construct = error.name; }
    return {name: fn.name, length: fn.length, source: Function.prototype.toString.call(fn),
      prototype: Object.hasOwn(fn, 'prototype'), construct};
  };
  const descriptor = (object, key) => {
    const item = Object.getOwnPropertyDescriptor(object, key); if (!item) return null;
    return {enumerable: item.enumerable, configurable: item.configurable,
      ...('value' in item ? {kind: 'data', writable: item.writable,
        value: typeof item.value === 'function' ? functionShape(item.value) : (typeof item.value === 'object' && item.value !== null ? '[object]' : item.value)}
        : {kind: 'accessor', get: item.get ? functionShape(item.get) : null, set: item.set ? functionShape(item.set) : null})};
  };
  const runSync = fn => {
    try { return {ok: true, value: fn()}; }
    catch (e) { return {ok: false, error: summarizeError(e)}; }
  };
  const runAsync = async fn => {
    try { return {ok: true, value: await fn()}; }
    catch (e) { return {ok: false, error: summarizeError(e)}; }
  };

  const out = {
    secure: isSecureContext,
    windowHasOwnWhen: Object.hasOwn(window, 'when'),
    windowProtoHasOwnWhen: Object.hasOwn(Window.prototype, 'when'),
    windowWhenEqEventTarget: window.when === EventTarget.prototype.when,
    globals: {},
    interfaces: {},
    subobjects: {},
    behavior: {},
  };

  for (const g of ['Observable', 'Subscriber', 'AbortController', 'AbortSignal', 'EventTarget']) {
    out.globals[g] = {present: g in globalThis, descriptor: descriptor(globalThis, g)};
    if (typeof globalThis[g] === 'function' && globalThis[g].prototype) {
      const proto = globalThis[g].prototype;
      const parent = Object.getPrototypeOf(proto);
      out.interfaces[g] = {
        constructor: functionShape(globalThis[g]),
        constructorKeys: Reflect.ownKeys(globalThis[g]).map(String),
        staticMembers: Object.fromEntries(
          Reflect.ownKeys(globalThis[g])
            .filter(k => !['length', 'name', 'prototype'].includes(String(k)))
            .map(k => [String(k), descriptor(globalThis[g], k)])
        ),
        prototypeKeys: Reflect.ownKeys(proto).map(String),
        parent: parent === Object.prototype ? 'Object.prototype' : (parent === EventTarget.prototype ? 'EventTarget.prototype' : (parent && parent.constructor ? parent.constructor.name : String(parent))),
        members: Object.fromEntries(Reflect.ownKeys(proto).map(k => [String(k), descriptor(proto, k)])),
      };
    }
  }

  for (const [label, expr] of [
    ['connection', 'navigator.connection'],
    ['orientation', 'screen.orientation'],
    ['viewport', 'visualViewport'],
  ]) {
    const obj = eval(expr);
    out.subobjects[label] = {
      hasWhen: 'when' in obj,
      ownWhen: Object.hasOwn(obj, 'when'),
      protoOwnWhen: Object.hasOwn(Object.getPrototypeOf(obj), 'when'),
      sameAsEventTarget: obj.when === EventTarget.prototype.when,
    };
  }

  out.behavior.whenBadReceiver = runSync(() => EventTarget.prototype.when.call({}));
  out.behavior.whenMissingArg = runSync(() => navigator.connection.when());
  out.behavior.whenBadOptions = runSync(() => navigator.connection.when('change', 123));
  out.behavior.windowWhenClick = runSync(() => Object.prototype.toString.call(when('click')));

  const ac = new AbortController();
  const connEvents = [];
  const connObs = navigator.connection.when('change');
  out.behavior.obsTag = Object.prototype.toString.call(connObs);
  out.behavior.obsInstance = connObs instanceof Observable;
  connObs.subscribe({
    next(ev) { connEvents.push('conn:' + ev.type); },
    complete() { connEvents.push('complete'); },
  }, {signal: ac.signal});
  navigator.connection.dispatchEvent(new Event('change'));
  navigator.connection.dispatchEvent(new Event('change'));
  ac.abort();
  navigator.connection.dispatchEvent(new Event('change'));
  out.behavior.dispatched = connEvents;

  out.behavior.obsNoNew = runSync(() => Observable(() => {}));
  out.behavior.obsMissingArg = runSync(() => new Observable());
  out.behavior.obsBadCallback = runSync(() => new Observable(123));
  out.behavior.subConstruct = runSync(() => new Subscriber());
  out.behavior.abortSigConstruct = runSync(() => new AbortSignal());

  let subShape = null;
  const customLog = [];
  const custom = new Observable(subscriber => {
    subShape = {
      tag: Object.prototype.toString.call(subscriber),
      instance: subscriber instanceof Subscriber,
      active: subscriber.active,
      signalTag: Object.prototype.toString.call(subscriber.signal),
      signalInstance: subscriber.signal instanceof AbortSignal,
      signalAborted: subscriber.signal.aborted,
    };
    subscriber.addTeardown(() => customLog.push('teardown'));
    customLog.push('active:' + subscriber.active);
    subscriber.next(1);
    subscriber.next(2);
    subscriber.complete();
    customLog.push('afterComplete:' + subscriber.active);
    customLog.push('signalAfterComplete:' + subscriber.signal.aborted);
    subscriber.next(3);
  });
  custom.subscribe({
    next(v) { customLog.push('next:' + v); },
    complete() { customLog.push('done'); },
  });
  out.behavior.subscriberShape = subShape;
  out.behavior.custom = customLog;

  const fnLog = [];
  Observable.from([4, 5]).subscribe(v => fnLog.push(v));
  out.behavior.fnSubscribe = fnLog;

  out.behavior.fromMissing = runSync(() => Observable.from());
  out.behavior.fromPrim = runSync(() => Observable.from(123));
  out.behavior.fromSameObs = Observable.from(custom) === custom;
  out.behavior.fromPromise = await runAsync(() => Observable.from(Promise.resolve('ok')).first());
  out.behavior.mapFilterTakeToArray = await runAsync(() =>
    Observable.from([1, 2, 3, 4, 5, 6])
      .filter(x => x % 2 === 0)
      .map((x, i) => x * 10 + i)
      .take(2)
      .toArray()
  );
  out.behavior.drop = await runAsync(() => Observable.from([1, 2, 3, 4]).drop(2).toArray());
  out.behavior.firstEmpty = await runAsync(() => Observable.from([]).first());
  out.behavior.last = await runAsync(() => Observable.from([7, 8, 9]).last());
  out.behavior.lastEmpty = await runAsync(() => Observable.from([]).last());
  out.behavior.find = await runAsync(() => Observable.from([1, 4, 6]).find(x => x > 3));
  out.behavior.every = await runAsync(() => Observable.from([2, 4, 6]).every(x => x % 2 === 0));
  out.behavior.some = await runAsync(() => Observable.from([1, 3, 4]).some(x => x % 2 === 0));
  out.behavior.reduce = await runAsync(() => Observable.from([1, 2, 3, 4]).reduce((a, b) => a + b, 10));
  out.behavior.reduceNoInit = await runAsync(() => Observable.from([1, 2, 3, 4]).reduce((a, b) => a + b));
  out.behavior.reduceEmptyNoInit = await runAsync(() => Observable.from([]).reduce((a, b) => a + b));
  out.behavior.forEach = await runAsync(async () => {
    const seen = [];
    const r = await Observable.from(['a', 'b']).forEach((v, i) => seen.push(v + i));
    return {rType: typeof r, seen};
  });
  out.behavior.flatMap = await runAsync(() =>
    Observable.from([1, 2]).flatMap(x => [x, x * 10]).toArray()
  );
  out.behavior.switchMap = await runAsync(() =>
    Observable.from([1, 2]).switchMap(x => [x, x + 100]).toArray()
  );
  out.behavior.catchOp = await runAsync(() =>
    new Observable(s => { s.next(1); s.error('boom'); })
      .catch(e => [e + '!'])
      .toArray()
  );
  const finLog = [];
  out.behavior.finallyOp = await runAsync(() =>
    Observable.from([9]).finally(() => finLog.push('fin')).toArray().then(v => ({v, finLog}))
  );
  const inspLog = [];
  out.behavior.inspectOp = await runAsync(() =>
    Observable.from([5]).inspect(v => inspLog.push(v)).toArray().then(v => ({v, inspLog}))
  );
  out.behavior.takeUntil = await runAsync(() => {
    const target = new EventTarget();
    const collected = [];
    navigator.connection.when('change').takeUntil(target.when('stop')).subscribe(e => collected.push(e.type));
    navigator.connection.dispatchEvent(new Event('change'));
    target.dispatchEvent(new Event('stop'));
    navigator.connection.dispatchEvent(new Event('change'));
    return collected;
  });

  // AbortController / AbortSignal behavior
  const ac2 = new AbortController();
  const abortFired = [];
  ac2.signal.onabort = () => abortFired.push('handler');
  ac2.signal.addEventListener('abort', () => abortFired.push('listener'));
  const beforeAbort = {aborted: ac2.signal.aborted, reason: ac2.signal.reason};
  ac2.abort();
  const afterDefaultAbort = {
    aborted: ac2.signal.aborted,
    reasonName: ac2.signal.reason && ac2.signal.reason.name,
    reasonDomEx: ac2.signal.reason instanceof DOMException,
    fired: abortFired,
    throwIfAborted: runSync(() => ac2.signal.throwIfAborted()),
  };
  const staticAborted = AbortSignal.abort('custom-reason');
  out.behavior.abortDetails = {
    beforeAbort,
    afterDefaultAbort,
    staticAborted: {aborted: staticAborted.aborted, reason: staticAborted.reason},
  };

  const pre = document.createElement('pre');
  pre.id = 'bom-reference';
  pre.textContent = JSON.stringify(out, null, 2);
  document.body.append(pre);
})();
