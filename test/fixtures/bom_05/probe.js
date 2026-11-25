(async () => {
  'use strict';
  const members = ['geolocation','clipboard','credentials','locks','wakeLock','keyboard','mediaSession','mediaCapabilities'];
  const extraGlobals = ['Geolocation','GeolocationPositionError','GeolocationPosition','GeolocationCoordinates',
    'Clipboard','ClipboardItem','CredentialsContainer','Credential',
    'LockManager','Lock','WakeLock','WakeLockSentinel',
    'Keyboard','KeyboardLayoutMap','MediaSession','MediaMetadata','MediaCapabilities'];
  const out = {secure: isSecureContext, origin: location.origin, shape: {}, globals: {}, interfaces: {}, calls: {}, receivers: {}, details: {}};
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
  async function observe(name, targetExpr, method, argsExpr, expectCb = false) {
    const effects = [], sentinel = {sentinel: name};
    let called = false, finish;
    const done = new Promise(r => finish = r);
    const success = function (...vals) { called = true; effects.push({kind: 'success', thisUndefined: this === undefined, values: vals.map(plain)}); finish(); };
    const failure = function (err) { called = true; effects.push({kind: 'failure', thisUndefined: this === undefined, error: summarizeError(err)}); finish(); };
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
          v => { effects.push({kind: 'resolved', value: plain(v), raw: (v && typeof v === 'object' && !Array.isArray(v) && Object.getPrototypeOf(v) === Object.prototype) ? v : undefined}); },
          e => { effects.push({kind: 'rejected', error: summarizeError(e), same: e === sentinel}); }
        );
      }
    } catch (e) { record.error = {...summarizeError(e), same: e === sentinel}; expectCb = false; }
    effects.push('after call');
    await Promise.resolve(); effects.push('microtask');
    let timer;
    const timeout = new Promise(r => timer = setTimeout(() => { record.timeout = true; r(); }, 2500));
    if (promise) await Promise.race([promise, timeout]);
    else if (expectCb && !called) await Promise.race([done, timeout]);
    clearTimeout(timer);
    await new Promise(r => setTimeout(r, 10));
    out.calls[name] = record;
  }
  // 1. geolocation
  if (navigator.geolocation) {
    for (const [n, m, a, cb] of [
      ['geo_get_missing', 'getCurrentPosition', '[]', false],
      ['geo_get_bad_cb', 'getCurrentPosition', '[17]', false],
      ['geo_get_bad_err', 'getCurrentPosition', '[success, 17]', false],
      ['geo_get_ok', 'getCurrentPosition', '[success, failure, {timeout: 0}]', true],
      ['geo_get_default', 'getCurrentPosition', '[success, failure]', true],
      ['geo_get_order', 'getCurrentPosition', '[success, failure, {get enableHighAccuracy(){effects.push("eha");return false;}, get maximumAge(){effects.push("maxAge");return 0;}, get timeout(){effects.push("timeout");return 0;}}]', true],
      ['geo_watch_missing', 'watchPosition', '[]', false],
      ['geo_watch_ok', 'watchPosition', '[success, failure, {timeout: 0}]', true],
      ['geo_clear_missing', 'clearWatch', '[]', false],
      ['geo_clear_num', 'clearWatch', '[1]', false],
    ]) await observe(n, 'navigator.geolocation', m, a, cb);
    if (typeof GeolocationPositionError === 'function') {
      out.details.geoErrorConstants = Object.fromEntries(
        ['PERMISSION_DENIED','POSITION_UNAVAILABLE','TIMEOUT'].map(k => [k, GeolocationPositionError[k]])
      );
    }
  }
  // 2. clipboard
  if (navigator.clipboard) {
    for (const [n, m, a] of [
      ['clip_read', 'read', '[]'],
      ['clip_readText', 'readText', '[]'],
      ['clip_write_missing', 'write', '[]'],
      ['clip_write_empty', 'write', '[[]]'],
      ['clip_writeText_missing', 'writeText', '[]'],
      ['clip_writeText_ok', 'writeText', "['hello']"],
    ]) await observe(n, 'navigator.clipboard', m, a);
  }
  // 3. credentials
  if (navigator.credentials) {
    for (const [n, m, a] of [
      ['cred_get_empty', 'get', '[]'],
      ['cred_get_dict', 'get', '[{}]'],
      ['cred_get_password', 'get', '[{password: true}]'],
      ['cred_get_bad', 'get', '[17]'],
      ['cred_create_empty', 'create', '[]'],
      ['cred_create_dict', 'create', '[{}]'],
      ['cred_create_bad', 'create', '[17]'],
      ['cred_prevent', 'preventSilentAccess', '[]'],
      ['cred_store_missing', 'store', '[]'],
      ['cred_store_bad', 'store', '[{}]'],
    ]) await observe(n, 'navigator.credentials', m, a);
  }
  // 4. locks
  if (navigator.locks) {
    for (const [n, m, a] of [
      ['locks_query', 'query', '[]'],
      ['locks_req_missing', 'request', '[]'],
      ['locks_req_one', 'request', "['res']"],
      ['locks_req_bad_cb', 'request', "['res', 17]"],
      ['locks_req_ok', 'request', "['res', lock => { effects.push({lockName: lock.name, lockMode: lock.mode, tag: Object.prototype.toString.call(lock)}); return 42; }]"],
      ['locks_req_opts', 'request', "['res', {mode: 'shared', ifAvailable: true}, lock => lock ? lock.mode : 'none']"],
      ['locks_req_bad_mode', 'request', "['res', {mode: 'bogus'}, () => {}]"],
      ['locks_req_steal_ifavail', 'request', "['res', {steal: true, ifAvailable: true}, () => {}]"],
      ['locks_req_slash', 'request', "['-bad', () => {}]"],
    ]) await observe(n, 'navigator.locks', m, a);
  }
  // 5. wakeLock
  if (navigator.wakeLock) {
    for (const [n, m, a] of [
      ['wake_default', 'request', '[]'],
      ['wake_screen', 'request', "['screen']"],
      ['wake_bad', 'request', "['bogus']"],
    ]) await observe(n, 'navigator.wakeLock', m, a);
    try {
      const s = await navigator.wakeLock.request('screen');
      out.details.wakeSentinel = {tag: Object.prototype.toString.call(s), released: s.released, type: s.type};
      await s.release();
      out.details.wakeAfterRelease = {released: s.released};
    } catch (e) { out.details.wakeError = summarizeError(e); }
  }
  // 6. keyboard
  if (navigator.keyboard) {
    for (const [n, m, a] of [
      ['kb_map', 'getLayoutMap', '[]'],
      ['kb_lock_empty', 'lock', '[]'],
      ['kb_lock_keys', 'lock', "[['KeyA','KeyB']]"],
      ['kb_lock_bad', 'lock', '[17]'],
      ['kb_unlock', 'unlock', '[]'],
    ]) await observe(n, 'navigator.keyboard', m, a);
    try {
      const m = await navigator.keyboard.getLayoutMap();
      out.details.kbMap = {tag: Object.prototype.toString.call(m), size: m.size, hasKeyA: m.has('KeyA'), getKeyA: m.get('KeyA')};
    } catch (e) { out.details.kbMapError = summarizeError(e); }
  }
  // 7. mediaSession & MediaMetadata
  if (navigator.mediaSession) {
    out.details.mediaSessionInitial = {metadata: navigator.mediaSession.metadata, playbackState: navigator.mediaSession.playbackState};
    if (typeof MediaMetadata === 'function') {
      const mm = new MediaMetadata({title: 'Song', artist: 'Artist', album: 'Album', artwork: [{src: 'https://example.com/a.png', sizes: '96x96', type: 'image/png'}]});
      navigator.mediaSession.metadata = mm;
      navigator.mediaSession.playbackState = 'playing';
      out.details.mediaSessionAfter = {
        sameMetadata: navigator.mediaSession.metadata === mm,
        title: mm.title, artist: mm.artist, album: mm.album, artworkLen: mm.artwork.length, artwork0: mm.artwork[0],
        sameArtwork: mm.artwork === mm.artwork,
        playbackState: navigator.mediaSession.playbackState,
      };
      navigator.mediaSession.metadata = null;
      navigator.mediaSession.playbackState = 'none';
    }
    for (const [n, m, a] of [
      ['ms_action_ok', 'setActionHandler', "['play', () => {}]"],
      ['ms_action_null', 'setActionHandler', "['play', null]"],
      ['ms_action_bad_enum', 'setActionHandler', "['bogus', () => {}]"],
      ['ms_action_missing', 'setActionHandler', '[]'],
      ['ms_cam_ok', 'setCameraActive', '[false]'],
      ['ms_cam_missing', 'setCameraActive', '[]'],
      ['ms_mic_ok', 'setMicrophoneActive', '[false]'],
      ['ms_pos_empty', 'setPositionState', '[]'],
      ['ms_pos_ok', 'setPositionState', '[{duration: 10, playbackRate: 1, position: 2}]'],
      ['ms_pos_bad_neg', 'setPositionState', '[{duration: -1}]'],
      ['ms_pos_bad_pos', 'setPositionState', '[{duration: 5, position: 10}]'],
      ['ms_pos_zero_rate', 'setPositionState', '[{duration: 5, playbackRate: 0}]'],
    ]) await observe(n, 'navigator.mediaSession', m, a);
  }
  // 8. mediaCapabilities
  if (navigator.mediaCapabilities) {
    for (const [n, m, a] of [
      ['mc_dec_missing', 'decodingInfo', '[]'],
      ['mc_dec_empty', 'decodingInfo', '[{}]'],
      ['mc_dec_file_video', 'decodingInfo', "[{type: 'file', video: {contentType: 'video/mp4; codecs=\"avc1.42E01E\"', width: 640, height: 480, bitrate: 800000, framerate: 30}}]"],
      ['mc_dec_file_audio', 'decodingInfo', "[{type: 'file', audio: {contentType: 'audio/mp4; codecs=\"mp4a.40.2\"'}}]"],
      ['mc_dec_bad_mime', 'decodingInfo', "[{type: 'file', video: {contentType: 'text/plain', width: 640, height: 480, bitrate: 800000, framerate: 30}}]"],
      ['mc_dec_unknown_codec', 'decodingInfo', "[{type: 'file', video: {contentType: 'video/mp4; codecs=\"unknown\"', width: 640, height: 480, bitrate: 800000, framerate: 30}}]"],
      ['mc_enc_missing', 'encodingInfo', '[]'],
      ['mc_enc_webrtc_video', 'encodingInfo', "[{type: 'webrtc', video: {contentType: 'video/webm; codecs=\"vp8\"', width: 640, height: 480, bitrate: 800000, framerate: 30}}]"],
    ]) await observe(n, 'navigator.mediaCapabilities', m, a);
  }
  const pre = document.createElement('pre'); pre.id = 'bom-reference'; pre.textContent = JSON.stringify(out); document.body.append(pre);
})();
