(async () => {
  const output = {secure:isSecureContext, origin:location.origin,
    activation:{active:navigator.userActivation.isActive,beenActive:navigator.userActivation.hasBeenActive},
    shape:{},vibration:{},related:{}};
  const nativeSource = Function.prototype.toString;
  const functionShape = method => {
    let construct;
    try {Reflect.construct(method,[]);construct="constructed";}catch(error){construct=error.name;}
    return {name:method.name,length:method.length,source:nativeSource.call(method),
      prototype:Object.hasOwn(method,"prototype"),construct};
  };
  for (const key of ["getInstalledRelatedApps","vibrate"]) {
    const descriptor=Object.getOwnPropertyDescriptor(Navigator.prototype,key);
    output.shape[key]={present:key in navigator,own:Object.hasOwn(navigator,key),
      descriptor:descriptor ? {enumerable:descriptor.enumerable,configurable:descriptor.configurable,
        writable:descriptor.writable,kind:"value" in descriptor?"data":"accessor",method:functionShape(descriptor.value)}:null};
  }
  const cases = [
    ["missing",null],["undefined","undefined"],["null","null"],["zero","0"],["negative","-1"],
    ["nan","NaN"],["infinity","Infinity"],["fraction","2.5"],["large","4294967297"],
    ["string","'13'"],["true","true"],["symbol","Symbol('pattern')"],["bigint","3n"],
    ["empty_array","[]"],["pattern","[1,2,3]"],["typed_array","new Uint16Array([1,2])"],
    ["set","new Set([1,2])"],["plain_object","({})"],
    ["object_valueof","({valueOf(){effects.push('valueOf');return 5;}})"],
    ["iter_undefined","({get [Symbol.iterator](){effects.push('iterator');return undefined;},valueOf(){effects.push('valueOf');return 5;}})"],
    ["iter_null","({get [Symbol.iterator](){effects.push('iterator');return null;},valueOf(){effects.push('valueOf');return 5;}})"],
    ["iter_noncallable","({get [Symbol.iterator](){effects.push('iterator');return 7;},valueOf(){effects.push('valueOf');return 5;}})"],
    ["iter_get_throw","({get [Symbol.iterator](){effects.push('iterator');throw sentinel;}})"],
    ["number_throw","({valueOf(){effects.push('valueOf');throw sentinel;}})"],
    ["sequence_symbol","[Symbol('item')]"],["sequence_bigint","[3n]"],
    ["function","Object.assign(function(){},{valueOf(){effects.push('valueOf');return 3;}})"],
    ["iter_number","({[Symbol.iterator](){effects.push('call');return 4;}})"],
    ["iter_next_number","({[Symbol.iterator](){return {next(){effects.push('next');return 4;},return(){effects.push('return');return {};}};}})"],
    ["iter_done_throw","({[Symbol.iterator](){return {next(){effects.push('next');return {get done(){effects.push('done');throw sentinel;}};},return(){effects.push('return');return {};}};}})"],
    ["iter_value_throw","({[Symbol.iterator](){return {next(){effects.push('next');return {done:false,get value(){effects.push('value');throw sentinel;}};},return(){effects.push('return');return {};}};}})"],
    ["iter_element_throw","({[Symbol.iterator](){return {next(){effects.push('next');return {done:false,value:{valueOf(){effects.push('coerce');throw sentinel;}}};},return(){effects.push('return');return {};}};}})"],
    ["long_sequence","Array.from({length:12},(_,index)=>({valueOf(){effects.push(index);return index;}}))"],
    ["iter_sequence","({get [Symbol.iterator](){effects.push('iterator');return function(){effects.push('call');let index=0;return {get next(){effects.push('get next');return function(){effects.push('next');return index++===0?{get done(){effects.push('done');return false;},get value(){effects.push('value');return {valueOf(){effects.push('coerce');return 1;}};}}:{done:true};};}};};}})"],
    ["negative_fraction","-2.5"],["false","false"],["negative_infinity","-Infinity"],
    ["boxed_number","Object(2.5)"],["boxed_string","Object('12')"],
    ["numeric_symbol_result","({valueOf(){effects.push('valueOf');return Symbol('numeric');}})"],
    ["numeric_bigint_result","({valueOf(){effects.push('valueOf');return 1n;}})"],
    ["numeric_typeerror","({valueOf(){effects.push('valueOf');throw new TypeError('user numeric');}})"],
    ["numeric_symbol_hint","({[Symbol.toPrimitive](hint){effects.push(hint);return 4;}})"],
    ["numeric_null_prototype","Object.create(null)"],
    ["array_iterator_getter","(()=>{const value=[1,2];Object.defineProperty(value,Symbol.iterator,{get(){effects.push('iterator');throw sentinel;}});return value;})()"],
    ["array_iterator_override","Object.assign([1,2],{[Symbol.iterator](){effects.push('call');throw sentinel;}})"],
    ["array_iterator_number","Object.assign([1,2],{[Symbol.iterator]:17})"],
    ["array_dynamic_shrink","(()=>{const value=[{valueOf(){effects.push('first');value.length=1;return 1;}},{valueOf(){effects.push('second');throw sentinel;}}];return value;})()"],
    ["array_dynamic_grow","(()=>{const value=[{valueOf(){effects.push('first');value.push({valueOf(){effects.push('second');throw sentinel;}});return 1;}}];return value;})()"],
    ["array_proxy","new Proxy([1,2],{get(target,key,receiver){effects.push(String(key));return Reflect.get(target,key,receiver);}})"],
    ["array_100_tail_throw","Array.from({length:101},(_,index)=>({valueOf(){effects.push(index);if(index===100)throw sentinel;return index;}}))"],
    ["iter_next_missing","({[Symbol.iterator](){return {get next(){effects.push('get next');return undefined;},get return(){effects.push('get return');throw sentinel;}};}})"],
    ["iter_next_noncallable","({[Symbol.iterator](){return {next:17,get return(){effects.push('get return');throw sentinel;}};}})"],
    ["iter_next_get_throw","({[Symbol.iterator](){return {get next(){effects.push('get next');throw sentinel;},get return(){effects.push('get return');throw Error('close');}};}})"],
    ["iter_next_throw","({[Symbol.iterator](){return {next(){effects.push('next');throw sentinel;},get return(){effects.push('get return');throw Error('close');}};}})"],
    ["iter_done_true_value_throw","({[Symbol.iterator](){return {next(){effects.push('next');return {get value(){effects.push('value');throw sentinel;},get done(){effects.push('done');return true;}};},get return(){effects.push('get return');throw Error('close');}};}})"],
    ["iter_done_true_value_object","({[Symbol.iterator](){return {next(){effects.push('next');return {get value(){effects.push('value');return {valueOf(){effects.push('coerce');throw sentinel;}};},get done(){effects.push('done');return true;}};}};}})"],
    ["iter_second_get_undefined","(()=>{let reads=0;return {get [Symbol.iterator](){effects.push('iterator');if(++reads===1)return function(){};return undefined;}};})()"],
    ["iter_second_get_noncallable","(()=>{let reads=0;return {get [Symbol.iterator](){effects.push('iterator');if(++reads===1)return function(){};return 17;}};})()"],
    ["iter_second_get_throw","(()=>{let reads=0;return {get [Symbol.iterator](){effects.push('iterator');if(++reads===1)return function(){};throw sentinel;}};})()"],
    ["iter_element_return_getter","({[Symbol.iterator](){return {next(){effects.push('next');return {value:{valueOf(){effects.push('coerce');throw sentinel;}},done:false};},get return(){effects.push('get return');throw Error('close');}};}})"],
    ["ignored_extra","(()=>{const value={valueOf(){effects.push('valueOf');return 1;}};return value;})()"],
    ["numeric_hook_noncallable","({[Symbol.toPrimitive]:17})"],
    ["numeric_hook_object","({[Symbol.toPrimitive]:new Proxy({},{get(){effects.push('unexpected');throw sentinel;}})})"],
    ["numeric_hook_returns_object","({[Symbol.toPrimitive](hint){effects.push(hint);return {};}})"],
    ["numeric_ordinary_no_primitive","({valueOf(){effects.push('valueOf');return {};},toString(){effects.push('toString');return {};}})"],
    ["typeerror_sentinel_number","({valueOf(){effects.push('valueOf');throw sentinel;}})"],
    ["typeerror_sentinel_element","[{valueOf(){effects.push('valueOf');throw sentinel;}}]"],
  ];
  output.vibrationSources=Object.fromEntries(cases);
  for (const [name,source] of cases) {
    const effects=[];
    const sentinel=name.startsWith("typeerror_sentinel")?new TypeError("original TypeError sentinel"):{sentinel:name};
    try {
      const value=source===null ? navigator.vibrate() : navigator.vibrate(eval(source));
      output.vibration[name]={value,effects};
    } catch(error) {
      output.vibration[name]={error:{name:error.name??null,message:error.message??null,same:error===sentinel},effects};
    }
  }
  output.vibration.invalidReceiver=(() => {
    const effects=[];
    try {return {value:navigator.vibrate.call({},{get [Symbol.iterator](){effects.push('iterator');return undefined;},valueOf(){effects.push('valueOf');return 1;}}),effects};}
    catch(error){return {error:{name:error.name,message:error.message},effects};}
  })();
  if (typeof navigator.getInstalledRelatedApps==="function") {
    const settle=async callback => {
      try {const promise=callback();try{return {promise:promise instanceof Promise,value:await promise};}
        catch(error){return {promise:promise instanceof Promise,error:{name:error.name,message:error.message}};}}
      catch(error){return {promise:false,synchronous:true,error:{name:error.name,message:error.message}};}
    };
    output.related.valid=await settle(()=>navigator.getInstalledRelatedApps());
    const first=navigator.getInstalledRelatedApps(), second=navigator.getInstalledRelatedApps();
    const left=await first,right=await second;
    output.related.identity={promisesFresh:first!==second,arraysFresh:left!==right,array:Array.isArray(left),frozen:Object.isFrozen(left)};
    output.related.invalidReceiver=await settle(()=>navigator.getInstalledRelatedApps.call({}));
    output.related.invalidReceivers={};
    for(const [name,receiver] of [["undefined",undefined],["null",null],["prototype",Navigator.prototype],
        ["proxy",new Proxy(navigator,{})],["forged",Object.create(Navigator.prototype)]]) {
      output.related.invalidReceivers[name]=await settle(()=>navigator.getInstalledRelatedApps.call(receiver));
    }
    output.related.extraArgument=await settle(()=>navigator.getInstalledRelatedApps(new Proxy({},{get(){throw Error('unused');}})));
    const effects=[];
    const pending=navigator.getInstalledRelatedApps().then(()=>effects.push('resolved'));
    effects.push('sync');await pending;output.related.order=effects;
    const inheritedThen = Object.getOwnPropertyDescriptor(Array.prototype,"then");
    const timing=[];
    Object.defineProperty(Array.prototype,"then",{configurable:true,get(){timing.push("then lookup");return undefined;}});
    try {
      const pendingArray=navigator.getInstalledRelatedApps();
      timing.push("after call");
      pendingArray.then(()=>timing.push("resolved"));
      await Promise.resolve();timing.push("microtask");
      await pendingArray;
      output.related.thenTiming=timing.slice();
    } finally {
      if(inheritedThen)Object.defineProperty(Array.prototype,"then",inheritedThen);else delete Array.prototype.then;
    }
  }
  const pre=document.createElement("pre");pre.id="bom02-reference";pre.textContent=JSON.stringify(output);document.body.appendChild(pre);
  return output;
})();
