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
    memoryInfoGlobalPresent: 'MemoryInfo' in globalThis,
    globals: {},
    interfaces: {},
    subobjects: {},
    behavior: {},
  };

  const interfaceNames = [
    'Performance', 'PerformanceEntry', 'PerformanceMark', 'PerformanceMeasure',
    'PerformanceNavigation', 'PerformanceTiming', 'EventCounts',
    'Scheduler', 'TaskController', 'TaskSignal', 'TaskPriorityChangeEvent',
  ];
  for (const g of interfaceNames) {
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

  // MemoryInfo private prototype shape
  const memSample = performance.memory;
  const memProto = Object.getPrototypeOf(memSample);
  out.subobjects.memoryInfo = {
    instanceOwnKeys: Reflect.ownKeys(memSample).map(String),
    instanceTag: Object.prototype.toString.call(memSample),
    protoParent: Object.getPrototypeOf(memProto) === Object.prototype ? 'Object.prototype' : 'other',
    protoKeys: Reflect.ownKeys(memProto).map(String),
    protoMembers: Object.fromEntries(Reflect.ownKeys(memProto).map(k => [String(k), descriptor(memProto, k)])),
    sameAsSecondRead: performance.memory === memSample,
    consoleSameProto: Object.getPrototypeOf(console.memory) === memProto,
    consoleSameAsSecondRead: console.memory === console.memory,
  };

  // performance, console, scheduler instance shapes
  out.subobjects.performance = {
    tag: Object.prototype.toString.call(performance),
    instancePerf: performance instanceof Performance,
    instanceET: performance instanceof EventTarget,
    ownKeys: Reflect.ownKeys(performance).map(String),
    timingSame: performance.timing === performance.timing,
    navigationSame: performance.navigation === performance.navigation,
    eventCountsSame: performance.eventCounts === performance.eventCounts,
    eventCountsKeys: Array.from(performance.eventCounts.keys()),
    interactionCount: performance.interactionCount,
    toJSONKeys: Object.keys(performance.toJSON()),
    timingToJSONKeys: Object.keys(performance.timing.toJSON()),
    navigationToJSON: performance.navigation.toJSON(),
  };

  out.subobjects.console = {
    tag: Object.prototype.toString.call(console),
    protoIsObject: Object.getPrototypeOf(console) === Object.prototype,
    ownKeys: Reflect.ownKeys(console).map(String),
    members: Object.fromEntries(Reflect.ownKeys(console).map(k => [String(k), descriptor(console, k)])),
  };

  out.subobjects.scheduler = {
    tag: Object.prototype.toString.call(scheduler),
    instanceSched: scheduler instanceof Scheduler,
    ownKeys: Reflect.ownKeys(scheduler).map(String),
  };

  // Behavior probes
  // 1. Performance EventTarget + when + onresourcetimingbufferfull
  const perfLog = [];
  performance.when('resourcetimingbufferfull').take(1).subscribe(ev => perfLog.push('when:' + ev.type));
  performance.onresourcetimingbufferfull = ev => perfLog.push('handler:' + ev.type);
  performance.dispatchEvent(new Event('resourcetimingbufferfull'));
  performance.onresourcetimingbufferfull = null;
  out.behavior.perfEvents = perfLog;

  // 2. PerformanceMark & PerformanceMeasure
  performance.clearMarks();
  performance.clearMeasures();
  const constructedMark = new PerformanceMark('ctor-mark', {startTime: 12, detail: {nested: 42}});
  out.behavior.constructedMark = {
    instanceMark: constructedMark instanceof PerformanceMark,
    instanceEntry: constructedMark instanceof PerformanceEntry,
    name: constructedMark.name,
    entryType: constructedMark.entryType,
    startTime: constructedMark.startTime,
    duration: constructedMark.duration,
    navIdType: typeof constructedMark.navigationId,
    detail: constructedMark.detail,
    toJSONKeys: Object.keys(constructedMark.toJSON()),
    inTimeline: performance.getEntriesByName('ctor-mark').length,
  };
  out.behavior.markNegStart = runSync(() => new PerformanceMark('neg', {startTime: -1}));
  out.behavior.entryConstruct = runSync(() => new PerformanceEntry());
  out.behavior.measureConstruct = runSync(() => new PerformanceMeasure());

  const calledMark = performance.mark('tl-mark', {startTime: 5, detail: 'payload'});
  const measuredOpts = performance.measure('tl-meas', {start: 2, end: 9, detail: {ok: true}});
  out.behavior.timelineEntries = {
    markInTimeline: performance.getEntriesByName('tl-mark').length,
    markStart: calledMark.startTime,
    markDetail: calledMark.detail,
    measInstance: measuredOpts instanceof PerformanceMeasure,
    measStart: measuredOpts.startTime,
    measDuration: measuredOpts.duration,
    measDetail: measuredOpts.detail,
  };
  out.behavior.measureAllThree = runSync(() => performance.measure('bad', {start: 1, duration: 2, end: 4}));
  out.behavior.measureMissingMark = runSync(() => performance.measure('bad2', 'does-not-exist'));
  performance.clearMarks();
  performance.clearMeasures();

  // 3. console.createTask, console.context, console.memory
  console.memory = 999;
  out.behavior.consoleMemAfterAssign = Object.prototype.toString.call(console.memory);
  out.behavior.consoleTaskNoArg = runSync(() => console.createTask());
  out.behavior.consoleTaskEmpty = runSync(() => console.createTask(''));
  const task = console.createTask('demo');
  out.behavior.consoleTaskRun = task.run(() => 42);
  out.behavior.consoleTaskExtraArgs = task.run((a, b) => [typeof a, typeof b], 19, 23);
  out.behavior.consoleTaskBadFn = runSync(() => task.run(123));
  const ctxConsole = console.context('sub');
  out.behavior.consoleContextKeys = Reflect.ownKeys(ctxConsole).map(String);

  // 4. Scheduler, TaskController, TaskSignal, TaskPriorityChangeEvent
  out.behavior.postTaskOk = await runAsync(() => scheduler.postTask(() => 'done'));
  out.behavior.postTaskMissing = await runAsync(() => scheduler.postTask());
  out.behavior.postTaskBadCb = await runAsync(() => scheduler.postTask(123));
  out.behavior.postTaskNegDelay = await runAsync(() => scheduler.postTask(() => 1, {delay: -1}));
  out.behavior.postTaskBadPrio = await runAsync(() => scheduler.postTask(() => 1, {priority: 'invalid'}));
  out.behavior.postTaskPreAborted = await runAsync(() => scheduler.postTask(() => 1, {signal: AbortSignal.abort('stopped')}));
  out.behavior.yieldOk = await runAsync(async () => typeof (await scheduler.yield()));

  const tc = new TaskController({priority: 'background'});
  const prioChanges = [];
  tc.signal.addEventListener('prioritychange', ev => {
    prioChanges.push({
      isTPCE: ev instanceof TaskPriorityChangeEvent,
      prev: ev.previousPriority,
      curr: tc.signal.priority,
    });
  });
  const tsDerived = TaskSignal.any([tc.signal], {priority: tc.signal});
  const derivedBefore = tsDerived.priority;
  tc.setPriority('user-blocking');
  tc.setPriority('user-blocking');
  const derivedAfter = tsDerived.priority;
  tc.abort('tc-stop');
  out.behavior.taskControl = {
    tcIsAbortController: tc instanceof AbortController,
    sigIsAbortSignal: tc.signal instanceof AbortSignal,
    prioChanges,
    derivedBefore,
    derivedAfter,
    derivedAborted: tsDerived.aborted,
    derivedReason: tsDerived.reason,
  };
  out.behavior.tpceMissing = runSync(() => new TaskPriorityChangeEvent('prioritychange'));
  out.behavior.tpceConstruct = runSync(() => {
    const e = new TaskPriorityChangeEvent('prioritychange', {previousPriority: 'user-visible'});
    return {type: e.type, prev: e.previousPriority, bubbles: e.bubbles};
  });

  const pre = document.createElement('pre');
  pre.id = 'bom-reference';
  pre.textContent = JSON.stringify(out);
  document.body.appendChild(pre);
})();
