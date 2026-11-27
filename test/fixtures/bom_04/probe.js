(async () => {
  'use strict';
  const methods = ['canShare','share','setAppBadge','clearAppBadge','registerProtocolHandler','unregisterProtocolHandler',
    'getUserMedia','webkitGetUserMedia','requestMIDIAccess','requestMediaKeySystemAccess'];
  const out = {secure: isSecureContext, origin: location.origin, activation: navigator.userActivation.isActive,
    shape: {}, calls: {}, receivers: {}, extra: {}};
  const summarizeError = error => (error && typeof error === 'object')
    ? {name: error.name, message: error.message, tag: Object.prototype.toString.call(error),
       domException: typeof DOMException === 'function' && error instanceof DOMException}
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
        value: typeof item.value === 'function' ? functionShape(item.value) : item.value}
        : {kind: 'accessor', get: item.get ? functionShape(item.get) : null, set: item.set ? functionShape(item.set) : null})};
  };
  for (const name of methods)
    out.shape[name] = {present: name in navigator, own: Object.hasOwn(navigator, name), descriptor: descriptor(Navigator.prototype, name)};
  out.shape.sameGetUserMedia = navigator.getUserMedia === navigator.webkitGetUserMedia;
  const plain = value => {
    if (value === undefined) return {type: 'undefined'};
    if (value === null || typeof value !== 'object') return {type: typeof value, value};
    return {type: 'object', tag: Object.prototype.toString.call(value)};
  };
  async function observe(name, method, source, expectCallback = false) {
    const effects = [], sentinel = {sentinel: name};
    let called = false, finish;
    const completion = new Promise(resolve => finish = resolve);
    const success = function (...values) { called = true;
      effects.push({kind: 'success', thisUndefined: this === undefined, values: values.map(plain)}); finish(); };
    const failure = function (error) { called = true;
      effects.push({kind: 'failure', thisUndefined: this === undefined, error: summarizeError(error)}); finish(); };
    const record = {};
    let promise = null;
    setTimeout(() => effects.push('task'), 0);
    try {
      const args = eval(source);
      const result = Reflect.apply(navigator[method], navigator, args);
      record.returned = plain(result);
      if (result instanceof Promise) {
        promise = result.then(value => { effects.push({kind: 'resolved', value: plain(value)}); return value; },
          error => { effects.push({kind: 'rejected', error: summarizeError(error), same: error === sentinel}); });
      }
    } catch (error) { record.error = {...summarizeError(error), same: error === sentinel}; expectCallback = false; }
    effects.push('after call');
    await Promise.resolve(); effects.push('microtask');
    let timer;
    const timeout = new Promise(resolve => timer = setTimeout(() => { record.timeout = true; resolve(); }, 3000));
    if (promise) await Promise.race([promise, timeout]);
    else if (expectCallback && !called) await Promise.race([completion, timeout]);
    clearTimeout(timer);
    await new Promise(resolve => setTimeout(resolve, 0));
    record.effects = effects; out.calls[name] = record; record.source = source;
    return promise;
  }
  const dictionaryProbe = `{get files(){effects.push('files');return undefined;},get text(){effects.push('text');return undefined;},get title(){effects.push('title');return undefined;},get url(){effects.push('url');return 'https://example.com/share';}}`;
  if (navigator.canShare) {
    for (const [name, source] of [
      ['canShare_missing', '[]'], ['canShare_undefined', '[undefined]'], ['canShare_null', '[null]'], ['canShare_empty', '[{}]'],
      ['canShare_url', "[{url:'https://example.com/'}]"], ['canShare_relative', "[{url:'relative/path'}]"],
      ['canShare_bad_url', "[{url:'http://[bad'}]"], ['canShare_javascript', "[{url:'javascript:void(0)'}]"],
      ['canShare_ftp', "[{url:'ftp://example.com/'}]"], ['canShare_title', "[{title:'t'}]"], ['canShare_text', "[{text:'x'}]"],
      ['canShare_empty_text', "[{text:''}]"], ['canShare_files_empty', '[{files:[]}]'],
      ['canShare_file', "[{files:[new File(['a'],'a.txt',{type:'text/plain'})]}]"],
      ['canShare_file_exe', "[{files:[new File(['a'],'a.exe',{type:'application/octet-stream'})]}]"],
      ['canShare_files_bad', '[{files:[17]}]'], ['canShare_number', '[17]'], ['canShare_string', "['x']"],
      ['canShare_order', '[' + dictionaryProbe + ']'],
      ['canShare_throw', "[{get url(){effects.push('url');throw sentinel;}}]"],
    ]) await observe(name, 'canShare', source);
  }
  if (navigator.share) {
    for (const [name, source] of [
      ['share_missing', '[]'], ['share_empty', '[{}]'], ['share_url', "[{url:'https://example.com/'}]"],
      ['share_bad_url', "[{url:'http://[bad'}]"], ['share_number', '[17]'], ['share_text', "[{text:'x'}]"],
      ['share_order', '[' + dictionaryProbe + ']'], ['share_throw', "[{get url(){effects.push('url');throw sentinel;}}]"],
      ['share_file', "[{files:[new File(['a'],'a.txt',{type:'text/plain'})]}]"],
    ]) await observe(name, 'share', source);
  }
  if (navigator.setAppBadge) {
    for (const [name, source] of [
      ['setAppBadge_missing', '[]'], ['setAppBadge_undefined', '[undefined]'], ['setAppBadge_zero', '[0]'], ['setAppBadge_five', '[5]'],
      ['setAppBadge_negative', '[-1]'], ['setAppBadge_fraction', '[1.5]'], ['setAppBadge_string', "['3']"],
      ['setAppBadge_nan', '[NaN]'], ['setAppBadge_infinity', '[Infinity]'], ['setAppBadge_huge', '[2**64]'],
      ['setAppBadge_max_safe', '[Number.MAX_SAFE_INTEGER]'], ['setAppBadge_over_safe', '[Number.MAX_SAFE_INTEGER+2]'],
      ['setAppBadge_symbol', "[Symbol('x')]"], ['setAppBadge_bigint', '[1n]'], ['setAppBadge_null', '[null]'],
      ['setAppBadge_convert', "[{valueOf(){effects.push('valueOf');return 2;}}]"],
      ['setAppBadge_throw', "[{valueOf(){effects.push('valueOf');throw sentinel;}}]"],
    ]) await observe(name, 'setAppBadge', source);
  }
  if (navigator.clearAppBadge) {
    await observe('clearAppBadge_missing', 'clearAppBadge', '[]');
    await observe('clearAppBadge_extra', 'clearAppBadge', "[{valueOf(){effects.push('valueOf');return 1;}}]");
  }
  const protocolCases = [
    ['missing', '[]'], ['one', "['web+test']"], ['relative', "['web+test','/handler?q=%s']"],
    ['absolute_same', "['web+test',location.origin+'/handler?q=%s']"], ['cross_origin', "['web+test','https://example.com/?q=%s']"],
    ['insecure_cross', "['web+test','http://example.com/?q=%s']"],
    ['no_placeholder', "['web+test','/handler']"], ['bad_scheme', "['notallowed','/h?%s']"], ['mailto', "['mailto','/h?%s']"],
    ['bitcoin', "['bitcoin','/h?%s']"], ['web_plus_only', "['web+','/h?%s']"], ['upper', "['WEB+Test','/h?%s']"],
    ['web_digit', "['web+te5t','/h?%s']"], ['non_ascii', "['web+t\u00e9st','/h?%s']"], ['bad_url', "['web+test','http://[bad/%s']"],
    ['javascript_url', "['web+test','javascript:%s']"], ['data_url', "['web+test','data:text/html,%s']"],
    ['http_scheme', "['http','/h?%s']"], ['ftp_scheme', "['ftp','/h?%s']"],
    ['convert', "[{toString(){effects.push('scheme');return 'web+x';}},{toString(){effects.push('url');return '/h?%s';}}]"],
    ['throw_scheme', "[{toString(){effects.push('scheme');throw sentinel;}},{toString(){effects.push('url');return '/h?%s';}}]"],
    ['symbol', "[Symbol('x'),'/h?%s']"], ['three', "['web+test','/h?%s','title']"],
  ];
  if (navigator.registerProtocolHandler) for (const [name, source] of protocolCases) await observe('register_' + name, 'registerProtocolHandler', source);
  if (navigator.unregisterProtocolHandler) for (const [name, source] of protocolCases) await observe('unregister_' + name, 'unregisterProtocolHandler', source);
  const mediaCases = [
    ['missing', '[]', false], ['constraints_only', '[{audio:true}]', false], ['two', '[{audio:true},success]', false],
    ['empty_constraints', '[{},success,failure]', true], ['audio', '[{audio:true},success,failure]', true],
    ['video', '[{video:true},success,failure]', true], ['bad_success', '[{audio:true},17,failure]', false],
    ['bad_failure', '[{audio:true},success,17]', false], ['null_failure', '[{audio:true},success,null]', false],
    ['number_constraints', '[17,success,failure]', false], ['false_both', '[{audio:false,video:false},success,failure]', true],
    ['order', "[{get audio(){effects.push('audio');return true;},get video(){effects.push('video');return false;}},success,failure]", true],
  ];
  for (const method of ['getUserMedia', 'webkitGetUserMedia'])
    if (navigator[method]) for (const [name, source, expect] of mediaCases) await observe(method + '_' + name, method, source, expect);
  if (navigator.requestMIDIAccess) {
    for (const [name, source] of [
      ['midi_missing', '[]'], ['midi_empty', '[{}]'], ['midi_sysex', '[{sysex:true}]'], ['midi_number', '[17]'], ['midi_null', '[null]'],
      ['midi_order', "[{get software(){effects.push('software');return false;},get sysex(){effects.push('sysex');return false;}}]"],
    ]) await observe(name, 'requestMIDIAccess', source);
  }
  if (navigator.requestMediaKeySystemAccess) {
    for (const [name, source] of [
      ['mksa_missing', '[]'], ['mksa_one', "['org.w3.clearkey']"], ['mksa_empty_list', "['org.w3.clearkey',[]]"],
      ['mksa_empty_system', "['',[{}]]"], ['mksa_widevine', "['com.widevine.alpha',[{}]]"],
      ['mksa_unknown', "['unknown.system',[{}]]"], ['mksa_clearkey_empty', "['org.w3.clearkey',[{}]]"],
      ['mksa_clearkey_video', "['org.w3.clearkey',[{initDataTypes:['cenc'],videoCapabilities:[{contentType:'video/mp4; codecs=\"avc1.42E01E\"'}]}]]"],
      ['mksa_not_iterable', "['org.w3.clearkey',17]"], ['mksa_bad_config', "['org.w3.clearkey',[17]]"],
      ['mksa_order', "[{toString(){effects.push('keySystem');return 'org.w3.clearkey';}},{[Symbol.iterator](){effects.push('iterator');return [][Symbol.iterator]();}}]"],
    ]) await observe(name, 'requestMediaKeySystemAccess', source);
    try {
      const access = await navigator.requestMediaKeySystemAccess('org.w3.clearkey', [{}]);
      const proto = Object.getPrototypeOf(access);
      out.extra.access = {tag: Object.prototype.toString.call(access), keySystem: access.keySystem,
        configuration: access.getConfiguration(), prototypeKeys: Reflect.ownKeys(proto).map(String),
        globalType: typeof MediaKeySystemAccess,
        members: Object.fromEntries(Reflect.ownKeys(proto).map(key => [String(key), descriptor(proto, key)]))};
      const keys = await access.createMediaKeys();
      const keysProto = Object.getPrototypeOf(keys);
      out.extra.mediaKeys = {tag: Object.prototype.toString.call(keys), prototypeKeys: Reflect.ownKeys(keysProto).map(String),
        members: Object.fromEntries(Reflect.ownKeys(keysProto).map(key => [String(key), descriptor(keysProto, key)]))};
    } catch (error) { out.extra.accessError = summarizeError(error); }
  }
  for (const method of methods) {
    if (!navigator[method]) continue;
    const effects = [];
    try {
      const result = navigator[method].call({});
      out.receivers[method] = {returned: plain(result)};
      if (result instanceof Promise) {
        await result.then(value => { out.receivers[method].resolved = plain(value); },
          error => { out.receivers[method].rejected = summarizeError(error); });
      }
    } catch (error) { out.receivers[method] = {error: summarizeError(error)}; }
  }
  const pre = document.createElement('pre'); pre.id = 'bom-reference'; pre.textContent = JSON.stringify(out); document.body.append(pre);
})();
