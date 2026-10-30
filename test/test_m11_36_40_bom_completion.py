"""M11-36..M11-40 acceptance tests: BOM completion batch."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-36.test/"


def _load(page):
    page.load("<html><body></body></html>", BASE)


def test_no_new_python_surface():
    for name in ("alert", "confirm", "prompt", "print", "structuredClone",
                 "scheduler", "Performance", "PerformanceObserver"):
        assert not hasattr(iv8, name)
    for name in ("alert", "confirm", "prompt", "structuredClone", "scheduler"):
        assert not hasattr(iv8.Page, name)


# --- M11-36 dialogs -------------------------------------------------------------

@on_only
def test_dialog_globals_are_deterministic_stubs():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("typeof alert") == "function"
        assert page.eval("typeof confirm") == "function"
        assert page.eval("typeof prompt") == "function"
        assert page.eval("typeof print") == "function"
        assert page.eval("alert('hello') === undefined") is True
        assert page.eval("confirm('sure?')") is False
        assert page.eval("prompt('value?', 'x')") is None
        assert page.eval("print() === undefined") is True
        assert page.eval("1 + 1") == 2


@on_only
def test_dialog_calls_are_watchable():
    with iv8.Page() as page:
        _load(page)
        page.watch_apis(["window.alert", "window.confirm", "window.prompt",
                         "window.print"])
        page.eval("alert('a'); confirm('b'); prompt('c'); print();")
        hits = page.read_watch_api_hits()
        assert [hit["path"] for hit in hits] == [
            "window.alert", "window.confirm", "window.prompt", "window.print"]


# --- M11-37 performance marks / measures ---------------------------------------

@on_only
def test_performance_mark_and_measure_use_virtual_time():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            "performance.mark('a').entryType") == "mark"
        assert page.advance(10) is True
        assert page.eval(
            "performance.mark('b').startTime") == 10
        assert page.eval(
            "performance.measure('m', 'a', 'b').duration") == 10
        assert page.eval(
            "performance.measure('m', 'a', 'b').startTime") == 0
        assert page.eval(
            "performance.measure('open').duration") == 10


@on_only
def test_performance_entries_queries_and_clears():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            performance.mark('a');
            performance.mark('b');
            performance.measure('m', 'a', 'b');
        """)
        assert page.eval(
            "performance.getEntries().map(e => e.name).join(',')"
        ) == "a,b,m"
        assert page.eval(
            "performance.getEntriesByType('mark').map(e => e.name).join(',')"
        ) == "a,b"
        assert page.eval(
            "performance.getEntriesByType('measure').map(e => e.name).join(',')"
        ) == "m"
        assert page.eval(
            "performance.getEntriesByName('b').length") == 1
        page.eval("performance.clearMarks('a')")
        assert page.eval(
            "performance.getEntriesByType('mark').map(e => e.name).join(',')"
        ) == "b"
        page.eval("performance.clearMarks()")
        assert page.eval("performance.getEntriesByType('mark').length") == 0
        assert page.eval("performance.getEntriesByType('measure').length") == 1
        page.eval("performance.clearMeasures()")
        assert page.eval("performance.getEntries().length") == 0


@on_only
def test_performance_measure_unknown_mark_and_reset_on_load():
    with iv8.Page() as page:
        _load(page)
        with pytest.raises(iv8.JSError):
            page.eval("performance.measure('m', 'missing')")
        page.eval("performance.mark('a')")
        assert page.eval("performance.getEntries().length") == 1
        page.load("<html><body></body></html>", BASE)
        assert page.eval("performance.getEntries().length") == 0
        assert page.eval("performance.now()") == 0


@on_only
def test_performance_methods_are_read_only_and_watchable():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            "performance.mark = 1; typeof performance.mark") == "function"
        page.watch_apis(["performance.mark", "performance.measure"])
        page.eval("performance.mark('x'); performance.measure('y');")
        hits = page.read_watch_api_hits()
        assert [hit["path"] for hit in hits] == [
            "performance.mark", "performance.measure"]


# --- M11-38 structuredClone ----------------------------------------------------

@on_only
def test_structured_clone_primitives_objects_arrays_and_identity():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              const o = {a: 1, nested: {b: [2, 3]}};
              const c = structuredClone(o);
              c.a = 9;
              c.nested.b.push(4);
              return [o.a, o.nested.b.join(','),
                      c.a, c.nested.b.join(','),
                      typeof structuredClone(undefined),
                      structuredClone(null) === null].join('|');
            })();
            """
        ) == "1|2,3|9|2,3,4|undefined|true"


@on_only
def test_structured_clone_preserves_cycles_and_shared_references():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              const o = {x: 1};
              const a = {left: o, right: o};
              a.self = a;
              const c = structuredClone(a);
              return [c.self === c, c.left === c.right,
                      c.left.x].join(',');
            })();
            """
        ) == "true,true,1"


