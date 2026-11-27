"""Native BOM-05 Navigator permission, lock, session, and media-capability objects pinned to Chrome."""

import hashlib
import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build"
)

FIXTURES = Path(__file__).parent / "fixtures" / "bom_05"
HTML = "<html><head></head><body></body></html>"
SECURE_BASE = "http://127.0.0.1:52000/reference"
INSECURE_BASE = "http://bom-fixture.invalid:52001/reference"
MEMBERS = (
    "geolocation",
    "clipboard",
    "credentials",
    "locks",
    "wakeLock",
    "keyboard",
    "mediaSession",
    "mediaCapabilities",
)
EXTRA_GLOBALS = (
    "Geolocation",
    "GeolocationPositionError",
    "GeolocationPosition",
    "GeolocationCoordinates",
    "Clipboard",
    "ClipboardItem",
    "CredentialsContainer",
    "Credential",
    "LockManager",
    "Lock",
    "WakeLock",
    "WakeLockSentinel",
    "Keyboard",
    "KeyboardLayoutMap",
    "MediaSession",
    "MediaMetadata",
    "MediaCapabilities",
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
  const plain = value => {
    if (value === undefined) return {type: 'undefined'};
    if (value === null || typeof value !== 'object') return {type: typeof value, value};
    if (Array.isArray(value)) return {type: 'array', length: value.length};
    return {type: 'object', tag: Object.prototype.toString.call(value)};
  };
"""


def test_shapes_globals_and_interfaces_match_chrome_across_contexts(
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
                const globals = {{}}, interfaces = {{}}, shape = {{}};
                for (const g of {json.dumps(list(EXTRA_GLOBALS))}) {{
                  globals[g] = {{present: g in globalThis, descriptor: descriptor(globalThis, g)}};
                  if (typeof globalThis[g] === 'function' && globalThis[g].prototype) {{
                    const proto = globalThis[g].prototype;
                    const parent = Object.getPrototypeOf(proto);
                    interfaces[g] = {{
                      constructor: functionShape(globalThis[g]),
                      constructorKeys: Reflect.ownKeys(globalThis[g]).map(String),
                      prototypeKeys: Reflect.ownKeys(proto).map(String),
                      parent: parent === Object.prototype ? 'Object.prototype' : (parent === EventTarget.prototype ? 'EventTarget.prototype' : (parent && parent.constructor ? parent.constructor.name : String(parent))),
                      members: Object.fromEntries(Reflect.ownKeys(proto).map(k => [String(k), descriptor(proto, k)])),
                    }};
                  }}
                }}
                for (const name of {json.dumps(list(MEMBERS))}) {{
                  const entry = {{present: name in navigator, own: Object.hasOwn(navigator, name), descriptor: descriptor(Navigator.prototype, name)}};
                  if (entry.present) {{
                    const value = navigator[name];
                    entry.stable = value === navigator[name];
                    entry.tag = Object.prototype.toString.call(value);
                    entry.ownKeys = Reflect.ownKeys(value).map(String);
                    entry.protoName = Object.getPrototypeOf(value).constructor.name;
                    try {{ Object.getOwnPropertyDescriptor(Navigator.prototype, name).get.call({{}}); entry.badReceiver = 'ok'; }}
                    catch (e) {{ entry.badReceiver = summarizeError(e); }}
                  }}
                  shape[name] = entry;
                }}
                return {{globals, interfaces, shape}};
                """,
            )
            assert actual["globals"] == reference["globals"], url
            assert actual["shape"] == reference["shape"], url
            for iface_name, expected_iface in reference["interfaces"].items():
                # EventTarget.prototype in Chrome has deferred `when` (BOM-08); all
                # BOM-05 own prototype keys and descriptors must match 1-to-1.
                assert actual["interfaces"][iface_name] == expected_iface, (url, iface_name)


