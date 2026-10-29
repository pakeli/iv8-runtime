"""M11-3 acceptance tests: requestIdleCallback / cancelIdleCallback.

A minimal MANUAL idle-pump model, mirroring the M11-2 frame pump.
`requestIdleCallback(callback[, options])` queues a callback and returns a handle;
`cancelIdleCallback(handle)` drops a queued one; the Python-side
`Page.run_idle_callbacks()` is the ONLY trigger — there is no real clock, no
background thread, and no automatic event loop.

One call runs exactly one round: the callbacks queued when the call begins, in
registration order. A callback that re-requests lands in the NEXT round. Each
callback receives a minimal IdleDeadline-style object (read-only `didTimeout`, always
false; `timeRemaining()`, a fixed budget) — deterministic by design. `options.timeout`
is type-validated but has NO real timeout semantics this phase. The idle queue is
independent of the timers, the animation frames, and the microtask queue.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://idle.test/"

# The fixed budget IdleDeadline.timeRemaining() reports (see kIdleBudgetMs).
IDLE_BUDGET_MS = 50


# --- API-shape guards (both build modes) -----------------------------------------

def test_surface_shape():
    assert callable(getattr(iv8.Page, "run_idle_callbacks", None))
    # the existing manual pumps are untouched
    for pump in ("run_timers", "run_jobs", "run_animation_frame"):
        assert callable(getattr(iv8.Page, pump, None))


def test_no_new_module_level_surface():
    for forbidden in ("requestIdleCallback", "cancelIdleCallback",
                      "request_idle_callback", "run_idle_callbacks",
                      "IdleDeadline"):
        assert not hasattr(iv8, forbidden)


def test_non_goals_absent_from_page():
    # M11-3 adds ONE pump. M11-29 adds the timer/microtask facade; idle callbacks
    # are still not driven by it. No real clock or task-queue surface.
    for forbidden in ("run_event_loop", "drain", "post_task", "now", "run_all"):
        assert not hasattr(iv8.Page, forbidden)
    assert callable(getattr(iv8.Page, "tick", None))
    assert callable(getattr(iv8.Page, "run_until_idle", None))


# --- JS globals exist -------------------------------------------------------------

@on_only
def test_globals_exist():
    with iv8.Page() as page:
        assert page.eval("typeof requestIdleCallback") == "function"
        assert page.eval("typeof cancelIdleCallback") == "function"
        assert page.eval("window.requestIdleCallback === requestIdleCallback") is True


@on_only
def test_request_returns_a_numeric_handle():
    with iv8.Page() as page:
        assert page.eval("typeof requestIdleCallback(function () {})") == "number"
        assert page.eval(
            "requestIdleCallback(function(){}) !== "
            "requestIdleCallback(function(){})") is True


@on_only
def test_non_goal_globals_absent():
    # No unified scheduler / clock leaked in alongside the idle globals.
    # queueMicrotask is no longer listed here: it landed separately in M11-4.
    with iv8.Page() as page:
        for name in ("performance", "scheduler", "requestPostAnimationFrame",
                     "IdleDeadline"):
            assert page.eval(f"typeof globalThis.{name}") == "undefined", name


# --- nothing runs without a pump --------------------------------------------------

@on_only
def test_callback_does_not_run_until_pumped():
    with iv8.Page() as page:
        page.eval("globalThis.ran = 0; requestIdleCallback(function () { ran++ })")
        assert page.eval("ran") == 0
        page.eval("1 + 1")  # no clock / thread: other work must not advance it
        assert page.eval("ran") == 0
        page.run_idle_callbacks()
        assert page.eval("ran") == 1


@on_only
def test_pump_with_empty_queue_is_a_noop():
    with iv8.Page() as page:
        page.run_idle_callbacks()
        page.run_idle_callbacks()
        assert page.eval("1 + 1") == 2


@on_only
def test_callback_runs_at_most_once_per_registration():
    with iv8.Page() as page:
        page.eval("globalThis.ran = 0; requestIdleCallback(function () { ran++ })")
        page.run_idle_callbacks()
        assert page.eval("ran") == 1
        page.run_idle_callbacks()
        page.run_idle_callbacks()
        assert page.eval("ran") == 1


# --- registration order -----------------------------------------------------------

@on_only
def test_callbacks_run_in_registration_order():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestIdleCallback(function () { log.push('a') });
            requestIdleCallback(function () { log.push('b') });
            requestIdleCallback(function () { log.push('c') });
        """)
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "a,b,c"


