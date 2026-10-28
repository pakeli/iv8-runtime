"""M11-6 acceptance tests: CustomEvent / MouseEvent / KeyboardEvent / FocusEvent.

The four constructors are plain JS event values layered on the existing Event /
EventTarget / bubbling model. There is no new Python API and no new pump.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://events.test/"
DOC = "<html><body><div id='outer'><span id='inner'></span></div></body></html>"
CLASSES = ("CustomEvent", "MouseEvent", "KeyboardEvent", "FocusEvent")


def _setup(page):
    page.load(DOC, BASE)


# --- shape guards (both build modes) -----------------------------------------

def test_no_new_python_surface():
    for name in ("Event",) + CLASSES:
        assert not hasattr(iv8, name)
    for name in CLASSES:
        assert not hasattr(iv8.Page, name)
    for pump in ("run_timers", "run_jobs", "run_animation_frame",
                 "run_idle_callbacks"):
        assert callable(getattr(iv8.Page, pump))


# --- globals / prototype chain ------------------------------------------------

@on_only
def test_global_shape_and_prototype_chain():
    with iv8.Page() as page:
        _setup(page)
        for name in CLASSES:
            assert page.eval(f"typeof {name}") == "function"
            assert page.eval(f"window.{name} === {name}") is True
            assert page.eval(f"globalThis.{name} === {name}") is True
            assert page.eval(
                f"Object.getPrototypeOf({name}.prototype) === Event.prototype"
            ) is True
        assert page.eval("typeof Event") == "function"


@on_only
@pytest.mark.parametrize("name", CLASSES)
def test_instanceof_chain(name):
    with iv8.Page() as page:
        _setup(page)
        assert page.eval(f"new {name}('x') instanceof {name}") is True
        assert page.eval(f"new {name}('x') instanceof Event") is True
        for other in CLASSES:
            if other != name:
                assert page.eval(
                    f"new {name}('x') instanceof {other}") is False


@on_only
@pytest.mark.parametrize("expr", [
    "CustomEvent('x')",
    "MouseEvent('x')",
    "KeyboardEvent('x')",
    "FocusEvent('x')",
])
def test_constructor_requires_new(expr):
    with iv8.Page() as page:
        _setup(page)
        with pytest.raises(iv8.JSError) as info:
            page.eval(expr)
        assert info.value.name == "TypeError"
        assert page.eval("1 + 1") == 2


# --- defaults / init handling -------------------------------------------------

@on_only
def test_defaults():
    with iv8.Page() as page:
        _setup(page)
        summary = page.eval("""
            (function () {
                var c = new CustomEvent('c');
                var m = new MouseEvent('m');
                var k = new KeyboardEvent('k');
                var f = new FocusEvent('f');
                return [
                    c.type, c.bubbles, c.detail === null,
                    m.type, m.bubbles, m.clientX, m.clientY, m.button,
                    m.buttons,
                    k.type, k.bubbles, k.key === '', k.code === '', k.repeat,
                    f.type, f.bubbles, f.relatedTarget === null
                ].join('|');
            })()
        """)
        assert summary == ("c|false|true|m|false|0|0|0|0|"
                           "k|false|true|true|false|f|false|true")


@on_only
def test_type_and_bubbles_match_event():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("new CustomEvent(42).type") == "42"
        assert page.eval("new MouseEvent('x', {bubbles: true}).bubbles") is True
        assert page.eval("new KeyboardEvent('x', {bubbles: 1}).bubbles") is True
        assert page.eval("new FocusEvent('x', {bubbles: 0}).bubbles") is False
        assert page.eval("new MouseEvent('x', null).bubbles") is False
        assert page.eval("new MouseEvent('x', 5).bubbles") is False


@on_only
def test_custom_event_detail():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var d = {a: 1};
                var e = new CustomEvent('x', {detail: d});
                return e.detail === d;
            })()
        """) is True
        assert page.eval("new CustomEvent('x', {detail: 'abc'}).detail") == "abc"
        assert page.eval("new CustomEvent('x', {detail: 0}).detail") == 0
        assert page.eval(
            "new CustomEvent('x', {detail: undefined}).detail === null") is True
        assert page.eval(
            "new CustomEvent('x', {}).detail === null") is True
        assert page.eval(
            "new CustomEvent('x', {detail: null}).detail === null") is True


