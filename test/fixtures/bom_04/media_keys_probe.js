(async () => {
  'use strict';
  const out = {secure: isSecureContext, configs: {}, access: null, mediaKeys: null};
  const summarizeError = error => (error && typeof error === 'object')
    ? {name: error.name, message: error.message, tag: Object.prototype.toString.call(error)} : {thrown: String(error)};
  const functionShape = fn => {
    let construct;
    try { Reflect.construct(fn, []); construct = 'constructed'; } catch (error) { construct = error.name; }
    return {name: fn.name, length: fn.length, source: Function.prototype.toString.call(fn), prototype: Object.hasOwn(fn, 'prototype'), construct};
  };
  const descriptor = (object, key) => {
    const item = Object.getOwnPropertyDescriptor(object, key); if (!item) return null;
    return {enumerable: item.enumerable, configurable: item.configurable,
      ...('value' in item ? {kind: 'data', writable: item.writable, value: typeof item.value === 'function' ? functionShape(item.value) : item.value}
        : {kind: 'accessor', get: item.get ? functionShape(item.get) : null, set: item.set ? functionShape(item.set) : null})};
  };
  const iface = (value) => {
    const proto = Object.getPrototypeOf(value);
    const ctor = proto.constructor;
    return {tag: Object.prototype.toString.call(value), ownKeys: Reflect.ownKeys(value).map(String),
      prototypeKeys: Reflect.ownKeys(proto).map(String), parentIsObject: Object.getPrototypeOf(proto) === Object.prototype,
      members: Object.fromEntries(Reflect.ownKeys(proto).map(key => [String(key), descriptor(proto, key)])),
      global: descriptor(globalThis, ctor.name), constructor: functionShape(ctor)};
  };
  const vp = 'video/mp4; codecs="avc1.42E01E"';
  const cases = {
    empty: [{}],
    init_only: [{initDataTypes: ['cenc']}],
    video_mp4: [{videoCapabilities: [{contentType: vp}]}],
    video_mp4_init: [{initDataTypes: ['cenc'], videoCapabilities: [{contentType: vp}]}],
    video_webm_vp9: [{videoCapabilities: [{contentType: 'video/webm; codecs="vp9"'}]}],
    video_webm_vp8: [{videoCapabilities: [{contentType: 'video/webm; codecs="vp8"'}]}],
    audio_mp4: [{audioCapabilities: [{contentType: 'audio/mp4; codecs="mp4a.40.2"'}]}],
    audio_webm_opus: [{audioCapabilities: [{contentType: 'audio/webm; codecs="opus"'}]}],
    audio_mp4_flac: [{audioCapabilities: [{contentType: 'audio/mp4; codecs="flac"'}]}],
    video_no_codecs: [{videoCapabilities: [{contentType: 'video/mp4'}]}],
    video_bad_mime: [{videoCapabilities: [{contentType: 'video/x-unknown; codecs="foo"'}]}],
    video_empty_type: [{videoCapabilities: [{contentType: ''}]}],
    video_hevc: [{videoCapabilities: [{contentType: 'video/mp4; codecs="hvc1.1.6.L93.B0"'}]}],
    mixed: [{videoCapabilities: [{contentType: 'video/x-unknown'}, {contentType: vp, robustness: ''}]}],
    robustness: [{videoCapabilities: [{contentType: vp, robustness: 'SW_SECURE_CRYPTO'}]}],
    init_webm: [{initDataTypes: ['webm'], videoCapabilities: [{contentType: vp}]}],
    init_keyids: [{initDataTypes: ['keyids'], videoCapabilities: [{contentType: vp}]}],
    init_bad: [{initDataTypes: ['bogus'], videoCapabilities: [{contentType: vp}]}],
    init_mixed: [{initDataTypes: ['bogus', 'cenc', 'keyids'], videoCapabilities: [{contentType: vp}]}],
    distinctive_required: [{distinctiveIdentifier: 'required', videoCapabilities: [{contentType: vp}]}],
    distinctive_not_allowed: [{distinctiveIdentifier: 'not-allowed', videoCapabilities: [{contentType: vp}]}],
    persistent_required: [{persistentState: 'required', videoCapabilities: [{contentType: vp}]}],
    persistent_not_allowed: [{persistentState: 'not-allowed', videoCapabilities: [{contentType: vp}]}],
    session_temporary: [{sessionTypes: ['temporary'], videoCapabilities: [{contentType: vp}]}],
    session_persistent: [{sessionTypes: ['persistent-license'], videoCapabilities: [{contentType: vp}]}],
    label: [{label: 'first', videoCapabilities: [{contentType: vp}]}],
    second_ok: [{initDataTypes: ['bogus']}, {label: 'second', videoCapabilities: [{contentType: vp}]}],
    encryption_scheme: [{videoCapabilities: [{contentType: vp, encryptionScheme: 'cenc'}]}],
    encryption_cbcs: [{videoCapabilities: [{contentType: vp, encryptionScheme: 'cbcs'}]}],
    encryption_bad: [{videoCapabilities: [{contentType: vp, encryptionScheme: 'bogus'}]}],
    audio_video: [{audioCapabilities: [{contentType: 'audio/mp4; codecs="mp4a.40.2"'}], videoCapabilities: [{contentType: vp}]}],
  };
  for (const [name, configs] of Object.entries(cases)) {
    for (const system of ['org.w3.clearkey']) {
      try {
        const access = await navigator.requestMediaKeySystemAccess(system, configs);
        out.configs[name] = {resolved: true, keySystem: access.keySystem, configuration: access.getConfiguration()};
      } catch (error) { out.configs[name] = {rejected: summarizeError(error)}; }
    }
  }
  for (const system of ['com.widevine.alpha', 'com.microsoft.playready', 'com.apple.fps', 'org.w3.clearkey.extra', 'ORG.W3.CLEARKEY', 'x-org.w3.clearkey'])
    try { await navigator.requestMediaKeySystemAccess(system, cases.video_mp4); out.configs['system_' + system] = {resolved: true}; }
    catch (error) { out.configs['system_' + system] = {rejected: summarizeError(error)}; }
  try {
    const access = await navigator.requestMediaKeySystemAccess('org.w3.clearkey', cases.video_mp4_init);
    out.access = iface(access);
    out.access.sameConfiguration = access.getConfiguration() === access.getConfiguration();
    out.access.receiverKeySystem = (() => { try { return Object.getOwnPropertyDescriptor(Object.getPrototypeOf(access), 'keySystem').get.call({}); } catch (error) { return summarizeError(error); } })();
    out.access.receiverGetConfiguration = (() => { try { return access.getConfiguration.call({}); } catch (error) { return summarizeError(error); } })();
    try { const r = access.createMediaKeys.call({}); out.access.receiverCreate = {returned: Object.prototype.toString.call(r)};
      await r.catch(error => { out.access.receiverCreate.rejected = summarizeError(error); }); } catch (error) { out.access.receiverCreate = summarizeError(error); }
    const order = [];
    const pending = access.createMediaKeys();
    pending.then(() => order.push('resolved'));
    Promise.resolve().then(() => order.push('microtask'));
    setTimeout(() => order.push('task'), 0);
    const keys = await pending;
    await new Promise(resolve => setTimeout(resolve, 20));
    out.access.createOrder = order;
    out.access.createDistinct = keys !== await access.createMediaKeys();
    out.mediaKeys = iface(keys);
    const calls = {};
    for (const [name, run] of Object.entries({
      certificate: () => keys.setServerCertificate(new Uint8Array([1, 2, 3])),
      certificate_empty: () => keys.setServerCertificate(new Uint8Array([])),
      certificate_missing: () => keys.setServerCertificate(),
      status_missing: () => keys.getStatusForPolicy(),
      status_empty: () => keys.getStatusForPolicy({}),
      status_hdcp: () => keys.getStatusForPolicy({minHdcpVersion: '1.0'}),
      session_default: () => keys.createSession(),
      session_temporary: () => keys.createSession('temporary'),
      session_persistent: () => keys.createSession('persistent-license'),
      session_bad: () => keys.createSession('bogus'),
    })) {
      try {
        const value = run();
        const record = {returned: Object.prototype.toString.call(value)};
        if (value instanceof Promise) await value.then(result => { record.resolved = typeof result === 'object' ? Object.prototype.toString.call(result) : result; },
          error => { record.rejected = summarizeError(error); });
        else if (value && typeof value === 'object') record.sessionKeys = Reflect.ownKeys(Object.getPrototypeOf(value)).map(String);
        calls[name] = record;
      } catch (error) { calls[name] = {error: summarizeError(error)}; }
    }
    out.mediaKeys.calls = calls;
  } catch (error) { out.accessError = summarizeError(error); }
  const pre = document.createElement('pre'); pre.id = 'bom-reference'; pre.textContent = JSON.stringify(out); document.body.append(pre);
})();
