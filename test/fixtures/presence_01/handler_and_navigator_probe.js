(async () => {
  const targets = [['window', window], ['document', document], ['element', document.createElement('div')]];
  const handlers = {};
  const shape = descriptor => ({
    enumerable:descriptor.enumerable, configurable:descriptor.configurable,
    get:{name:descriptor.get.name, length:descriptor.get.length, source:Function.prototype.toString.call(descriptor.get)},
    set:{name:descriptor.set.name, length:descriptor.set.length, source:Function.prototype.toString.call(descriptor.set)}
  });
  for (const [name, target] of targets) {
    const initial = target.onclick;
    const callback = () => false;
    target.onclick = callback;
    const assignedIdentity = target.onclick === callback;
    const event = new Event('click', {cancelable:true});
    const dispatched = target.dispatchEvent(event);
    target.onclick = 42;
    const clearedByNumber = target.onclick === null;
    target.onclick = callback;
    target.onclick = {};
    const clearedByObject = target.onclick === null;
    let holder = target;
    let depth = 0;
    while (!Object.getOwnPropertyDescriptor(holder, 'onclick')) {
      holder = Object.getPrototypeOf(holder); depth += 1;
    }
    handlers[name] = {initialNull:initial === null, assignedIdentity, dispatched,
      canceled:event.defaultPrevented, clearedByNumber, clearedByObject,
      descriptorDepth:depth, descriptor:shape(Object.getOwnPropertyDescriptor(holder, 'onclick'))};
  }
  const ua = navigator.userAgentData;
  const brands = ua.brands;
  const navigatorResults = {
    brandsIdentity:brands === ua.brands, brandsFrozen:Object.isFrozen(brands),
    brandElementsFrozen:brands.map(Object.isFrozen),
    userActivationIdentity:navigator.userActivation === navigator.userActivation,
    ancestorOriginsIdentity:location.ancestorOrigins === location.ancestorOrigins,
    entropyArguments:{}
  };
  for (const [name, argument] of [['undefined', undefined], ['array', []], ['set', new Set(['architecture'])], ['string', 'architecture']]) {
    let promise;
    try { promise = ua.getHighEntropyValues(argument); }
    catch (error) { navigatorResults.entropyArguments[name] = {channel:'synchronous', name:error.name, message:error.message}; continue; }
    try {
      const result = await promise;
      navigatorResults.entropyArguments[name] = {channel:'fulfilled', keys:Object.keys(result).sort()};
    } catch (error) {
      navigatorResults.entropyArguments[name] = {channel:'promise_rejection', name:error.name, message:error.message};
    }
  }

  const objectMetadata = value => {
    const prototype = Object.getPrototypeOf(value);
    return {tag:Object.prototype.toString.call(value), ownKeys:Reflect.ownKeys(value).map(String),
      prototypeTag:prototype === null ? null : Object.prototype.toString.call(prototype),
      prototypeKeys:prototype === null ? [] : Reflect.ownKeys(prototype).map(String)};
  };
  const consoleResults = {};
  if (typeof console.createTask === 'function') {
    const task = console.createTask('iv8-local-metadata');
    consoleResults.task = objectMetadata(task);
    consoleResults.task.callbackReturned = task.run(() => 41) === 41;
    consoleResults.task.run = {name:task.run.name,length:task.run.length,
      source:Function.prototype.toString.call(task.run),prototype:Object.hasOwn(task.run,'prototype')};
  }
  if (typeof console.context === 'function') {
    const context = console.context('iv8-local-metadata');
    consoleResults.context = objectMetadata(context);
  }
  // Inspect only property names, never usedJSHeapSize/totalJSHeapSize/limit values.
  consoleResults.memoryKeys = Object.getOwnPropertyNames(Object.getPrototypeOf(console.memory)).sort();

  const result = {handlers, navigator:navigatorResults, console:consoleResults};
  const output = document.createElement('pre');
  output.textContent = JSON.stringify(result);
  document.body.appendChild(output);
})()
