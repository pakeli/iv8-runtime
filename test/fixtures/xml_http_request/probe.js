(() => {
  'use strict';
  const nativeSource = Function.prototype.toString;
  const functionShape = value => {
    let constructable = true;
    try { Reflect.construct(function () {}, [], value); } catch { constructable = false; }
    return {name:value.name, length:value.length, source:nativeSource.call(value),
      ownPrototype:Object.hasOwn(value,'prototype'), constructable,
      ownKeys:Reflect.ownKeys(value).map(String)};
  };
  const primitive = value => value === undefined ? '<undefined>' :
    typeof value === 'number' && !Number.isFinite(value) ? String(value) :
    Array.isArray(value) ? value.map(primitive) :
    value !== null && typeof value === 'object' ? {tag:Object.prototype.toString.call(value)} :
    typeof value === 'symbol' ? String(value) : value;
  const descriptorShape = value => {
    const result = {enumerable:value.enumerable, configurable:value.configurable};
    if ('value' in value) Object.assign(result,{kind:'data', writable:value.writable,
      value:typeof value.value === 'function' ? functionShape(value.value) : primitive(value.value)});
    else Object.assign(result,{kind:'accessor', get:value.get ? functionShape(value.get) : null,
      set:value.set ? functionShape(value.set) : null});
    return result;
  };
  const ownSurface = object => Object.fromEntries(Reflect.ownKeys(object).map(key =>
    [String(key),descriptorShape(Object.getOwnPropertyDescriptor(object,key))]));
  const chain = object => {
    const result=[];
    for(let current=object;current && result.length<8;current=Object.getPrototypeOf(current)) {
      const constructor=Object.getOwnPropertyDescriptor(current,'constructor');
      result.push({tag:Object.prototype.toString.call(current),
        ownConstructor:constructor && typeof constructor.value==='function' ? constructor.value.name : null,
        ownKeys:Reflect.ownKeys(current).map(String)});
    }
    return result;
  };
  const outcome = action => {
    try {return {returned:primitive(action())};}
    catch(error) {return {name:error.name,message:error.message,code:error.code};}
  };
  const locate = (object,key) => {
    for(let holder=object,depth=0;holder;holder=Object.getPrototypeOf(holder),depth++) {
      const descriptor=Object.getOwnPropertyDescriptor(holder,key);
      if(descriptor)return {depth,owner:Object.prototype.toString.call(holder),descriptor:descriptorShape(descriptor)};
    }
    return null;
  };
  const xhr = new XMLHttpRequest();
  const upload = xhr.upload;
  const constructors = {};
  for(const name of ['XMLHttpRequest','XMLHttpRequestEventTarget','XMLHttpRequestUpload']) {
    const constructor=globalThis[name];
    constructors[name]={function:functionShape(constructor),globalDescriptor:descriptorShape(Object.getOwnPropertyDescriptor(globalThis,name)),
      ownSurface:ownSurface(constructor),prototypeSurface:ownSurface(constructor.prototype),
      constructorParent:Object.getPrototypeOf(constructor).name,
      callWithoutNew:outcome(()=>constructor()),
      construct:outcome(()=>{const value=new constructor();return Object.prototype.toString.call(value);})};
  }
  const fields=['readyState','timeout','withCredentials','upload','responseURL','status','statusText','responseType','response','responseText','responseXML'];
  const handlers=['onreadystatechange','onloadstart','onprogress','onabort','onerror','onload','ontimeout','onloadend'];
  const defaults={},owners={},uploadOwners={};
  for(const name of [...fields,...handlers]) {
    defaults[name]=outcome(()=>name==='upload'?Object.prototype.toString.call(xhr[name]):xhr[name]);
    owners[name]=locate(xhr,name);
  }
  for(const name of handlers.slice(1))uploadOwners[name]=locate(upload,name);
  const wrongReceivers={methods:{},getters:{},setters:{}};
  const methodArguments={open:['GET','/unused'],setRequestHeader:['X-Probe','value'],send:[],abort:[],
    getResponseHeader:['X-Probe'],getAllResponseHeaders:[],overrideMimeType:['text/plain']};
  for(const [name,arguments_] of Object.entries(methodArguments)) {
    const method=XMLHttpRequest.prototype[name];
    const samples={};
    for(const [kind,receiver] of [['plain',{}],['prototype',XMLHttpRequest.prototype],['forged',Object.create(XMLHttpRequest.prototype)],['proxy',new Proxy(xhr,{get(){throw Error('receiver get trap');}})]]) {
      let conversions=0;
      const bad={toString(){conversions++;throw Error('argument coercion');},valueOf(){conversions++;throw Error('argument coercion');}};
      const argumentsForBrand=arguments_.map(()=>bad);
      samples[kind]=outcome(()=>method.call(receiver,...argumentsForBrand));
      samples[kind].argumentConversions=conversions;
    }
    wrongReceivers.methods[name]=samples;
  }
  for(const name of [...fields,...handlers]) {
    let holder=xhr;
    while(!Object.getOwnPropertyDescriptor(holder,name))holder=Object.getPrototypeOf(holder);
    const descriptor=Object.getOwnPropertyDescriptor(holder,name);
    if(descriptor.get)wrongReceivers.getters[name]=outcome(()=>descriptor.get.call({}));
    if(descriptor.set){let conversions=0;
      wrongReceivers.setters[name]=outcome(()=>descriptor.set.call({}, {toString(){conversions++;throw Error('must not coerce');}}));
      wrongReceivers.setters[name].argumentConversions=conversions;
    }
  }
  const handlerSemantics={};
  for(const [label,target,name] of [['xhr',xhr,'onreadystatechange'],['upload',upload,'onprogress']]) {
    const callback=()=>false;
    target[name]=callback;
    const assignedIdentity=target[name]===callback;
    target[name]=42;
    handlerSemantics[label]={initialNull:defaults[name]?.returned===null || label==='upload',assignedIdentity,
      numberClears:target[name]===null,enumeratedOnInstance:Object.keys(target).includes(name)};
    target[name]=callback;target[name]={};handlerSemantics[label].objectClears=target[name]===null;
  }
  const synthetic=[];
  upload.addEventListener('progress',event=>synthetic.push({channel:'listener',type:event.type,
    target:event.target===upload,currentTarget:event.currentTarget===upload,eventPhase:event.eventPhase,isTrusted:event.isTrusted}));
  upload.onprogress=event=>{synthetic.push({channel:'handler',type:event.type,target:event.target===upload});return false;};
  const event=new Event('progress',{cancelable:true});
  const syntheticReturned=upload.dispatchEvent(event);
  const localErrors={};
  const test=(name,action)=>{localErrors[name]=outcome(action);};
  test('sendBeforeOpen',()=>new XMLHttpRequest().send());
  test('headerBeforeOpen',()=>new XMLHttpRequest().setRequestHeader('X-Probe','value'));
  test('responseHeaderBeforeOpen',()=>new XMLHttpRequest().getResponseHeader('X-Probe'));
  test('allHeadersBeforeOpen',()=>new XMLHttpRequest().getAllResponseHeaders());
  test('overrideMimeBeforeOpen',()=>new XMLHttpRequest().overrideMimeType('text/plain'));
  test('abortBeforeOpen',()=>{const value=new XMLHttpRequest();value.abort();return value.readyState;});
  test('openGetAsync',()=>{const value=new XMLHttpRequest();value.open('GET','/unused');return value.readyState;});
  test('openGetSync',()=>{const value=new XMLHttpRequest();value.open('GET','/unused',false);return value.readyState;});
  test('openInvalidMethod',()=>new XMLHttpRequest().open('bad method','/unused'));
  test('openForbiddenMethod',()=>new XMLHttpRequest().open('TRACE','/unused'));
  test('openInvalidURL',()=>new XMLHttpRequest().open('GET','http://['));
  test('openSyncNonzeroTimeout',()=>{const value=new XMLHttpRequest();value.timeout=1;value.open('GET','/unused',false);});
  test('syncTimeoutSetter',()=>{const value=new XMLHttpRequest();value.open('GET','/unused',false);value.timeout=1;});
  test('syncResponseTypeSetter',()=>{const value=new XMLHttpRequest();value.open('GET','/unused',false);value.responseType='json';});
  test('invalidResponseTypeIgnored',()=>{const value=new XMLHttpRequest();value.responseType='madeup';return value.responseType;});
  test('subclass',()=>{class CustomRequest extends XMLHttpRequest{};const value=new CustomRequest();return [value instanceof CustomRequest,value instanceof XMLHttpRequest,value.upload===value.upload];});
  const result={conditions:{url:location.href,origin:location.origin,isSecureContext,worker:false,userAgent:navigator.userAgent},constructors,
    prototypeChains:{xhr:chain(xhr),upload:chain(upload)},defaults,owners,uploadOwners,
    identities:{uploadSameObject:xhr.upload===xhr.upload,uploadInstance:upload instanceof XMLHttpRequestUpload,
      uploadEventTarget:upload instanceof XMLHttpRequestEventTarget,uploadGenericTarget:upload instanceof EventTarget,
      xhrEventTarget:xhr instanceof XMLHttpRequestEventTarget,
      eventTargetMethodsShared:xhr.addEventListener===EventTarget.prototype.addEventListener && upload.dispatchEvent===EventTarget.prototype.dispatchEvent},
    handlerSemantics,syntheticUpload:{events:synthetic,returned:syntheticReturned,defaultPrevented:event.defaultPrevented},wrongReceivers,localErrors};
  const output=document.createElement('pre');output.id='xhr-reference';output.textContent=JSON.stringify(result);
  document.body.replaceChildren(output);
})();
