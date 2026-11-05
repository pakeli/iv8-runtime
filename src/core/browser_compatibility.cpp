#include "iv8/browser_compatibility.h"

#include <pybind11/pybind11.h>

#include "iv8/dom_url.h"

namespace iv8 {
namespace {

// No rendering, real device access, storage allocation, or network I/O. The
// closure owns its WeakMaps and browser object identities for one generation.
constexpr char kBootstrap[] = R"JS((function (resolveBeacon) {
  'use strict';
  const styles = new WeakMap(), elements = new WeakMap();
  const supported = new Set(('color background background-color background-image ' +
    'width height min-width min-height max-width max-height display position ' +
    'left right top bottom margin margin-left margin-top margin-right margin-bottom ' +
    'padding padding-left padding-top padding-right padding-bottom border ' +
    'border-color border-width border-style border-radius box-sizing box-shadow ' +
    'font font-size font-family font-weight font-style line-height letter-spacing ' +
    'text-align text-decoration text-overflow white-space word-break overflow ' +
    'overflow-x overflow-y opacity visibility z-index float clear cursor content ' +
    'transform transform-origin transition animation animation-name flex flex-grow ' +
    'flex-shrink flex-basis flex-direction flex-wrap align-items justify-content ' +
    'gap row-gap column-gap grid grid-template-columns grid-template-rows ' +
    'user-select pointer-events vertical-align object-fit outline zoom ' +
    '-webkit-transform -webkit-transition -webkit-user-select -webkit-appearance').split(/\s+/));
  const cssName = name => name === 'cssFloat' ? 'float' :
    String(name).replace(/[A-Z]/g, character => '-' + character.toLowerCase());
  const isSupported = name => supported.has(name) || name.startsWith('--');
  function declarationParts(text) {
    const output = []; let current = '', quote = '', depth = 0;
    text = String(text).replace(/\/\*[\s\S]*?\*\//g, '');
    for (let index = 0; index < text.length; ++index) {
      const character = text[index];
      if (quote) {
        current += character;
        if (character === '\\' && index + 1 < text.length) current += text[++index];
        else if (character === quote) quote = '';
      } else if (character === '"' || character === "'") { quote = character; current += character; }
      else if (character === '(') { ++depth; current += character; }
      else if (character === ')') { --depth; current += character; }
      else if (character === ';' && depth === 0) { output.push(current); current = ''; }
      else current += character;
    }
    if (!quote && depth === 0) output.push(current);
    return output;
  }
  function parse(text) {
    const output = new Map();
    for (const part of declarationParts(text)) {
      const colon = part.indexOf(':'); if (colon < 1) continue;
      let name = part.slice(0, colon).trim();
      if (!name.startsWith('--')) name = name.toLowerCase();
      let value = part.slice(colon + 1).trim(), priority = '';
      if (/!\s*important\s*$/i.test(value)) {
        priority = 'important'; value = value.replace(/!\s*important\s*$/i, '').trim();
      }
      if (!isSupported(name) || !value) continue;
      if (output.has(name) && output.get(name).priority && !priority) continue;
      output.set(name, {value, priority});
    }
    return output;
  }
  const serialize = declarations => Array.from(declarations,
    ([name, entry]) => name + ': ' + entry.value + (entry.priority ? ' !important' : '') + ';').join(' ');
  function elementOf(style) {
    if (!elements.has(style)) throw new TypeError('Illegal invocation');
    return elements.get(style);
  }
  const declarationsOf = style => parse(elementOf(style).getAttribute('style') || '');
  function CSSStyleDeclaration() { throw new TypeError('Illegal constructor'); }
  const prototype = CSSStyleDeclaration.prototype;
  Object.defineProperties(prototype, {
    [Symbol.toStringTag]: {value: 'CSSStyleDeclaration', configurable: true},
    cssText: {enumerable: true, configurable: true,
      get() { return serialize(declarationsOf(this)); },
      set(value) { elementOf(this).setAttribute('style', serialize(parse(value))); }},
    length: {enumerable: true, configurable: true, get() { return declarationsOf(this).size; }},
    parentRule: {enumerable: true, configurable: true, get() { elementOf(this); return null; }},
  });
  Object.assign(prototype, {
    getPropertyValue(name) { return declarationsOf(this).get(String(name))?.value || ''; },
    getPropertyPriority(name) { return declarationsOf(this).get(String(name))?.priority || ''; },
    setProperty(name, value, priority = '') {
      const element = elementOf(this); name = String(name); value = value == null ? '' : String(value);
      priority = String(priority).toLowerCase();
      if (!isSupported(name)) return;
      if (!value) { this.removeProperty(name); return; }
      if (priority && priority !== 'important') return;
      const parsed = parse(name + ': ' + value + (priority ? ' !important' : '') + ';');
      if (!parsed.has(name)) return;
      const declarations = declarationsOf(this); declarations.set(name, parsed.get(name));
      element.setAttribute('style', serialize(declarations));
    },
    removeProperty(name) {
      name = String(name); const declarations = declarationsOf(this);
      const previous = declarations.get(name)?.value || ''; declarations.delete(name);
      elementOf(this).setAttribute('style', serialize(declarations)); return previous;
    },
    item(index) { return Array.from(declarationsOf(this).keys())[Number(index) >>> 0] || ''; },
    [Symbol.iterator]: Array.prototype.values,
  });
  function styleFor(element) {
    if (styles.has(element)) return styles.get(element);
    const target = Object.create(prototype);
    elements.set(target, element);
    const proxy = new Proxy(target, {
      get(target, key, receiver) {
        if (typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key))
          return Array.from(declarationsOf(receiver).keys())[Number(key)];
        if (typeof key === 'string' && isSupported(cssName(key)))
          return prototype.getPropertyValue.call(receiver, cssName(key));
        return Reflect.get(target, key, receiver);
      },
      set(target, key, value, receiver) {
        if (typeof key === 'string' && isSupported(cssName(key))) {
          prototype.setProperty.call(receiver, cssName(key), value); return true;
        }
        return Reflect.set(target, key, value, receiver);
      },
      has(target, key) { return (typeof key === 'string' && isSupported(cssName(key))) || Reflect.has(target, key); },
      ownKeys(target) { return [...Array.from(declarationsOf(target).keys(), (_, index) => String(index)), ...Reflect.ownKeys(target)]; },
      getOwnPropertyDescriptor(target, key) {
        if (typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key)) {
          const value = Array.from(declarationsOf(target).keys())[Number(key)];
          if (value !== undefined) return {value, writable: false, enumerable: true, configurable: true};
        }
        return Reflect.getOwnPropertyDescriptor(target, key);
      },
    });
    elements.set(proxy, element); styles.set(element, proxy); return proxy;
  }
  Object.defineProperty(globalThis, 'CSSStyleDeclaration', {value: CSSStyleDeclaration, writable: true, configurable: true});

