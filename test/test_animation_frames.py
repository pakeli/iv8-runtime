"""M11-2 acceptance tests: requestAnimationFrame / cancelAnimationFrame.

A minimal MANUAL frame-pump model. `requestAnimationFrame(callback)` queues a
callback and returns a handle; `cancelAnimationFrame(handle)` drops a queued one; the
Python-side `Page.run_animation_frame()` is the ONLY trigger — there is no real clock,
no background thread, and no automatic event loop.

One call runs exactly one frame: the callbacks queued when the call begins, in
registration order. A callback that re-requests a frame lands in the NEXT frame, so a
self-rescheduling callback advances one frame per call rather than spinning. The RAF
queue is independent of the M2-4 timers and of the microtask queue.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://raf.test/"


# --- API-shape guards (both build modes) -----------------------------------------

def test_surface_shape():
    assert callable(getattr(iv8.Page, "run_animation_frame", None))
    # the existing manual pumps are untouched
    assert callable(getattr(iv8.Page, "run_timers", None))
    assert callable(getattr(iv8.Page, "run_jobs", None))


def test_no_new_module_level_surface():
    for forbidden in ("requestAnimationFrame", "cancelAnimationFrame",
                      "request_animation_frame", "run_animation_frame"):
        assert not hasattr(iv8, forbidden)


def test_non_goals_absent_from_page():
    # M11-2 adds ONE pump. M11-29 adds the timer/microtask facade; rAF is still
    # not driven by it. No real clock, auto loop, or frame-timing surface.
    for forbidden in ("run_event_loop", "start", "set_frame_rate",
                      "run_frames", "now"):
        assert not hasattr(iv8.Page, forbidden)
    assert callable(getattr(iv8.Page, "tick", None))
    assert callable(getattr(iv8.Page, "run_until_idle", None))


# --- JS globals exist -------------------------------------------------------------

@on_only
def test_globals_exist():
    with iv8.Page() as page:
        assert page.eval("typeof requestAnimationFrame") == "function"
        assert page.eval("typeof cancelAnimationFrame") == "function"
        # window is the global object, so both are reachable through it.
        assert page.eval("window.requestAnimationFrame === requestAnimationFrame") \
            is True


@on_only
def test_request_returns_a_numeric_handle():
    with iv8.Page() as page:
        assert page.eval("typeof requestAnimationFrame(function () {})") == "number"
        # Handles are distinct.
        assert page.eval(
            "requestAnimationFrame(function(){}) !== "
            "requestAnimationFrame(function(){})") is True


# --- nothing runs without a pump --------------------------------------------------

@on_only
def test_callback_does_not_run_until_pumped():
    with iv8.Page() as page:
        page.eval("globalThis.ran = 0; requestAnimationFrame(function () { ran++ })")
        # No clock and no background thread: repeated evals must not advance it.
        assert page.eval("ran") == 0
        page.eval("1 + 1")
        assert page.eval("ran") == 0
        page.run_animation_frame()
        assert page.eval("ran") == 1


@on_only
def test_pump_with_empty_queue_is_a_noop():
    with iv8.Page() as page:
        page.run_animation_frame()
        page.run_animation_frame()
        assert page.eval("1 + 1") == 2


@on_only
def test_callback_runs_at_most_once_per_registration():
    with iv8.Page() as page:
        page.eval("globalThis.ran = 0; requestAnimationFrame(function () { ran++ })")
        page.run_animation_frame()
        assert page.eval("ran") == 1
        # A RAF callback never repeats: later frames must not re-run it.
        page.run_animation_frame()
        page.run_animation_frame()
        assert page.eval("ran") == 1


# --- registration order -----------------------------------------------------------

@on_only
def test_callbacks_run_in_registration_order():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestAnimationFrame(function () { log.push('a') });
            requestAnimationFrame(function () { log.push('b') });
            requestAnimationFrame(function () { log.push('c') });
        """)
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "a,b,c"


@on_only
def test_all_queued_callbacks_run_in_one_frame():
    with iv8.Page() as page:
        page.eval("""
            globalThis.count = 0;
            for (let i = 0; i < 25; i++) {
                requestAnimationFrame(function () { count++ });
            }
        """)
        page.run_animation_frame()
        assert page.eval("count") == 25


