#include "bootstrap_scripts.h"

#include <cstring>
#include <unordered_set>

#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

// --- M11-40 scheduler.postTask ------------------------------------------------
//
// A small Promise-based wrapper over the existing timer queue. There is no
// priority queue, AbortSignal, or automatic loop: the task runs on the next
// run_timers()/facade turn, and the Promise settles through the normal
// microtask checkpoint.
constexpr char kSchedulerBootstrap[] = R"JS(
(function () {
  'use strict';
  function postTask(callback, options) {
    if (typeof callback !== 'function') {
      throw new TypeError('scheduler.postTask: callback must be a function');
    }
    options = options || {};
    var delay = 0;
    if (options.delay !== undefined) {
      delay = Number(options.delay);
      if (!isFinite(delay) || delay < 0) {
        throw new TypeError(
          'scheduler.postTask: delay must be a non-negative finite number');
      }
    }
    return new Promise(function (resolve, reject) {
      setTimeout(function () {
        try {
          resolve(callback());
        } catch (error) {
          reject(error);
        }
      }, delay);
    });
  }
  var scheduler = Object.create(null);
  Object.defineProperty(scheduler, 'postTask', {
    value: postTask, writable: false, enumerable: true, configurable: false
  });
  Object.defineProperty(globalThis, 'scheduler', {
    value: scheduler, writable: false, enumerable: true, configurable: false
  });
})();
)JS";

// --- M11-41..M11-45 BOM metadata / device stubs ------------------------------
//
// The remaining deterministic BOM surfaces that need no layout, network, OS
// permission prompt, or background event source: the visual viewport, a small
// media-query evaluator, deterministic StorageManager/Permissions stubs, and
// legacy empty plugin metadata. Everything is installed as read-only JS
// properties; Promises settle through the normal Page.run_jobs() checkpoint.
constexpr char kBomExtrasBootstrap[] = R"JS(
(function () {
  'use strict';

  function defineReadonly(target, name, value) {
    Object.defineProperty(target, name, {
      value: value,
      writable: false,
      enumerable: true,
      configurable: false
    });
  }

  // Step 5g: capture the temporary native result factory and remove its global
  // binding. matchMedia itself stays a JS function (its toString behavior is
  // unchanged); only the returned list is HostObject-backed.
  var makeMediaQueryList = globalThis.__iv8MakeMediaQueryList;
  delete globalThis.__iv8MakeMediaQueryList;

  // The reported viewport is fixed for the generation, so read it once during
  // the internal bootstrap instead of on every media-query evaluation. These
  // two reads are visible to the access trace as internal-source events.
  var viewportWidth = Number(globalThis.innerWidth || 0);
  var viewportHeight = Number(globalThis.innerHeight || 0);

  // M11-45: navigator plugins/mimeTypes/javaEnabled are installed natively
  // through NavigatorHost (see its get_property) so reads/calls can be traced.

  // M11-42: bounded media-query evaluation. Supported media types are all,
  // screen, and print; supported features are width/height (exact/min/max in
  // px), orientation, prefers-color-scheme, prefers-reduced-motion, hover,
  // pointer, display-mode, update, and scripting. Unsupported queries are
  // simply false. There are no dynamic viewport changes, so no change events.
  function lengthPx(value) {
    var match = /^([0-9]*\.?[0-9]+)px$/i.exec(value.trim());
    return match === null ? NaN : parseFloat(match[1]);
  }

  function featureMatches(rawFeature) {
    var feature = rawFeature.trim().toLowerCase();
    var colon = feature.indexOf(':');
    var name = colon < 0 ? feature : feature.slice(0, colon).trim();
    var value = colon < 0 ? '' : feature.slice(colon + 1).trim();
    var width = viewportWidth;
    var height = viewportHeight;

    if (name === 'width' || name === 'min-width' || name === 'max-width') {
      var w = lengthPx(value);
      if (isNaN(w)) return false;
      return name === 'width' ? width === w
           : name === 'min-width' ? width >= w : width <= w;
    }
    if (name === 'height' || name === 'min-height' || name === 'max-height') {
      var h = lengthPx(value);
      if (isNaN(h)) return false;
      return name === 'height' ? height === h
           : name === 'min-height' ? height >= h : height <= h;
    }
    if (name === 'orientation') {
      if (value === 'portrait') return height >= width;
      if (value === 'landscape') return width > height;
      return false;
    }
    if (name === 'prefers-color-scheme') {
      return value === 'light';
    }
    if (name === 'prefers-reduced-motion') {
      return value === 'no-preference';
    }
    if (name === 'hover' || name === 'any-hover') {
      return value === 'hover';
    }
    if (name === 'pointer' || name === 'any-pointer') {
      return value === 'fine';
    }
    if (name === 'display-mode') {
      return value === 'browser';
    }
    if (name === 'update') {
      return value === 'fast';
    }
    if (name === 'scripting') {
      return value === 'enabled';
    }
    return false;
  }

  function singleQueryMatches(rawQuery) {
    var query = rawQuery.trim();
    if (query === '') return true;
    var negate = false;
    if (/^only\s+/i.test(query)) {
      query = query.replace(/^only\s+/i, '').trim();
    }
    if (/^not\s+/i.test(query)) {
      negate = true;
      query = query.replace(/^not\s+/i, '').trim();
    }
    var parts = query.split(/\s+and\s+/i);
    var matched = true;
    for (var i = 0; i < parts.length; i++) {
      var part = parts[i].trim();
      if (part === '') {
        matched = false;
        break;
      }
      if (part.charAt(0) === '(') {
        if (part.charAt(part.length - 1) !== ')' ||
            !featureMatches(part.slice(1, -1))) {
          matched = false;
          break;
        }
        continue;
      }
      var type = part.toLowerCase();
      if (type === 'all' || type === 'screen') {
        continue;
      }
      matched = false;
      break;
    }
    return negate ? !matched : matched;
  }

  function mediaMatches(rawQuery) {
    var queries = String(rawQuery).split(',');
    for (var i = 0; i < queries.length; i++) {
      if (singleQueryMatches(queries[i])) return true;
    }
    return false;
  }

  defineReadonly(globalThis, 'matchMedia', function (query) {
    var text = String(query);
    return makeMediaQueryList(text, mediaMatches(text));
  });

  // M11-43: deterministic StorageManager. It is installed natively through
  // NavigatorHost (see make_storage_manager_object) so its reads/calls can be
  // access-traced while keeping the plain-object shape.

  // M11-44: navigator.permissions is installed natively through NavigatorHost
  // (see make_permissions_object) so its parent read, query() calls, and returned
  // PermissionStatus reads/calls can be access-traced while keeping the plain
  // descriptor/prototype shape.
})();
)JS";