@on_only
def test_mouse_event_numeric_fields():
    with iv8.Page() as page:
        _setup(page)
        summary = page.eval("""
            (function () {
                var e = new MouseEvent('x', {
                    clientX: '5', clientY: 5.9, button: -1, buttons: 3
                });
                var f = new MouseEvent('x', {
                    clientX: Infinity, clientY: NaN,
                    button: undefined, buttons: null
                });
                return [e.clientX, e.clientY, e.button, e.buttons,
                        f.clientX, f.clientY, f.button, f.buttons,
                        Number.isInteger(e.clientX),
                        Number.isInteger(e.clientY)].join('|');
            })()
        """)
        assert summary == "5|5|-1|3|0|0|0|0|true|true"


@on_only
def test_keyboard_event_fields():
    with iv8.Page() as page:
        _setup(page)
        summary = page.eval("""
            (function () {
                var e = new KeyboardEvent('x', {
                    key: 65, code: 'KeyA', repeat: 1
                });
                var f = new KeyboardEvent('x', {
                    key: null, code: undefined, repeat: 0
                });
                return [e.key, e.code, e.repeat,
                        f.key, f.code === '', f.repeat].join('|');
            })()
        """)
        assert summary == "65|KeyA|true|null|true|false"


@on_only
def test_focus_event_related_target():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var el = document.getElementById('inner');
                var e = new FocusEvent('focus', {relatedTarget: el});
                return e.relatedTarget === el;
            })()
        """) is True
        assert page.eval(
            "new FocusEvent('x', {relatedTarget: 42}).relatedTarget") == 42
        assert page.eval(
            "new FocusEvent('x', {relatedTarget: undefined})"
            ".relatedTarget === null") is True
        assert page.eval(
            "new FocusEvent('x', {}).relatedTarget === null") is True


# --- field immutability -------------------------------------------------------

@on_only
def test_fields_are_read_only():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var c = new CustomEvent('x', {detail: 1});
                var m = new MouseEvent('x', {clientX: 1});
                var k = new KeyboardEvent('x', {key: 'a'});
                var f = new FocusEvent('x', {relatedTarget: 1});
                c.detail = 2;
                m.clientX = 2;
                k.key = 'b';
                f.relatedTarget = 2;
                return [c.detail, m.clientX, k.key, f.relatedTarget].join('|');
            })()
        """) == "1|1|a|1"
        assert page.eval("""
            (function () {
                'use strict';
                var c = new CustomEvent('x', {detail: 1});
                try { c.detail = 2; return 'no'; }
                catch (err) { return err.name; }
            })()
        """) == "TypeError"


# --- dispatch / bubbling ------------------------------------------------------

@on_only
def test_dispatch_custom_event_bubbles():
    with iv8.Page() as page:
        _setup(page)
        result = page.eval("""
            (function () {
                var log = [];
                var inner = document.getElementById('inner');
                var outer = document.getElementById('outer');
                inner.addEventListener('ping', function (e) {
                    log.push('inner:' + e.detail.x + ':' +
                             (e.target === inner) + ':' +
                             (e.currentTarget === inner));
                });
                outer.addEventListener('ping', function (e) {
                    // Wrapper identity is not guaranteed; assert the id.
                    log.push('outer:' + e.detail.x + ':' +
                             (e.currentTarget.id === 'outer'));
                });
                document.addEventListener('ping', function (e) {
                    log.push('doc:' + (e.currentTarget === document));
                });
                window.addEventListener('ping', function (e) {
                    log.push('win:' + (e.currentTarget === window));
                });
                var ok = inner.dispatchEvent(new CustomEvent('ping', {
                    bubbles: true, detail: {x: 7}
                }));
                return [ok, log.join(',')].join('|');
            })()
        """)
        assert result == ("true|inner:7:true:true,outer:7:true,"
                          "doc:true,win:true")


