(async () => {
  'use strict';
  const members = ['mediaDevices','serviceWorker','storageBuckets','login','managed','ink','devicePosture','virtualKeyboard','windowControlsOverlay','cpuPerformance'];
  const extraGlobals = ['MediaDevices','MediaDeviceInfo','InputDeviceInfo','ServiceWorkerContainer','ServiceWorker','ServiceWorkerRegistration',
    'StorageBucketManager','StorageBucket','NavigatorLogin','NavigatorManagedData','Ink','DelegatedInkTrailPresenter',
    'DevicePosture','VirtualKeyboard','WindowControlsOverlay','DOMRect','DOMRectReadOnly'];
  const out = {secure: isSecureContext, origin: location.origin, navigatorOwnKeys: Reflect.ownKeys(Navigator.prototype).map(String),
    shape: {}, globals: {}, interfaces: {}, calls: {}, details: {}};
  const summarizeError = error => (error && typeof error === 'object')
    ? {name: error.name, message: error.message, code: 'code' in error ? error.code : undefined,
       tag: Object.prototype.toString.call(error), domException: typeof DOMException === 'function' && error instanceof DOMException}
    : {thrown: typeof error, value: String(error)};
  const functionShape = fn => {
    let construct;
    try { Reflect.construct(fn, []); construct = 'constructed'; } catch (error) { construct = error.name; }
    return {name: fn.name, length: fn.length, source: Function.prototype.toString.call(fn),
      prototype: Object.hasOwn(fn, 'prototype'), construct};
  };
  const descriptor = (object, key) => {
    const item = Object.getOwnPropertyDescriptor(object, key); if (!item) return null;
    return {enumerable: item.enumerable, configurable: item.configurable,
      ...('value' in item ? {kind: 'data', writable: item.writable,
        value: typeof item.value === 'function' ? functionShape(item.value) : (typeof item.value === 'object' && item.value !== null ? '[object]' : item.value)}
        : {kind: 'accessor', get: item.get ? functionShape(item.get) : null, set: item.set ? functionShape(item.set) : null})};
  };
  const plain = value => {
    if (value === undefined) return {type: 'undefined'};
    if (value === null || typeof value !== 'object') return {type: typeof value, value};
    if (Array.isArray(value)) return {type: 'array', length: value.length, items: value.map(x => typeof x === 'object' && x ? Object.prototype.toString.call(x) : x)};
    return {type: 'object', tag: Object.prototype.toString.call(value)};
  };
  for (const g of extraGlobals) {
    out.globals[g] = {present: g in globalThis, descriptor: descriptor(globalThis, g)};
    if (typeof globalThis[g] === 'function' && globalThis[g].prototype) {
      const proto = globalThis[g].prototype;
      const parent = Object.getPrototypeOf(proto);
      out.interfaces[g] = {
        constructor: functionShape(globalThis[g]),
        constructorKeys: Reflect.ownKeys(globalThis[g]).map(String),
        prototypeKeys: Reflect.ownKeys(proto).map(String),
        parent: parent === Object.prototype ? 'Object.prototype' : (parent === EventTarget.prototype ? 'EventTarget.prototype' : (parent && parent.constructor ? parent.constructor.name : String(parent))),
        members: Object.fromEntries(Reflect.ownKeys(proto).map(k => [String(k), descriptor(proto, k)])),
      };
    }
  }
  for (const name of members) {
    const entry = {present: name in navigator, own: Object.hasOwn(navigator, name), descriptor: descriptor(Navigator.prototype, name)};
    if (entry.present) {
      const value = navigator[name];
      entry.valueType = typeof value;
      if (typeof value === 'object' && value !== null) {
        entry.stable = value === navigator[name];
        entry.tag = Object.prototype.toString.call(value);
        entry.ownKeys = Reflect.ownKeys(value).map(String);
        entry.protoName = Object.getPrototypeOf(value).constructor.name;
      } else {
        entry.value = value;
      }
      try { Object.getOwnPropertyDescriptor(Navigator.prototype, name).get.call({}); entry.badReceiver = 'ok'; }
      catch (e) { entry.badReceiver = summarizeError(e); }
    }
    out.shape[name] = entry;
  }
  async function observe(name, targetExpr, method, argsExpr) {
    const effects = [], sentinel = {sentinel: name};
    const record = {effects};
    setTimeout(() => effects.push('task'), 0);
    let promise = null;
    try {
      const target = eval(targetExpr);
      const args = eval(argsExpr);
      const result = Reflect.apply(target[method], target, args);
      record.returned = plain(result);
      if (result instanceof Promise) {
        promise = result.then(
          v => { effects.push({kind: 'resolved', value: plain(v)}); },
          e => { effects.push({kind: 'rejected', error: summarizeError(e), same: e === sentinel}); }
        );
      }
    } catch (e) { record.error = {...summarizeError(e), same: e === sentinel}; }
    effects.push('after call');
    await Promise.resolve(); effects.push('microtask');
    let timer;
    const timeout = new Promise(r => timer = setTimeout(() => { record.timeout = true; r(); }, 2500));
    if (promise) await Promise.race([promise, timeout]);
    clearTimeout(timer);
    await new Promise(r => setTimeout(r, 10));
    out.calls[name] = record;
  }
  // 1. mediaDevices
  if (navigator.mediaDevices) {
    out.details.supportedConstraints = navigator.mediaDevices.getSupportedConstraints();
    const devs = await navigator.mediaDevices.enumerateDevices();
    out.details.enumerateDevices = devs.map(d => ({deviceId: d.deviceId, kind: d.kind, label: d.label, groupId: typeof d.groupId, tag: Object.prototype.toString.call(d), json: d.toJSON()}));
    for (const [n, m, a] of [
      ['md_enum', 'enumerateDevices', '[]'],
      ['md_gum_empty', 'getUserMedia', '[]'],
      ['md_gum_dict', 'getUserMedia', '[{}]'],
      ['md_gum_audio', 'getUserMedia', '[{audio: true}]'],
      ['md_gum_bad', 'getUserMedia', '[17]'],
      ['md_gdm_empty', 'getDisplayMedia', '[]'],
      ['md_gdm_video', 'getDisplayMedia', '[{video: true}]'],
      ['md_capture_empty', 'setCaptureHandleConfig', '[]'],
      ['md_capture_ok', 'setCaptureHandleConfig', "[{handle: 'abc', exposeOrigin: true}]"],
      ['md_capture_long', 'setCaptureHandleConfig', "[{handle: 'x'.repeat(1025)}]"],
    ]) await observe(n, 'navigator.mediaDevices', m, a);
  }
  // 2. serviceWorker
  if (navigator.serviceWorker) {
    out.details.sw = {
      controller: navigator.serviceWorker.controller,
      readySame: navigator.serviceWorker.ready === navigator.serviceWorker.ready,
      readyTag: Object.prototype.toString.call(navigator.serviceWorker.ready),
      startMessages: navigator.serviceWorker.startMessages(),
    };
    for (const [n, m, a] of [
      ['sw_getReg_empty', 'getRegistration', '[]'],
      ['sw_getReg_same', 'getRegistration', "['/']"],
      ['sw_getReg_cross', 'getRegistration', "['https://example.com/']"],
      ['sw_getRegs', 'getRegistrations', '[]'],
      ['sw_reg_missing', 'register', '[]'],
      ['sw_reg_cross', 'register', "['https://example.com/sw.js']"],
      ['sw_reg_local', 'register', "['/missing-sw.js']"],
    ]) await observe(n, 'navigator.serviceWorker', m, a);
  }
  // 3. storageBuckets
  if (navigator.storageBuckets) {
    for (const [n, m, a] of [
      ['sb_keys_initial', 'keys', '[]'],
      ['sb_open_missing', 'open', '[]'],
      ['sb_open_bad_upper', 'open', "['BadName']"],
      ['sb_open_bad_dash', 'open', "['-bad']"],
      ['sb_open_ok', 'open', "['my-bucket']"],
      ['sb_keys_after', 'keys', '[]'],
      ['sb_del_missing', 'delete', '[]'],
      ['sb_del_bad', 'delete', "['BadName']"],
      ['sb_del_ok', 'delete', "['my-bucket']"],
    ]) await observe(n, 'navigator.storageBuckets', m, a);
  }
  // 4. login
  if (navigator.login) {
    for (const [n, m, a] of [
      ['login_missing', 'setStatus', '[]'],
      ['login_bad', 'setStatus', "['bogus']"],
      ['login_in', 'setStatus', "['logged-in']"],
      ['login_out', 'setStatus', "['logged-out']"],
    ]) await observe(n, 'navigator.login', m, a);
  }
  // 5. managed
  if (navigator.managed) {
    for (const [n, m, a] of [
      ['mgd_missing', 'getManagedConfiguration', '[]'],
      ['mgd_bad', 'getManagedConfiguration', '[17]'],
      ['mgd_ok', 'getManagedConfiguration', "[['a','b']]"],
    ]) await observe(n, 'navigator.managed', m, a);
  }
  // 6. ink
  if (navigator.ink) {
    for (const [n, m, a] of [
      ['ink_empty', 'requestPresenter', '[]'],
      ['ink_dict', 'requestPresenter', '[{}]'],
      ['ink_null_area', 'requestPresenter', '[{presentationArea: null}]'],
      ['ink_el', 'requestPresenter', '[{presentationArea: document.body}]'],
      ['ink_bad_area', 'requestPresenter', '[{presentationArea: 17}]'],
      ['ink_bad', 'requestPresenter', '[17]'],
    ]) await observe(n, 'navigator.ink', m, a);
  }
  // 7. devicePosture
  if (navigator.devicePosture) {
    out.details.devicePosture = {type: navigator.devicePosture.type, onchange: navigator.devicePosture.onchange};
  }
  // 8. virtualKeyboard
  if (navigator.virtualKeyboard) {
    const r = navigator.virtualKeyboard.boundingRect;
    out.details.virtualKeyboard = {
      overlaysContent: navigator.virtualKeyboard.overlaysContent,
      rectTag: Object.prototype.toString.call(r),
      rectSame: r === navigator.virtualKeyboard.boundingRect,
      rectJson: r.toJSON(),
    };
    navigator.virtualKeyboard.overlaysContent = true;
    out.details.virtualKeyboardAfter = navigator.virtualKeyboard.overlaysContent;
    navigator.virtualKeyboard.overlaysContent = false;
    out.details.vkShow = navigator.virtualKeyboard.show();
    out.details.vkHide = navigator.virtualKeyboard.hide();
  }
  // 9. windowControlsOverlay
  if (navigator.windowControlsOverlay) {
    const r = navigator.windowControlsOverlay.getTitlebarAreaRect();
    out.details.wco = {
      visible: navigator.windowControlsOverlay.visible,
      rectTag: Object.prototype.toString.call(r),
      rectSame: r === navigator.windowControlsOverlay.getTitlebarAreaRect(),
      rectJson: r.toJSON(),
    };
  }
  const pre = document.createElement('pre'); pre.id = 'bom-reference'; pre.textContent = JSON.stringify(out); document.body.append(pre);
})();