// The bootstrap captures and deletes the hidden natives, then exposes a normal
// writable global function. Using captured originals keeps postMessage working
// if page scripts later replace structuredClone / MessageEvent / setTimeout.
// Target origins are bounded: "*", "/", the page origin, or the origin plus a
// trailing slash deliver; every other value is silently dropped. A non-empty
// transfer list throws TypeError. Delivery is a timer task, so it runs on
// Page.run_timers() (or a facade turn), not Page.run_jobs().
constexpr char kWindowPostMessageBootstrap[] = R"JS(
(function () {
  'use strict';

  var dispatch = globalThis.__iv8PostMessageDispatch;
  var recordCall = globalThis.__iv8PostMessageRecord;
  delete globalThis.__iv8PostMessageDispatch;
  delete globalThis.__iv8PostMessageRecord;

  var scheduleTask = globalThis.setTimeout;
  var clone = globalThis.structuredClone;
  var MessageEventCtor = globalThis.MessageEvent;
  var windowOrigin = String(globalThis.origin);

  function normalizeTargetOrigin(targetOrigin) {
    if (targetOrigin === undefined) return '/';
    var text = String(targetOrigin);
    if (text === '*') return '*';
    if (text === '/' || text === windowOrigin ||
        text === windowOrigin + '/') {
      return windowOrigin;
    }
    return null;
  }

  function postMessage(message, targetOrigin, transfer) {
    if (arguments.length < 1) {
      throw new TypeError('postMessage: message is required');
    }
    recordCall();
    if (transfer !== undefined && transfer !== null) {
      if (typeof transfer.length !== 'number' || transfer.length !== 0) {
        throw new TypeError('postMessage: transfer lists are not supported');
      }
    }
    if (normalizeTargetOrigin(targetOrigin) === null) {
      return;  // target origin mismatch: silently drop
    }
    var data = clone(message);
    var event = new MessageEventCtor('message', {
      data: data,
      origin: windowOrigin,
      lastEventId: '',
      source: globalThis,
      ports: []
    });
    scheduleTask(function () { dispatch(event); }, 0);
  }

  Object.defineProperty(globalThis, 'postMessage', {
    value: postMessage,
    writable: true,
    enumerable: true,
    configurable: true
  });
})();
)JS";

}  // namespace iv8::browser
