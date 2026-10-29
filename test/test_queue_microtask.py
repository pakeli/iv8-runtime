"""M11-4 acceptance tests: queueMicrotask.

`queueMicrotask(callback)` enqueues a callback on the existing V8 microtask queue;
`Page.run_jobs()` is the ONLY trigger. This slice adds no new Python API, no clock,
no background thread, and no automatic event loop. The microtask queue remains
independent of the M2-4 timers, M11-2 animation frames, and M11-3 idle callbacks.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://microtask.test/"


# --- API-shape guards (both build modes) -----------------------------------------

def test_surface_shape():
    assert callable(getattr(iv8.Page, "run_jobs", None))
    # M11-4 adds no pump and does not disturb the existing four-pump surface.
    for pump in ("run_timers", "run_animation_frame", "run_idle_callbacks"):
        assert callable(getattr(iv8.Page, pump, None))


def test_no_new_module_level_surface():
    for forbidden in ("queueMicrotask", "queue_microtask", "run_microtasks"):
        assert not hasattr(iv8, forbidden)


def test_non_goals_absent_from_page():
    # No auto-loop / clock / task-queue surface leaked in alongside the global.
    # M11-29 adds the timer/microtask facade, so tick/run_until_idle are present.
    for forbidden in ("run_event_loop", "drain", "post_task", "now", "run_all",
                      "queue_microtask"):
        assert not hasattr(iv8.Page, forbidden)
    assert callable(getattr(iv8.Page, "tick", None))
    assert callable(getattr(iv8.Page, "run_until_idle", None))


# --- JS global exists -------------------------------------------------------------

@on_only
def test_global_exists():
    with iv8.Page() as page:
        assert page.eval("typeof queueMicrotask") == "function"
        assert page.eval("window.queueMicrotask === queueMicrotask") is True
        assert page.eval("globalThis.queueMicrotask === queueMicrotask") is True


@on_only
def test_returns_undefined():
    with iv8.Page() as page:
        assert page.eval("queueMicrotask(function () {})") is iv8.JSUndefined


@on_only
def test_callback_receives_no_arguments():
    with iv8.Page() as page:
        page.eval("""
            globalThis.argc = -1;
            queueMicrotask(function () { argc = arguments.length });
        """)
        page.run_jobs()
        assert page.eval("argc") == 0


# --- nothing runs without a pump --------------------------------------------------

@on_only
def test_callback_does_not_run_until_pumped():
    with iv8.Page() as page:
        page.eval("globalThis.ran = 0; queueMicrotask(function () { ran++ })")
        assert page.eval("ran") == 0
        page.eval("1 + 1")  # other work must not advance the microtask queue
        assert page.eval("ran") == 0
        page.run_jobs()
        assert page.eval("ran") == 1


@on_only
def test_pumping_with_empty_queue_is_a_noop():
    with iv8.Page() as page:
        page.run_jobs()
        page.run_jobs()
        assert page.eval("1 + 1") == 2


@on_only
def test_callback_runs_at_most_once_per_registration():
    with iv8.Page() as page:
        page.eval("globalThis.ran = 0; queueMicrotask(function () { ran++ })")
        page.run_jobs()
        assert page.eval("ran") == 1
        page.run_jobs()
        page.run_jobs()
        assert page.eval("ran") == 1


# --- FIFO / ordering --------------------------------------------------------------

@on_only
def test_callbacks_run_in_registration_order():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            queueMicrotask(function () { log.push('a') });
            queueMicrotask(function () { log.push('b') });
            queueMicrotask(function () { log.push('c') });
        """)
        page.run_jobs()
        assert page.eval("log.join(',')") == "a,b,c"


@on_only
def test_nested_microtask_appends_to_the_same_checkpoint():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            queueMicrotask(function () {
                log.push('a');
                queueMicrotask(function () { log.push('c') });
            });
            queueMicrotask(function () { log.push('b') });
        """)
        page.run_jobs()
        assert page.eval("log.join(',')") == "a,b,c"


@on_only
def test_interleaves_with_promise_reactions_in_fifo_order():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            queueMicrotask(function () { log.push('q1') });
            Promise.resolve().then(function () { log.push('p1') });
            queueMicrotask(function () { log.push('q2') });
            Promise.resolve().then(function () { log.push('p2') });
        """)
        page.run_jobs()
        assert page.eval("log.join(',')") == "q1,p1,q2,p2"


# --- callback validation ----------------------------------------------------------