@on_only
def test_all_queued_callbacks_run_in_one_round():
    with iv8.Page() as page:
        page.eval("""
            globalThis.count = 0;
            for (let i = 0; i < 25; i++) {
                requestIdleCallback(function () { count++ });
            }
        """)
        page.run_idle_callbacks()
        assert page.eval("count") == 25


# --- round boundary: a re-request lands in the NEXT round -------------------------

@on_only
def test_rescheduled_callback_runs_in_the_next_round():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestIdleCallback(function first() {
                log.push('first');
                requestIdleCallback(function second() { log.push('second') });
            });
        """)
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "first"
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "first,second"


@on_only
def test_self_rescheduling_callback_advances_one_round_per_pump():
    # Bounded-pump guarantee: must not spin forever inside one call.
    with iv8.Page() as page:
        page.eval("""
            globalThis.ticks = 0;
            function tick() { ticks++; requestIdleCallback(tick); }
            requestIdleCallback(tick);
        """)
        for expected in (1, 2, 3, 4):
            page.run_idle_callbacks()
            assert page.eval("ticks") == expected


@on_only
def test_nested_requests_do_not_run_in_the_same_round():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestIdleCallback(function () {
                log.push('a');
                requestIdleCallback(function () { log.push('a-child') });
            });
            requestIdleCallback(function () {
                log.push('b');
                requestIdleCallback(function () { log.push('b-child') });
            });
        """)
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "a,b"
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "a,b,a-child,b-child"


# --- cancelIdleCallback -----------------------------------------------------------

@on_only
def test_cancel_prevents_the_callback():
    with iv8.Page() as page:
        page.eval("""
            globalThis.ran = 0;
            const handle = requestIdleCallback(function () { ran++ });
            cancelIdleCallback(handle);
        """)
        page.run_idle_callbacks()
        assert page.eval("ran") == 0


@on_only
def test_cancel_only_the_named_handle():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestIdleCallback(function () { log.push('a') });
            const h = requestIdleCallback(function () { log.push('b') });
            requestIdleCallback(function () { log.push('c') });
            cancelIdleCallback(h);
        """)
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "a,c"


@on_only
def test_cancel_mid_round_prevents_a_later_callback_in_the_same_round():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            globalThis.second = 0;
            requestIdleCallback(function () {
                log.push('first');
                cancelIdleCallback(second);
            });
            globalThis.second = requestIdleCallback(function () {
                log.push('second');
            });
        """)
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "first"


@on_only
def test_cancel_of_unknown_or_stale_handle_is_a_noop():
    with iv8.Page() as page:
        page.eval("cancelIdleCallback(99999)")
        page.eval("cancelIdleCallback(0)")
        page.eval("cancelIdleCallback(-1)")
        page.eval("""
            globalThis.ran = 0;
            globalThis.h = requestIdleCallback(function () { ran++ });
        """)
        page.run_idle_callbacks()
        assert page.eval("ran") == 1
        page.eval("cancelIdleCallback(h)")  # already ran
        page.eval("cancelIdleCallback(h)")  # again
        page.run_idle_callbacks()
        assert page.eval("ran") == 1


# --- IdleDeadline shape -----------------------------------------------------------

@on_only
def test_callback_receives_one_deadline_argument():
    with iv8.Page() as page:
        page.eval("""
            globalThis.argc = -1;
            globalThis.kind = '';
            requestIdleCallback(function (deadline) {
                argc = arguments.length;
                kind = typeof deadline;
            });
        """)
        page.run_idle_callbacks()
        assert page.eval("argc") == 1
        assert page.eval("kind") == "object"


@on_only
def test_deadline_did_timeout_is_false():
    with iv8.Page() as page:
        page.eval("""
            globalThis.dt = null;
            globalThis.dtType = '';
            requestIdleCallback(function (d) { dt = d.didTimeout; dtType = typeof d.didTimeout });
        """)
        page.run_idle_callbacks()
        assert page.eval("dt") is False
        assert page.eval("dtType") == "boolean"


@on_only
def test_deadline_time_remaining_returns_the_fixed_budget():
    with iv8.Page() as page:
        page.eval("""
            globalThis.tr = null;
            globalThis.trType = '';
            requestIdleCallback(function (d) {
                trType = typeof d.timeRemaining;
                tr = d.timeRemaining();
            });
        """)
        page.run_idle_callbacks()
        assert page.eval("trType") == "function"
        assert page.eval("tr") == IDLE_BUDGET_MS


