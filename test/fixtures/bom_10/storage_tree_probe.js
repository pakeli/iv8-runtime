(async () => {
  const shape=fn=>{let constructable=false;try{Reflect.construct(Object,[],fn);constructable=true}catch(_){}return {name:fn.name,length:fn.length,source:Function.prototype.toString.call(fn),ownPrototype:Object.hasOwn(fn,'prototype'),constructable}};
  const describe=obj=>({keys:Reflect.ownKeys(obj).map(String),members:Object.fromEntries(Reflect.ownKeys(obj).map(k=>{const d=Object.getOwnPropertyDescriptor(obj,k);return [String(k),{enumerable:d.enumerable,configurable:d.configurable,...('value'in d?{kind:'data',writable:d.writable,value:typeof d.value==='function'?shape(d.value):typeof d.value==='string'?d.value:typeof d.value}:{kind:'accessor',get:d.get?shape(d.get):null,set:d.set?shape(d.set):null})}]}))});
  const error=e=>({name:e.name,message:e.message,code:typeof e.code==='number'?e.code:null,dom:e instanceof DOMException});
  const run=async(fn,project=v=>v===undefined?'undefined':v)=>{let v;try{v=fn()}catch(e){return {mode:'throw',error:error(e)}}const promise=v instanceof Promise;try{return {mode:promise?'promise':'return',value:project(await v)}}catch(e){return {mode:'promise',error:error(e)}}};
  const out={secure:isSecureContext,present:'storage'in navigator};
  if(out.present){
    const root=await navigator.storage.getDirectory();
    const dir=await root.getDirectoryHandle('tree-probe',{create:true});
    try{
      const file=await dir.getFileHandle('test.txt',{create:true});
      const child=await dir.getDirectoryHandle('child',{create:true});
      out.permission={read:await run(()=>root.queryPermission()),write:await run(()=>root.requestPermission({mode:'readwrite'})),nullOptions:await run(()=>root.queryPermission(null)),badOptions:await run(()=>root.queryPermission(3)),badMode:await run(()=>root.queryPermission({mode:'x'}))};
      out.names={};
      for(const name of ['', '.', '..', 'a/b', 'a\\b', 'a\0b', '\0x'])out.names[JSON.stringify(name)]=await run(()=>dir.getFileHandle(name,{create:true}),h=>h.name);
      out.dictionary={primitive:await run(()=>dir.getFileHandle('x',3)),null:await run(()=>dir.getFileHandle('x',null)),createTruthy:await run(()=>dir.getFileHandle('truthy',{create:1}),h=>h.kind)};
      out.file=await run(()=>file.getFile(),f=>({name:f.name,size:f.size,type:f.type,instance:f instanceof File,textType:typeof f.text,arrayBufferType:typeof f.arrayBuffer}));
      out.fileUpper=await run(async()=>await(await dir.getFileHandle('UPPER.TXT',{create:true})).getFile(),f=>f.type);
      out.fileUnknown=await run(async()=>await(await dir.getFileHandle('unknown.iv8ext',{create:true})).getFile(),f=>f.type);
      out.identity={newWrapper:file!==await dir.getFileHandle('test.txt'),same:await file.isSameEntry(await dir.getFileHandle('test.txt')),different:await file.isSameEntry(child)};
      out.resolve={self:await dir.resolve(dir),file:await dir.resolve(file),child:await root.resolve(child),outside:await child.resolve(file)};
      out.bad={sameMissing:await run(()=>file.isSameEntry()),samePlain:await run(()=>file.isSameEntry({})),resolvePlain:await run(()=>dir.resolve({})),removeMissing:await run(()=>dir.removeEntry()),fileBadReceiver:await run(()=>FileSystemFileHandle.prototype.getFile.call({})),entriesBadReceiver:await run(()=>FileSystemDirectoryHandle.prototype.entries.call({})),permissionBadReceiver:await run(()=>FileSystemHandle.prototype.queryPermission.call({}))};
      const iter=dir.entries(),proto=Object.getPrototypeOf(iter);
      out.iterator={tag:Object.prototype.toString.call(iter),ownKeys:Reflect.ownKeys(iter).map(String),prototype:describe(proto),parent:describe(Object.getPrototypeOf(proto)),symbolIdentity:FileSystemDirectoryHandle.prototype[Symbol.asyncIterator]===FileSystemDirectoryHandle.prototype.entries,self:iter[Symbol.asyncIterator]()===iter,badNext:await run(()=>proto.next.call({}))};
      out.entries=[];for await(const [name,handle]of dir.entries())out.entries.push([name,handle.kind]);out.entries.sort((a,b)=>a[0]<b[0]?-1:a[0]>b[0]?1:0);
      out.keys=[];for await(const name of dir.keys())out.keys.push(name);out.keys.sort();
      out.values=[];for await(const handle of dir.values())out.values.push([handle.name,handle.kind]);out.values.sort((a,b)=>a[0]<b[0]?-1:a[0]>b[0]?1:0);
      const empty=child.values();out.iterator.emptyFirst=await empty.next();out.iterator.emptyAgain=await empty.next();
      const removed=await dir.getDirectoryHandle('remove-me',{create:true});await removed.getFileHandle('inside',{create:true});
      out.removal={nonempty:await run(()=>removed.remove()),recursive:await run(()=>removed.remove({recursive:true})),again:await run(()=>removed.remove()),staleLookup:await run(()=>removed.getFileHandle('inside')),sameRemoved:await run(()=>removed.isSameEntry(removed)),stalePermission:await run(()=>removed.queryPermission())};
      await file.remove();const replacement=await dir.getFileHandle('test.txt',{create:true});out.removal.recreatedIsSame=await file.isSameEntry(replacement);out.removal.staleFile=await run(()=>file.getFile(),f=>f.size);
      const typeFile=await dir.getFileHandle('typechange',{create:true});await typeFile.remove();const typeDir=await dir.getDirectoryHandle('typechange',{create:true});
      out.removal.changedKindSame=await typeFile.isSameEntry(typeDir);out.removal.changedKindRead=await run(()=>typeFile.getFile(),f=>f.size);
      const mutate=await dir.getDirectoryHandle('mutation',{create:true});await mutate.getFileHandle('b',{create:true});await mutate.getFileHandle('a',{create:true});
      const before=mutate.keys();await mutate.getFileHandle('c',{create:true});out.mutation={beforeFirstNext:[]};for await(const name of before)out.mutation.beforeFirstNext.push(name);
      const during=mutate.keys();const first=await during.next();await mutate.removeEntry('c');await mutate.getFileHandle('d',{create:true});out.mutation.afterFirstNext=[first.value];for await(const name of during)out.mutation.afterFirstNext.push(name);
      out.mutation.beforeFirstNext.sort();out.mutation.afterFirstNext.sort();
      const nulHandle=await dir.getFileHandle('a\0b');const shortHandle=await dir.getFileHandle('a');
      out.nul={name:nulHandle.name,shortName:shortHandle.name,same:await nulHandle.isSameEntry(shortHandle),fileName:(await nulHandle.getFile()).name};
      out.unicode=[];for(const name of ['\ud800','x\udc00y','folder-\ud83d\ude00'])out.unicode.push(await run(()=>dir.getFileHandle(name,{create:true}),h=>h.name));
      out.coercion=[];const marker=new RangeError('storage-marker');
      const opts={get create(){out.coercion.push('create');return false}};
      out.nameBeforeOptions=await run(()=>dir.getFileHandle({toString(){out.coercion.push('name');throw marker}},opts));
      try{await dir.getFileHandle('x',{get create(){throw marker}});out.errorIdentity=false}catch(e){out.errorIdentity=e===marker}
    }finally{await root.removeEntry('tree-probe',{recursive:true})}
    out.removeRoot=await run(()=>root.remove());
    out.rootAfterRemove=await run(()=>root.getDirectoryHandle('after-root-remove',{create:true}),h=>h.name);
    await root.removeEntry('after-root-remove',{recursive:true});
  }
  globalThis.__bom10StorageTreeResult=out;return out;
})()