  const navigatorObject = navigator;
  const navigatorPrototype = Object.getPrototypeOf(navigatorObject);
  const brand = receiver => { if (receiver !== navigatorObject) throw new TypeError('Illegal invocation'); };
  function method(name, value) {
    Object.defineProperty(navigatorPrototype, name, {value, writable: true, enumerable: true, configurable: true});
  }
  method('sendBeacon', function sendBeacon(url) {
    brand(this);
    if (arguments.length === 0) throw new TypeError('A beacon URL is required');
    // Native resolver is supplied by the closure; no HTTP request is queued.
    const target = resolveBeacon(String(url));
    if (!target || !/^https?:/i.test(target)) throw new TypeError('Beacon URL must be HTTP or HTTPS');
    return false;
  });
  if (isSecureContext) {
    const battery = Object.create(null), listeners = new Map();
    for (const [name, value] of Object.entries({charging: true, chargingTime: 0,
        dischargingTime: Infinity, level: 1})) Object.defineProperty(battery, name, {enumerable: true, value});
    Object.defineProperty(battery, Symbol.toStringTag, {value: 'BatteryManager'});
    Object.assign(battery, {
      addEventListener(type, callback) {
        if (this !== battery) throw new TypeError('Illegal invocation');
        if (callback == null) return;
        const entries = listeners.get(String(type)) || new Set(); entries.add(callback); listeners.set(String(type), entries);
      },
      removeEventListener(type, callback) {
        if (this !== battery) throw new TypeError('Illegal invocation');
        listeners.get(String(type))?.delete(callback);
      },
      dispatchEvent(event) {
        if (this !== battery || !(event instanceof Event)) throw new TypeError('Illegal invocation');
        for (const callback of Array.from(listeners.get(String(event.type)) || [])) {
          if (typeof callback === 'function') callback.call(this, event);
          else if (typeof callback.handleEvent === 'function') callback.handleEvent(event);
        }
        return !event.defaultPrevented;
      },
    });
    const batteryPromise = Promise.resolve(battery);
    method('getBattery', function getBattery() { brand(this); return batteryPromise; });
  }
  const quota = Object.create(null);
  Object.defineProperty(quota, Symbol.toStringTag, {value: 'DeprecatedStorageQuota'});
  Object.assign(quota, {
    queryUsageAndQuota(success, failure) {
      if (this !== quota) throw new TypeError('Illegal invocation');
      navigatorObject.storage.estimate().then(result => { if (typeof success === 'function') success(result.usage, result.quota); },
        error => { if (typeof failure === 'function') failure(error); });
    },
    requestQuota(size, success, failure) {
      if (this !== quota) throw new TypeError('Illegal invocation');
      size = Number(size);
      Promise.resolve().then(() => {
        if (size === 0) { if (typeof success === 'function') success(0); }
        else if (typeof failure === 'function') {
          const error = new Error('Persistent quota allocation is unavailable'); error.name = 'NotSupportedError'; failure(error);
        }
      });
    },
  });
  Object.defineProperty(navigatorPrototype, 'webkitPersistentStorage', {enumerable: true, configurable: true,
    get() { brand(this); return quota; }});
  return styleFor;
}))JS";

