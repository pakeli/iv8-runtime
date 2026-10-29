"""M11-29 acceptance tests: deterministic event-loop facade (timers + microtasks)."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://event-loop.test/"


def _load(page):
    page.load("<html><body></body></html>", BASE)
    page.eval("globalThis.log = [];")


# --- shape guards ---------------------------------------------------------------

def test_surface_shape():
    for name in ("tick", "advance", "run_until_idle"):
        assert callable(getattr(iv8.Page, name, None))
    assert hasattr(iv8.Page, "event_loop_time_ms")
    for name in ("EventLoop", "eventLoop", "loop"):
        assert not hasattr(iv8, name)


# --- clock and timer ordering ---------------------------------------------------

@on_only
def test_initial_clock_and_advance_window():
    with iv8.Page() as page:
        _load(page)
        assert page.event_loop_time_ms == 0
        page.eval("""
            setTimeout(() => log.push('a'), 10);
            setTimeout(() => log.push('b'), 0);
            setTimeout(() => log.push('c'), 5);
        """)
        assert page.advance(0) is True
        assert page.eval("log.join(',')") == "b"
        assert page.event_loop_time_ms == 0
        assert page.advance(4) is True
        assert page.eval("log.join(',')") == "b"
        assert page.event_loop_time_ms == 4
        assert page.advance(1) is True
        assert page.eval("log.join(',')") == "b,c"
        assert page.advance(5) is True
        assert page.eval("log.join(',')") == "b,c,a"
        assert page.event_loop_time_ms == 10


@on_only
def test_same_due_registration_order():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            setTimeout(() => log.push('first'), 10);
            setTimeout(() => log.push('second'), 10);
        """)
        assert page.advance(10) is True
        assert page.eval("log.join(',')") == "first,second"


@on_only
def test_microtask_checkpoint_before_and_after_task():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            queueMicrotask(() => log.push('pre'));
            setTimeout(() => {
                log.push('timer');
                Promise.resolve().then(() => log.push('promise'));
                queueMicrotask(() => log.push('queued'));
            }, 0);
        """)
        assert page.advance(0) is True
        assert page.eval("log.join(',')") == "pre,timer,promise,queued"


@on_only
def test_nested_timer_inside_and_outside_window():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            setTimeout(() => {
                log.push('outer');
                setTimeout(() => log.push('inner'), 5);
            }, 10);
        """)
        assert page.advance(10) is True
        assert page.eval("log.join(',')") == "outer"
        assert page.advance(4) is True
        assert page.eval("log.join(',')") == "outer"
        assert page.advance(1) is True
        assert page.eval("log.join(',')") == "outer,inner"
        assert page.event_loop_time_ms == 15


@on_only
def test_advance_without_work_still_moves_clock():
    with iv8.Page() as page:
        _load(page)
        assert page.advance(123) is True
        assert page.event_loop_time_ms == 123


# --- intervals and caps ---------------------------------------------------------

@on_only
def test_interval_runs_multiple_times_in_window():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            globalThis.ticks = 0;
            setInterval(() => { ticks++; }, 10);
        """)
        assert page.advance(30) is True
        assert page.eval("ticks") == 3
        assert page.event_loop_time_ms == 30
        assert page.advance(10) is True
        assert page.eval("ticks") == 4


@on_only
def test_zero_delay_interval_has_virtual_progress():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            globalThis.ticks = 0;
            setInterval(() => { ticks++; }, 0);
        """)
        assert page.advance(0) is True
        assert page.eval("ticks") == 1
        assert page.event_loop_time_ms == 0
        assert page.advance(3) is True
        assert page.eval("ticks") == 4
        assert page.event_loop_time_ms == 3


@on_only
def test_run_until_idle_advances_to_future_timers():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            setTimeout(() => {
                log.push('a');
                setTimeout(() => log.push('b'), 5);
            }, 10);
            setTimeout(() => log.push('c'), 20);
        """)
        assert page.run_until_idle() is True
        assert page.eval("log.join(',')") == "a,b,c"
        assert page.event_loop_time_ms == 20


@on_only
def test_run_until_idle_interval_is_bounded():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            globalThis.ticks = 0;
            setInterval(() => { ticks++; }, 10);
        """)
        assert page.run_until_idle(max_steps=3) is False
        assert page.eval("ticks") == 3
        assert page.event_loop_time_ms == 30
        assert page.run_until_idle(max_steps=2) is False
        assert page.eval("ticks") == 5
        assert page.event_loop_time_ms == 50