def run_bom05_call(page, case_name, target_expr, method, args_expr):
    page.eval(
        "(() => { 'use strict';"
        + JS_HELPERS
        + f"""
        globalThis.bom05Record = (function() {{
          const effects = [], sentinel = {{sentinel: {json.dumps(case_name)}}};
          const success = function (...vals) {{ effects.push({{kind: 'success', thisUndefined: this === undefined, values: vals.map(plain)}}); }};
          const failure = function (err) {{ effects.push({{kind: 'failure', thisUndefined: this === undefined, error: summarizeError(err)}}); }};
          const record = {{effects}};
          setTimeout(() => effects.push('task'), 0);
          try {{
            const target = eval({json.dumps(targetExpr := target_expr)});
            const args = eval({json.dumps(args_expr)});
            const result = Reflect.apply(target[{json.dumps(method)}], target, args);
            record.returned = plain(result);
            if (result instanceof Promise) {{
              result.then(
                v => {{ effects.push({{kind: 'resolved', value: plain(v), raw: (v && typeof v === 'object' && !Array.isArray(v) && Object.getPrototypeOf(v) === Object.prototype) ? v : undefined}}); }},
                e => {{ effects.push({{kind: 'rejected', error: summarizeError(e), same: e === sentinel}}); }}
              );
            }}
          }} catch (e) {{
            record.error = {{...summarizeError(e), same: e === sentinel}};
          }}
          effects.push('after call');
          Promise.resolve().then(() => effects.push('microtask'));
          return record;
        }})();
        }})();"""
    )
    page.run_until_idle(max_steps=30)
    return json.loads(page.eval("JSON.stringify(bom05Record)"))


