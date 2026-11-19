#include "iv8/browser_compatibility.h"

#include <pybind11/pybind11.h>

#include "iv8/dom_url.h"
#include "iv8/access_origin.h"
#include "browser/window/post_message.h"

namespace iv8 {
namespace {

// No rendering, real device access, storage allocation, or network I/O. The
// closure owns its WeakMaps and browser object identities for one generation.
constexpr char kBootstrap[] = R"JS((function (resolveBeacon, beginCall, endCall, dataProperty, withUser) {
  'use strict';
  const runtimeField = (object, name) => withUser(object, name);
  const stringTagKey = Symbol.toStringTag;
  const styles = new WeakMap(), elements = new WeakMap(), traceTargets = new WeakMap();
  const traceTargetOf = WeakMap.prototype.get.bind(traceTargets);
  const rememberTraceTarget = WeakMap.prototype.set.bind(traceTargets);
  let interceptedProperty = null;
  const traceTarget = receiver => traceTargetOf(receiver) || prototype;
  function beginTrace(operation, receiver, label, member, args) {
    return beginCall('', args, operation, receiver, 'host_object', label, member);
  }
  function beginStyleProperty(operation, receiver, member, args) {
    if (interceptedProperty && interceptedProperty.receiver === receiver &&
        interceptedProperty.operation === operation && interceptedProperty.member === member) {
      interceptedProperty = null;
      return;
    }
    return beginTrace(operation, traceTarget(receiver), 'CSSStyleDeclaration', member, args);
  }
  function defineData(target, name, value, writable, enumerable, configurable, label, owner) {
    dataProperty(target, name, value, writable, enumerable, configurable, label, owner);
  }
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
    withUser(() => String(name)).replace(/[A-Z]/g, character => '-' + character.toLowerCase());
  const isSupported = name => supported.has(name) || name.startsWith('--');
  function declarationParts(text) {
    const output = []; let current = '', quote = '', depth = 0;
    text = withUser(() => String(text)).replace(/\/\*[\s\S]*?\*\//g, '');
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
  const declarationsOf = style => {
    const element = elementOf(style);
    return parse(withUser(() => element.getAttribute('style'), element, 'getAttribute', false, true) || '');
  };
  function CSSStyleDeclaration() {
      const traceToken = beginCall('window.CSSStyleDeclaration', arguments);
      let traceResult, traceError, traceThrew = false;
      try { throw new TypeError('Illegal constructor');
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    }
  const prototype = CSSStyleDeclaration.prototype;
  defineData(prototype, stringTagKey, 'CSSStyleDeclaration', false, false,
    true, 'CSSStyleDeclaration.prototype', prototype);
  Object.defineProperties(prototype, {
    cssText: {enumerable: true, configurable: true,
      get() {
      const traceToken = beginStyleProperty('get', this, 'cssText', arguments);
      let traceResult, traceError, traceThrew = false;
      try { return traceResult = (serialize(declarationsOf(this)));
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
      set(value) {
      const traceToken = beginStyleProperty('set', this, 'cssText', arguments);
      let traceResult, traceError, traceThrew = false;
      try {
        const element = elementOf(this);
        withUser(() => element.setAttribute('style', serialize(parse(value))), element, 'setAttribute', false, true);
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    }},
    length: {enumerable: true, configurable: true, get() {
      const traceToken = beginStyleProperty('get', this, 'length', arguments);
      let traceResult, traceError, traceThrew = false;
      try { return traceResult = (declarationsOf(this).size);
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    }},
    parentRule: {enumerable: true, configurable: true, get() {
      const traceToken = beginStyleProperty('get', this, 'parentRule', arguments);
      let traceResult, traceError, traceThrew = false;
      try { elementOf(this); return traceResult = (null);
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    }},
  });
  Object.assign(prototype, {
    getPropertyValue(name) {
      const traceToken = beginTrace('call', traceTarget(this), 'CSSStyleDeclaration', 'getPropertyValue', arguments);
      let traceResult, traceError, traceThrew = false;
      try { return traceResult = (declarationsOf(this).get(withUser(() => String(name)))?.value || '');
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
    getPropertyPriority(name) {
      const traceToken = beginTrace('call', traceTarget(this), 'CSSStyleDeclaration', 'getPropertyPriority', arguments);
      let traceResult, traceError, traceThrew = false;
      try { return traceResult = (declarationsOf(this).get(withUser(() => String(name)))?.priority || '');
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
    setProperty(name, value, priority = '') {
      const traceToken = beginTrace('call', traceTarget(this), 'CSSStyleDeclaration', 'setProperty', arguments);
      let traceResult, traceError, traceThrew = false;
      try {
      const element = elementOf(this); name = withUser(() => String(name)); value = value == null ? '' : withUser(() => String(value));
      priority = withUser(() => String(priority)).toLowerCase();
      if (!isSupported(name)) return;
      if (!value) { this.removeProperty(name); return; }
      if (priority && priority !== 'important') return;
      const parsed = parse(name + ': ' + value + (priority ? ' !important' : '') + ';');
      if (!parsed.has(name)) return;
      const declarations = declarationsOf(this); declarations.set(name, parsed.get(name));
      withUser(() => element.setAttribute('style', serialize(declarations)), element, 'setAttribute', false, true);

      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
    removeProperty(name) {
      const traceToken = beginTrace('call', traceTarget(this), 'CSSStyleDeclaration', 'removeProperty', arguments);
      let traceResult, traceError, traceThrew = false;
      try {
      name = withUser(() => String(name)); const declarations = declarationsOf(this);
      const previous = declarations.get(name)?.value || ''; declarations.delete(name);
      const element = elementOf(this);
      withUser(() => element.setAttribute('style', serialize(declarations)), element, 'setAttribute', false, true);
      return traceResult = (previous);

      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
    item(index) {
      const traceToken = beginTrace('call', traceTarget(this), 'CSSStyleDeclaration', 'item', arguments);
      let traceResult, traceError, traceThrew = false;
      try { return traceResult = (Array.from(declarationsOf(this).keys())[withUser(() => Number(index)) >>> 0] || '');
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
    [Symbol.iterator]: Array.prototype.values,
  });
  const styleMethods = new Set(['constructor', 'getPropertyValue', 'getPropertyPriority',
    'setProperty', 'removeProperty', 'item', Symbol.iterator]);
  const isStyleMethod = Set.prototype.has.bind(styleMethods);
  // NativeData callbacks expose the defining prototype through V8 Holder().
  // References retain that owner; method calls below use each style target.
  for (const name of styleMethods) {
    const descriptor = Object.getOwnPropertyDescriptor(prototype, name);
    defineData(prototype, name, descriptor.value, descriptor.writable, descriptor.enumerable,
      descriptor.configurable, 'CSSStyleDeclaration.prototype', prototype);
  }
  function styleFor(element) {
    if (styles.has(element)) return styles.get(element);
    const target = Object.create(prototype);
    elements.set(target, element); rememberTraceTarget(target, target);
    const proxy = new Proxy(target, {
      get(target, key, receiver) {
        if (isStyleMethod(key) || key === stringTagKey) {
          return withUser(() => Reflect.get(target, key, receiver), true);
        }
      const traceToken = beginTrace('get', target, 'CSSStyleDeclaration', key, []);
      let traceResult, traceError, traceThrew = false;
      const previousProperty = interceptedProperty;
      interceptedProperty = {operation: 'get', receiver, member: key};
      try {
        if (typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key))
          return traceResult = (Array.from(declarationsOf(receiver).keys())[withUser(() => Number(key))]);
        if (typeof key === 'string' && isSupported(cssName(key)))
          return traceResult = (prototype.getPropertyValue.call(receiver, cssName(key)));
        return traceResult = withUser(() => Reflect.get(target, key, receiver), target, key);

      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        interceptedProperty = previousProperty;
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
      set(target, key, value, receiver) {
      const traceToken = beginTrace('set', target, 'CSSStyleDeclaration', key, [value]);
      let traceResult, traceError, traceThrew = false;
      const previousProperty = interceptedProperty;
      interceptedProperty = {operation: 'set', receiver, member: key};
      try {
        if (typeof key === 'string' && isSupported(cssName(key))) {
          prototype.setProperty.call(receiver, cssName(key), value); return traceResult = (true);
        }
        return traceResult = withUser(() => Reflect.set(target, key, value, receiver), target, key, true);

      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        interceptedProperty = previousProperty;
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
      has(target, key) {
      const traceToken = beginTrace('has', target, 'CSSStyleDeclaration', key, []);
      let traceResult, traceError, traceThrew = false;
      try { return traceResult = ((typeof key === 'string' && isSupported(cssName(key))) || Reflect.has(target, key));
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
      ownKeys(target) {
      const traceToken = beginTrace('own_keys', target, 'CSSStyleDeclaration', '*', []);
      let traceResult, traceError, traceThrew = false;
      try { return traceResult = ([...Array.from(declarationsOf(target).keys(), (_, index) => withUser(() => String(index))), ...Reflect.ownKeys(target)]);
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
      getOwnPropertyDescriptor(target, key) {
      const traceToken = beginTrace('get_own_property_descriptor', target, 'CSSStyleDeclaration', key, []);
      let traceResult, traceError, traceThrew = false;
      try {
        if (typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key)) {
          const value = Array.from(declarationsOf(target).keys())[withUser(() => Number(key))];
          if (value !== undefined) return traceResult = ({value, writable: false, enumerable: true, configurable: true});
        }
        return traceResult = (Reflect.getOwnPropertyDescriptor(target, key));

      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
    });
    elements.set(proxy, element); rememberTraceTarget(proxy, target); styles.set(element, proxy); return proxy;
  }
  Object.defineProperty(globalThis, 'CSSStyleDeclaration', {value: CSSStyleDeclaration, writable: true, configurable: true});

  const navigatorObject = navigator;
  const navigatorPrototype = Object.getPrototypeOf(navigatorObject);
  const brand = receiver => { if (receiver !== navigatorObject) throw new TypeError('Illegal invocation'); };
  function method(name, value) {
    defineData(navigatorPrototype, name, value, true, true, true, 'navigator', navigatorObject);
  }
  method('sendBeacon', function sendBeacon(url) {
      const traceToken = beginTrace('call', navigatorObject, 'navigator', 'sendBeacon', arguments);
      let traceResult, traceError, traceThrew = false;
      try {
    brand(this);
    if (arguments.length === 0) throw new TypeError('A beacon URL is required');
    // Native resolver is supplied by the closure; no HTTP request is queued.
    const target = resolveBeacon(withUser(() => String(url)));
    if (!target || !/^https?:/i.test(target)) throw new TypeError('Beacon URL must be HTTP or HTTPS');
    return traceResult = (false);

      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    });
  if (isSecureContext) {
    const battery = Object.create(null), listeners = new Map();
    for (const [name, value] of Object.entries({charging: true, chargingTime: 0,
        dischargingTime: Infinity, level: 1})) defineData(battery, name, value, false, true, false, 'BatteryManager', battery);
    defineData(battery, stringTagKey, 'BatteryManager', false, false, false, 'BatteryManager', battery);
    Object.assign(battery, {
      addEventListener(type, callback) {
      const traceToken = beginTrace('call', battery, 'BatteryManager', 'addEventListener', arguments);
      let traceResult, traceError, traceThrew = false;
      try {
        if (this !== battery) throw new TypeError('Illegal invocation');
        if (callback == null) return;
        const entries = listeners.get(withUser(() => String(type))) || new Set(); entries.add(callback); listeners.set(withUser(() => String(type)), entries);

      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
      removeEventListener(type, callback) {
      const traceToken = beginTrace('call', battery, 'BatteryManager', 'removeEventListener', arguments);
      let traceResult, traceError, traceThrew = false;
      try {
        if (this !== battery) throw new TypeError('Illegal invocation');
        listeners.get(withUser(() => String(type)))?.delete(callback);

      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
      dispatchEvent(event) {
      const traceToken = beginTrace('call', battery, 'BatteryManager', 'dispatchEvent', arguments);
      let traceResult, traceError, traceThrew = false;
      try {
        if (this !== battery || !(event instanceof Event)) throw new TypeError('Illegal invocation');
        const eventType = runtimeField(event, 'type');
        for (const callback of Array.from(listeners.get(withUser(() => String(eventType))) || [])) {
          if (typeof callback === 'function') withUser(() => callback.call(this, event));
          else if (withUser(() => typeof callback.handleEvent) === 'function') withUser(() => callback.handleEvent(event));
        }
        return traceResult = (!runtimeField(event, 'defaultPrevented'));

      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
    });
    for (const name of ['addEventListener', 'removeEventListener', 'dispatchEvent']) {
      const descriptor = Object.getOwnPropertyDescriptor(battery, name);
      defineData(battery, name, descriptor.value, true, true, true, 'BatteryManager', battery);
    }
    const batteryPromise = Promise.resolve(battery);
    method('getBattery', function getBattery() {
      const traceToken = beginTrace('call', navigatorObject, 'navigator', 'getBattery', arguments);
      let traceResult, traceError, traceThrew = false;
      try { brand(this); return traceResult = (batteryPromise);
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    });
  }
  const quota = Object.create(null);
  defineData(quota, stringTagKey, 'DeprecatedStorageQuota', false, false, false,
    'DeprecatedStorageQuota', quota);
  Object.assign(quota, {
    queryUsageAndQuota(success, failure) {
      const traceToken = beginTrace('call', quota, 'DeprecatedStorageQuota', 'queryUsageAndQuota', arguments);
      let traceResult, traceError, traceThrew = false;
      try {
      if (this !== quota) throw new TypeError('Illegal invocation');
      navigatorObject.storage.estimate().then(result => { if (typeof success === 'function') withUser(() => success(result.usage, result.quota)); },
        error => { if (typeof failure === 'function') withUser(() => failure(error)); });

      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
    requestQuota(size, success, failure) {
      const traceToken = beginTrace('call', quota, 'DeprecatedStorageQuota', 'requestQuota', arguments);
      let traceResult, traceError, traceThrew = false;
      try {
      if (this !== quota) throw new TypeError('Illegal invocation');
      size = withUser(() => Number(size));
      Promise.resolve().then(() => {
        if (size === 0) { if (typeof success === 'function') withUser(() => success(0)); }
        else if (typeof failure === 'function') {
          const error = new Error('Persistent quota allocation is unavailable'); error.name = 'NotSupportedError'; withUser(() => failure(error));
        }
      });

      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    },
  });
  for (const name of ['queryUsageAndQuota', 'requestQuota']) {
    const descriptor = Object.getOwnPropertyDescriptor(quota, name);
    defineData(quota, name, descriptor.value, true, true, true, 'DeprecatedStorageQuota', quota);
  }
  Object.defineProperty(navigatorPrototype, 'webkitPersistentStorage', {enumerable: true, configurable: true,
    get() {
      const traceToken = beginTrace('get', navigatorObject, 'navigator', 'webkitPersistentStorage', arguments);
      let traceResult, traceError, traceThrew = false;
      try { brand(this); return traceResult = (quota);
      } catch (error) {
        traceThrew = true; traceError = error; throw error;
      } finally {
        endCall(traceToken, traceThrew ? traceError : traceResult, traceThrew);
      }
    }});
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
        !v8::Script::Compile(context, source).ToLocal(&script)) return;
    if (auto* trace = access_trace_from_isolate(isolate)) {
        trace->register_runtime_script(script->GetUnboundScript()->ScriptId());
    }
    if (!script->Run(context).ToLocal(&result)) return;
    v8::Local<v8::String> base;
    v8::Local<v8::Function> resolver;
    if (!result->IsFunction() ||
        !v8::String::NewFromUtf8(isolate, base_url.c_str()).ToLocal(&base) ||
        !v8::Function::New(context, &resolve_beacon_url, base).ToLocal(&resolver)) return;
    v8::Local<v8::Function> begin, end, data_property, user_operation;
    if (!browser::make_script_call_trace_hooks(isolate, context, begin, end, data_property, user_operation)) return;
    v8::Local<v8::Value> arguments[] = {resolver, begin, end, data_property, user_operation};
    v8::Local<v8::Value> factory;
    if (!result.As<v8::Function>()->Call(context, v8::Undefined(isolate), 5, arguments).ToLocal(&factory)) return;
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