# --- frame boundary: a re-request lands in the NEXT frame -------------------------

@on_only
def test_rescheduled_callback_runs_in_the_next_frame():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestAnimationFrame(function first() {
                log.push('first');
                requestAnimationFrame(function second() { log.push('second') });
            });
        """)
        page.run_animation_frame()
        # 'second' was queued DURING the frame, so it must not have run yet.
        assert page.eval("log.join(',')") == "first"
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "first,second"


@on_only
def test_self_rescheduling_callback_advances_one_frame_per_pump():
    # The bounded-pump guarantee: an unconditionally self-rescheduling callback must
    # not spin forever inside a single run_animation_frame().
    with iv8.Page() as page:
        page.eval("""
            globalThis.ticks = 0;
            function tick() { ticks++; requestAnimationFrame(tick); }
            requestAnimationFrame(tick);
        """)
        for expected in (1, 2, 3, 4):
            page.run_animation_frame()
            assert page.eval("ticks") == expected


@on_only
def test_nested_requests_do_not_run_in_the_same_frame():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestAnimationFrame(function () {
                log.push('a');
                requestAnimationFrame(function () { log.push('a-child') });
            });
            requestAnimationFrame(function () {
                log.push('b');
                requestAnimationFrame(function () { log.push('b-child') });
            });
        """)
        page.run_animation_frame()
        # Both frame-1 callbacks ran; neither child did.
        assert page.eval("log.join(',')") == "a,b"
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "a,b,a-child,b-child"


# --- cancelAnimationFrame ---------------------------------------------------------

@on_only
def test_cancel_prevents_the_callback():
    with iv8.Page() as page:
        page.eval("""
            globalThis.ran = 0;
            const handle = requestAnimationFrame(function () { ran++ });
            cancelAnimationFrame(handle);
        """)
        page.run_animation_frame()
        assert page.eval("ran") == 0


@on_only
def test_cancel_only_the_named_handle():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestAnimationFrame(function () { log.push('a') });
            const h = requestAnimationFrame(function () { log.push('b') });
            requestAnimationFrame(function () { log.push('c') });
            cancelAnimationFrame(h);
        """)
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "a,c"


@on_only
def test_cancel_mid_frame_prevents_a_later_callback_in_the_same_frame():
    # A callback earlier in the frame cancels a later one that has not run yet.
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            globalThis.second = 0;
            requestAnimationFrame(function () {
                log.push('first');
                cancelAnimationFrame(second);
            });
            globalThis.second = requestAnimationFrame(function () {
                log.push('second');
            });
        """)
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "first"


@on_only
def test_cancel_of_unknown_or_stale_handle_is_a_noop():
    with iv8.Page() as page:
        # unknown / never-issued / already-run / double-cancel: none may throw.
        page.eval("cancelAnimationFrame(99999)")
        page.eval("cancelAnimationFrame(0)")
        page.eval("cancelAnimationFrame(-1)")
        page.eval("""
            globalThis.ran = 0;
            globalThis.h = requestAnimationFrame(function () { ran++ });
        """)
        page.run_animation_frame()
        assert page.eval("ran") == 1
        page.eval("cancelAnimationFrame(h)")  # already ran
        page.eval("cancelAnimationFrame(h)")  # again
        page.run_animation_frame()
        assert page.eval("ran") == 1


@on_only
def test_cancel_then_request_again():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            const h = requestAnimationFrame(function () { log.push('cancelled') });
            cancelAnimationFrame(h);
            requestAnimationFrame(function () { log.push('kept') });
        """)
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "kept"


# --- independence from timers and microtasks -------------------------------------

@on_only
def test_frame_pump_does_not_fire_timers():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            setTimeout(function () { log.push('timeout') }, 0);
            requestAnimationFrame(function () { log.push('frame') });
        """)
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "frame"
        page.run_timers()
        assert page.eval("log.join(',')") == "frame,timeout"


