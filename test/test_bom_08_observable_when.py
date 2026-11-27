"""Native BOM-08 Observable, Subscriber, AbortController/AbortSignal, and EventTarget.prototype.when."""

import hashlib
import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build"
)

FIXTURES = Path(__file__).parent / "fixtures" / "bom_08"
INVENTORY = Path(__file__).parent.parent / "docs" / "browser_api_inventory.json"
HTML = "<html><head></head><body><button id='btn'>go</button></body></html>"
SECURE_BASE = "http://127.0.0.1:59200/reference"
INSECURE_BASE = "http://bom-fixture.invalid:59201/reference"
DECLARED_INTERFACES = (
    "Observable",
    "Subscriber",
    "AbortController",
    "AbortSignal",
    "EventTarget",
)


def evaluate(page, body):
    return json.loads(page.eval("JSON.stringify((() => { 'use strict'; " + body + " })())"))


@pytest.fixture(scope="module")
def secure_reference():
    capture = json.loads((FIXTURES / "chrome.json").read_text(encoding="utf-8"))
    assert capture["probe_sha256"] == hashlib.sha256(
        (FIXTURES / "probe.js").read_bytes()
    ).hexdigest()
    return capture["result"]


@pytest.fixture(scope="module")
def insecure_reference():
    capture = json.loads((FIXTURES / "chrome-insecure.json").read_text(encoding="utf-8"))
    assert capture["probe_sha256"] == hashlib.sha256(
        (FIXTURES / "probe.js").read_bytes()
    ).hexdigest()
    return capture["result"]


@pytest.fixture(scope="module")
def opaque_reference():
    capture = json.loads((FIXTURES / "chrome-opaque.json").read_text(encoding="utf-8"))
    assert capture["probe_sha256"] == hashlib.sha256(
        (FIXTURES / "probe.js").read_bytes()
    ).hexdigest()
    return capture["result"]


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, SECURE_BASE)
        yield value


JS_HELPERS = """
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
  const runSync = fn => {
    try { return {ok: true, value: fn()}; }
    catch (e) { return {ok: false, error: summarizeError(e)}; }
  };
"""


def test_bom08_shapes_globals_and_interfaces_match_chrome(
    secure_reference, insecure_reference, opaque_reference
):
    for url, expected_secure, reference in (
        (SECURE_BASE, True, secure_reference),
        (INSECURE_BASE, False, insecure_reference),
        ("about:blank", False, opaque_reference),
    ):
        with iv8.Page() as current:
            current.load(HTML, url)
            assert current.eval("isSecureContext") is expected_secure
            actual = evaluate(
                current,
                JS_HELPERS
                + f"""
                const globals = {{}}, interfaces = {{}}, subobjects = {{}};
                for (const g of {json.dumps(list(DECLARED_INTERFACES))}) {{
                  globals[g] = {{present: g in globalThis, descriptor: descriptor(globalThis, g)}};
                  const proto = globalThis[g].prototype;
                  const parent = Object.getPrototypeOf(proto);
                  interfaces[g] = {{
                    constructor: functionShape(globalThis[g]),
                    constructorKeys: Reflect.ownKeys(globalThis[g]).map(String),
                    staticMembers: Object.fromEntries(
                      Reflect.ownKeys(globalThis[g])
                        .filter(k => !['length', 'name', 'prototype'].includes(String(k)))
                        .map(k => [String(k), descriptor(globalThis[g], k)])
                    ),
                    prototypeKeys: Reflect.ownKeys(proto).map(String),
                    parent: parent === Object.prototype ? 'Object.prototype' : (parent === EventTarget.prototype ? 'EventTarget.prototype' : (parent && parent.constructor ? parent.constructor.name : String(parent))),
                    members: Object.fromEntries(Reflect.ownKeys(proto).map(k => [String(k), descriptor(proto, k)])),
                  }};
                }}
                for (const [label, expr] of [
                  ['connection', 'navigator.connection'],
                  ['orientation', 'screen.orientation'],
                  ['viewport', 'visualViewport'],
                ]) {{
                  const obj = eval(expr);
                  subobjects[label] = {{
                    hasWhen: 'when' in obj,
                    ownWhen: Object.hasOwn(obj, 'when'),
                    protoOwnWhen: Object.hasOwn(Object.getPrototypeOf(obj), 'when'),
                    sameAsEventTarget: obj.when === EventTarget.prototype.when,
                  }};
                }}
                return {{
                  windowHasOwnWhen: Object.hasOwn(window, 'when'),
                  windowProtoHasOwnWhen: Object.hasOwn(Window.prototype, 'when'),
                  windowWhenEqEventTarget: window.when === EventTarget.prototype.when,
                  globals, interfaces, subobjects,
                }};
                """,
            )
            assert actual["windowHasOwnWhen"] == reference["windowHasOwnWhen"]
            assert actual["windowProtoHasOwnWhen"] == reference["windowProtoHasOwnWhen"]
            assert actual["windowWhenEqEventTarget"] == reference["windowWhenEqEventTarget"]
            assert actual["globals"] == reference["globals"], url
            assert actual["interfaces"] == reference["interfaces"], url
            assert actual["subobjects"] == reference["subobjects"], url