@on_only
def test_advance_max_steps_caps_self_rescheduling_timeout():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            globalThis.ticks = 0;
            function again() {
                ticks++;
                setTimeout(again, 0);
            }
            setTimeout(again, 0);
        """)
        assert page.advance(0, max_steps=5) is False
        assert page.eval("ticks") == 5
        assert page.event_loop_time_ms == 0


# --- tick -----------------------------------------------------------------------

@on_only
def test_tick_runs_one_current_timer_and_never_advances_time():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            setTimeout(() => {
                log.push('now');
                Promise.resolve().then(() => log.push('micro'));
            }, 0);
            setTimeout(() => log.push('later'), 10);
        """)
        assert page.tick() is True
        assert page.eval("log.join(',')") == "now,micro"
        assert page.event_loop_time_ms == 0
        assert page.tick() is False
        assert page.advance(10) is True
        assert page.eval("log.join(',')") == "now,micro,later"


@on_only
def test_tick_with_no_work_is_false():
    with iv8.Page() as page:
        _load(page)
        assert page.tick() is False


# --- cancellation / errors ------------------------------------------------------

@on_only
def test_cancel_timeout_and_interval():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            const id = setTimeout(() => log.push('cancelled'), 0);
            clearTimeout(id);
            globalThis.ticks = 0;
            const interval = setInterval(() => {
                ticks++;
                if (ticks === 1) clearInterval(interval);
            }, 1);
        """)
        assert page.run_until_idle() is True
        assert page.eval("log.join(',')") == ""
        assert page.eval("ticks") == 1


@on_only
def test_throwing_timer_does_not_stop_later_work():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            setTimeout(() => { throw new Error('boom'); }, 0);
            setTimeout(() => log.push('after'), 0);
        """)
        assert page.advance(0) is True
        assert page.eval("log.join(',')") == "after"
        assert page.eval("1 + 1") == 2


# --- generation / legacy / rAF isolation ---------------------------------------

@on_only
def test_load_resets_clock_and_pending_timers():
    with iv8.Page() as page:
        _load(page)
        page.eval("setTimeout(() => log.push('old'), 100);")
        assert page.advance(50) is True
        assert page.event_loop_time_ms == 50
        page.load("<html><body></body></html>", BASE)
        assert page.event_loop_time_ms == 0
        page.eval("globalThis.log = [];")
        assert page.run_until_idle() is True
        assert page.eval("log.length") == 0
        page.eval("setTimeout(() => log.push('new'), 1);")
        assert page.advance(1) is True
        assert page.eval("log.join(',')") == "new"


@on_only
def test_legacy_pumps_stay_independent_of_virtual_time():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            setTimeout(() => {
                log.push('timer');
                Promise.resolve().then(() => log.push('micro'));
            }, 1000);
        """)
        page.run_timers()
        assert page.eval("log.join(',')") == "timer"
        assert page.event_loop_time_ms == 0
        page.run_jobs()
        assert page.eval("log.join(',')") == "timer,micro"


@on_only
def test_raf_and_idle_remain_separate():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            globalThis.raf = 0;
            globalThis.idle = 0;
            requestAnimationFrame(() => raf++);
            requestIdleCallback(() => idle++);
        """)
        assert page.advance(1000) is True
        assert page.eval("raf") == 0
        assert page.eval("idle") == 0
        page.run_animation_frame()
        page.run_idle_callbacks()
        assert page.eval("raf") == 1
        assert page.eval("idle") == 1


# --- validation / disposal ------------------------------------------------------

@on_only
def test_argument_validation():
    with iv8.Page() as page:
        _load(page)
        for bad in (-1, True, False, 1.5, "1", None):
            with pytest.raises(TypeError):
                page.advance(bad)
        for bad in (0, -1, True, False, 1.5, "1", None):
            with pytest.raises(TypeError):
                page.advance(0, max_steps=bad)
            with pytest.raises(TypeError):
                page.run_until_idle(max_steps=bad)


@on_only
def test_disposed_page_uses_the_existing_error_path():
    page = iv8.Page()
    _load(page)
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.tick()
    with pytest.raises(iv8.JSContextDisposedError):
        page.advance(1)
    with pytest.raises(iv8.JSContextDisposedError):
        page.run_until_idle()
    with pytest.raises(iv8.JSContextDisposedError):
        page.event_loop_time_ms


@on_only
def test_no_new_js_globals():
    # The facade is Python-side only; no JS global is installed.
    with iv8.Page() as page:
        for name in ("tick", "advance", "runUntilIdle", "eventLoopTime"):
            assert page.eval(f"typeof globalThis.{name}") == "undefined"
