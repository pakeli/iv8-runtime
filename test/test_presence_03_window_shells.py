"""Ordinary Window shells preserve identity and never claim backend success."""

import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")


def test_css_escape_matches_isolated_chrome_identifier_and_utf16_reference():
    reference = json.loads((Path(__file__).parent / "fixtures/presence_03_css_escape/chrome.json").read_text())
    inputs = [json.loads(case["input_json"]) for case in reference["cases"]]
    expected = [json.loads(case["output_json"]) for case in reference["cases"]]
    with iv8.Page() as page:
        actual = json.loads(page.eval("JSON.stringify(" + json.dumps(inputs) + ".map(value => CSS.escape(value)))"))
        assert actual == expected


def test_window_shells_use_canonical_prototypes_without_new_interface_globals():
    with iv8.Page() as page:
        assert page.eval("""(() => {
            const cachePrototype = Object.getPrototypeOf(caches);
            const hidden = ['CacheStorage', 'CookieStore', 'IDBFactory', 'Navigation',
                'DocumentPictureInPicture', 'LaunchQueue', 'SpeechSynthesis',
                'TrustedTypePolicyFactory', 'Viewport', 'CustomElementRegistry',
                'CrashReportContext', 'External'];
            let nodeFilterConstructable = true;
            try { Reflect.construct(String, [], NodeFilter); }
            catch (error) { nodeFilterConstructable = false; }
            return caches === window.caches && cachePrototype.constructor.name === 'CacheStorage' &&
                cachePrototype.constructor.prototype === cachePrototype &&
                Object.getPrototypeOf(CSS) === Object.prototype &&
                Object.prototype.toString.call(CSS) === '[object CSS]' &&
                Object.getPrototypeOf(GPUBufferUsage) === Object.prototype &&
                GPUBufferUsage.COPY_DST === 8 && GPUTextureUsage.TRANSIENT_ATTACHMENT === 32 &&
                NodeFilter.SHOW_ALL === 4294967295 && NodeFilter.FILTER_ACCEPT === 1 &&
                !Object.hasOwn(NodeFilter, 'prototype') && !nodeFilterConstructable &&
                Object.getPrototypeOf(styleMedia).constructor === Object && fence === null &&
                hidden.every(name => !(name in globalThis)) &&
                [cookieStore, navigation, documentPictureInPicture, speechSynthesis].every(
                    target => target instanceof EventTarget);
        })()""")


def test_event_shells_dispatch_synthetic_events_and_reset_with_generation():
    with iv8.Page() as page:
        result = json.loads(page.eval("""JSON.stringify((() => {
            globalThis.shellEvents = [];
            cookieStore.addEventListener('change', () => shellEvents.push('listener'));
            cookieStore.onchange = () => { shellEvents.push('handler'); return false; };
            cookieStore.marker = 1;
            Object.getPrototypeOf(cookieStore).marker = 1;
            const event = new Event('change', {cancelable:true});
            return {returned:cookieStore.dispatchEvent(event), canceled:event.defaultPrevented,
                events:shellEvents, active:navigator.userActivation.isActive};
        })())"""))
        assert result == {"returned": False, "canceled": True,
                          "events": ["listener", "handler"], "active": False}
        page.load("<html><body></body></html>", "https://shell-lifecycle.test/")
        assert page.eval("""cookieStore.marker === undefined &&
            Object.getPrototypeOf(cookieStore).marker === undefined && cookieStore.onchange === null &&
            cookieStore.dispatchEvent(new Event('change', {cancelable:true})) === true""")


def test_unavailable_window_backends_throw_or_reject_instead_of_fulfilling():
    with iv8.Page() as page:
        with pytest.raises(iv8.JSError) as caught:
            page.eval("indexedDB.open('offline-only')")
        assert caught.value.name == "NotSupportedError"
        assert page.eval("""(() => {
            globalThis.cacheOutcome = 'pending';
            const promise = caches.open('offline-only');
            promise.then(() => { cacheOutcome = 'unexpected fulfillment'; },
                error => { cacheOutcome = error.name; });
            return typeof promise.then === 'function';
        })()""")
        page.run_jobs()
        assert page.eval("cacheOutcome") == "NotSupportedError"
