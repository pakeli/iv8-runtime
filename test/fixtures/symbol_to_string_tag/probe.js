(() => {
  'use strict';
  const tag=Symbol.toStringTag;
  const objectToString=Object.prototype.toString;
  const reflectGet=Reflect.get;
  const names=new WeakMap();
  for(const name of ['Window','Document','HTMLDocument','Element','HTMLDivElement','Navigator','Screen','Location',
    'EventTarget','XMLHttpRequest','XMLHttpRequestUpload','Storage','HTMLCollection','HTMLAllCollection',
    'NamedNodeMap','DOMStringMap','URL','URLSearchParams','Blob','File','Attr','MutationRecord',
    'NavigatorUAData','UserActivation','DOMStringList']) {
    const constructor=globalThis[name];
    if(typeof constructor==='function' && constructor.prototype)names.set(constructor.prototype,name+'.prototype');
  }
  const windowProperties=Object.getPrototypeOf(Window.prototype);
  names.set(windowProperties,'WindowProperties');
  if(typeof CSS==='object')names.set(CSS,'CSS');
  const element=document.createElement('div');element.id='symbol-reference';element.setAttribute('data-value','one');
  document.body.appendChild(element);
  const xhr=new XMLHttpRequest();
  const observer=new MutationObserver(()=>{});observer.observe(element,{attributes:true});
  element.setAttribute('title','changed');const mutationRecord=observer.takeRecords()[0];observer.disconnect();
  const targets={
    window,'Window.prototype':Window.prototype,WindowProperties:windowProperties,
    document,'Document.prototype':Document.prototype,'HTMLDocument.prototype':HTMLDocument.prototype,
    element,'Element.prototype':Element.prototype,'HTMLDivElement.prototype':HTMLDivElement.prototype,
    'EventTarget.prototype':EventTarget.prototype,navigator,screen,location,
    xhr,upload:xhr.upload,localStorage,sessionStorage,
    htmlCollection:document.getElementsByTagName('div'),htmlAllCollection:document.all,
    namedNodeMap:element.attributes,domStringMap:element.dataset,
    url:new URL('/value',location.href),urlSearchParams:new URLSearchParams('a=1'),
    blob:new Blob(['value']),file:new File(['value'],'sample.txt'),
    attr:element.attributes.item(0),mutationRecord,
    userAgentData:navigator.userAgentData,userActivation:navigator.userActivation,
    ancestorOrigins:location.ancestorOrigins,CSS
  };
  const descriptorShape=descriptor=>descriptor?{kind:'value'in descriptor?'data':'accessor',
    value:descriptor.value===undefined?'<undefined>':descriptor.value,
    writable:descriptor.writable,enumerable:descriptor.enumerable,configurable:descriptor.configurable}:null;
  const outcome=operation=>{
    try{const value=operation();return {returned:value===undefined?'<undefined>':value};}
    catch(error){return {name:error.name,message:error.message};}
  };
  function locate(target){
    for(let owner=target,depth=0;owner!==null && owner!==undefined && depth<12;owner=Object.getPrototypeOf(owner),depth++){
      const descriptor=Object.getOwnPropertyDescriptor(owner,tag);
      if(descriptor)return {owner,depth,descriptor};
    }
    return null;
  }
  const owners={},objects={};
  for(const [label,target] of Object.entries(targets)){
    if(target===undefined || target===null){objects[label]={available:false};continue;}
    const found=locate(target);
    const value=target[tag];
    const row={available:true,value:value===undefined?'<undefined>':value,
      objectToString:Reflect.apply(objectToString,target,[]),ownTag:Object.hasOwn(target,tag),
      repeatedReadSame:value===target[tag]};
    if(found){
      const ownerName=names.get(found.owner) || String(found.descriptor.value)+'.prototype';
      row.owner=ownerName;row.depth=found.depth;
      if(!Object.hasOwn(owners,ownerName))owners[ownerName]={descriptor:descriptorShape(found.descriptor),
        directValue:found.owner[tag],objectToString:Reflect.apply(objectToString,found.owner,[]),
        ownKeys:Reflect.ownKeys(found.owner).map(String),ownSymbols:Object.getOwnPropertySymbols(found.owner).map(String)};
    }else row.owner=null;
    objects[label]=row;
  }
  const overrides={};
  for(const label of ['window','navigator','element','location','xhr','upload','localStorage',
                     'htmlCollection','htmlAllCollection','namedNodeMap','domStringMap','url','blob','CSS']){
    const target=targets[label],before=Object.getOwnPropertyDescriptor(target,tag),initial=target[tag];
    const result={initial,reflectSet:outcome(()=>Reflect.set(target,tag,'IgnoredTag')),
      afterAssignment:target[tag],strictAssignment:outcome(()=>{target[tag]='IgnoredTag';})};
    try{
      Object.defineProperty(target,tag,{value:'OwnOverride',writable:true,enumerable:false,configurable:true});
      result.override={value:target[tag],ownTag:Object.hasOwn(target,tag),objectToString:Reflect.apply(objectToString,target,[])};
      result.deleteOwn=Reflect.deleteProperty(target,tag);
      result.afterDelete={value:target[tag]===undefined?'<undefined>':target[tag],objectToString:Reflect.apply(objectToString,target,[])};
      if(before)Object.defineProperty(target,tag,before);
    }catch(error){result.overrideError={name:error.name,message:error.message};}
    finally{
      if(before)Object.defineProperty(target,tag,before);
      else Reflect.deleteProperty(target,tag);
    }
    overrides[label]=result;
  }
  const owner=Object.getPrototypeOf(navigator);
  const original=Object.getOwnPropertyDescriptor(owner,tag);
  const child=Object.create(owner);
  const receiverTraps=[];
  const proxyReceiver=new Proxy({}, {
    get(target,key){receiverTraps.push('get:'+String(key));return Reflect.get(target,key);},
    getPrototypeOf(target){receiverTraps.push('getPrototypeOf');return Reflect.getPrototypeOf(target);},
    getOwnPropertyDescriptor(target,key){receiverTraps.push('descriptor:'+String(key));return Reflect.getOwnPropertyDescriptor(target,key);}
  });
  const inherited={value:child[tag],objectToString:Reflect.apply(objectToString,child,[]),
    reflectGetWithReceiver:reflectGet(owner,tag,proxyReceiver),receiverTraps,
    reflectSetWithReceiver:Reflect.set(owner,tag,'ChangedTag',child),childHasOwnAfterSet:Object.hasOwn(child,tag),ownerValue:owner[tag]};
  Object.defineProperty(child,tag,{value:'ChildOverride',writable:true,enumerable:false,configurable:true});
  inherited.definedOwn={value:child[tag],ownerUnchanged:owner[tag]===original.value};
  inherited.deletedOwn=Reflect.deleteProperty(child,tag);inherited.restoredInherited=child[tag];
  const getter={calls:0,receivers:[]};
  try{
    Object.defineProperty(owner,tag,{configurable:true,enumerable:original.enumerable,
      get(){getter.calls++;getter.receivers.push(this===child?'child':this===proxyReceiver?'proxy':'owner');return 'GetterTag';}});
    getter.direct=reflectGet(owner,tag,proxyReceiver);
    getter.inherited=child[tag];
    getter.objectToString=Reflect.apply(objectToString,child,[]);
    getter.finalCalls=getter.calls;
    const sentinel={original:'tag sentinel'};
    let throwingCalls=0;
    Object.defineProperty(owner,tag,{configurable:true,get(){throwingCalls++;throw sentinel;}});
    try{child[tag];}catch(error){getter.directThrewSame=error===sentinel;}
    try{Reflect.apply(objectToString,child,[]);}catch(error){getter.toStringThrewSame=error===sentinel;}
    getter.throwingCalls=throwingCalls;
    let ignoredConversions=0,nonstringCalls=0;
    const nonstring={toString(){ignoredConversions++;throw Error('must not convert tag');}};
    Object.defineProperty(owner,tag,{configurable:true,get(){nonstringCalls++;return nonstring;}});
    getter.nonstringDirectSame=child[tag]===nonstring;
    getter.nonstringObjectToString=Reflect.apply(objectToString,child,[]);
    getter.nonstringCalls=nonstringCalls;getter.ignoredConversions=ignoredConversions;
  }finally{Object.defineProperty(owner,tag,original);}
  const ownerMutation={};
  try{
    ownerMutation.deleted=Reflect.deleteProperty(owner,tag);
    ownerMutation.afterDelete={ownTag:Object.hasOwn(owner,tag),value:owner[tag]===undefined?'<undefined>':owner[tag],
      navigatorValue:navigator[tag]===undefined?'<undefined>':navigator[tag],objectToString:Reflect.apply(objectToString,navigator,[])};
    Object.defineProperty(owner,tag,{value:'RedefinedTag',writable:true,enumerable:true,configurable:true});
    ownerMutation.redefined={descriptor:descriptorShape(Object.getOwnPropertyDescriptor(owner,tag)),navigatorValue:navigator[tag],
      objectToString:Reflect.apply(objectToString,navigator,[])};
  }finally{Object.defineProperty(owner,tag,original);}
  ownerMutation.restored={descriptor:descriptorShape(Object.getOwnPropertyDescriptor(owner,tag)),navigatorValue:navigator[tag]};
  const result={conditions:{url:location.href,origin:location.origin,isSecureContext,worker:false,userAgent:navigator.userAgent},
    owners,objects,overrides,inherited,getter,ownerMutation};
  const output=document.createElement('pre');output.id='symbol-tag-reference';output.textContent=JSON.stringify(result);
  document.body.replaceChildren(output);
})();