@on_only
def test_time_remaining_is_deterministic_and_does_not_decrease():
    # No clock: repeated calls within one callback must return the same value.
    with iv8.Page() as page:
        page.eval("""
            globalThis.vals = [];
            requestIdleCallback(function (d) {
                vals.push(d.timeRemaining());
                for (let i = 0; i < 5000; i++) { /* burn work */ }
                vals.push(d.timeRemaining());
                vals.push(d.timeRemaining());
            });
        """)
        page.run_idle_callbacks()
        assert page.eval("vals.join(',')") == ",".join([str(IDLE_BUDGET_MS)] * 3)


@on_only
def test_deadline_is_read_only():
    with iv8.Page() as page:
        page.eval("""
            globalThis.after = null;
            requestIdleCallback(function (d) {
                d.didTimeout = true;          // sloppy-mode write: silent no-op
                after = d.didTimeout;
            });
        """)
        page.run_idle_callbacks()
        assert page.eval("after") is False


@on_only
def test_each_callback_gets_its_own_deadline():
    with iv8.Page() as page:
        page.eval("""
            globalThis.same = null;
            globalThis.first = null;
            requestIdleCallback(function (d) { first = d });
            requestIdleCallback(function (d) { same = (d === first) });
        """)
        page.run_idle_callbacks()
        assert page.eval("same") is False


# --- options.timeout validation ---------------------------------------------------

@on_only
def test_valid_timeout_is_accepted():
    with iv8.Page() as page:
        for opts in ("{timeout: 100}", "{timeout: 0}", "{timeout: 1.5}",
                     "{timeout: 2000}"):
            assert page.eval(
                f"typeof requestIdleCallback(function(){{}}, {opts})") == "number"


@on_only
def test_absent_or_empty_options_accepted():
    with iv8.Page() as page:
        for opts in ("", ", undefined", ", null", ", {}",
                     ", {timeout: undefined}"):
            assert page.eval(
                f"typeof requestIdleCallback(function(){{}}{opts})") == "number"


@on_only
def test_unknown_option_keys_are_ignored():
    with iv8.Page() as page:
        assert page.eval(
            "typeof requestIdleCallback(function(){}, {priority: 'low', x: 1})") \
            == "number"


@on_only
def test_non_object_options_raises_type_error():
    with iv8.Page() as page:
        for bad in ("42", "'str'", "true"):
            with pytest.raises(iv8.JSError) as info:
                page.eval(f"requestIdleCallback(function(){{}}, {bad})")
            assert info.value.name == "TypeError"


@on_only
def test_non_number_timeout_raises_type_error():
    with iv8.Page() as page:
        for bad in ("'100'", "{}", "[]", "true", "null"):
            with pytest.raises(iv8.JSError) as info:
                page.eval(f"requestIdleCallback(function(){{}}, {{timeout: {bad}}})")
            assert info.value.name == "TypeError"


@on_only
def test_non_finite_or_negative_timeout_raises_type_error():
    with iv8.Page() as page:
        for bad in ("NaN", "Infinity", "-Infinity", "-1", "-0.5"):
            with pytest.raises(iv8.JSError) as info:
                page.eval(f"requestIdleCallback(function(){{}}, {{timeout: {bad}}})")
            assert info.value.name == "TypeError"


@on_only
def test_rejected_options_queues_nothing():
    # A validation failure must not leave a half-registered callback behind.
    with iv8.Page() as page:
        page.eval("globalThis.ran = 0")
        with pytest.raises(iv8.JSError):
            page.eval("requestIdleCallback(function () { ran++ }, {timeout: 'x'})")
        page.run_idle_callbacks()
        assert page.eval("ran") == 0


@on_only
def test_timeout_has_no_firing_semantics():
    # NON-GOAL guard: timeout is recorded only. A timeout of 0 must not make the
    # callback run any earlier (i.e. without a pump), and it must not run twice.
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestIdleCallback(function () { log.push('zero') }, {timeout: 0});
            requestIdleCallback(function () { log.push('big') }, {timeout: 999999});
        """)
        assert page.eval("log.length") == 0  # no pump yet -> nothing ran
        page.run_idle_callbacks()
        # Registration order decides, NOT the timeout value.
        assert page.eval("log.join(',')") == "zero,big"
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "zero,big"  # never repeats


# --- independence from timers / frames / microtasks -------------------------------

@on_only
def test_idle_pump_does_not_fire_timers_or_frames():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            setTimeout(function () { log.push('timeout') }, 0);
            requestAnimationFrame(function () { log.push('frame') });
            requestIdleCallback(function () { log.push('idle') });
        """)
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "idle"
        page.run_timers()
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "idle,timeout,frame"