def test_bom_subobjects_and_navigator_reach_100_percent_inventory_coverage(page):
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    targets = {
        "navigator": "navigator",
        "navigator.connection": "navigator.connection",
        "screen.orientation": "screen.orientation",
        "visualViewport": "visualViewport",
    }
    for inv_key, expr in targets.items():
        obj_inv = inventory["objects"][inv_key]
        expected = set(obj_inv["exposed"]) | set(obj_inv["missing"])
        assert len(expected) == obj_inv["reference_count"]
        missing_in_page = evaluate(
            page,
            f"""
            const target = {expr};
            return {json.dumps(sorted(expected))}.filter(k => !(k in target));
            """,
        )
        assert missing_in_page == [], (inv_key, missing_in_page)


def test_observable_operators_when_and_abort_semantics_match_chrome(
    page, secure_reference
):
    page.eval(
        "(() => { 'use strict';"
        + JS_HELPERS
        + """
        const runAsync = async fn => {
          try { return {ok: true, value: await fn()}; }
          catch (e) { return {ok: false, error: summarizeError(e)}; }
        };
        globalThis.bom08Out = {};
        (async () => {
          const out = globalThis.bom08Out;
          out.whenBadReceiver = runSync(() => EventTarget.prototype.when.call({}));
          out.whenMissingArg = runSync(() => navigator.connection.when());
          out.whenBadOptions = runSync(() => navigator.connection.when('change', 123));
          out.windowWhenClick = runSync(() => Object.prototype.toString.call(when('click')));

          const ac = new AbortController();
          const connEvents = [];
          const connObs = navigator.connection.when('change');
          out.obsTag = Object.prototype.toString.call(connObs);
          out.obsInstance = connObs instanceof Observable;
          connObs.subscribe({
            next(ev) { connEvents.push('conn:' + ev.type); },
            complete() { connEvents.push('complete'); },
          }, {signal: ac.signal});
          navigator.connection.dispatchEvent(new Event('change'));
          navigator.connection.dispatchEvent(new Event('change'));
          ac.abort();
          navigator.connection.dispatchEvent(new Event('change'));
          out.dispatched = connEvents;

          out.obsNoNew = runSync(() => Observable(() => {}));
          out.obsMissingArg = runSync(() => new Observable());
          out.obsBadCallback = runSync(() => new Observable(123));
          out.subConstruct = runSync(() => new Subscriber());
          out.abortSigConstruct = runSync(() => new AbortSignal());

          let subShape = null;
          const customLog = [];
          const custom = new Observable(subscriber => {
            subShape = {
              tag: Object.prototype.toString.call(subscriber),
              instance: subscriber instanceof Subscriber,
              active: subscriber.active,
              signalTag: Object.prototype.toString.call(subscriber.signal),
              signalInstance: subscriber.signal instanceof AbortSignal,
              signalAborted: subscriber.signal.aborted,
            };
            subscriber.addTeardown(() => customLog.push('teardown'));
            customLog.push('active:' + subscriber.active);
            subscriber.next(1);
            subscriber.next(2);
            subscriber.complete();
            customLog.push('afterComplete:' + subscriber.active);
            customLog.push('signalAfterComplete:' + subscriber.signal.aborted);
            subscriber.next(3);
          });
          custom.subscribe({
            next(v) { customLog.push('next:' + v); },
            complete() { customLog.push('done'); },
          });
          out.subscriberShape = subShape;
          out.custom = customLog;

          const fnLog = [];
          Observable.from([4, 5]).subscribe(v => fnLog.push(v));
          out.fnSubscribe = fnLog;

          out.fromMissing = runSync(() => Observable.from());
          out.fromPrim = runSync(() => Observable.from(123));
          out.fromSameObs = Observable.from(custom) === custom;
          out.fromPromise = await runAsync(() => Observable.from(Promise.resolve('ok')).first());
          out.mapFilterTakeToArray = await runAsync(() =>
            Observable.from([1, 2, 3, 4, 5, 6])
              .filter(x => x % 2 === 0)
              .map((x, i) => x * 10 + i)
              .take(2)
              .toArray()
          );
          out.drop = await runAsync(() => Observable.from([1, 2, 3, 4]).drop(2).toArray());
          out.firstEmpty = await runAsync(() => Observable.from([]).first());
          out.last = await runAsync(() => Observable.from([7, 8, 9]).last());
          out.lastEmpty = await runAsync(() => Observable.from([]).last());
          out.find = await runAsync(() => Observable.from([1, 4, 6]).find(x => x > 3));
          out.every = await runAsync(() => Observable.from([2, 4, 6]).every(x => x % 2 === 0));
          out.some = await runAsync(() => Observable.from([1, 3, 4]).some(x => x % 2 === 0));
          out.reduce = await runAsync(() => Observable.from([1, 2, 3, 4]).reduce((a, b) => a + b, 10));
          out.reduceNoInit = await runAsync(() => Observable.from([1, 2, 3, 4]).reduce((a, b) => a + b));
          out.reduceEmptyNoInit = await runAsync(() => Observable.from([]).reduce((a, b) => a + b));
          out.forEach = await runAsync(async () => {
            const seen = [];
            const r = await Observable.from(['a', 'b']).forEach((v, i) => seen.push(v + i));
            return {rType: typeof r, seen};
          });
          out.flatMap = await runAsync(() =>
            Observable.from([1, 2]).flatMap(x => [x, x * 10]).toArray()
          );
          out.switchMap = await runAsync(() =>
            Observable.from([1, 2]).switchMap(x => [x, x + 100]).toArray()
          );
          out.catchOp = await runAsync(() =>
            new Observable(s => { s.next(1); s.error('boom'); })
              .catch(e => [e + '!'])
              .toArray()
          );
          const finLog = [];
          out.finallyOp = await runAsync(() =>
            Observable.from([9]).finally(() => finLog.push('fin')).toArray().then(v => ({v, finLog}))
          );
          const inspLog = [];
          out.inspectOp = await runAsync(() =>
            Observable.from([5]).inspect(v => inspLog.push(v)).toArray().then(v => ({v, inspLog}))
          );
          out.takeUntil = await runAsync(() => {
            const target = new EventTarget();
            const collected = [];
            navigator.connection.when('change').takeUntil(target.when('stop')).subscribe(e => collected.push(e.type));
            navigator.connection.dispatchEvent(new Event('change'));
            target.dispatchEvent(new Event('stop'));
            navigator.connection.dispatchEvent(new Event('change'));
            return collected;
          });
        })();
        })()"""
    )
    page.run_until_idle(max_steps=30)
    actual = evaluate(page, "return globalThis.bom08Out;")
    ref_beh = dict(secure_reference["behavior"])
    ref_beh.pop("abortDetails", None)
    assert actual == ref_beh


