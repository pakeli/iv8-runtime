(async () => {
  'use strict';
  const out={secure:isSecureContext,origin:location.origin,shape:{},calls:{},sources:{},errors:{}};
  const quota=navigator.webkitTemporaryStorage;
  const proto=Object.getPrototypeOf(quota);
  const summarizeError=error=>({name:error.name,message:error.message});
  const functionShape=fn=>{
    let construct;
    try{Reflect.construct(fn,[]);construct='constructed';}catch(error){construct=error.name;}
    return {name:fn.name,length:fn.length,source:Function.prototype.toString.call(fn),
      prototype:Object.hasOwn(fn,'prototype'),construct};
  };
  const descriptor=(object,key)=>{
    const item=Object.getOwnPropertyDescriptor(object,key);if(!item)return null;
    return {enumerable:item.enumerable,configurable:item.configurable,
      ...('value'in item?{kind:'data',writable:item.writable,
        value:typeof item.value==='function'?functionShape(item.value):item.value}:
        {kind:'accessor',get:item.get?functionShape(item.get):null,set:item.set?functionShape(item.set):null})};
  };
  for(const name of ['webkitTemporaryStorage','webkitPersistentStorage'])
    out.shape[name]={present:name in navigator,own:Object.hasOwn(navigator,name),
      descriptor:descriptor(Navigator.prototype,name)};
  out.shape.quota={sameAliases:quota===navigator.webkitPersistentStorage,stable:quota===navigator.webkitTemporaryStorage,
    tag:Object.prototype.toString.call(quota),ownKeys:Reflect.ownKeys(quota).map(String),
    prototypeKeys:Reflect.ownKeys(proto).map(String),prototypeParentIsObject:Object.getPrototypeOf(proto)===Object.prototype,
    globalType:typeof DeprecatedStorageQuota,constructorIsObject:quota.constructor===Object,
    methods:Object.fromEntries(['queryUsageAndQuota','requestQuota'].map(name=>[name,descriptor(proto,name)])),
    tagDescriptor:descriptor(proto,Symbol.toStringTag)};
  out.shape.domError={globalType:typeof DOMError};
  if(typeof DOMError==='function'){
    const instance=new DOMError('CustomError','custom message');
    out.shape.domError={globalType:'function',constructor:functionShape(DOMError),
      ownKeys:Reflect.ownKeys(instance).map(String),prototypeKeys:Reflect.ownKeys(DOMError.prototype).map(String),
      prototypeParentIsObject:Object.getPrototypeOf(DOMError.prototype)===Object.prototype,
      constructorParentIsFunction:Object.getPrototypeOf(DOMError)===Function.prototype,
      instanceOfError:instance instanceof Error,tag:Object.prototype.toString.call(instance),
      name:instance.name,message:instance.message,
      members:Object.fromEntries(['name','message','constructor'].map(name=>[name,descriptor(DOMError.prototype,name)])),
      tagDescriptor:descriptor(DOMError.prototype,Symbol.toStringTag)};
    for(const [name,source] of [['missing','new DOMError()'],['undefined','new DOMError(undefined)'],
      ['null','new DOMError(null,null)'],['omitted_message',"new DOMError('Name')"],['undefined_message',"new DOMError('Name',undefined)"],['known_name',"new DOMError('AbortError')"],
      ['call',"DOMError('Name')"],['symbol',"new DOMError(Symbol('name'))"],['symbol_message',"new DOMError('Name',Symbol('message'))"]]){
      try{const value=eval(source);out.errors[name]={name:value.name,message:value.message};}
      catch(error){out.errors[name]={error:summarizeError(error)};}
    }
    let prototypeReads=0;
    const childPrototype=Object.create(DOMError.prototype);
    const target=new Proxy(function(){},{get(target,key,receiver){
      if(key==='prototype'){prototypeReads++;return childPrototype;}
      return Reflect.get(target,key,receiver);
    }});
    const constructed=Reflect.construct(DOMError,['CustomError'],target);
    out.shape.domError.reflectConstruct={prototypeReads,prototypeMatches:Object.getPrototypeOf(constructed)===childPrototype,
      name:constructed.name,message:constructed.message};
  }
  async function observe(name,method,source,expectCallback=true){
    const effects=[],sentinel={sentinel:name};let called=false;let finish;
    const completion=new Promise(resolve=>finish=resolve);
    const success=function(...values){called=true;effects.push({kind:'success',thisUndefined:this===undefined,values});finish();};
    const failure=function(error){called=true;effects.push({kind:'failure',thisUndefined:this===undefined,
      error:summarizeError(error),tag:Object.prototype.toString.call(error),instanceOfError:error instanceof Error,
      domError:typeof DOMError==='function'&&error instanceof DOMError,ownKeys:Reflect.ownKeys(error).map(String)});finish();};
    const record={};
    try{const args=eval(source);const result=Reflect.apply(quota[method],quota,args);record.returned=result===undefined?'undefined':typeof result;}
    catch(error){record.error={...summarizeError(error),same:error===sentinel};expectCallback=false;}
    effects.push('after call');
    await Promise.resolve();effects.push('microtask');
    if(expectCallback&&!called){let timer;await Promise.race([completion,new Promise(resolve=>timer=setTimeout(()=>{record.timeout=true;resolve();},3000))]);clearTimeout(timer);}
    record.effects=effects;out.calls[name]=record;out.sources[name]={method,source,expectCallback};
  }
  await observe('query_ok','queryUsageAndQuota','[success,failure]');
  await observe('request_zero','requestQuota','[0,success,failure]');
  await observe('request_small','requestQuota','[17,success,failure]');
  await observe('request_negative','requestQuota','[-1,success,failure]');
  for(const [name,method,source] of [
    ['query_missing','queryUsageAndQuota','[]'],['query_null','queryUsageAndQuota','[null,failure]'],
    ['query_undefined','queryUsageAndQuota','[undefined,failure]'],['query_object','queryUsageAndQuota','[{},failure]'],
    ['query_bad_error','queryUsageAndQuota','[success,17]'],['query_null_error','queryUsageAndQuota','[success,null]'],
    ['request_missing','requestQuota','[]'],['request_no_callbacks','requestQuota','[1]'],
    ['request_null_callbacks','requestQuota','[1,null,null]'],['request_bad_callback','requestQuota','[1,{},failure]'],
    ['request_bad_error','requestQuota','[1,success,{}]'],['request_symbol','requestQuota',"[Symbol('size'),success,failure]"],
    ['request_bigint','requestQuota','[1n,success,failure]'],
    ['request_nan','requestQuota','[NaN,success,failure]'],['request_inf','requestQuota','[Infinity,success,failure]'],
    ['request_fraction','requestQuota','[7.9,success,failure]'],['request_wrap','requestQuota','[18446744073709551616,success,failure]'],
    ['request_string','requestQuota',"['17',success,failure]"],
    ['request_convert','requestQuota',"[{valueOf(){effects.push('valueOf');return 17;}},success,failure]"],
    ['request_negative_fraction','requestQuota','[-0.5,success,failure]'],
    ['request_primitive','requestQuota',"[{[Symbol.toPrimitive](hint){effects.push(hint);return 13;}},success,failure]"],
    ['request_object_symbol','requestQuota',"[{valueOf(){effects.push('valueOf');return Symbol('size');}},success,failure]"],
    ['query_proxy_callback','queryUsageAndQuota',"[new Proxy(success,{apply(target,receiver,args){effects.push('apply');return Reflect.apply(target,receiver,args);}}),failure]"],
    ['query_html_dda','queryUsageAndQuota','[document.all,failure]'],
    ['request_throw','requestQuota',"[{valueOf(){effects.push('valueOf');throw sentinel;}},success,failure]"],
    ['request_conversion_before_callback','requestQuota',"[{valueOf(){effects.push('valueOf');return 1;}},17,failure]"],
  ])await observe(name,method,source,name==='query_html_dda'?location.origin==='null':
    !['request_no_callbacks','request_null_callbacks'].includes(name));
  for(const method of ['queryUsageAndQuota','requestQuota']){
    try{quota[method].call({});out.errors[method+'Receiver']={returned:true};}
    catch(error){out.errors[method+'Receiver']={error:summarizeError(error)};}
  }
  for(const name of ['webkitTemporaryStorage','webkitPersistentStorage']){
    try{Object.getOwnPropertyDescriptor(Navigator.prototype,name).get.call({});out.errors[name+'Receiver']={returned:true};}
    catch(error){out.errors[name+'Receiver']={error:summarizeError(error)};}
  }
  const pre=document.createElement('pre');pre.id='bom03-reference';pre.textContent=JSON.stringify(out);document.body.appendChild(pre);
  return out;
})();
