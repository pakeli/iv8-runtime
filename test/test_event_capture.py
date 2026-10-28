"""M11-11 acceptance tests: capture phase, eventPhase, listener options."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://capture.test/"
DOC = "<html><body><div id='outer'><span id='inner'></span></div></body></html>"


def _setup(page):
    page.load(DOC, BASE)
    page.eval("""
        globalThis.outer = document.getElementById('outer');
        globalThis.inner = document.getElementById('inner');
    """)


def test_no_new_python_surface():
    for name in ("EventTarget", "EventListener", "capture"):
        assert not hasattr(iv8, name)
    for name in ("addEventListener", "removeEventListener", "dispatchEvent"):
        assert not hasattr(iv8.Page, name)


@on_only
def test_phase_order():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                globalThis.log = [];
                function add(target, name, capture) {
                    target.addEventListener('t', function (e) {
                        log.push(name + ':' + e.eventPhase);
                    }, capture);
                }
                add(window, 'w-c', true);
                add(document, 'd-c', true);
                add(outer, 'o-c', true);
                add(inner, 'i-c', true);
                add(inner, 'i-b', false);
                add(outer, 'o-b', false);
                add(document, 'd-b', false);
                add(window, 'w-b', false);
                var event = new Event('t', {bubbles: true});
                inner.dispatchEvent(event);
                return log.join(',') + '|' + event.eventPhase;
            })()
        """) == "w-c:1,d-c:1,o-c:1,i-c:2,i-b:2,o-b:3,d-b:3,w-b:3|0"


@on_only
def test_capture_runs_for_non_bubbling_event():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                globalThis.log = [];
                function add(target, name, capture) {
                    target.addEventListener('t', function (e) {
                        log.push(name + ':' + e.eventPhase);
                    }, capture);
                }
                add(window, 'w-c', true);
                add(document, 'd-c', true);
                add(outer, 'o-c', true);
                add(inner, 'i-c', true);
                add(inner, 'i-b', false);
                add(outer, 'o-b', false);
                add(document, 'd-b', false);
                add(window, 'w-b', false);
                inner.dispatchEvent(new Event('t'));
                return log.join(',');
            })()
        """) == "w-c:1,d-c:1,o-c:1,i-c:2,i-b:2"


@on_only
def test_detached_subtree_captures_and_bubbles_internally():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                globalThis.log = [];
                function add(target, name, capture) {
                    target.addEventListener('t', function (e) {
                        log.push(name + ':' + e.eventPhase);
                    }, capture);
                }
                var parent = document.createElement('div');
                var child = document.createElement('span');
                parent.appendChild(child);
                add(parent, 'p-c', true);
                add(child, 'c-c', true);
                add(child, 'c-b', false);
                add(parent, 'p-b', false);
                document.addEventListener('t', function () { log.push('doc'); }, true);
                window.addEventListener('t', function () { log.push('win'); }, true);
                child.dispatchEvent(new Event('t', {bubbles: true}));
                return log.join(',');
            })()
        """) == "p-c:1,c-c:2,c-b:2,p-b:3"


@on_only
def test_stop_propagation_in_capture():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                globalThis.log = [];
                window.addEventListener('t', function (e) {
                    log.push('win:' + e.eventPhase);
                    e.stopPropagation();
                }, true);
                document.addEventListener('t', function () { log.push('doc'); }, true);
                outer.addEventListener('t', function () { log.push('outer'); }, true);
                inner.addEventListener('t', function () { log.push('inner'); });
                var event = new Event('t', {bubbles: true});
                inner.dispatchEvent(event);
                return log.join(',') + '|' + event.eventPhase;
            })()
        """) == "win:1|0"


@on_only
def test_stop_immediate_propagation_in_capture():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                globalThis.log = [];
                outer.addEventListener('t', function (e) {
                    log.push('first:' + e.eventPhase);
                    e.stopImmediatePropagation();
                }, true);
                outer.addEventListener('t', function () { log.push('second'); }, true);
                inner.addEventListener('t', function () { log.push('target'); });
                outer.addEventListener('t', function () { log.push('bubble'); });
                inner.dispatchEvent(new Event('t', {bubbles: true}));
                return log.join(',');
            })()
        """) == "first:1"