CALL_MATRIX = [
    ("geo_get_missing", "navigator.geolocation", "getCurrentPosition", "[]"),
    ("geo_get_bad_cb", "navigator.geolocation", "getCurrentPosition", "[17]"),
    ("geo_get_bad_err", "navigator.geolocation", "getCurrentPosition", "[success, 17]"),
    ("geo_get_ok", "navigator.geolocation", "getCurrentPosition", "[success, failure, {timeout: 0}]"),
    ("geo_get_default", "navigator.geolocation", "getCurrentPosition", "[success, failure]"),
    ("geo_get_order", "navigator.geolocation", "getCurrentPosition", '[success, failure, {get enableHighAccuracy(){effects.push("eha");return false;}, get maximumAge(){effects.push("maxAge");return 0;}, get timeout(){effects.push("timeout");return 0;}}]'),
    ("geo_watch_missing", "navigator.geolocation", "watchPosition", "[]"),
    ("geo_watch_ok", "navigator.geolocation", "watchPosition", "[success, failure, {timeout: 0}]"),
    ("geo_clear_missing", "navigator.geolocation", "clearWatch", "[]"),
    ("geo_clear_num", "navigator.geolocation", "clearWatch", "[1]"),
    ("clip_read", "navigator.clipboard", "read", "[]"),
    ("clip_readText", "navigator.clipboard", "readText", "[]"),
    ("clip_write_missing", "navigator.clipboard", "write", "[]"),
    ("clip_write_empty", "navigator.clipboard", "write", "[[]]"),
    ("clip_writeText_missing", "navigator.clipboard", "writeText", "[]"),
    ("clip_writeText_ok", "navigator.clipboard", "writeText", "['hello']"),
    ("cred_get_empty", "navigator.credentials", "get", "[]"),
    ("cred_get_dict", "navigator.credentials", "get", "[{}]"),
    ("cred_get_password", "navigator.credentials", "get", "[{password: true}]"),
    ("cred_get_bad", "navigator.credentials", "get", "[17]"),
    ("cred_create_empty", "navigator.credentials", "create", "[]"),
    ("cred_create_dict", "navigator.credentials", "create", "[{}]"),
    ("cred_create_bad", "navigator.credentials", "create", "[17]"),
    ("cred_prevent", "navigator.credentials", "preventSilentAccess", "[]"),
    ("cred_store_missing", "navigator.credentials", "store", "[]"),
    ("cred_store_bad", "navigator.credentials", "store", "[{}]"),
    ("locks_query", "navigator.locks", "query", "[]"),
    ("locks_req_missing", "navigator.locks", "request", "[]"),
    ("locks_req_one", "navigator.locks", "request", "['res']"),
    ("locks_req_bad_cb", "navigator.locks", "request", "['res', 17]"),
    ("locks_req_ok", "navigator.locks", "request", "['res', lock => { effects.push({lockName: lock.name, lockMode: lock.mode, tag: Object.prototype.toString.call(lock)}); return 42; }]"),
    ("locks_req_opts", "navigator.locks", "request", "['res', {mode: 'shared', ifAvailable: true}, lock => lock ? lock.mode : 'none']"),
    ("locks_req_bad_mode", "navigator.locks", "request", "['res', {mode: 'bogus'}, () => {}]"),
    ("locks_req_steal_ifavail", "navigator.locks", "request", "['res', {steal: true, ifAvailable: true}, () => {}]"),
    ("locks_req_slash", "navigator.locks", "request", "['-bad', () => {}]"),
    ("wake_default", "navigator.wakeLock", "request", "[]"),
    ("wake_screen", "navigator.wakeLock", "request", "['screen']"),
    ("wake_bad", "navigator.wakeLock", "request", "['bogus']"),
    ("kb_map", "navigator.keyboard", "getLayoutMap", "[]"),
    ("kb_lock_empty", "navigator.keyboard", "lock", "[]"),
    ("kb_lock_keys", "navigator.keyboard", "lock", "[['KeyA','KeyB']]"),
    ("kb_lock_bad", "navigator.keyboard", "lock", "[17]"),
    ("kb_unlock", "navigator.keyboard", "unlock", "[]"),
    ("ms_action_ok", "navigator.mediaSession", "setActionHandler", "['play', () => {}]"),
    ("ms_action_null", "navigator.mediaSession", "setActionHandler", "['play', null]"),
    ("ms_action_bad_enum", "navigator.mediaSession", "setActionHandler", "['bogus', () => {}]"),
    ("ms_action_missing", "navigator.mediaSession", "setActionHandler", "[]"),
    ("ms_cam_ok", "navigator.mediaSession", "setCameraActive", "[false]"),
    ("ms_cam_missing", "navigator.mediaSession", "setCameraActive", "[]"),
    ("ms_mic_ok", "navigator.mediaSession", "setMicrophoneActive", "[false]"),
    ("ms_pos_empty", "navigator.mediaSession", "setPositionState", "[]"),
    ("ms_pos_ok", "navigator.mediaSession", "setPositionState", "[{duration: 10, playbackRate: 1, position: 2}]"),
    ("ms_pos_bad_neg", "navigator.mediaSession", "setPositionState", "[{duration: -1}]"),
    ("ms_pos_bad_pos", "navigator.mediaSession", "setPositionState", "[{duration: 5, position: 10}]"),
    ("ms_pos_zero_rate", "navigator.mediaSession", "setPositionState", "[{duration: 5, playbackRate: 0}]"),
    ("mc_dec_missing", "navigator.mediaCapabilities", "decodingInfo", "[]"),
    ("mc_dec_empty", "navigator.mediaCapabilities", "decodingInfo", "[{}]"),
    ("mc_dec_file_video", "navigator.mediaCapabilities", "decodingInfo", "[{type: 'file', video: {contentType: 'video/mp4; codecs=\"avc1.42E01E\"', width: 640, height: 480, bitrate: 800000, framerate: 30}}]"),
    ("mc_dec_file_audio", "navigator.mediaCapabilities", "decodingInfo", "[{type: 'file', audio: {contentType: 'audio/mp4; codecs=\"mp4a.40.2\"'}}]"),
    ("mc_dec_bad_mime", "navigator.mediaCapabilities", "decodingInfo", "[{type: 'file', video: {contentType: 'text/plain', width: 640, height: 480, bitrate: 800000, framerate: 30}}]"),
    ("mc_dec_unknown_codec", "navigator.mediaCapabilities", "decodingInfo", "[{type: 'file', video: {contentType: 'video/mp4; codecs=\"unknown\"', width: 640, height: 480, bitrate: 800000, framerate: 30}}]"),
    ("mc_enc_missing", "navigator.mediaCapabilities", "encodingInfo", "[]"),
    ("mc_enc_webrtc_video", "navigator.mediaCapabilities", "encodingInfo", "[{type: 'webrtc', video: {contentType: 'video/webm; codecs=\"vp8\"', width: 640, height: 480, bitrate: 800000, framerate: 30}}]"),
]