@on_only
def test_timer_and_frame_pumps_do_not_run_idle_callbacks():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestIdleCallback(function () { log.push('idle') });
            setTimeout(function () { log.push('timeout') }, 0);
            requestAnimationFrame(function () { log.push('frame') });
        """)
        page.run_timers()
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "timeout,frame"
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "timeout,frame,idle"


@on_only
def test_idle_pump_does_not_drain_microtasks():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            Promise.resolve().then(function () { log.push('job') });
            requestIdleCallback(function () { log.push('idle') });
        """)
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "idle"
        page.run_jobs()
        assert page.eval("log.join(',')") == "idle,job"


@on_only
def test_handle_spaces_do_not_cross_cancel():
    # cancelIdleCallback must not cancel a timer or a frame callback, and
    # clearTimeout / cancelAnimationFrame must not cancel an idle callback.
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            globalThis.t = setTimeout(function () { log.push('timeout') }, 0);
            globalThis.f = requestAnimationFrame(function () { log.push('frame') });
            cancelIdleCallback(t);
            cancelIdleCallback(f);
        """)
        page.run_timers()
        page.run_animation_frame()
        assert page.eval("log.join(',')") == "timeout,frame"
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            globalThis.h = requestIdleCallback(function () { log.push('idle') });
            clearTimeout(h);
            cancelAnimationFrame(h);
        """)
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "idle"


# --- error handling ---------------------------------------------------------------

@on_only
def test_throwing_callback_is_swallowed_and_round_continues():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            requestIdleCallback(function () { throw new Error('boom') });
            requestIdleCallback(function () { log.push('after') });
        """)
        page.run_idle_callbacks()  # must not raise
        assert page.eval("log.join(',')") == "after"
        assert page.eval("1 + 1") == 2


@on_only
def test_non_function_or_missing_argument_is_ignored():
    with iv8.Page() as page:
        for bad in ("undefined", "null", "42", "'str'", "{}", "[]"):
            assert page.eval(f"typeof requestIdleCallback({bad})") == "undefined"
        assert page.eval("typeof requestIdleCallback()") == "undefined"
        page.eval("cancelIdleCallback()")
        page.run_idle_callbacks()
        assert page.eval("1 + 1") == 2


# --- load() interaction -----------------------------------------------------------

@on_only
def test_globals_present_in_a_loaded_generation():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", BASE)
        assert page.eval("typeof requestIdleCallback") == "function"
        assert page.eval("typeof cancelIdleCallback") == "function"


@on_only
def test_repeated_load_keeps_the_globals_and_resets_the_queue():
    with iv8.Page() as page:
        for generation in range(3):
            page.eval("globalThis.ran = 0; requestIdleCallback(function () { ran++ })")
            page.load("<html><body></body></html>", f"{BASE}{generation}")
            assert page.eval("typeof requestIdleCallback") == "function"
            # The previous generation's queue is gone, not resurrected.
            page.run_idle_callbacks()
            assert page.eval("typeof ran") == "undefined"


@on_only
def test_html_script_can_queue_an_idle_callback():
    html = """
    <html><body><script>
      globalThis.log = [];
      requestIdleCallback(function (d) { log.push('from-html:' + d.timeRemaining()) });
    </script></body></html>
    """
    with iv8.Page() as page:
        page.load(html, BASE)
        assert page.eval("log.join(',')") == ""  # a load does not pump
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == f"from-html:{IDLE_BUDGET_MS}"


# --- dispose() error paths --------------------------------------------------------

@on_only
def test_pump_after_dispose_raises():
    page = iv8.Page()
    page.eval("requestIdleCallback(function () {})")
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.run_idle_callbacks()


@on_only
def test_other_pumps_still_raise_after_dispose():
    page = iv8.Page()
    page.dispose()
    for pump in ("run_idle_callbacks", "run_animation_frame", "run_timers",
                 "run_jobs"):
        with pytest.raises(iv8.JSContextDisposedError):
            getattr(page, pump)()


@on_only
def test_dispose_with_pending_callbacks_is_clean():
    # Queued callbacks hold V8 handles; disposal must release them without error.
    page = iv8.Page()
    page.eval("""
        for (let i = 0; i < 10; i++) {
            requestIdleCallback(function () {}, {timeout: 100});
        }
    """)
    page.dispose()
    page.dispose()  # idempotent
    assert page.disposed is True
