(() => {
  const names = [
    'CSS','GPUBufferUsage','GPUColorWrite','GPUMapMode','GPUShaderStage','GPUTextureUsage','NodeFilter',
    'PERSISTENT','TEMPORARY','caches','captureEvents','chrome','close','cookieStore','crashReport',
    'createImageBitmap','credentialless','customElements','documentPictureInPicture','event','external',
    'fence','fetchLater','find','getComputedStyle','getScreenDetails','getSelection','indexedDB',
    'launchQueue','navigation','offscreenBuffering','open','originAgentCluster','queryLocalFonts',
    'releaseEvents','reportError','requestResize','showDirectoryPicker','showOpenFilePicker','showSaveFilePicker',
    'speechSynthesis','styleMedia','trustedTypes','viewport','webkitCancelAnimationFrame',
    'webkitRequestAnimationFrame','webkitRequestFileSystem','webkitResolveLocalFileSystemURL','when'
  ];
  const functionShape = fn => ({name:fn.name,length:fn.length,source:Function.prototype.toString.call(fn),
    prototype:Object.hasOwn(fn,'prototype'),constructable:(() => {
      try {Reflect.construct(String,[],fn);return true;} catch(error) {
        if (!(error instanceof TypeError)) throw error;return false;
      }
    })()});
  const descriptorSummary = descriptor => {
    if (descriptor === undefined) return null;
    const result={enumerable:descriptor.enumerable,configurable:descriptor.configurable};
    if (Object.hasOwn(descriptor,'value')) {
      result.kind=typeof descriptor.value === 'function' ? 'function' : 'data';
      result.writable=descriptor.writable;result.type=typeof descriptor.value;
      if (typeof descriptor.value === 'function') result.function=functionShape(descriptor.value);
      else if (descriptor.value === null || ['string','number','boolean'].includes(typeof descriptor.value)) result.value=descriptor.value;
    } else {
      result.kind='accessor';result.get=descriptor.get === undefined ? null : functionShape(descriptor.get);
      result.set=descriptor.set === undefined ? null : functionShape(descriptor.set);
    }
    return result;
  };
  const directMembers = object => Object.getOwnPropertyNames(object).filter(name => !/^[0-9]+$/.test(name)).sort()
    .map(name => ({name,descriptor:descriptorSummary(Object.getOwnPropertyDescriptor(object,name))}));
  const constructorName = object => {
    for (let current=object;current !== null;current=Object.getPrototypeOf(current)) {
      const descriptor=Object.getOwnPropertyDescriptor(current,'constructor');
      if (descriptor !== undefined) return typeof descriptor.value === 'function' ? descriptor.value.name : null;
    }
    return null;
  };
  const members={};
  for (const name of names) {
    const result={present:name in window};
    try {
      let owner=window,depth=0;
      while (owner !== null && Object.getOwnPropertyDescriptor(owner,name) === undefined) {
        owner=Object.getPrototypeOf(owner);depth += 1;
      }
      result.descriptor=owner === null ? null : descriptorSummary(Object.getOwnPropertyDescriptor(owner,name));
      result.owner_depth=owner === null ? null : depth;
      const value=window[name];result.type=typeof value;
      if (typeof value === 'function') {
        result.function=functionShape(value);
        if (name === 'NodeFilter') result.static_members=directMembers(value);
      }
      else if (value === null || ['string','number','boolean'].includes(typeof value)) result.value=value;
      if (value !== null && typeof value === 'object') {
        const prototype=Object.getPrototypeOf(value);
        const parent=prototype === null ? null : Object.getPrototypeOf(prototype);
        result.namespace={tag:Object.prototype.toString.call(value),stable:window[name] === value,
          prototype_null:prototype === null,prototype_is_object_prototype:prototype === Object.prototype,
          prototype_tag:prototype === null ? null : Object.prototype.toString.call(prototype),
          prototype_constructor:prototype === null ? null : constructorName(prototype),
          parent_is_object_prototype:parent === Object.prototype,
          parent_constructor:parent === null ? null : constructorName(parent),
          own_members:directMembers(value),
          prototype_members:prototype === null || prototype === Object.prototype ? [] : directMembers(prototype)};
      }
    } catch(error) {result.error={name:error.name,message:error.message};}
    members[name]=result;
  }
  const snapshot={members,conditions:{secure_context:isSecureContext,cross_origin_isolated:crossOriginIsolated}};
  const output=document.createElement('pre');output.textContent=JSON.stringify(snapshot);document.body.appendChild(output);
  return JSON.stringify(snapshot);
})()