v8::Local<v8::Private> style_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "iv8.inlineStyle.factory"));
}

void resolve_beacon_url(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::String::Utf8Value input(info.GetIsolate(), info[0]);
    v8::String::Utf8Value base(info.GetIsolate(), info.Data());
    if (*input == nullptr || *base == nullptr) return;
    auto url = resolve_dom_url(*input, *base);
    if (!url.valid) return;
    v8::Local<v8::String> result;
    if (v8::String::NewFromUtf8(info.GetIsolate(), url.href.c_str()).ToLocal(&result))
        info.GetReturnValue().Set(result);
}

}  // namespace

void install_browser_compatibility(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& base_url) {
    pybind11::gil_scoped_release release;
    v8::Local<v8::String> source;
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> result;
    if (!v8::String::NewFromUtf8(isolate, kBootstrap).ToLocal(&source) ||
        !v8::Script::Compile(context, source).ToLocal(&script) ||
        !script->Run(context).ToLocal(&result)) return;
    v8::Local<v8::String> base;
    v8::Local<v8::Function> resolver;
    if (!result->IsFunction() ||
        !v8::String::NewFromUtf8(isolate, base_url.c_str()).ToLocal(&base) ||
        !v8::Function::New(context, &resolve_beacon_url, base).ToLocal(&resolver)) return;
    v8::Local<v8::Value> argument = resolver;
    v8::Local<v8::Value> factory;
    if (!result.As<v8::Function>()->Call(context, v8::Undefined(isolate), 1, &argument).ToLocal(&factory)) return;
    (void)context->Global()->SetPrivate(context, style_slot(isolate), factory);
}

v8::Local<v8::Value> make_inline_style(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> element) {
    v8::Local<v8::Value> factory;
    v8::Local<v8::Value> result;
    if (!context->Global()->GetPrivate(context, style_slot(isolate)).ToLocal(&factory) ||
        !factory->IsFunction() ||
        !factory.As<v8::Function>()->Call(context, v8::Undefined(isolate), 1, &element).ToLocal(&result))
        return v8::Undefined(isolate);
    return result;
}

}  // namespace iv8
