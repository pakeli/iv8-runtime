"""M11-9 acceptance tests: cancelable / defaultPrevented / preventDefault /
stopImmediatePropagation.

The capture phase, default actions, and listener options remain out of scope.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://cancellation.test/"
DOC = "<html><body><div id='outer'><span id='inner'></span></div></body></html>"


def _setup(page):
    page.load(DOC, BASE)


def test_no_new_python_surface():
    for name in ("Event", "CancellableEvent", "preventDefault"):
        assert not hasattr(iv8, name)
    for name in ("preventDefault", "stopImmediatePropagation"):
        assert not hasattr(iv8.Page, name)


@on_only
def test_defaults():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("new Event('e').cancelable") is False
        assert page.eval("new Event('e').defaultPrevented") is False
        assert page.eval(
            "typeof new Event('e').preventDefault") == "function"
        assert page.eval(
            "typeof new Event('e').stopImmediatePropagation") == "function"
        assert page.eval(
            "new Event('e').preventDefault()") is iv8.JSUndefined
        assert page.eval("new Event('e').defaultPrevented") is False
        for member in ("eventPhase", "composed", "timeStamp"):
            assert page.eval(
                f"typeof new Event('e').{member}") == "undefined"


@on_only
def test_cancelable_from_init():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval(
            "new Event('e', {cancelable: true}).cancelable") is True
        assert page.eval(
            "new Event('e', {cancelable: 1}).cancelable") is True
        assert page.eval(
            "new Event('e', {cancelable: 0}).cancelable") is False
        assert page.eval("new Event('e', null).cancelable") is False
        assert page.eval("new Event('e', 5).cancelable") is False


@on_only
def test_prevent_default_requires_cancelable():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var event = new Event('e');
                event.preventDefault();
                return event.defaultPrevented;
            })()
        """) is False
        assert page.eval("""
            (function () {
                var event = new Event('e', {cancelable: true});
                event.preventDefault();
                return event.defaultPrevented;
            })()
        """) is True


@on_only
def test_dispatch_event_return_value():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var event = new Event('e', {cancelable: true});
                var seen = 0;
                document.getElementById('inner').addEventListener('e', function (ev) {
                    seen++;
                    ev.preventDefault();
                });
                var ok = document.getElementById('inner').dispatchEvent(event);
                return [ok, event.defaultPrevented, seen].join('|');
            })()
        """) == "false|true|1"
        assert page.eval("""
            (function () {
                var event = new Event('e');
                document.getElementById('inner').addEventListener('e', function (ev) {
                    ev.preventDefault();
                });
                return document.getElementById('inner').dispatchEvent(event);
            })()
        """) is True


@on_only
def test_dispatch_event_return_value_window_and_document():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                window.addEventListener('w', function (ev) { ev.preventDefault(); });
                return window.dispatchEvent(new Event('w', {cancelable: true}));
            })()
        """) is False
        assert page.eval("""
            (function () {
                document.addEventListener('d', function (ev) { ev.preventDefault(); });
                return document.dispatchEvent(new Event('d', {cancelable: true}));
            })()
        """) is False


@on_only
def test_stop_propagation_keeps_same_target_listeners():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var log = [];
                var inner = document.getElementById('inner');
                var outer = document.getElementById('outer');
                inner.addEventListener('t', function (ev) {
                    log.push('first');
                    ev.stopPropagation();
                });
                inner.addEventListener('t', function () { log.push('second'); });
                outer.addEventListener('t', function () { log.push('outer'); });
                inner.dispatchEvent(new Event('t', {bubbles: true}));
                return log.join(',');
            })()
        """) == "first,second"


@on_only
def test_stop_immediate_propagation_stops_same_target_and_bubbling():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var log = [];
                var inner = document.getElementById('inner');
                var outer = document.getElementById('outer');
                inner.addEventListener('t', function (ev) {
                    log.push('first');
                    ev.stopImmediatePropagation();
                });
                inner.addEventListener('t', function () { log.push('second'); });
                outer.addEventListener('t', function () { log.push('outer'); });
                document.addEventListener('t', function () { log.push('doc'); });
                window.addEventListener('t', function () { log.push('win'); });
                inner.dispatchEvent(new Event('t', {bubbles: true}));
                return log.join(',');
            })()
        """) == "first"


@on_only
def test_cancellation_is_inherited_by_subclasses():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var target = document.getElementById('inner');
                var event = new CustomEvent('c', {
                    cancelable: true, detail: {x: 1}
                });
                target.addEventListener('c', function (ev) {
                    ev.preventDefault();
                });
                var ok = target.dispatchEvent(event);
                return [ok, event.defaultPrevented, event.detail.x].join('|');
            })()
        """) == "false|true|1"
        assert page.eval(
            "new MouseEvent('m', {cancelable: true}).cancelable") is True
        assert page.eval(
            "new KeyboardEvent('k', {cancelable: true}).cancelable") is True
        assert page.eval(
            "new FocusEvent('f', {cancelable: true}).cancelable") is True