@on_only
@pytest.mark.parametrize("expression", [
    "queueMicrotask()",
    "queueMicrotask(undefined)",
    "queueMicrotask(null)",
    "queueMicrotask(1)",
    "queueMicrotask('x')",
    "queueMicrotask({})",
    "queueMicrotask([])",
])
def test_non_callable_throws_type_error(expression):
    with iv8.Page() as page:
        with pytest.raises(iv8.JSError) as info:
            page.eval(expression)
        assert info.value.name == "TypeError"
        # The rejected call leaves the page usable and nothing queued.
        assert page.eval("1 + 1") == 2
        page.run_jobs()


# --- error isolation --------------------------------------------------------------

@on_only
def test_throwing_microtask_is_swallowed_and_later_ones_still_run():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            queueMicrotask(function () {
                log.push('before');
                throw new Error('boom');
            });
            queueMicrotask(function () { log.push('after') });
        """)
        page.run_jobs()  # must not raise
        assert page.eval("log.join(',')") == "before,after"
        assert page.eval("1 + 1") == 2


# --- independence from the other pumps --------------------------------------------

@on_only
def test_other_pumps_do_not_drain_microtasks():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            queueMicrotask(function () { log.push('micro') });
            setTimeout(function () { log.push('timer') }, 0);
            requestAnimationFrame(function () { log.push('frame') });
            requestIdleCallback(function () { log.push('idle') });
        """)
        page.run_timers()
        page.run_animation_frame()
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "timer,frame,idle"
        page.run_jobs()
        assert page.eval("log.join(',')") == "timer,frame,idle,micro"


@on_only
def test_run_jobs_does_not_fire_the_other_queues():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            queueMicrotask(function () { log.push('micro') });
            setTimeout(function () { log.push('timer') }, 0);
            requestAnimationFrame(function () { log.push('frame') });
            requestIdleCallback(function () { log.push('idle') });
        """)
        page.run_jobs()
        assert page.eval("log.join(',')") == "micro"
        page.run_timers()
        page.run_animation_frame()
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "micro,timer,frame,idle"


@on_only
def test_microtask_queued_from_a_timer_runs_on_the_next_run_jobs():
    with iv8.Page() as page:
        page.eval("""
            globalThis.log = [];
            setTimeout(function () {
                log.push('timer');
                queueMicrotask(function () { log.push('micro') });
            }, 0);
        """)
        page.run_timers()
        assert page.eval("log.join(',')") == "timer"
        page.run_jobs()
        assert page.eval("log.join(',')") == "timer,micro"


# --- load() interaction -----------------------------------------------------------

@on_only
def test_global_present_in_a_loaded_generation():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", BASE)
        assert page.eval("typeof queueMicrotask") == "function"


@on_only
def test_load_discards_pending_microtasks():
    with iv8.Page() as page:
        page.eval("globalThis.ran = 0; queueMicrotask(function () { ran++ })")
        page.load("<html><body></body></html>", BASE)
        assert page.eval("typeof ran") == "undefined"
        page.run_jobs()
        # The previous generation's queue was torn down, not resurrected.
        assert page.eval("typeof ran") == "undefined"


@on_only
def test_html_script_can_queue_a_microtask():
    html = """
    <html><body><script>
      globalThis.log = [];
      queueMicrotask(function () { log.push('from-html') });
    </script></body></html>
    """
    with iv8.Page() as page:
        page.load(html, BASE)
        assert page.eval("log.join(',')") == ""  # a load does not pump
        page.run_jobs()
        assert page.eval("log.join(',')") == "from-html"


# --- dispose() error paths --------------------------------------------------------

@on_only
def test_run_jobs_after_dispose_raises():
    page = iv8.Page()
    page.eval("queueMicrotask(function () {})")
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.run_jobs()


@on_only
def test_dispose_with_pending_microtasks_is_clean():
    page = iv8.Page()
    page.eval("""
        for (let i = 0; i < 10; i++) {
            queueMicrotask(function () {});
        }
    """)
    page.dispose()
    page.dispose()  # idempotent
    assert page.disposed is True


# --- monitoring integration -------------------------------------------------------

@on_only
def test_watch_apis_records_queue_microtask_calls():
    with iv8.Page() as page:
        page.watch_apis(["window.queueMicrotask"])
        page.eval("queueMicrotask(function () {})")
        hits = page.read_watch_api_hits()
        assert len(hits) == 1
        assert hits[0]["path"] == "window.queueMicrotask"
        assert hits[0]["type"] == "call"
        # read-and-clear: a second read is empty
        assert page.read_watch_api_hits() == []