@on_only
def test_stop_propagation_at_target_keeps_target_listeners():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                globalThis.log = [];
                inner.addEventListener('t', function (e) {
                    log.push('a:' + e.eventPhase);
                    e.stopPropagation();
                });
                inner.addEventListener('t', function (e) {
                    log.push('b:' + e.eventPhase);
                }, true);
                inner.addEventListener('t', function (e) {
                    log.push('c:' + e.eventPhase);
                });
                outer.addEventListener('t', function () { log.push('bubble'); });
                inner.dispatchEvent(new Event('t', {bubbles: true}));
                return log.join(',');
            })()
        """) == "a:2,b:2,c:2"


@on_only
def test_once():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var count = 0;
                function listener() { count++; }
                inner.addEventListener('t', listener, {once: true});
                inner.dispatchEvent(new Event('t'));
                inner.dispatchEvent(new Event('t'));
                return count;
            })()
        """) == 1
        assert page.eval("""
            (function () {
                var count = 0;
                function listener() {
                    count++;
                    if (count < 5) {
                        inner.dispatchEvent(new Event('t'));
                    }
                }
                inner.addEventListener('t', listener, {once: true});
                inner.dispatchEvent(new Event('t'));
                return count;
            })()
        """) == 1


@on_only
def test_remove_listener_matches_capture_flag():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                globalThis.log = [];
                function listener() { log.push('f'); }
                inner.addEventListener('t', listener, true);
                inner.removeEventListener('t', listener);
                inner.dispatchEvent(new Event('t'));
                inner.removeEventListener('t', listener, true);
                inner.dispatchEvent(new Event('t'));
                return log.join(',');
            })()
        """) == "f"


@on_only
def test_same_callback_with_different_capture_flags():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var count = 0;
                function listener() { count++; }
                inner.addEventListener('t', listener, true);
                inner.addEventListener('t', listener, true);  // duplicate ignored
                inner.addEventListener('t', listener, false); // distinct identity
                inner.dispatchEvent(new Event('t'));
                return count;
            })()
        """) == 2


@on_only
def test_passive_suppresses_prevent_default():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var event = new Event('t', {cancelable: true});
                inner.addEventListener('t', function (e) {
                    e.preventDefault();
                }, {passive: true});
                var ok = inner.dispatchEvent(event);
                return [ok, event.defaultPrevented].join('|');
            })()
        """) == "true|false"
        assert page.eval("""
            (function () {
                var event = new Event('t', {cancelable: true});
                inner.addEventListener('t', function (e) {
                    e.preventDefault();
                });
                var ok = inner.dispatchEvent(event);
                return [ok, event.defaultPrevented].join('|');
            })()
        """) == "false|true"


@on_only
def test_window_dispatch_is_at_target_only():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                globalThis.log = [];
                window.addEventListener('t', function (e) {
                    log.push('w-c:' + e.eventPhase);
                }, true);
                window.addEventListener('t', function (e) {
                    log.push('w-b:' + e.eventPhase);
                });
                document.addEventListener('t', function () { log.push('doc'); }, true);
                window.dispatchEvent(new Event('t', {bubbles: true}));
                return log.join(',');
            })()
        """) == "w-c:2,w-b:2"


@on_only
def test_document_dispatch_phases():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                globalThis.log = [];
                window.addEventListener('t', function (e) {
                    log.push('w-c:' + e.eventPhase);
                }, true);
                document.addEventListener('t', function (e) {
                    log.push('d-b:' + e.eventPhase);
                });
                document.addEventListener('t', function (e) {
                    log.push('d-c:' + e.eventPhase);
                }, true);
                window.addEventListener('t', function (e) {
                    log.push('w-b:' + e.eventPhase);
                });
                document.dispatchEvent(new Event('t', {bubbles: true}));
                return log.join(',');
            })()
        """) == "w-c:1,d-b:2,d-c:2,w-b:3"


@on_only
def test_boolean_capture_option():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                globalThis.log = [];
                outer.addEventListener('t', function (e) {
                    log.push('outer:' + e.eventPhase);
                }, true);
                inner.addEventListener('t', function (e) {
                    log.push('inner:' + e.eventPhase);
                });
                inner.dispatchEvent(new Event('t'));
                return log.join(',');
            })()
        """) == "outer:1,inner:2"


@on_only
def test_filereader_native_event_is_at_target():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.log = [];
            globalThis.reader = new FileReader();
            reader.addEventListener('load', function (e) {
                log.push('capture:' + e.eventPhase);
            }, true);
            reader.addEventListener('load', function (e) {
                log.push('bubble:' + e.eventPhase);
            });
            reader.readAsText(new Blob(['x']));
        """)
        page.run_jobs()
        assert page.eval("log.join(',')") == "capture:2,bubble:2"
