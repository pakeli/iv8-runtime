"""M11-52: window.postMessage + MessageEvent for the single context."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-52.test/"
ORIGIN = "https://m11-52.test"
EMPTY = "<html><body></body></html>"


def test_no_new_python_surface():
    for attr in ("postMessage", "MessageEvent", "onmessage"):
        assert not hasattr(iv8.Page, attr)


@on_only
def test_api_shape_and_hidden_natives_are_removed():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            "[typeof window.postMessage, typeof MessageEvent,"
            " window.postMessage === postMessage,"
            " typeof window.__iv8PostMessageDispatch,"
            " typeof window.__iv8PostMessageRecord].join('|')"
        ) == "function|function|true|undefined|undefined"
        assert page.eval("window.postMessage.length") == 3


@on_only
def test_message_event_constructor_defaults_and_fields():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            """
            (() => {
              const plain = new MessageEvent('m');
              const ports = [window, 'x'];
              const source = {custom: 1};
              const e = new MessageEvent('message', {
                data: {a: 1}, origin: 'https://origin.test',
                lastEventId: 'id-1', source: source, ports: ports
              });
              ports[0] = null;
              return [plain.type, plain.data, plain.origin,
                      plain.lastEventId, plain.source,
                      plain.ports.length,
                      e.type, e.data.a, e.origin, e.lastEventId,
                      e.source === source, e.ports.length,
                      e.ports[0] === window, e.ports[1],
                      e instanceof MessageEvent, e instanceof Event,
                      e.bubbles, e.cancelable].join('|');
            })();
            """
        ) == "m|||||0|message|1|https://origin.test|id-1|true|2|true|x|true|true|false|false"
        with pytest.raises(iv8.JSError) as info:
            page.eval("MessageEvent('m')")
        assert info.value.name == "TypeError"


@on_only
def test_post_message_is_async_and_clones_the_message():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            """
            (() => {
              window.__msgs = [];
              window.__phase = -1;
              window.__current = false;
              window.onmessage = function (e) {
                __msgs.push(e);
                __phase = e.eventPhase;
                __current = e.currentTarget === window;
              };
              const data = {x: 1, nested: {y: 2}};
              const result = window.postMessage(data, '*');
              data.x = 3;
              data.nested.y = 4;
              return [String(result), __msgs.length].join('|');
            })();
            """
        ) == "undefined|0"
        page.run_timers()
        assert page.eval(
            """
            (() => {
              const e = __msgs[0];
              return [__msgs.length, e instanceof MessageEvent,
                      e instanceof Event, e.type, e.data.x,
                      e.data.nested.y, e.origin, e.source === window,
                      e.lastEventId, e.ports.length, __phase,
                      __current].join('|');
            })();
            """
        ) == f"1|true|true|message|1|2|{ORIGIN}|true||0|2|true"


@on_only
def test_target_origin_matrix():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            f"""
            (() => {{
              window.__got = [];
              window.onmessage = function (e) {{ __got.push(e.data); }};
              postMessage('omitted');
              postMessage('star', '*');
              postMessage('exact', '{ORIGIN}');
              postMessage('slash', '{ORIGIN}/');
              postMessage('other', 'https://other.test');
              postMessage('empty', '');
              return __got.length;
            }})();
            """
        ) == 0
        page.run_timers()
        assert page.eval("__got.join(',')") == \
            "omitted,star,exact,slash"


@on_only
def test_post_message_runs_listeners_then_onmessage_and_is_fifo():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            """
            (() => {
              window.__order = [];
              window.addEventListener('message', function (e) {
                __order.push('listener:' + e.data + ':' + e.eventPhase);
              });
              window.onmessage = function (e) {
                __order.push('handler:' + e.data + ':' + e.eventPhase);
              };
              postMessage('a', '*');
              postMessage('b', '*');
              postMessage('c', '*');
              return __order.length;
            })();
            """
        ) == 0
        page.run_timers()
        assert page.eval("__order.join('|')") == \
            ("listener:a:2|handler:a:2|"
             "listener:b:2|handler:b:2|"
             "listener:c:2|handler:c:2")


@on_only
def test_transfer_and_argument_validation():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            "(() => { window.__n = 0;"
            " window.onmessage = () => { __n += 1; };"
            " postMessage('ok', '*', []); return __n; })()"
        ) == 0
        page.run_timers()
        assert page.eval("__n") == 1
        for js in ("postMessage('x', '*', [{}])",
                   "postMessage('x', '*', {})",
                   "postMessage()"):
            with pytest.raises(iv8.JSError) as info:
                page.eval(js)
            assert info.value.name == "TypeError"


@on_only
def test_uncloneable_messages_throw_type_error_and_do_not_deliver():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        page.eval("window.__n = 0; window.onmessage = () => { __n += 1; }")
        for js in ("postMessage(function () {}, '*')",
                   "postMessage(Symbol('x'), '*')",
                   "postMessage(window, '*')"):
            with pytest.raises(iv8.JSError) as info:
                page.eval(js)
            assert info.value.name == "TypeError"
        page.run_timers()
        assert page.eval("__n") == 0


@on_only
def test_captured_originals_survive_page_overrides():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            """
            (() => {
              window.__got = [];
              window.onmessage = function (e) { __got.push(e.data); };
              window.structuredClone = function () {
                throw new Error('overridden clone');
              };
              window.MessageEvent = function () {
                throw new Error('overridden event');
              };
              window.setTimeout = function () {
                throw new Error('overridden timer');
              };
              window.postMessage({a: 1}, '*');
              return __got.length;
            })();
            """
        ) == 0
        page.run_timers()
        assert page.eval("__got.length + ':' + __got[0].a") == "1:1"


@on_only
def test_post_message_is_watchable():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        page.watch_apis(["window.postMessage"])
        page.eval("postMessage('x', '*')")
        hits = page.read_watch_api_hits()
        assert [hit["path"] for hit in hits] == ["window.postMessage"]