@on_only
def test_structured_clone_date_map_set_and_buffers():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              const d = new Date(123456);
              const dc = structuredClone(d);
              const m = new Map([['a', {x: 1}]]);
              const mc = structuredClone(m);
              const s = new Set([1, 2, 3]);
              const sc = structuredClone(s);
              const buf = new Uint8Array([1, 2, 3]).buffer;
              const bc = structuredClone(buf);
              const typed = new Uint8Array([4, 5, 6]);
              const tc = structuredClone(typed);
              return [dc instanceof Date, dc.getTime() === d.getTime(),
                      dc !== d,
                      mc !== m, mc.get('a').x, mc.get('a') !== m.get('a'),
                      sc !== s, sc.has(2),
                      bc !== buf, new Uint8Array(bc)[0],
                      tc !== typed, tc instanceof Uint8Array,
                      Array.from(tc).join(',')].join(',');
            })();
            """
        ) == ("true,true,true,true,1,true,true,true,true,1,true,true,4,5,6")


@on_only
def test_structured_clone_views_share_the_cloned_buffer_and_keep_offsets():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              const buf = new ArrayBuffer(8);
              const view = new Uint8Array(buf, 1, 3);
              view.set([4, 5, 6]);
              const copy = structuredClone({buf, view,
                                            view2: new Uint8Array(buf, 2, 2)});
              return [copy.view.buffer === copy.buf,
                      copy.view2.buffer === copy.buf,
                      copy.view.byteOffset, copy.view.length,
                      Array.from(copy.view).join(','),
                      Array.from(copy.view2).join(','),
                      new Uint8Array(buf)[1]].join('|');
            })();
            """
        ) == "true|true|1|3|4,5,6|5,6|4"


@on_only
def test_structured_clone_boxed_primitives():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              const n = new Number(5);
              const b = new Boolean(true);
              const s = new String('x');
              const big = Object(12345678901234567890n);
              const c = structuredClone({n, b, s, big, again: n});
              return [c.n instanceof Number, c.n.valueOf(),
                      c.b instanceof Boolean, c.b.valueOf(),
                      c.s instanceof String, c.s.valueOf(),
                      c.big instanceof Object, c.big.valueOf() === 12345678901234567890n,
                      c.n !== n, c.again === c.n].join('|');
            })();
            """
        ) == "true|5|true|true|true|x|true|true|true|true"


@on_only
def test_structured_clone_rejects_unsupported_and_host_values():
    with iv8.Page() as page:
        _load(page)
        bad = (
            "structuredClone(function () {})",
            "structuredClone(Symbol('x'))",
            "structuredClone(Object(Symbol('x')))",
            "structuredClone(Promise.resolve())",
            "structuredClone(document.body)",
            "structuredClone(window)",
            "structuredClone(new WeakMap())",
        )
        for expression in bad:
            with pytest.raises(iv8.JSError) as info:
                page.eval(expression)
            assert info.value.name == "TypeError", expression


# --- M11-39 window browsing-context metadata -----------------------------------

@on_only
def test_window_has_a_single_top_level_context():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("window.parent === window") is True
        assert page.eval("window.top === window") is True
        assert page.eval("window.frames === window") is True
        assert page.eval("window.frames.length") == 0
        assert page.eval("window.opener === null") is True
        # parent/frames are [Replaceable]; opener is writable; top is
        # [LegacyUnforgeable] and ignores assignment.
        assert page.eval("(window.parent = null, window.parent === null)") is True
        assert page.eval("(window.top = null, window.top === window)") is True
        assert page.eval("(window.frames = {}, window.frames !== window)") is True
        assert page.eval("(window.opener = {}, window.opener !== null)") is True


@on_only
def test_window_context_globals_allow_script_declarations():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("var parent = 1; parent") == 1
        assert page.eval("let frames = 2; frames") == 2
        # top is unforgeable, so a lexical declaration remains illegal.
        with pytest.raises(iv8.JSError):
            page.eval("let top = 3;")


# --- M11-40 scheduler.postTask -------------------------------------------------

@on_only
def test_scheduler_surface_and_task_result():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("typeof scheduler") == "object"
        assert page.eval("typeof scheduler.postTask") == "function"
        assert page.eval(
            "scheduler.postTask(function () { return 42; }) instanceof Promise"
        ) is True
        page.eval("""
            globalThis.result = null;
            scheduler.postTask(() => 42).then(v => { result = v; });
        """)
        page.run_timers()
        page.run_jobs()
        assert page.eval("result") == 42


@on_only
def test_scheduler_delay_and_rejection():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            globalThis.delayed = null;
            globalThis.failure = null;
            scheduler.postTask(() => 7, {delay: 10}).then(v => { delayed = v; });
            scheduler.postTask(() => { throw new Error('boom'); })
                .catch(e => { failure = e.message; });
        """)
        assert page.advance(10) is True
        assert page.eval("delayed") == 7
        page.run_timers()
        page.run_jobs()
        assert page.eval("failure") == "boom"


@on_only
def test_scheduler_rejects_invalid_arguments():
    with iv8.Page() as page:
        _load(page)
        with pytest.raises(iv8.JSError) as info:
            page.eval("scheduler.postTask(1)")
        assert info.value.name == "TypeError"
        with pytest.raises(iv8.JSError) as info:
            page.eval("scheduler.postTask(() => 1, {delay: -1})")
        assert info.value.name == "TypeError"
        assert page.eval("typeof scheduler.postTask") == "function"