@on_only
def test_timer_pump_does_not_run_frame_callbacks():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestAnimationFrame(function () { log.push('frame') });
            setTimeout(function () { log.push('timeout') }, 0);
        """)
        page.run_timers()
        assert page.eval("log.join(',')") == "timeout"
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "timeout,frame"


@on_only
def test_cancel_animation_frame_does_not_cancel_a_timer():
    # Separate handle spaces: a timer id passed to cancelAnimationFrame must not
    # cancel that timer, and vice versa.
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            globalThis.t = setTimeout(function () { log.push('timeout') }, 0);
            cancelAnimationFrame(t);
        """)
        page.run_timers()
        assert page.eval("log.join(',')") == "timeout"


@on_only
def test_clear_timeout_does_not_cancel_a_frame_callback():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            globalThis.h = requestAnimationFrame(function () { log.push('frame') });
            clearTimeout(h);
        """)
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "frame"


@on_only
def test_frame_pump_does_not_drain_microtasks():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            Promise.resolve().then(function () { log.push('job') });
            requestAnimationFrame(function () { log.push('frame') });
        """)
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "frame"
        page.run_jobs()
        assert page.eval("log.join(',')") == "frame,job"


# --- error handling ---------------------------------------------------------------

@on_only
def test_throwing_callback_is_swallowed_and_page_stays_usable():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestAnimationFrame(function () { throw new Error('boom') });
            requestAnimationFrame(function () { log.push('after') });
        """)
        page.run_animation_frame()  # must not raise
        # A throwing callback does not stop the rest of the frame.
        assert page.eval("log.join(',')") == "after"
        assert page.eval("1 + 1") == 2


@on_only
def test_non_function_argument_is_ignored():
    with iv8.Page() as page:
        for bad in ("undefined", "null", "42", "'str'", "{}", "[]"):
            assert page.eval(f"typeof requestAnimationFrame({bad})") == "undefined"
        page.run_animation_frame()  # nothing queued, no error
        assert page.eval("1 + 1") == 2


@on_only
def test_no_argument_call_is_ignored():
    with iv8.Page() as page:
        assert page.eval("typeof requestAnimationFrame()") == "undefined"
        page.eval("cancelAnimationFrame()")
        page.run_animation_frame()
        assert page.eval("1 + 1") == 2


@on_only
def test_callback_receives_no_timestamp_argument():
    # This runtime has no clock, so the minimal model passes no argument.
    with iv8.Page() as page:
        page.eval("""
            globalThis.argc = -1;
            requestAnimationFrame(function () { argc = arguments.length });
        """)
        page.run_animation_frame()
        assert page.eval("argc") == 0


# --- load() interaction -----------------------------------------------------------

@on_only
def test_globals_present_in_a_loaded_generation():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", BASE)
        assert page.eval("typeof requestAnimationFrame") == "function"
        assert page.eval("typeof cancelAnimationFrame") == "function"


@on_only
def test_load_discards_the_previous_generation_queue():
    with iv8.Page() as page:
        page.eval("globalThis.ran = 0; requestAnimationFrame(function () { ran++ })")
        page.load("<html><body></body></html>", BASE)
        # The generation (and its queue) is gone; pumping must be safe and must not
        # resurrect the old callback.
        page.run_animation_frame()
        assert page.eval("typeof ran") == "undefined"


@on_only
def test_html_script_can_queue_a_frame_callback():
    html = """
    <html><body><script>
      globalThis.log = [];
      requestAnimationFrame(function () { log.push('from-html') });
    </script></body></html>
    """
    with iv8.Page() as page:
        page.load(html, BASE)
        # A load does not pump frames.
        assert page.eval("log.join(',')") == ""
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "from-html"


# --- dispose() error paths --------------------------------------------------------

@on_only
def test_pump_after_dispose_raises():
    page = iv8.Page()
    page.eval("requestAnimationFrame(function () {})")
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.run_animation_frame()


@on_only
def test_dispose_with_a_pending_callback_is_clean():
    # A queued callback holds a V8 handle; disposal must release it without error.
    page = iv8.Page()
    page.eval("""
        for (let i = 0; i < 10; i++) { requestAnimationFrame(function () {}) }
    """)
    page.dispose()
    page.dispose()  # idempotent
    assert page.disposed is True
