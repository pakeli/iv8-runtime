(async () => {
  const output = {secure: isSecureContext, shape: {}, capabilities: {}, events: {}, orientation: {}};
  const errorRecord = error => ({name: error.name, message: error.message});
  const attempt = callback => {
    try { const value = callback(); return {value: value === undefined ? "undefined" : value}; }
    catch (error) { return {error: errorRecord(error)}; }
  };
  const settle = async callback => {
    try {
      const value = callback();
      const promise = value instanceof Promise;
      try { return {promise, value: await value ?? "undefined"}; }
      catch (error) { return {promise, error: errorRecord(error)}; }
    } catch (error) { return {promise: false, synchronous: true, error: errorRecord(error)}; }
  };
  const functionShape = method => ({name: method.name, length: method.length,
    source: Function.prototype.toString.call(method),
    prototype: Object.hasOwn(method, "prototype"),
    construct: attempt(() => Reflect.construct(method, [])).error?.name ?? "constructed"});
  const descriptor = (owner, key) => {
    const value = Object.getOwnPropertyDescriptor(owner, key);
    if (!value) return null;
    return {enumerable: value.enumerable, configurable: value.configurable,
      ...(Object.hasOwn(value, "value") ? {kind:"data", writable: value.writable,
        ...(typeof value.value === "function" ? {method:functionShape(value.value)} : {value:value.value})} :
        {kind:"accessor", get:value.get ? functionShape(value.get) : null,
          set:value.set ? functionShape(value.set) : null})};
  };
  const targets = {Scheduling:navigator.scheduling, NetworkInformation:navigator.connection,
    ScreenOrientation:screen.orientation, VisualViewport:visualViewport};
  for (const [name, target] of Object.entries(targets)) {
    const prototype = Object.getPrototypeOf(target);
    output.shape[name] = {constructor: functionShape(globalThis[name]),
      parent: Object.getPrototypeOf(prototype) === EventTarget.prototype ? "EventTarget" : "Object",
      samePrototype: prototype === globalThis[name].prototype,
      tag: Object.prototype.toString.call(target), own: Reflect.ownKeys(target).map(String),
      members:Object.fromEntries(Reflect.ownKeys(prototype).map(key => [String(key), descriptor(prototype,key)])),
      inheritedEventMethods: ["addEventListener","removeEventListener","dispatchEvent"].map(key =>
        target[key] === EventTarget.prototype[key])};
  }
  output.shape.Navigator = Object.fromEntries(["getGamepads","scheduling"].map(key => [key,descriptor(Navigator.prototype,key)]));
  const first = navigator.getGamepads(), second = navigator.getGamepads();
  output.capabilities.gamepads = {array:Array.isArray(first), value:first, frozen:Object.isFrozen(first),
    fresh:first !== second, wrongReceiver:attempt(() => navigator.getGamepads.call({})),
    ignoredArgument:attempt(() => navigator.getGamepads({get valueOf(){throw Error("unused");}}))};
  const options = [];
  const pending = argument => attempt(() => navigator.scheduling.isInputPending(argument));
  output.capabilities.scheduling = {stable:navigator.scheduling === navigator.scheduling,
    results:[pending(undefined), pending(null), pending({}), pending({includeContinuous:true}),
      pending({get includeContinuous(){options.push("read");return {valueOf(){throw Error("no coercion");}};}}),
      pending(1), pending("x"), pending(Symbol("options"))], options,
    wrongReceiver:attempt(() => navigator.scheduling.isInputPending.call({})),
    getterWrongReceiver:attempt(() => Object.getOwnPropertyDescriptor(Navigator.prototype,"scheduling").get.call({}))};
  for (const [name,target] of Object.entries(targets).filter(([name]) => name !== "Scheduling")) {
    const kind = name === "VisualViewport" ? "scrollend" : "change";
    const seen = [];
    const listener = function(event){seen.push(["listener",this===target,event.target===target,event.currentTarget===target,event.eventPhase]);};
    target.addEventListener(kind,listener,{once:true});
    target["on"+kind] = function(event){seen.push(["handler",this===target,event.target===target,event.currentTarget===target,event.eventPhase]);event.preventDefault();};
    const event = new Event(kind,{cancelable:true});
    const returned = target.dispatchEvent(event);
    const returnedSecond = target.dispatchEvent(new Event(kind));
    target["on"+kind] = 17;
    output.events[name] = {seen,returned,returnedSecond,handlerAfterPrimitive:target["on"+kind],
      finalState:[event.currentTarget===null,event.eventPhase,event.composedPath().length],
      invalidDispatch:attempt(() => target.dispatchEvent({type:kind})),
      invalidReceiver:attempt(() => target.dispatchEvent.call({},event))};
  }
  output.orientation.stable = screen.orientation === screen.orientation;
  output.orientation.connectionStable = navigator.connection === navigator.connection;
  for (const kind of ["portrait","landscape","any","natural","portrait-primary","bad",null,undefined]) {
    output.orientation[String(kind)] = await settle(() => screen.orientation.lock(kind));
  }
  output.orientation.noArgument = await settle(() => screen.orientation.lock());
  output.orientation.wrongReceiver = await settle(() => screen.orientation.lock.call({},"portrait"));
  output.orientation.unlock = attempt(() => screen.orientation.unlock());
  output.orientation.unlockWrongReceiver = attempt(() => screen.orientation.unlock.call({}));
  const pre = document.createElement("pre");pre.id="bom-reference";pre.textContent=JSON.stringify(output);
  document.body.appendChild(pre);
  return output;
})();