def test_all_captured_bom05_call_cases_match_chrome_reference(
    page, secure_reference, insecure_reference
):
    for case_name, target_expr, method, args_expr in CALL_MATRIX:
        actual = run_bom05_call(page, case_name, target_expr, method, args_expr)
        assert actual == secure_reference["calls"][case_name], case_name

    with iv8.Page() as insecure_page:
        insecure_page.load(HTML, INSECURE_BASE)
        for case_name, target_expr, method, args_expr in CALL_MATRIX:
            if case_name not in insecure_reference["calls"]:
                continue
            actual = run_bom05_call(insecure_page, case_name, target_expr, method, args_expr)
            assert actual == insecure_reference["calls"][case_name], ("insecure", case_name)


def test_wake_lock_sentinel_keyboard_map_media_metadata_and_clipboard_item_details(
    page, secure_reference
):
    page.eval(
        """
        globalThis.bom05Details = {};
        (async () => {
          bom05Details.geoErrorConstants = Object.fromEntries(
            ['PERMISSION_DENIED','POSITION_UNAVAILABLE','TIMEOUT'].map(k => [k, GeolocationPositionError[k]])
          );
          const s = await navigator.wakeLock.request('screen');
          let releaseEvents = 0;
          s.onrelease = () => { releaseEvents++; };
          s.addEventListener('release', () => { releaseEvents++; });
          bom05Details.wakeSentinel = {tag: Object.prototype.toString.call(s), released: s.released, type: s.type};
          await s.release();
          await s.release();
          bom05Details.wakeAfterRelease = {released: s.released};
          bom05Details.releaseEvents = releaseEvents;

          const m = await navigator.keyboard.getLayoutMap();
          bom05Details.kbMap = {tag: Object.prototype.toString.call(m), size: m.size, hasKeyA: m.has('KeyA'), getKeyA: m.get('KeyA')};

          bom05Details.mediaSessionInitial = {metadata: navigator.mediaSession.metadata, playbackState: navigator.mediaSession.playbackState};
          const mm = new MediaMetadata({title: 'Song', artist: 'Artist', album: 'Album', artwork: [{src: 'https://example.com/a.png', sizes: '96x96', type: 'image/png'}]});
          navigator.mediaSession.metadata = mm;
          navigator.mediaSession.playbackState = 'playing';
          bom05Details.mediaSessionAfter = {
            sameMetadata: navigator.mediaSession.metadata === mm,
            title: mm.title, artist: mm.artist, album: mm.album, artworkLen: mm.artwork.length, artwork0: mm.artwork[0],
            sameArtwork: mm.artwork === mm.artwork,
            playbackState: navigator.mediaSession.playbackState,
          };

          const blob = new Blob(['hi'], {type: 'text/plain'});
          const item = new ClipboardItem({'text/plain': blob});
          bom05Details.clipItem = {
            types: item.types,
            sameBlob: (await item.getType('text/plain')) === blob,
          };
          try { await item.getType('image/png'); }
          catch (e) { bom05Details.clipItemMissing = e.name; }
        })();
        """
    )
    page.run_until_idle(max_steps=30)
    details = json.loads(page.eval("JSON.stringify(bom05Details)"))
    for key in (
        "geoErrorConstants",
        "wakeSentinel",
        "wakeAfterRelease",
        "kbMap",
        "mediaSessionInitial",
        "mediaSessionAfter",
    ):
        assert details[key] == secure_reference["details"][key], key
    assert details["releaseEvents"] == 2
    assert details["clipItem"] == {"types": ["text/plain"], "sameBlob": True}
    assert details["clipItemMissing"] == "NotFoundError"