def test_async_iterable_abort_signal_timeout_any_and_dom_bubbling(page):
    page.eval(
        """
        globalThis.bom08Extra = {};
        (async () => {
          async function* gen() {
            yield 'x';
            yield 'y';
          }
          bom08Extra.asyncIter = await Observable.from(gen()).toArray();

          // DOM bubbling through document.when and window.when
          const seen = [];
          document.when('click').take(1).subscribe(e => seen.push('doc:' + e.target.id));
          window.when('click').take(1).subscribe(e => seen.push('win:' + e.target.id));
          document.getElementById('btn').dispatchEvent(new Event('click', {bubbles: true}));
          bom08Extra.bubbled = seen;

          // AbortSignal.any & AbortSignal.timeout
          const ac1 = new AbortController();
          const ac2 = new AbortController();
          const combined = AbortSignal.any([ac1.signal, ac2.signal]);
          ac2.abort('stop-2');
          bom08Extra.anyAborted = combined.aborted && combined.reason === 'stop-2';

          const timed = AbortSignal.timeout(25);
          bom08Extra.timedBefore = timed.aborted;
          globalThis.bom08Timed = timed;

          // Promise-returning Observable methods reject on bad receiver
          const bad = [];
          for (const m of ['every', 'find', 'first', 'forEach', 'last', 'reduce', 'some', 'toArray']) {
            try {
              await Observable.prototype[m].call({});
              bad.push('ok');
            } catch (e) {
              bad.push(e.name === 'TypeError');
            }
          }
          bom08Extra.badReceivers = bad;
        })();
        """
    )
    page.advance(50, max_steps=30)
    extra = evaluate(
        page,
        """
        return {
          ...globalThis.bom08Extra,
          timedAfter: globalThis.bom08Timed.aborted,
          timedReasonName: globalThis.bom08Timed.reason.name,
        };
        """,
    )
    assert extra == {
        "asyncIter": ["x", "y"],
        "bubbled": ["doc:btn", "win:btn"],
        "anyAborted": True,
        "timedBefore": False,
        "badReceivers": [True] * 8,
        "timedAfter": True,
        "timedReasonName": "TimeoutError",
    }


