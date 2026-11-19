(() => {
  'use strict';
  const functionToString = Function.prototype.toString;
  const objectToString = Object.prototype.toString;
  const apply = Reflect.apply;
  const methods = {
    'Function.prototype.toString': functionToString,
    'Object.prototype.toString': objectToString
  };
  function functionShape(value) {
    let constructable = true;
    try { Reflect.construct(function () {}, [], value); } catch { constructable = false; }
    return {name:value.name, length:value.length, source:apply(functionToString,value,[]),
      ownPrototype:Object.hasOwn(value,'prototype'), constructable,
      ownKeys:Reflect.ownKeys(value).map(String),
      functionPrototype:Object.getPrototypeOf(value) === Function.prototype};
  }
  function descriptorShape(descriptor) {
    return {kind:'value' in descriptor ? 'data' : 'accessor',
      writable:descriptor.writable, enumerable:descriptor.enumerable, configurable:descriptor.configurable,
      value:typeof descriptor.value === 'function' ? functionShape(descriptor.value) : descriptor.value};
  }
  const shapes = {};
  for (const [name,method] of Object.entries(methods)) {
    const owner = name.startsWith('Function') ? Function.prototype : Object.prototype;
    shapes[name] = {function:functionShape(method), descriptor:descriptorShape(Object.getOwnPropertyDescriptor(owner,'toString')),
      ownDescriptors:{name:descriptorShape(Object.getOwnPropertyDescriptor(method,'name')),
        length:descriptorShape(Object.getOwnPropertyDescriptor(method,'length'))},
      repeatedIdentity:method === owner.toString};
  }
  function state(receiver) {
    return {receiver, effects:{tagGetterCalls:0, getKeys:[], getPrototypeOf:0, ownKeys:0,
      getOwnPropertyDescriptor:0, conversionCalls:0, nameGetterCalls:0}, hasSentinel:false};
  }
  function sampleFunction() { return function Sample(value) { return value + 1; }; }
  function tagged(value, receiver = {}) {
    const sample=state(receiver);
    Object.defineProperty(receiver,Symbol.toStringTag,{configurable:true,
      get(){sample.effects.tagGetterCalls++;return value;}});
    return sample;
  }
  function throwingTag(kind) {
    const sample=state({});
    sample.hasSentinel=true;
    sample.sentinel=kind==='undefined' ? undefined : kind==='string' ? 'original tag sentinel' : {marker:'original tag sentinel'};
    Object.defineProperty(sample.receiver,Symbol.toStringTag,{configurable:true,
      get(){sample.effects.tagGetterCalls++;throw sample.sentinel;}});
    return sample;
  }
  function proxySample(target, revoked) {
    const sample=state(null);
    const pair=Proxy.revocable(target,{
      get(object,key,receiver){sample.effects.getKeys.push(String(key));return Reflect.get(object,key,receiver);},
      getPrototypeOf(object){sample.effects.getPrototypeOf++;return Reflect.getPrototypeOf(object);},
      ownKeys(object){sample.effects.ownKeys++;return Reflect.ownKeys(object);},
      getOwnPropertyDescriptor(object,key){sample.effects.getOwnPropertyDescriptor++;return Reflect.getOwnPropertyDescriptor(object,key);}
    });
    sample.receiver=pair.proxy;
    if(revoked)pair.revoke();
    return sample;
  }
  const factories = {
    undefined:()=>state(undefined), null:()=>state(null), boolean:()=>state(true),
    number:()=>state(17), nan:()=>state(NaN), string:()=>state('sample'),
    bigint:()=>state(17n), symbol:()=>state(Symbol('sample')),
    boxedNumber:()=>state(Object(17)), boxedString:()=>state(Object('sample')),
    boxedSymbol:()=>state(Object(Symbol('sample'))),
    plainObject:()=>state({}), nullPrototype:()=>state(Object.create(null)), array:()=>state([]),
    date:()=>state(new Date(0)), map:()=>state(new Map()), set:()=>state(new Set()),
    regexp:()=>state(/sample/), error:()=>state(new Error('sample')),
    ordinaryFunction:()=>state(sampleFunction()), arrowFunction:()=>state((value) => value + 1),
    asyncFunction:()=>state(async function AsyncSample() {}),
    generatorFunction:()=>state(function* GeneratorSample() {}),
    classConstructor:()=>state(class SampleClass {}),
    nativeFunction:()=>state(Math.max), functionPrototype:()=>state(Function.prototype),
    boundUserFunction:()=>state(sampleFunction().bind(null)),
    boundNativeFunction:()=>state(Math.max.bind(null)),
    callableProxy:()=>proxySample(sampleFunction(),false),
    objectProxy:()=>proxySample({},false), arrayProxy:()=>proxySample([],false),
    revokedCallableProxy:()=>proxySample(sampleFunction(),true),
    revokedObjectProxy:()=>proxySample({},true),
    customTag:()=>tagged('CustomTag'), emptyTag:()=>tagged(''),
    numericTag:()=>tagged(17), nullTag:()=>tagged(null), undefinedTag:()=>tagged(undefined),
    symbolTag:()=>tagged(Symbol('ignored')),
    boxedTag:()=>tagged(Object('ignored')),
    callableCustomTag:()=>tagged('CallableTag',sampleFunction()),
    throwingObjectTag:()=>throwingTag('object'), throwingStringTag:()=>throwingTag('string'),
    throwingUndefinedTag:()=>throwingTag('undefined'),
    poisonedOrdinaryProperties:()=>{
      const sample=state({});
      for(const name of ['toString','valueOf','constructor'])Object.defineProperty(sample.receiver,name,
        {configurable:true,get(){sample.effects.conversionCalls++;throw new Error('must not read '+name);}});
      return sample;
    },
    poisonedFunctionName:()=>{
      const sample=state(sampleFunction());
      Object.defineProperty(sample.receiver,'name',{configurable:true,get(){sample.effects.nameGetterCalls++;throw new Error('must not read function name');}});
      return sample;
    }
  };
  function observe(operation,sample) {
    let outcome;
    try {const value=operation();outcome={returned:value === undefined ? '<undefined>' : value};}
    catch(error) {
      if(sample.hasSentinel && error === sample.sentinel)outcome={threwSameSentinel:true,sentinelType:typeof error};
      else outcome={name:error.name,message:error.message};
    }
    return {outcome,effects:sample.effects};
  }
  const receivers = {};
  for(const [label,create] of Object.entries(factories)) {
    receivers[label]={};
    for(const [name,method] of Object.entries(methods)) {
      const sample=create();
      receivers[label][name]=observe(()=>apply(method,sample.receiver,[]),sample);
    }
  }
  const forms = {};
  const invokers = {
    call:(method,receiver)=>method.call(receiver),
    apply:(method,receiver)=>method.apply(receiver,[]),
    bind:(method,receiver)=>method.bind(receiver)(),
    reflectApply:(method,receiver)=>Reflect.apply(method,receiver,[])
  };
  for(const [name,method] of Object.entries(methods)) {
    forms[name]={};
    for(const label of ['undefined','null','number','ordinaryFunction','plainObject','customTag',
                        'throwingObjectTag','callableProxy','revokedCallableProxy']) {
      forms[name][label]={};
      for(const [form,invoke] of Object.entries(invokers)) {
        const sample=factories[label]();
        forms[name][label][form]=observe(()=>invoke(method,sample.receiver),sample);
      }
    }
  }
  const direct = {};
  for(const [name,method] of Object.entries(methods)) {
    direct[name]=observe(()=>method(),state(undefined));
  }
  direct.functionPrototypeMember=observe(()=>Function.prototype.toString(),state(Function.prototype));
  direct.objectPrototypeMember=observe(()=>Object.prototype.toString(),state(Object.prototype));
  const directFunction=factories.ordinaryFunction();
  direct.functionNaturalMember=observe(()=>directFunction.receiver.toString(),directFunction);
  const directObject=factories.customTag();
  direct.objectNaturalMember=observe(()=>directObject.receiver.toString(),directObject);
  const boundCreation = {};
  for(const [name,method] of Object.entries(methods)) {
    const sample=factories.customTag();
    const bound=method.bind(sample.receiver);
    const creationEffects={...sample.effects,getKeys:[...sample.effects.getKeys]};
    boundCreation[name]={shape:functionShape(bound),creationEffects,
      invoked:observe(()=>bound(),sample)};
  }
  const result={conditions:{url:location.href,origin:location.origin,isSecureContext,worker:false,userAgent:navigator.userAgent},
    shapes,identities:{differentMethods:functionToString !== objectToString,
      functionInherited:sampleFunction().toString === functionToString,
      objectInherited:({}).toString === objectToString},receivers,forms,direct,boundCreation};
  const output=document.createElement('pre');output.id='to-string-reference';output.textContent=JSON.stringify(result);
  document.body.replaceChildren(output);
})();