def test_geolocation_clear_watch_and_lock_manager_contention(page):
    page.eval(
        """
        globalThis.watchLog = [];
        const id1 = navigator.geolocation.watchPosition(() => {}, e => watchLog.push(['w1', e.code]));
        const id2 = navigator.geolocation.watchPosition(() => {}, e => watchLog.push(['w2', e.code]));
        navigator.geolocation.clearWatch(id1);

        globalThis.lockOrder = [];
        let releaseFirst;
        const gate = new Promise(r => { releaseFirst = r; });
        navigator.locks.request('mutex', async lock => {
          lockOrder.push('acquired-1:' + lock.mode);
          await gate;
          lockOrder.push('releasing-1');
        });
        navigator.locks.request('mutex', {ifAvailable: true}, lock => {
          lockOrder.push('ifAvail:' + String(lock));
        });
        navigator.locks.request('mutex', async lock => {
          lockOrder.push('acquired-2:' + lock.mode);
        });
        setTimeout(async () => {
          const q = await navigator.locks.query();
          lockOrder.push('snapshot:' + q.held.length + ':' + q.pending.length);
          releaseFirst();
        }, 5);
        """
    )
    page.run_until_idle(max_steps=40)
    assert json.loads(page.eval("JSON.stringify(watchLog)")) == [["w2", 1]]
    assert json.loads(page.eval("JSON.stringify(lockOrder)")) == [
        "acquired-1:exclusive",
        "ifAvail:null",
        "snapshot:1:1",
        "releasing-1",
        "acquired-2:exclusive",
    ]


def test_bom05_access_tracing_watch_apis_and_reload_isolation(page):
    page.watch_apis([
        "navigator.geolocation.getCurrentPosition",
        "navigator.clipboard.readText",
        "navigator.credentials.preventSilentAccess",
        "navigator.locks.request",
        "navigator.wakeLock.request",
        "navigator.keyboard.getLayoutMap",
        "navigator.mediaSession.setActionHandler",
        "navigator.mediaCapabilities.decodingInfo",
    ])
    page.start_access_trace()
    page.eval(
        """
        navigator.geolocation.getCurrentPosition(() => {}, () => {});
        navigator.clipboard.readText().catch(() => {});
        navigator.credentials.preventSilentAccess();
        navigator.locks.request('trace-lock', () => 1);
        navigator.wakeLock.request('screen');
        navigator.keyboard.getLayoutMap();
        navigator.mediaSession.setActionHandler('play', () => {});
        navigator.mediaCapabilities.decodingInfo({
          type: 'file',
          audio: {contentType: 'audio/mp4; codecs="mp4a.40.2"'},
        });
        navigator.clipboard.readText.call({}).catch(() => {});
        """
    )
    page.run_until_idle(max_steps=30)
    hits = [entry["path"] for entry in page.read_watch_api_hits()]
    for expected in (
        "navigator.geolocation.getCurrentPosition",
        "navigator.clipboard.readText",
        "navigator.credentials.preventSilentAccess",
        "navigator.locks.request",
        "navigator.wakeLock.request",
        "navigator.keyboard.getLayoutMap",
        "navigator.mediaSession.setActionHandler",
        "navigator.mediaCapabilities.decodingInfo",
    ):
        assert expected in hits, expected
    trace_events = page.read_access_trace(clear=True)["events"]
    gets = [(e["label"], e["member"]) for e in trace_events if e["operation"] == "get"]
    assert ("navigator", "geolocation") in gets
    assert ("navigator", "clipboard") in gets

    old_geo = page.eval("navigator.geolocation")
    page.load(HTML, INSECURE_BASE)
    assert not old_geo.context_alive
    assert page.eval("'geolocation' in navigator && !('clipboard' in navigator)")