def test_bom08_monitoring_and_generation_teardown(page):
    page.watch_apis([
        "navigator.connection.when",
        "screen.orientation.when",
        "visualViewport.when",
        "window.when",
        "Observable.from",
        "Observable.subscribe",
        "AbortController.abort",
        "AbortSignal.abort",
    ])
    page.start_access_trace()
    page.eval(
        """
        const ac = new AbortController();
        navigator.connection.when('change').subscribe(() => {}, {signal: ac.signal});
        screen.orientation.when('change');
        visualViewport.when('resize');
        when('click');
        Observable.from([1]).subscribe(() => {});
        ac.abort();
        AbortSignal.abort();
        """
    )
    page.run_until_idle(max_steps=20)
    hits = [entry["path"] for entry in page.read_watch_api_hits()]
    for expected in (
        "navigator.connection.when",
        "screen.orientation.when",
        "visualViewport.when",
        "window.when",
        "Observable.from",
        "Observable.subscribe",
        "AbortController.abort",
        "AbortSignal.abort",
    ):
        assert expected in hits, expected
    events = page.read_access_trace(clear=True)["events"]
    assert all(e.get("source_kind") in {"eval", "timer"} for e in events)

    old_obs = page.eval("navigator.connection.when('change')")
    page.load(HTML, INSECURE_BASE)
    assert not old_obs.context_alive
    assert page.eval("typeof Observable === 'function' && typeof navigator.connection.when === 'function'")


