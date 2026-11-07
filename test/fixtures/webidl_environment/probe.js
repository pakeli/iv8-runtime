() => {
  const results = {};
  function outcome(action) {
    try { const value = action(); return {returned: value === undefined ? '<undefined>' : value}; }
    catch (error) { return {error: error.name, message: error.message}; }
  }
  function add(name, action) { results[name] = outcome(action); }
  function chain(object) {
    const values = [];
    let current = object;
    for (let depth = 0; current && depth < 8; depth++) {
      const descriptor = Object.getOwnPropertyDescriptor(current, 'constructor');
      values.push({tag: Object.prototype.toString.call(current),
                   ownConstructor: descriptor && typeof descriptor.value === 'function' ? descriptor.value.name : null});
      current = Object.getPrototypeOf(current);
    }
    return values;
  }
  function descriptor(object, key) {
    let current = object;
    for (let depth = 0; current && depth < 8; depth++) {
      const value = Object.getOwnPropertyDescriptor(current, key);
      if (value) return {depth, enumerable: value.enumerable, configurable: value.configurable,
        kind: 'value' in value ? 'data' : 'accessor',
        writable: value.writable === undefined ? null : value.writable,
        getter: typeof value.get === 'function' ? {name:value.get.name, length:value.get.length, source:Function.prototype.toString.call(value.get)} : null,
        setter: typeof value.set === 'function' ? {name:value.set.name, length:value.set.length, source:Function.prototype.toString.call(value.set)} : null};
      current = Object.getPrototypeOf(current);
    }
    return null;
  }
  const objects = {window, document, navigator, screen, location,
    div:document.createElement('div'), form:document.createElement('form'),
    script:document.createElement('script'), meta:document.createElement('meta')};
  for (const [name, object] of Object.entries(objects)) {
    add(name+'.tag', () => Object.prototype.toString.call(object));
    add(name+'.constructor', () => object.constructor && object.constructor.name);
    add(name+'.chain', () => chain(object));
  }
  for (const name of ['Window','Document','HTMLDocument','Navigator','Screen','Location','EventTarget','Node','Element','HTMLElement','HTMLDivElement','HTMLFormElement','HTMLScriptElement','HTMLMetaElement','XMLHttpRequest','Worker','fetch','open']) {
    add('global.'+name, () => typeof globalThis[name]);
  }
  const methods = {
    createElement:document.createElement, getElementById:document.getElementById,
    getElementsByTagName:document.getElementsByTagName, addEventListener:document.addEventListener,
    removeChild:document.body.removeChild, getAttribute:objects.div.getAttribute,
    replace:location.replace, storageGet:localStorage.getItem,
  };
  for (const [name, method] of Object.entries(methods)) {
    add('method.'+name, () => ({name:method.name, length:method.length,
      source:Function.prototype.toString.call(method),
      hasPrototype:Object.hasOwn(method,'prototype'),
      keys:Object.getOwnPropertyNames(method).sort()}));
  }
  for (const [name, object, key] of [
    ['document.cookie',document,'cookie'], ['document.readyState',document,'readyState'], ['document.referrer',document,'referrer'],
    ['navigator.userAgent',navigator,'userAgent'], ['navigator.webdriver',navigator,'webdriver'],
    ['div.id',objects.div,'id'], ['form.action',objects.form,'action'],
    ['script.src',objects.script,'src'], ['meta.content',objects.meta,'content'],
    ['location.href',location,'href'],
  ]) add('descriptor.'+name, () => descriptor(object,key));
  add('keys.document', () => Object.keys(document).sort());
  add('keys.navigator', () => Object.keys(navigator).sort());
  add('ownNames.document', () => Object.getOwnPropertyNames(document).sort());
  add('brand.createElement.wrongThis', () => document.createElement.call({},'div'));
  add('brand.getElementById.wrongThis', () => document.getElementById.call({},'missing'));
  add('brand.getAttribute.wrongThis', () => objects.div.getAttribute.call({},'id'));
  add('brand.getElementsByTagName.wrongThis', () => document.getElementsByTagName.call({},'div'));
  add('construct.createElement', () => Object.prototype.toString.call(new document.createElement('div')));
  add('construct.getAttribute', () => Object.prototype.toString.call(new objects.div.getAttribute('id')));
  add('storage.get.identity', () => localStorage.getItem === localStorage.getItem);
  add('document.method.identity', () => document.createElement === document.createElement);
  add('node.method.identity', () => objects.div.getAttribute === objects.div.getAttribute);
  return results;
}