@on_only
@pytest.mark.parametrize("ctor,init,check", [
    ("MouseEvent", "{clientX: 3, clientY: 4, button: 1}", "e.clientX === 3 && e.clientY === 4 && e.button === 1"),
    ("KeyboardEvent", "{key: 'a', code: 'KeyA', repeat: true}", "e.key === 'a' && e.code === 'KeyA' && e.repeat === true"),
    ("FocusEvent", "{relatedTarget: null}", "e.relatedTarget === null"),
])
def test_dispatch_subclass_values(ctor, init, check):
    with iv8.Page() as page:
        _setup(page)
        assert page.eval(f"""
            (function () {{
                var target = document.getElementById('inner');
                var seen = false;
                target.addEventListener('t', function (e) {{
                    seen = ({check}) && e.target === target;
                }});
                var ok = target.dispatchEvent(new {ctor}('t', {init}));
                return ok && seen;
            }})()
        """) is True


@on_only
def test_default_bubbles_is_false():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var log = [];
                document.getElementById('outer').addEventListener(
                    't', function () { log.push('outer'); });
                document.getElementById('inner').dispatchEvent(
                    new KeyboardEvent('t', {key: 'a'}));
                return log.length;
            })()
        """) == 0


@on_only
def test_stop_propagation_is_inherited():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var log = [];
                var inner = document.getElementById('inner');
                var outer = document.getElementById('outer');
                inner.addEventListener('t', function (e) {
                    log.push('inner');
                    e.stopPropagation();
                });
                outer.addEventListener('t', function () { log.push('outer'); });
                inner.dispatchEvent(
                    new MouseEvent('t', {bubbles: true, clientX: 1}));
                return log.join(',');
            })()
        """) == "inner"


# --- non-goals / non-throwing edges ------------------------------------------

@on_only
def test_no_capture_phase_surface():
    with iv8.Page() as page:
        _setup(page)
        for member in ("eventPhase", "composed", "timeStamp"):
            assert page.eval(
                f"typeof new CustomEvent('x').{member}") == "undefined"
        assert page.eval("typeof new CustomEvent('x').detail") != "undefined"
        # M11-9 adds cancellation; the inherited methods exist here too.
        assert page.eval(
            "typeof new CustomEvent('x').preventDefault") == "function"


@on_only
def test_event_itself_is_unchanged():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("new Event('x').type") == "x"
        assert page.eval("new Event('x') instanceof Event") is True
        assert page.eval("typeof new Event('x').detail") == "undefined"
        assert page.eval(
            "typeof new Event('x').stopPropagation") == "function"


@on_only
def test_non_object_init_uses_defaults():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var c = new CustomEvent('x', null);
                var m = new MouseEvent('x', 5);
                var k = new KeyboardEvent('x', 's');
                var f = new FocusEvent('x', true);
                return [c.detail === null, m.clientX, k.key === '',
                        f.relatedTarget === null].join('|');
            })()
        """) == "true|0|true|true"


@on_only
@pytest.mark.parametrize("expr", [
    "new KeyboardEvent('x', {key: Symbol('k')})",
    "new MouseEvent('x', {clientX: Symbol('n')})",
])
def test_throwing_field_conversion_propagates(expr):
    with iv8.Page() as page:
        _setup(page)
        with pytest.raises(iv8.JSError) as info:
            page.eval(expr)
        assert info.value.name == "TypeError"
        assert page.eval("1 + 1") == 2


@on_only
def test_throwing_init_getter_propagates():
    with iv8.Page() as page:
        _setup(page)
        with pytest.raises(iv8.JSError) as info:
            page.eval("""
                new CustomEvent('x', {
                    get detail() { throw new Error('boom'); }
                })
            """)
        assert info.value.name == "Error"
        assert page.eval("1 + 1") == 2


# --- generation behaviour -----------------------------------------------------

@on_only
def test_load_installs_fresh_constructors():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.oldCtor = CustomEvent;
            globalThis.oldEvent = new CustomEvent('x', {detail: 1});
        """)
        page.load(DOC, BASE)
        assert page.eval("typeof CustomEvent") == "function"
        assert page.eval("typeof oldCtor") == "undefined"
        assert page.eval("typeof oldEvent") == "undefined"
        assert page.eval("new CustomEvent('y').detail === null") is True
        assert page.eval(
            "new FocusEvent('z') instanceof Event") is True