def test_codex_review_regressions_match_chrome_158(page):
    page.eval(
        """
        globalThis.codexReg = {};
        (async () => {
          const out = globalThis.codexReg;

          // 1. switchMap replacing active async inner + 1-based index
          {
            const log = [];
            let outerSub;
            const innerSubs = {};
            const outer = new Observable(sub => { outerSub = sub; });
            const mapped = outer.switchMap((x, idx) => {
              log.push('map:' + x + ':' + idx);
              return new Observable(sub => {
                log.push('inner-start:' + x);
                innerSubs[x] = sub;
                sub.addTeardown(() => log.push('inner-teardown:' + x));
                sub.next(x + 'a');
              });
            });
            const arrPromise = mapped.toArray();
            outerSub.next(1);
            outerSub.next(2);
            log.push('after2:' + innerSubs[1].active + ':' + innerSubs[2].active);
            outerSub.complete();
            let settledEarly = false;
            arrPromise.then(() => { settledEarly = true; });
            await Promise.resolve();
            log.push('early:' + settledEarly);
            innerSubs[2].next('2b');
            innerSubs[2].complete();
            out.switchMap = { arr: await arrPromise, log };
          }

          // 2. navigator.locks.request queued signal abort, sync abort, and post-grant abort
          {
            const log = [];
            let releaseFirst;
            const firstGranted = new Promise(resolve => {
              navigator.locks.request('lock-a', async () => {
                log.push('first-granted');
                resolve();
                await new Promise(r => { releaseFirst = r; });
                log.push('first-releasing');
              });
            });
            await firstGranted;
            const acQueued = new AbortController();
            let queuedSettledImmediately = false;
            const queuedPromise = navigator.locks.request('lock-a', { signal: acQueued.signal }, () => {
              log.push('queued-cb-ran');
              return 'unexpected';
            }).then(v => ({ ok: v }), e => {
              queuedSettledImmediately = true;
              return { err: String(e) };
            });
            acQueued.abort('queued-aborted');
            await Promise.resolve();
            const settledBeforeRelease = queuedSettledImmediately;
            releaseFirst();
            await new Promise(r => setTimeout(r, 10));
            const queuedFinal = await queuedPromise;

            const acSync = new AbortController();
            let syncCbRan = false;
            const syncPromise = navigator.locks.request('lock-b', { signal: acSync.signal }, () => {
              syncCbRan = true;
              return 'sync-ok';
            }).then(v => ({ ok: v }), e => ({ err: String(e) }));
            acSync.abort('sync-aborted');
            await new Promise(r => setTimeout(r, 10));
            const syncFinal = await syncPromise;

            const acGranted = new AbortController();
            let releaseThird;
            let thirdEnteredResolve;
            const thirdEntered = new Promise(r => { thirdEnteredResolve = r; });
            const thirdPromise = navigator.locks.request('lock-c', { signal: acGranted.signal }, async () => {
              thirdEnteredResolve();
              await new Promise(r => { releaseThird = r; });
              return 'granted-ok';
            }).then(v => ({ ok: v }), e => ({ err: String(e) }));
            await thirdEntered;
            acGranted.abort('abort-after-grant');
            releaseThird();
            const thirdFinal = await thirdPromise;

            out.locks = { log, settledBeforeRelease, queuedFinal, syncCbRan, syncFinal, thirdFinal };
          }

          // 3. Observable active producer sharing (multicast) + pre-aborted + re-entrant
          {
            const log = [];
            const subs = [];
            const obs = new Observable(sub => {
              log.push('producer:' + subs.length + ':active=' + sub.active);
              subs.push(sub);
              sub.signal.addEventListener('abort', () => log.push('sub-abort:' + String(sub.signal.reason)));
              sub.addTeardown(() => log.push('producer-td'));
            });
            const ac1 = new AbortController();
            const ac2 = new AbortController();
            obs.subscribe(v => log.push('o1:' + v), { signal: ac1.signal });
            obs.subscribe(v => log.push('o2:' + v), { signal: ac2.signal });
            obs.subscribe(v => log.push('o3-aborted:' + v), { signal: AbortSignal.abort('ignored') });
            subs[0].next('A');
            ac1.abort('r1');
            subs[0].next('B');
            ac2.abort('r2');

            // Pre-aborted when idle still invokes callback with inactive subscriber
            let preAbortedActive = null;
            new Observable(sub => { preAbortedActive = sub.active; }).subscribe(
              () => {}, { signal: AbortSignal.abort() }
            );

            // Re-entrant subscribe inside next only sees future emissions
            const reLog = [];
            let sRe;
            const obsRe = new Observable(sub => { reLog.push('start'); sRe = sub; });
            obsRe.subscribe(v => {
              reLog.push('o1:' + v);
              if (v === 1) obsRe.subscribe(v2 => reLog.push('o2:' + v2));
            });
            sRe.next(1);
            sRe.next(2);
            sRe.complete();
            out.multicast = { log, preAbortedActive, reLog };
          }

          // 4. flatMap sequential queueing + 1-based index + outer error aborting inner
          {
            const log = [];
            let outerSub;
            const inners = {};
            const outer = new Observable(sub => { outerSub = sub; });
            const arrP = outer.flatMap((x, idx) => {
              log.push('mapper:' + x + ':' + idx);
              return new Observable(sub => {
                log.push('inner-sub:' + x);
                inners[x] = sub;
                sub.addTeardown(() => log.push('inner-td:' + x));
              });
            }).toArray();
            outerSub.next(1);
            outerSub.next(2);
            outerSub.complete();
            log.push('hasInner2Before1Complete=' + Boolean(inners[2]));
            inners[1].next('1a');
            inners[1].next('1b');
            inners[1].complete();
            log.push('hasInner2After1Complete=' + Boolean(inners[2]));
            inners[2].next('2a');
            inners[2].complete();
            out.flatMap = { log, arr: await arrP };
          }

          // 5. Async iterable early termination (.take(1) and abort) vs normal exhaustion
          {
            const log = [];
            const iterTake = {
              [Symbol.asyncIterator]() {
                let i = 0;
                return {
                  async next() { log.push('next:' + (++i)); return { value: i * 10, done: false }; },
                  async return(r) { log.push('return:' + arguments.length + ':' + r.name); return { done: true }; }
                };
              }
            };
            const taken = await Observable.from(iterTake).take(1).toArray();

            const logExhaust = [];
            const iterExhaust = {
              [Symbol.asyncIterator]() {
                let i = 0;
                return {
                  async next() { ++i; logExhaust.push('next:' + i); return i <= 2 ? { value: i, done: false } : { done: true }; },
                  async return() { logExhaust.push('return-called'); return { done: true }; }
                };
              }
            };
            const exhausted = await Observable.from(iterExhaust).toArray();
            out.asyncIter = { taken, log, exhausted, logExhaust };
          }

          // 6. Large AbortSignal.timeout (>= 2^31) must not fire immediately
          {
            const s1 = AbortSignal.timeout(2147483648);
            const s2 = AbortSignal.timeout(5000000000);
            await new Promise(r => setTimeout(r, 30));
            out.timeoutLarge = { s1: s1.aborted, s2: s2.aborted };
          }

          // 7. inspect({ abort() { throw ... } }) reports without interrupting teardown
          {
            const windowErrors = [];
            const onErr = ev => {
              ev.preventDefault();
              windowErrors.push(ev.error ? ev.error.message : ev.message);
            };
            window.addEventListener('error', onErr);
            const log = [];
            const ac = new AbortController();
            ac.signal.addEventListener('abort', () => log.push('signal-abort-listener'));
            const obs = new Observable(sub => {
              sub.addTeardown(() => log.push('producer-teardown'));
            });
            obs.inspect({
              abort(reason) {
                log.push('inspect-abort:' + reason);
                throw new Error('inspect-abort-boom');
              }
            }).subscribe({ next() {} }, { signal: ac.signal });
            let abortThrew = null;
            try {
              ac.abort('stop-now');
            } catch (e) {
              abortThrew = e.message;
            }
            window.removeEventListener('error', onErr);
            out.inspectAbortThrow = { abortThrew, log, windowErrors };
          }
        })();
        """
    )
    page.advance(100, max_steps=100)
    reg = evaluate(page, "return globalThis.codexReg;")
    assert reg["switchMap"] == {
        "arr": ["1a", "2a", "2b"],
        "log": [
            "map:1:1",
            "inner-start:1",
            "inner-teardown:1",
            "map:2:2",
            "inner-start:2",
            "after2:false:true",
            "early:false",
            "inner-teardown:2",
        ],
    }
    assert reg["locks"] == {
        "log": ["first-granted", "first-releasing"],
        "settledBeforeRelease": True,
        "queuedFinal": {"err": "queued-aborted"},
        "syncCbRan": False,
        "syncFinal": {"err": "sync-aborted"},
        "thirdFinal": {"ok": "granted-ok"},
    }
    assert reg["multicast"] == {
        "log": [
            "producer:0:active=true",
            "o1:A",
            "o2:A",
            "o2:B",
            "sub-abort:r2",
            "producer-td",
        ],
        "preAbortedActive": False,
        "reLog": ["start", "o1:1", "o1:2", "o2:2"],
    }
    assert reg["flatMap"] == {
        "log": [
            "mapper:1:1",
            "inner-sub:1",
            "hasInner2Before1Complete=false",
            "inner-td:1",
            "mapper:2:2",
            "inner-sub:2",
            "hasInner2After1Complete=true",
            "inner-td:2",
        ],
        "arr": ["1a", "1b", "2a"],
    }
    assert reg["asyncIter"] == {
        "taken": [10],
        "log": ["next:1", "return:1:AbortError"],
        "exhausted": [1, 2],
        "logExhaust": ["next:1", "next:2", "next:3"],
    }
    assert reg["timeoutLarge"] == {"s1": False, "s2": False}
    assert reg["inspectAbortThrow"] == {
        "abortThrew": None,
        "log": [
            "inspect-abort:stop-now",
            "producer-teardown",
            "signal-abort-listener",
        ],
        "windowErrors": ["inspect-abort-boom"],
    }

