(async () => {
  'use strict';
  const members = ['bluetooth','hid','serial','usb','xr','gpu','presentation'];
  const extraGlobals = ['Bluetooth','BluetoothDevice','HID','HIDDevice','Serial','SerialPort',
    'USB','USBDevice','XRSystem','XRSession','GPU','GPUAdapter','WGSLLanguageFeatures',
    'Presentation','PresentationRequest','PresentationReceiver','PresentationConnectionAvailableEvent'];
  const out = {secure: isSecureContext, origin: location.origin, shape: {}, globals: {}, interfaces: {}, calls: {}, details: {}};
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
    if (Array.isArray(value)) return {type: 'array', length: value.length};
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
      entry.stable = value === navigator[name];
      entry.tag = Object.prototype.toString.call(value);
      entry.ownKeys = Reflect.ownKeys(value).map(String);
      entry.protoName = Object.getPrototypeOf(value).constructor.name;
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
  if (navigator.bluetooth) {
    for (const [n, m, a] of [
      ['bt_avail', 'getAvailability', '[]'],
      ['bt_req_empty', 'requestDevice', '[]'],
      ['bt_req_dict', 'requestDevice', '[{}]'],
      ['bt_req_all', 'requestDevice', '[{acceptAllDevices: true}]'],
      ['bt_req_bad', 'requestDevice', '[17]'],
    ]) await observe(n, 'navigator.bluetooth', m, a);
  }
  if (navigator.hid) {
    for (const [n, m, a] of [
      ['hid_get', 'getDevices', '[]'],
      ['hid_req_missing', 'requestDevice', '[]'],
      ['hid_req_empty', 'requestDevice', '[{}]'],
      ['hid_req_filters', 'requestDevice', '[{filters: []}]'],
    ]) await observe(n, 'navigator.hid', m, a);
  }
  if (navigator.serial) {
    for (const [n, m, a] of [
      ['ser_get', 'getPorts', '[]'],
      ['ser_req_empty', 'requestPort', '[]'],
      ['ser_req_dict', 'requestPort', '[{}]'],
      ['ser_req_bad', 'requestPort', '[17]'],
    ]) await observe(n, 'navigator.serial', m, a);
  }
  if (navigator.usb) {
    for (const [n, m, a] of [
      ['usb_get', 'getDevices', '[]'],
      ['usb_req_missing', 'requestDevice', '[]'],
      ['usb_req_empty', 'requestDevice', '[{}]'],
      ['usb_req_filters', 'requestDevice', '[{filters: []}]'],
    ]) await observe(n, 'navigator.usb', m, a);
  }
  if (navigator.xr) {
    for (const [n, m, a] of [
      ['xr_sup_missing', 'isSessionSupported', '[]'],
      ['xr_sup_inline', 'isSessionSupported', "['inline']"],
      ['xr_sup_vr', 'isSessionSupported', "['immersive-vr']"],
      ['xr_sup_ar', 'isSessionSupported', "['immersive-ar']"],
      ['xr_sup_bad', 'isSessionSupported', "['bogus']"],
      ['xr_req_missing', 'requestSession', '[]'],
      ['xr_req_vr', 'requestSession', "['immersive-vr']"],
      ['xr_req_bad', 'requestSession', "['bogus']"],
    ]) await observe(n, 'navigator.xr', m, a);
  }
  if (navigator.gpu) {
    out.details.gpuFormat = navigator.gpu.getPreferredCanvasFormat();
    const f = navigator.gpu.wgslLanguageFeatures;
    out.details.wgsl = {
      same: f === navigator.gpu.wgslLanguageFeatures,
      tag: Object.prototype.toString.call(f),
      size: f.size,
      values: [...f.values()],
    };
    for (const [n, m, a] of [
      ['gpu_req_empty', 'requestAdapter', '[]'],
      ['gpu_req_dict', 'requestAdapter', '[{}]'],
      ['gpu_req_power', 'requestAdapter', "[{powerPreference: 'high-performance'}]"],
      ['gpu_req_bad_power', 'requestAdapter', "[{powerPreference: 'bogus'}]"],
      ['gpu_req_bad', 'requestAdapter', '[17]'],
    ]) await observe(n, 'navigator.gpu', m, a);
  }
  if (navigator.presentation) {
    out.details.presentation = {
      defaultRequest: navigator.presentation.defaultRequest,
      receiver: navigator.presentation.receiver,
    };
    if (typeof PresentationRequest === 'function') {
      try {
        const pr = new PresentationRequest('https://example.com/present');
        navigator.presentation.defaultRequest = pr;
        out.details.presentationAfter = {
          same: navigator.presentation.defaultRequest === pr,
          tag: Object.prototype.toString.call(pr),
        };
        navigator.presentation.defaultRequest = null;
      } catch (e) { out.details.presentationErr = summarizeError(e); }
    }
  }
  const pre = document.createElement('pre'); pre.id = 'bom-reference'; pre.textContent = JSON.stringify(out); document.body.append(pre);
})();