def test_bom05_review_regressions_fifo_steal_signal_blob_mediation_and_event(page):
    page.eval(
        """
        globalThis.reviewOut = {};
        (async () => {
          // 1. FIFO fairness: shared2 must wait behind exclusive waiter
          const fifo = [];
          let releaseS1;
          const s1Gate = new Promise(r => { releaseS1 = r; });
          const pS1 = navigator.locks.request('fifo', {mode: 'shared'}, async () => {
            fifo.push('s1-start');
            await s1Gate;
            fifo.push('s1-end');
          });
          await new Promise(r => setTimeout(r, 0));
          const pEx = navigator.locks.request('fifo', {mode: 'exclusive'}, async () => {
            fifo.push('ex');
          });
          const pS2 = navigator.locks.request('fifo', {mode: 'shared'}, async () => {
            fifo.push('s2');
          });
          releaseS1();
          await Promise.all([pS1, pEx, pS2]);
          reviewOut.fifo = fifo;

          // 2. Steal rejects displaced holder with AbortError immediately
          const steal = [];
          let finishHolder;
          const holderGate = new Promise(r => { finishHolder = r; });
          const p1 = navigator.locks.request('s', async () => {
            steal.push('p1-start');
            await holderGate;
            steal.push('p1-end');
            return 'ok1';
          }).then(v => steal.push('p1-res:' + v), e => steal.push('p1-rej:' + e.name));
          await new Promise(r => setTimeout(r, 0));
          const p2 = navigator.locks.request('s', {steal: true}, async () => {
            steal.push('p2-start');
            return 'ok2';
          }).then(v => steal.push('p2-res:' + v));
          await p2;
          finishHolder();
          await p1;
          reviewOut.steal = steal;

          // 3. ClipboardItem string & Promise<string> normalize to Blob
          const ci1 = new ClipboardItem({'text/plain': 'hello'});
          const b1 = await ci1.getType('text/plain');
          const ci2 = new ClipboardItem({'text/plain': Promise.resolve('world')});
          const b2 = await ci2.getType('text/plain');
          reviewOut.ci = {
            b1Blob: b1 instanceof Blob, b1Type: b1.type, b1Text: await b1.text(),
            b2Blob: b2 instanceof Blob, b2Type: b2.type, b2Text: await b2.text(),
          };

          // 4. Signal & mediation validation
          try { await navigator.credentials.get({password: true, mediation: 'invalid'}); }
          catch (e) { reviewOut.badMediation = e.name + ': ' + e.message; }
          try { await navigator.locks.request('x', {signal: {}}, () => {}); }
          catch (e) { reviewOut.badSignal = e.name + ': ' + e.message; }

          // 5. WakeLockSentinel.release independent of globalThis.Event
          const savedEvent = globalThis.Event;
          globalThis.Event = function() { throw new Error('tampered'); };
          try {
            const sentinel = await navigator.wakeLock.request('screen');
            let fired = 0;
            sentinel.onrelease = () => { fired++; };
            await sentinel.release();
            reviewOut.wakeEventFired = fired;
          } finally {
            globalThis.Event = savedEvent;
          }
        })();
        """
    )
    page.run_until_idle(max_steps=60)
    out = json.loads(page.eval("JSON.stringify(reviewOut)"))
    assert out["fifo"] == ["s1-start", "s1-end", "ex", "s2"]
    assert out["steal"] == ["p1-start", "p1-rej:AbortError", "p2-start", "p2-res:ok2", "p1-end"]
    assert out["ci"] == {
        "b1Blob": True, "b1Type": "text/plain", "b1Text": "hello",
        "b2Blob": True, "b2Type": "text/plain", "b2Text": "world",
    }
    assert "CredentialMediationRequirement" in out["badMediation"]
    assert "AbortSignal" in out["badSignal"]
    assert out["wakeEventFired"] == 1
