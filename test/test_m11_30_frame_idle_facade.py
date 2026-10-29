"""M11-30 acceptance tests: rAF/idle integration into the event-loop facade."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-30.test/"


def _load(page):
    page.load("<html><body></body></html>", BASE)
    page.eval("globalThis.log = [];")


@on_only
def test_raf_runs_at_16ms_frame_boundary():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            requestAnimationFrame(() => log.push('f1'));
            requestAnimationFrame(() => log.push('f2'));
        """)
        assert page.tick() is False
        assert page.advance(15) is True
        assert page.eval("log.join(',')") == ""
        assert page.event_loop_time_ms == 15
        assert page.advance(1) is True
        assert page.eval("log.join(',')") == "f1,f2"
        assert page.event_loop_time_ms == 16


@on_only
def test_raf_callback_requeues_into_next_frame():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            let n = 0;
            function tick() {
                n++;
                log.push('f' + n);
                if (n < 3) requestAnimationFrame(tick);
            }
            requestAnimationFrame(tick);
        """)
        assert page.advance(16) is True
        assert page.eval("log.join(',')") == "f1"
        assert page.advance(16) is True
        assert page.eval("log.join(',')") == "f1,f2"
        assert page.advance(16) is True
        assert page.eval("log.join(',')") == "f1,f2,f3"


@on_only
def test_raf_snapshot_does_not_include_same_frame_requeue():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            requestAnimationFrame(() => {
                log.push('first');
                requestAnimationFrame(() => log.push('second'));
            });
        """)
        assert page.advance(16) is True
        assert page.eval("log.join(',')") == "first"
        assert page.advance(16) is True
        assert page.eval("log.join(',')") == "first,second"


@on_only
def test_idle_runs_when_no_timer_or_frame_is_due():
    with iv8.Page() as page:
        _load(page)
        page.eval("requestIdleCallback(() => log.push('idle'));")
        assert page.tick() is True
        assert page.eval("log.join(',')") == "idle"
        assert page.event_loop_time_ms == 0
        assert page.tick() is False


@on_only
def test_timer_precedes_idle_at_same_current_time():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            requestIdleCallback(() => log.push('idle'));
            setTimeout(() => log.push('timer'), 0);
        """)
        assert page.tick() is True
        assert page.eval("log.join(',')") == "timer"
        assert page.tick() is True
        assert page.eval("log.join(',')") == "timer,idle"


@on_only
def test_timer_precedes_frame_at_the_same_due_time():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            setTimeout(() => log.push('timer'), 16);
            requestAnimationFrame(() => log.push('frame'));
        """)
        assert page.advance(16) is True
        assert page.eval("log.join(',')") == "timer,frame"


@on_only
def test_microtasks_after_frame_and_idle():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            requestAnimationFrame(() => {
                log.push('frame');
                Promise.resolve().then(() => log.push('frame-micro'));
            });
            requestIdleCallback(() => {
                log.push('idle');
                queueMicrotask(() => log.push('idle-micro'));
            });
        """)
        assert page.advance(16) is True
        # Idle is eligible at time 0 (no timer/frame due), then the frame at 16.
        assert page.eval("log.join(',')") == (
            "idle,idle-micro,frame,frame-micro")


@on_only
def test_run_until_idle_includes_frames_and_idle():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            requestAnimationFrame(() => log.push('frame'));
            requestIdleCallback(() => log.push('idle'));
            setTimeout(() => log.push('timer'), 20);
        """)
        assert page.run_until_idle() is True
        assert page.eval("log.join(',')") == "idle,frame,timer"
        assert page.event_loop_time_ms == 20


@on_only
def test_self_rescheduling_raf_hits_cap():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            let frames = 0;
            function again() {
                frames++;
                requestAnimationFrame(again);
            }
            requestAnimationFrame(again);
        """)
        assert page.run_until_idle(max_steps=3) is False
        assert page.eval("frames") == 3
        assert page.event_loop_time_ms == 48


@on_only
def test_self_rescheduling_idle_hits_cap():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            let rounds = 0;
            function again() {
                rounds++;
                requestIdleCallback(again);
            }
            requestIdleCallback(again);
        """)
        assert page.advance(0, max_steps=3) is False
        assert page.eval("rounds") == 3
        assert page.event_loop_time_ms == 0


@on_only
def test_advance_stops_at_frame_cap_without_jumping_clock():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            function again() { requestAnimationFrame(again); }
            requestAnimationFrame(again);
        """)
        assert page.advance(1000, max_steps=2) is False
        assert page.event_loop_time_ms == 32


@on_only
def test_load_resets_frame_clock_and_queues():
    with iv8.Page() as page:
        _load(page)
        page.eval("requestAnimationFrame(() => log.push('old'));")
        assert page.advance(16) is True
        assert page.eval("log.join(',')") == "old"
        page.load("<html><body></body></html>", BASE)
        assert page.event_loop_time_ms == 0
        page.eval("globalThis.log = []; requestAnimationFrame(() => log.push('new'));")
        assert page.advance(15) is True
        assert page.eval("log.join(',')") == ""
        assert page.advance(1) is True
        assert page.eval("log.join(',')") == "new"


@on_only
def test_legacy_frame_and_idle_pumps_still_work():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            requestAnimationFrame(() => log.push('legacy-frame'));
            requestIdleCallback(() => log.push('legacy-idle'));
        """)
        page.run_animation_frame()
        page.run_idle_callbacks()
        assert page.eval("log.join(',')") == "legacy-frame,legacy-idle"
        # Legacy pumps do not advance the facade clock.
        assert page.event_loop_time_ms == 0


@on_only
def test_frame_and_idle_errors_are_swallowed():
    with iv8.Page() as page:
        _load(page)
        page.eval("""
            requestAnimationFrame(() => { throw new Error('frame'); });
            requestIdleCallback(() => { throw new Error('idle'); });
            requestAnimationFrame(() => log.push('after-frame'));
            requestIdleCallback(() => log.push('after-idle'));
        """)
        assert page.advance(16) is True
        assert page.eval("log.join(',')") == "after-idle,after-frame"
        assert page.eval("1 + 1") == 2
