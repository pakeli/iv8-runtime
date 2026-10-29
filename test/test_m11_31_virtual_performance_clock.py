"""M11-31 acceptance tests: virtual performance clock and rAF timestamps."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-31.test/"


def _load(page):
    page.load("<html><body></body></html>", BASE)
    page.eval("globalThis.log = [];")


# --- shape guards ---------------------------------------------------------------

def test_no_new_python_surface():
    for name in ("Performance", "performance", "PerformanceTiming"):
        assert not hasattr(iv8, name)
    assert not hasattr(iv8.Page, "performance")


@on_only
def test_surface_shape_and_virtual_epoch():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("typeof performance") == "object"
        assert page.eval("typeof performance.now") == "function"
        assert page.eval("performance.timeOrigin") == 0
        assert page.eval("performance.now()") == 0
        assert page.eval("window.performance === performance") is True
        assert page.eval("performance.now()") == page.event_loop_time_ms


@on_only
def test_performance_and_time_origin_are_read_only():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              performance.now = 1;
              performance.timeOrigin = 99;
              performance = null;
              return [typeof performance.now,
                      performance.timeOrigin,
                      typeof performance].join(',');
            })();
            """
        ) == "function,0,object"


# --- clock progression ----------------------------------------------------------

@on_only
def test_advance_moves_now_and_tick_does_not():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("performance.now()") == 0
        assert page.advance(123) is True
        assert page.eval("performance.now()") == 123
        assert page.event_loop_time_ms == 123
        assert page.tick() is False
        assert page.eval("performance.now()") == 123


@on_only
def test_timer_callback_sees_its_due_time():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            globalThis.at = -1;
            setTimeout(() => { at = performance.now(); }, 25);
        """)
        assert page.advance(25) is True
        assert page.eval("at") == 25
        assert page.eval("performance.now()") == 25


@on_only
def test_microtasks_see_the_same_virtual_time():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            globalThis.at = -1;
            setTimeout(() => {
                Promise.resolve().then(() => { at = performance.now(); });
            }, 40);
        """)
        assert page.advance(40) is True
        assert page.eval("at") == 40


@on_only
def test_run_until_idle_leaves_now_at_the_last_due_time():
    with iv8.Page() as page:
        _load(page)
        page.eval("setTimeout(() => {}, 30);")
        assert page.run_until_idle() is True
        assert page.eval("performance.now()") == 30
        assert page.event_loop_time_ms == 30


# --- rAF timestamps -------------------------------------------------------------

@on_only
def test_facade_raf_receives_the_frame_timestamp():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            globalThis.stamp = -1;
            globalThis.inside = -1;
            requestAnimationFrame(t => {
                stamp = t;
                inside = performance.now();
            });
            setTimeout(() => log.push('timer'), 16);
        """)
        assert page.advance(16) is True
        assert page.eval("stamp") == 16
        assert page.eval("inside") == 16
        assert page.eval("log.join(',')") == "timer"


@on_only
def test_all_callbacks_in_one_frame_share_the_timestamp():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            globalThis.stamps = [];
            requestAnimationFrame(t => stamps.push(t));
            requestAnimationFrame(t => stamps.push(t));
        """)
        assert page.advance(16) is True
        assert page.eval("stamps.join(',')") == "16,16"


@on_only
def test_legacy_pump_passes_the_unchanged_clock():
    with iv8.Page() as page:
        _load(page)
        assert page.advance(30) is True
        page.eval("""
            globalThis.stamp = -1;
            requestAnimationFrame(t => { stamp = t; });
        """)
        page.run_animation_frame()
        assert page.eval("stamp") == 30
        assert page.event_loop_time_ms == 30


@on_only
def test_legacy_timer_pump_does_not_move_the_clock():
    with iv8.Page() as page:
        _load(page)
        page.eval("setTimeout(() => {}, 1000);")
        page.run_timers()
        assert page.eval("performance.now()") == 0
        assert page.event_loop_time_ms == 0


@on_only
def test_load_resets_the_virtual_clock():
    with iv8.Page() as page:
        _load(page)
        assert page.advance(50) is True
        assert page.eval("performance.now()") == 50
        page.load("<html><body></body></html>", BASE)
        assert page.eval("performance.now()") == 0
        assert page.eval("performance.timeOrigin") == 0


# --- monitoring / non-goals -----------------------------------------------------

@on_only
def test_watch_apis_records_performance_now():
    with iv8.Page() as page:
        _load(page)
        page.watch_apis(["performance.now"])
        page.eval("performance.now()")
        hits = page.read_watch_api_hits()
        assert len(hits) == 1
        assert hits[0]["path"] == "performance.now"
        assert hits[0]["type"] == "call"


@on_only
def test_performance_api_beyond_now_is_absent():
    with iv8.Page() as page:
        _load(page)
        for name in ("mark", "measure", "getEntries", "timing", "navigation"):
            assert page.eval(f"typeof performance.{name}") == "undefined", name
        # Date is still V8's real clock, not the virtual facade clock.
        assert page.eval("typeof Date.now()") == "number"
