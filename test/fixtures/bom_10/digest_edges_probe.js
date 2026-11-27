(async () => {
  const err = e => ({name:e.name,message:e.message,code:typeof e.code==='number'?e.code:null,dom:e instanceof DOMException});
  const run = async fn => {
    let value;
    try {value=fn()} catch(e) {return {mode:'throw',error:err(e)}}
    const promise=value instanceof Promise;
    try {await value;return {mode:promise?'promise':'return',ok:true}}
    catch(e){return {mode:'promise',error:err(e)}}
  };
  const out={secure:isSecureContext,random:{},digest:{}};
  const randomCases={
    missing:()=>crypto.getRandomValues(), buffer:()=>crypto.getRandomValues(new ArrayBuffer(2)),
    float:()=>crypto.getRandomValues(new Float32Array(1)), float64:()=>crypto.getRandomValues(new Float64Array(1)),
    dataView:()=>crypto.getRandomValues(new DataView(new ArrayBuffer(2))), array:()=>crypto.getRandomValues([]),
    null:()=>crypto.getRandomValues(null), symbol:()=>crypto.getRandomValues(Symbol()),
    quota:()=>crypto.getRandomValues(new Uint8Array(65537)),
    badReceiver:()=>Crypto.prototype.getRandomValues.call({},new Uint8Array(1)),
    detached:()=>{const b=new ArrayBuffer(2);const view=new Uint8Array(b);b.transfer();return crypto.getRandomValues(view)},
  };
  for(const [name,fn] of Object.entries(randomCases))out.random[name]=await run(fn);
  const getter=Object.getOwnPropertyDescriptor(globalThis,'crypto').get;
  out.random.globalReceiver=await run(()=>getter.call({}));
  if(typeof crypto.randomUUID==='function')out.random.uuidReceiver=await run(()=>Crypto.prototype.randomUUID.call({}));
  if(crypto.subtle){
    out.supports={};
    const supportCases={missing:()=>SubtleCrypto.supports(),missingAlgorithm:()=>SubtleCrypto.supports('digest'),
      sha:()=>SubtleCrypto.supports('digest','SHA-256'), lowercase:()=>SubtleCrypto.supports('digest','sha-256'),
      dictionary:()=>SubtleCrypto.supports('digest',{name:'SHA-384'}),
      unknownOperation:()=>SubtleCrypto.supports('invalid','SHA-256'),
      operationCase:()=>SubtleCrypto.supports('DIGEST','SHA-256'),
      unknownAlgorithm:()=>SubtleCrypto.supports('digest','invalid'),
      operationMismatch:()=>SubtleCrypto.supports('digest','AES-GCM'),
      symbolOperation:()=>SubtleCrypto.supports(Symbol(),'SHA-256'),
      symbolAlgorithm:()=>SubtleCrypto.supports('digest',Symbol()),
      nullAlgorithm:()=>SubtleCrypto.supports('digest',null),
      emptyAlgorithm:()=>SubtleCrypto.supports('digest',{}),
      nestedSymbol:()=>SubtleCrypto.supports('digest',{name:Symbol()}),
      invalidOperationSymbol:()=>SubtleCrypto.supports('invalid',Symbol()),
      receiver:()=>SubtleCrypto.supports.call({},'digest','SHA-256'),
      extra:()=>SubtleCrypto.supports('digest','SHA-256',123)};
    for(const [name,fn]of Object.entries(supportCases)){
      try{out.supports[name]={value:fn()}}catch(e){out.supports[name]={error:err(e)}}
    }
    const marker=new Error('support-marker');
    try{SubtleCrypto.supports('digest',{get name(){throw marker}});out.supports.getterIdentity=false}
    catch(e){out.supports.getterIdentity=e===marker}
    const supportReads=[];
    out.supports.invalidOperationDictionary=SubtleCrypto.supports('invalid',{get name(){supportReads.push('name');return 'SHA-256'}});
    out.supports.invalidOperationReads=supportReads;
    out.supports.throwingGetterResult=SubtleCrypto.supports('digest',{get name(){throw marker}});
    const bytes=()=>new Uint8Array([97,98,99]);
    const cases={
      nameUndefined:()=>crypto.subtle.digest({name:undefined},bytes()),
      nameNull:()=>crypto.subtle.digest({name:null},bytes()),
      nameNumber:()=>crypto.subtle.digest({name:42},bytes()),
      nameSymbol:()=>crypto.subtle.digest({name:Symbol()},bytes()),
      rsa:()=>crypto.subtle.digest('RSA-PSS',bytes()),
      space:()=>crypto.subtle.digest(' SHA-256 ',bytes()),
      resizable:()=>crypto.subtle.digest('SHA-256',new ArrayBuffer(3,{maxByteLength:8})),
      resizableView:()=>crypto.subtle.digest('SHA-256',new Uint8Array(new ArrayBuffer(3,{maxByteLength:8}))),
      detachInName:()=>{const view=bytes();return crypto.subtle.digest({get name(){view.buffer.transfer();return 'SHA-256'}},view)},
    };
    for(const [name,fn] of Object.entries(cases))out.digest[name]=await run(fn);
    for(const name of ['ML-KEM-768','ML-KEM-1024','ML-DSA-44','ML-DSA-65','ML-DSA-87','ChaCha20-Poly1305'])
      out.digest[name]=await run(()=>crypto.subtle.digest(name,bytes()));
    const read=[];
    out.digest.badDataBeforeDictionary=await run(()=>crypto.subtle.digest({get name(){read.push('name');return 'SHA-256'}},{}));
    out.digest.badDataReads=read;
    let calls=0;const original=new Error('conversion-marker');
    out.digest.errorIdentity=await crypto.subtle.digest({get name(){calls++;throw original}},bytes()).then(()=>false,e=>e===original);
    out.digest.errorReads=calls;
    const modified=bytes();
    out.digest.snapshotInName=Array.from(new Uint8Array(await crypto.subtle.digest({get name(){modified.fill(0);return 'SHA-256'}},modified)),v=>v.toString(16).padStart(2,'0')).join('');
  }
  globalThis.__bom10DigestEdgesResult=out;
  return out;
})()
