"""BOM-09 related BOM coverage: performance (22/22), console (25/25), scheduler (2/2)."""

import hashlib
import json
import logging
from pathlib import Path
import threading
import time

import pytest

import iv8

pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires V8-linked build"
)

FIXTURES = Path(__file__).resolve().parent / "fixtures" / "bom_09"
PROBE_PATH = FIXTURES / "probe.js"


def _load_fixture(filename: str) -> dict:
    return json.loads((FIXTURES / filename).read_text(encoding="utf-8"))


@pytest.mark.parametrize(
    ("fixture_name", "base_url"),
    [
        ("chrome.json", "http://127.0.0.1:8080/reference"),
        ("chrome-insecure.json", "http://bom-fixture.invalid:8080/reference"),
        ("chrome-opaque.json", "about:blank"),
    ],
)
def test_bom_09_probe_matches_chrome_158_fixtures(fixture_name, base_url):
    fixture = _load_fixture(fixture_name)
    probe_bytes = PROBE_PATH.read_bytes()
    assert hashlib.sha256(probe_bytes).hexdigest() == fixture["probe_sha256"]

    with iv8.Page() as page:
        page.load("<!doctype html><html><head></head><body></body></html>", base_url)
        page.eval(probe_bytes.decode("utf-8"), name="probe.js")
        page.run_until_idle(max_steps=50)
        page.run_jobs()
        actual = json.loads(
            page.eval("document.getElementById('bom-reference').textContent")
        )

    assert actual == fixture["result"]


def test_performance_edge_cases_and_deterministic_profile():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", "https://perf-bom09.test/")
        result = json.loads(
            page.eval(
                """
                JSON.stringify((() => {
                  const err = fn => {
                    try { return {ok: true, value: fn()}; }
                    catch (e) {
                      return {
                        ok: false,
                        name: e.name,
                        message: e.message,
                        dom: e instanceof DOMException,
                      };
                    }
                  };
                  const m = new PerformanceMark('detached', {detail: {k: [1, 2]}});
                  const sameDetail = m.detail === m.detail;
                  const t1 = performance.timing.navigationStart;
                  const t2 = performance.timing.loadEventEnd;
                  const httpsSecureStart = performance.timing.secureConnectionStart > 0;
                  const ec = performance.eventCounts;
                  const ecSum = Array.from(ec.values()).reduce((a, b) => a + b, 0);
                  const ecIterTag = Object.prototype.toString.call(ec.entries());
                  const ecHasClick = ec.has('click') && !ec.has('nonexistent');
                  const ecGetClick = ec.get('click') === 0 && ec.get('nonexistent') === undefined;
                  let feCount = 0;
                  ec.forEach(function (v, k, map) {
                    if (v === 0 && typeof k === 'string' && map === ec && this.tag === 9) feCount++;
                  }, {tag: 9});

                  performance.setResourceTimingBufferSize(128);
                  performance.clearResourceTimings();

                  return {
                    sameDetail,
                    monotonicTiming: t1 === 1700000000000 && t2 >= t1,
                    httpsSecureStart,
                    ecSize: ec.size,
                    ecSum,
                    ecIterTag,
                    ecHasClick,
                    ecGetClick,
                    feCount,
                    mark0: err(() => performance.mark()),
                    markCtor0: err(() => new PerformanceMark()),
                    markReservedCall: err(() => performance.mark('navigationStart')),
                    markReservedCtor: err(() => new PerformanceMark('navigationStart')),
                    markBadOpt: err(() => performance.mark('x', 123)),
                    markNaN: err(() => performance.mark('x', {startTime: NaN})),
                    markUncloneable: err(() => new PerformanceMark('fn', {detail: () => {}})),
                    measure0: err(() => performance.measure()),
                    measureEmptyObj: err(() => {
                      const e = performance.measure('emp', {});
                      return [e.startTime, e.duration];
                    }),
                    measureDetailOnly: err(() => performance.measure('d', {detail: 1})),
                    measureObjAndEnd: err(() => performance.measure('oe', {start: 0}, 'x')),
                    measureNegStart: err(() => performance.measure('ns', {start: -1})),
                    measureZeroTiming: err(() => performance.measure('zt', 'unloadEventStart')),
                    measureNavStart: err(() => performance.measure('ok-nav', 'navigationStart').startTime),
                    getByName0: err(() => performance.getEntriesByName()),
                    getByNameFiltered: (() => {
                      performance.mark('shared');
                      return [
                        performance.getEntriesByName('shared', 'mark').length,
                        performance.getEntriesByName('shared', 'measure').length,
                      ];
                    })(),
                    getByType0: err(() => performance.getEntriesByType()),
                    setBuf0: err(() => performance.setResourceTimingBufferSize()),
                    memIllegal: err(() =>
                      Object.getOwnPropertyDescriptor(
                        Object.getPrototypeOf(performance.memory), 'totalJSHeapSize'
                      ).get.call({})
                    ),
                  };
                })())
                """
            )
        )

    assert result["sameDetail"] is True
    assert result["monotonicTiming"] is True
    assert result["httpsSecureStart"] is True
    assert result["ecSize"] == 36
    assert result["ecSum"] == 0
    assert result["ecIterTag"] == "[object EventCounts Iterator]"
    assert result["ecHasClick"] is True
    assert result["ecGetClick"] is True
    assert result["feCount"] == 36
    assert result["mark0"]["name"] == "TypeError"
    assert result["markCtor0"]["name"] == "TypeError"
    assert result["markReservedCall"] == {
        "ok": False,
        "name": "SyntaxError",
        "message": "Failed to execute 'mark' on 'Performance': 'navigationStart' is part of the PerformanceTiming interface, and cannot be used as a mark name.",
        "dom": True,
    }
    assert result["markReservedCtor"] == {
        "ok": False,
        "name": "SyntaxError",
        "message": "Failed to construct 'PerformanceMark': 'navigationStart' is part of the PerformanceTiming interface, and cannot be used as a mark name.",
        "dom": True,
    }
    assert result["markBadOpt"]["name"] == "TypeError"
    assert result["markNaN"]["name"] == "TypeError"
    assert result["markUncloneable"]["dom"] is True
    assert result["markUncloneable"]["name"] == "DataCloneError"
    assert result["measure0"]["name"] == "TypeError"
    assert result["measureEmptyObj"] == {"ok": True, "value": [0, 0]}
    assert result["measureDetailOnly"]["name"] == "TypeError"
    assert result["measureObjAndEnd"]["name"] == "TypeError"
    assert result["measureNegStart"]["name"] == "TypeError"
    assert result["measureZeroTiming"]["dom"] is True
    assert result["measureZeroTiming"]["name"] == "InvalidAccessError"
    assert result["measureNavStart"] == {"ok": True, "value": 0}
    assert result["getByName0"]["name"] == "TypeError"
    assert result["getByNameFiltered"] == [1, 0]
    assert result["getByType0"]["name"] == "TypeError"
    assert result["setBuf0"]["name"] == "TypeError"
    assert result["memIllegal"] == {
        "ok": False,
        "name": "TypeError",
        "message": "Illegal invocation",
        "dom": False,
    }


def test_scheduler_cancellation_recursion_and_illegal_receivers():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", "https://sched-bom09.test/")
        page.eval(
            """
            globalThis.outcomes = {};
            const ac = new TaskController({priority: 'background'});
            let ranCancelled = false;
            scheduler.postTask(() => { ranCancelled = true; return 1; }, {delay: 500, signal: ac.signal})
              .then(
                v => { outcomes.cancelled = {ok: true, v}; },
                e => { outcomes.cancelled = {ok: false, e}; }
              );
            ac.abort('cancelled-early');
            outcomes.ranCancelled = ranCancelled;

            // Recursive setPriority guard
            const tcRec = new TaskController({priority: 'background'});
            tcRec.signal.addEventListener('prioritychange', () => {
              try { tcRec.setPriority('user-visible'); outcomes.rec = 'no-error'; }
              catch (err) { outcomes.rec = {name: err.name, dom: err instanceof DOMException}; }
            }, {once: true});
            tcRec.setPriority('user-blocking');

            // Illegal receiver rejections on Scheduler.prototype
            Scheduler.prototype.postTask.call({})
              .catch(err => { outcomes.illegalPost = {name: err.name, message: err.message}; });
            Scheduler.prototype.yield.call({})
              .catch(err => { outcomes.illegalYield = {name: err.name, message: err.message}; });
            """
        )
        # Cancelled 500ms task must NOT advance virtual clock on run_until_idle!
        page.run_until_idle(max_steps=20)
        page.run_jobs()
        assert page.event_loop_time_ms == 0
        outcomes = json.loads(page.eval("JSON.stringify(outcomes)"))
        assert outcomes["ranCancelled"] is False
        assert outcomes["cancelled"] == {"ok": False, "e": "cancelled-early"}
        assert outcomes["rec"] == {"name": "NotAllowedError", "dom": True}
        assert outcomes["illegalPost"] == {
            "name": "TypeError",
            "message": "Failed to execute 'postTask' on 'Scheduler': Illegal invocation",
        }
        assert outcomes["illegalYield"] == {
            "name": "TypeError",
            "message": "Failed to execute 'yield' on 'Scheduler': Illegal invocation",
        }


def test_console_context_create_task_and_monitoring(caplog):
    with iv8.Page() as page:
        page.load("<html><body></body></html>", "https://console-bom09.test/")
        page.watch_apis(
            [
                "console.context",
                "console.createTask",
                "performance.toJSON",
                "scheduler.yield",
                "TaskController.setPriority",
                "TaskSignal.any",
            ]
        )
        page.start_access_trace()
        with caplog.at_level(logging.INFO, logger="iv8.console"):
            check = json.loads(
                page.eval(
                    """
                    JSON.stringify((() => {
                      const ctx = console.context('worker');
                      ctx.log('hello-ctx');
                      const task = console.createTask('render');
                      let badNew = null;
                      let badThis = null;
                      try { new task.run(() => 1); } catch (e) { badNew = e.message; }
                      try { task.run.call({}, () => 1); } catch (e) { badThis = e.name + ':' + e.message; }
                      const val = task.run(() => 99);
                      const j = performance.toJSON();
                      scheduler.yield();
                      const tc = new TaskController();
                      tc.setPriority('user-blocking');
                      const comb = TaskSignal.any([tc.signal]);
                      const m1 = console.memory.totalJSHeapSize;
                      console.memory = 1;
                      return {val, badNew, badThis, timeOrigin: j.timeOrigin, combPrio: comb.priority, m1};
                    })())
                    """,
                    name="bom09-mon.js",
                )
            )
        page.run_until_idle(max_steps=10)
        hits = [h["path"] for h in page.read_watch_api_hits()]
        events = page.read_access_trace(clear=True)["events"]

        assert check == {
            "val": 99,
            "badNew": "'run' called with illegal receiver.",
            "badThis": "TypeError:Illegal invocation",
            "timeOrigin": 0,
            "combPrio": "user-visible",
            "m1": 10000000,
        }
        assert any("[worker] hello-ctx" in r.getMessage() for r in caplog.records)
        assert hits == [
            "console.context",
            "console.createTask",
            "performance.toJSON",
            "scheduler.yield",
            "TaskController.setPriority",
            "TaskSignal.any",
        ]
        assert any(
            e["object_type"] == "console" and e["member"] == "memory" and e["operation"] == "get"
            for e in events
        )
        assert any(
            e["object_type"] == "console" and e["member"] == "memory" and e["operation"] == "set"
            for e in events
        )
        # Generation reload tears down all retained entry/subobject V8 handles cleanly.
        page.eval("performance.mark('gen1', {detail: {x: 1}});")
        page.load("<html><body></body></html>", "https://console-bom09.test/")
        assert page.eval("performance.getEntries().length") == 0


def test_review_regressions_all_7_findings():
    # Finding #1 [P1]: to_py=True and JSValue.to_py() release the Python GIL
    # while executing JS property getters.
    with iv8.JSContext() as ctx_slow:
        slow_source = """
        ({
          get value() {
            const end = Date.now() + 250;
            while (Date.now() < end) {}
            return 77;
          }
        })
        """
        for use_handle in (False, True):
            companion_elapsed = None

            def companion():
                nonlocal companion_elapsed
                t0 = time.perf_counter()
                time.sleep(0.02)
                with iv8.JSContext() as ctx_fast:
                    assert ctx_fast.eval("40 + 2", to_py=True) == 42
                companion_elapsed = time.perf_counter() - t0

            worker = threading.Thread(target=companion)
            if use_handle:
                handle = ctx_slow.eval(slow_source, to_py=False)
                worker.start()
                converted = handle.to_py()
            else:
                worker.start()
                converted = ctx_slow.eval(slow_source, to_py=True)
            worker.join(timeout=5.0)
            assert converted == {"value": 77}
            assert companion_elapsed is not None
            assert companion_elapsed < 0.18, companion_elapsed

    with iv8.Page() as page:
        page.load("<html><body></body></html>", "https://review-bom09.test/")

        # Finding #2 [P1]: DataCloneError thrown (never swallowed) for mark,
        # new PerformanceMark, and measure.
        clone_errors = json.loads(
            page.eval(
                """
                JSON.stringify((() => {
                  const check = fn => {
                    try { fn(); return {ok: true}; }
                    catch (e) { return {ok: false, name: e.name, dom: e instanceof DOMException}; }
                  };
                  return {
                    mark: check(() => performance.mark('bad', {detail: () => {}})),
                    ctor: check(() => new PerformanceMark('bad', {detail: () => {}})),
                    measure: check(() => performance.measure('bad', {start: 0, detail: () => {}})),
                  };
                })())
                """
            )
        )
        assert clone_errors == {
            "mark": {"ok": False, "name": "DataCloneError", "dom": True},
            "ctor": {"ok": False, "name": "DataCloneError", "dom": True},
            "measure": {"ok": False, "name": "DataCloneError", "dom": True},
        }

        # Finding #3 [P1]: clearMarks()/clearMeasures() do not retain detail
        # ArrayBuffers on C++ HostObjects across repeated mark/measure + clear cycles.
        page.eval(
            """
            globalThis.clearedRefs = [];
            for (let i = 0; i < 100; ++i) {
              const m = performance.mark('buf-' + i, {detail: new Uint8Array(1024 * 1024).fill(7)});
              clearedRefs.push(new WeakRef(m.detail));
              performance.clearMarks();
            }
            """
        )
        page.run_jobs()
        alive_details = page.eval(
            """
            (() => {
              for (let round = 0; round < 4; ++round) {
                let hold = [];
                for (let i = 0; i < 400; ++i) hold.push(new Array(16384).fill(i));
                hold = null;
              }
              return clearedRefs.filter(w => w.deref() !== undefined).length;
            })()
            """,
            to_py=True,
        )
        assert page.eval("performance.getEntries().length") == 0
        assert alive_details == 0

        # Finding #4 [P2] & #5 [P2]: Scheduler priority ordering, dynamic
        # TaskSignal promotion, and multi-hop A -> B -> C priority propagation.
        page.eval(
            """
            globalThis.schedReview = {};
            const order1 = [];
            scheduler.postTask(() => order1.push('bg'), {priority: 'background'});
            scheduler.postTask(() => order1.push('ub'), {priority: 'user-blocking'});
            scheduler.postTask(() => order1.push('uv'), {priority: 'user-visible'});
            setTimeout(() => order1.push('timeout'), 0);
            scheduler.yield().then(() => order1.push('yield'));
            schedReview.order1 = order1;

            const order2 = [];
            const tcDyn = new TaskController({priority: 'background'});
            scheduler.postTask(() => order2.push('promoted'), {delay: 5, signal: tcDyn.signal});
            scheduler.postTask(() => order2.push('uv'), {delay: 5, priority: 'user-visible'});
            tcDyn.setPriority('user-blocking');
            schedReview.order2 = order2;

            const tcA = new TaskController({priority: 'background'});
            const sigB = TaskSignal.any([], {priority: tcA.signal});
            const sigC = TaskSignal.any([], {priority: sigB});
            const chain = [];
            let recInC = null;
            tcA.signal.addEventListener('prioritychange', e =>
              chain.push(['A', e.previousPriority, tcA.signal.priority, sigB.priority, sigC.priority]));
            sigB.addEventListener('prioritychange', e =>
              chain.push(['B', e.previousPriority, tcA.signal.priority, sigB.priority, sigC.priority]));
            sigC.addEventListener('prioritychange', e => {
              chain.push(['C', e.previousPriority, tcA.signal.priority, sigB.priority, sigC.priority]);
              try { tcA.setPriority('user-visible'); recInC = 'ok'; }
              catch (err) { recInC = err.name; }
            });
            tcA.setPriority('user-blocking');
            schedReview.chain = chain;
            schedReview.recInC = recInC;
            """
        )
        page.run_until_idle(max_steps=30)
        page.run_jobs()
        sched_review = page.eval("schedReview", to_py=True)
        assert sched_review["order1"] == ["ub", "yield", "uv", "timeout", "bg"]
        assert sched_review["order2"] == ["promoted", "uv"]
        assert sched_review["chain"] == [
            ["A", "background", "user-blocking", "background", "background"],
            ["B", "background", "user-blocking", "user-blocking", "background"],
            ["C", "background", "user-blocking", "user-blocking", "user-blocking"],
        ]
        assert sched_review["recInC"] == "NotAllowedError"

    # Finding #6 [P2] & #7 [P2]: fresh page with Array.prototype[Symbol.iterator]
    # replaced BEFORE first EventCounts iterator call, plus console metadata check.
    with iv8.Page() as fresh_page:
        fresh_page.load("<html><body></body></html>", "https://fresh-bom09.test/")
        meta = fresh_page.eval(
            """
            (() => {
              let userArrayIterCalls = 0;
              const orig = Array.prototype[Symbol.iterator];
              Array.prototype[Symbol.iterator] = function () {
                userArrayIterCalls++;
                return orig.call(this);
              };
              const iter = performance.eventCounts.entries();
              const first = iter.next();
              Array.prototype[Symbol.iterator] = orig;

              const seenStep = [];
              const customIter = {
                [Symbol.iterator]() {
                  return {
                    next() {
                      return {
                        get done() { seenStep.push('done'); return true; },
                        get value() { seenStep.push('value'); throw new Error('must not read value'); },
                      };
                    }
                  };
                }
              };
              TaskSignal.any(customIter);
              AbortSignal.any(customIter);

              return {
                userArrayIterCalls,
                firstDone: first.done,
                seenStep,
                consoleErrorLen: console.error.length,
                subscriberErrorLen: Subscriber.prototype.error.length,
                consoleFirstKey: Reflect.ownKeys(console).map(String)[0],
              };
            })()
            """,
            to_py=True,
        )
        assert meta == {
            "userArrayIterCalls": 0,
            "firstDone": False,
            "seenStep": ["done", "done"],
            "consoleErrorLen": 0,
            "subscriberErrorLen": 1,
            "consoleFirstKey": "debug",
        }


def test_codex_second_pass_regressions():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", "https://bom09-codex.test/")
        _run_codex_second_pass_regressions(page)


def _run_codex_second_pass_regressions(page):
    # 1. [P2] WebIDL [EnforceRange] unsigned long long rejects >= 2^53 and >= 2^64 before casting
    page.eval(
        """
        globalThis.__codexDelay = {};
        for (const [k, v] of [
          ['pow64', 2 ** 64],
          ['pow53', 2 ** 53],
          ['maxSafe', 2 ** 53 - 1],
        ]) {
          scheduler.postTask(() => 'ok', { delay: v, signal: AbortSignal.abort('pre-aborted') }).then(
            (res) => { globalThis.__codexDelay[k] = { ok: true, res }; },
            (err) => {
              globalThis.__codexDelay[k] = {
                ok: false,
                isString: typeof err === 'string',
                name: err && err.name,
                val: String((err && err.message) || err),
              };
            }
          );
        }
        """
    )
    page.run_until_idle()
    delay_res = page.eval("globalThis.__codexDelay", to_py=True)
    assert delay_res["pow64"]["isString"] is False
    assert delay_res["pow64"]["name"] == "TypeError"
    assert delay_res["pow53"]["isString"] is False
    assert delay_res["pow53"]["name"] == "TypeError"
    assert delay_res["maxSafe"] == {
        "ok": False,
        "isString": True,
        "name": iv8.JSUndefined,
        "val": "pre-aborted",
    }

    # 2. [P2] scheduler.yield() continuation priority & task attribution inheritance
    page.eval(
        """
        globalThis.__yieldChecks = {};
        // 2a. Top-level yield queued BEFORE user-blocking postTask must still run AFTER user-blocking
        {
          const order = [];
          globalThis.__yieldChecks.top = order;
          scheduler.postTask(() => order.push('uv1'), { priority: 'user-visible' });
          scheduler.yield().then(() => order.push('top-yield'));
          scheduler.postTask(() => order.push('ub'), { priority: 'user-blocking' });
          scheduler.postTask(() => order.push('bg'), { priority: 'background' });
          scheduler.postTask(() => order.push('uv2'), { priority: 'user-visible' });
        }
        """
    )
    page.run_until_idle()
    assert page.eval("globalThis.__yieldChecks.top", to_py=True) == [
        "ub",
        "top-yield",
        "uv1",
        "uv2",
        "bg",
    ]

    page.eval(
        """
        // 2b. Chained yields inside background postTask inherit background continuation priority
        {
          const order = [];
          globalThis.__yieldChecks.bg = order;
          scheduler.postTask(async () => {
            order.push('bg1-start');
            scheduler.postTask(() => order.push('uv-from-bg'), { priority: 'user-visible' });
            scheduler.postTask(() => order.push('bg-inner'), { priority: 'background' });
            await scheduler.yield();
            order.push('bg1-after-yield1');
            await scheduler.yield();
            order.push('bg1-after-yield2');
          }, { priority: 'background' });
          scheduler.postTask(() => order.push('bg2'), { priority: 'background' });
        }
        """
    )
    page.run_until_idle()
    assert page.eval("globalThis.__yieldChecks.bg", to_py=True) == [
        "bg1-start",
        "uv-from-bg",
        "bg1-after-yield1",
        "bg1-after-yield2",
        "bg2",
        "bg-inner",
    ]

    page.eval(
        """
        // 2c. Interleaved gate microtask + dynamic TaskSignal + setTimeout boundary + abort inheritance
        {
          const gateOrder = [];
          globalThis.__yieldChecks.gate = gateOrder;
          let resolveGate;
          const gate = new Promise((r) => { resolveGate = r; });
          scheduler.postTask(async () => {
            await gate;
            await scheduler.yield();
            gateOrder.push('bg-yield');
          }, { priority: 'background' });
          scheduler.postTask(() => gateOrder.push('uv-task'), { priority: 'user-visible' });
          scheduler.postTask(() => resolveGate(), { priority: 'user-blocking' });
        }
        """
    )
    page.run_until_idle()
    assert page.eval("globalThis.__yieldChecks.gate", to_py=True) == [
        "uv-task",
        "bg-yield",
    ]

    page.eval(
        """
        {
          const dynOrder = [];
          globalThis.__yieldChecks.dyn = dynOrder;
          const tc = new TaskController({ priority: 'user-visible' });
          scheduler.postTask(async () => {
            dynOrder.push('dyn-start');
            tc.setPriority('background');
            scheduler.postTask(() => dynOrder.push('uv'), { priority: 'user-visible' });
            scheduler.postTask(() => dynOrder.push('bg'), { priority: 'background' });
            await scheduler.yield();
            dynOrder.push('dyn-after-yield');
          }, { signal: tc.signal });

          const stOrder = [];
          globalThis.__yieldChecks.st = stOrder;
          scheduler.postTask(() => {
            setTimeout(async () => {
              scheduler.postTask(() => stOrder.push('uv'), { priority: 'user-visible' });
              scheduler.postTask(() => stOrder.push('bg'), { priority: 'background' });
              await scheduler.yield();
              stOrder.push('settimeout-yield');
            }, 0);
          }, { priority: 'background' });

          const tcAbort = new TaskController({ priority: 'user-visible' });
          scheduler.postTask(async () => {
            tcAbort.abort('yield-aborted');
            try {
              await scheduler.yield();
              globalThis.__yieldChecks.aborted = 'resolved';
            } catch (e) {
              globalThis.__yieldChecks.aborted = e;
            }
          }, { signal: tcAbort.signal });
        }
        """
    )
    page.run_until_idle()
    assert page.eval("globalThis.__yieldChecks.dyn", to_py=True) == [
        "dyn-start",
        "uv",
        "dyn-after-yield",
        "bg",
    ]
    assert page.eval("globalThis.__yieldChecks.st", to_py=True) == [
        "settimeout-yield",
        "uv",
        "bg",
    ]
    assert page.eval("globalThis.__yieldChecks.aborted", to_py=True) == "yield-aborted"

    # 3. [P2] Reclaim completed postTask entries & reused AbortSignal callbacks
    page.eval(
        """
        globalThis.__reusedAc = new AbortController();
        globalThis.__reclaimRefs = [];
        for (let i = 0; i < 200; i++) {
          const payload = { buf: new Uint8Array(4096) };
          globalThis.__reclaimRefs.push(new WeakRef(payload));
          scheduler.postTask(() => payload.buf.byteLength, { signal: globalThis.__reusedAc.signal });
        }
        """
    )
    page.run_until_idle()
    alive_after_reused_signal = page.eval(
        """
        (() => {
          for (let round = 0; round < 4; ++round) {
            let hold = [];
            for (let i = 0; i < 400; ++i) hold.push(new Array(16384).fill(i));
            hold = null;
          }
          const alivePayloads = globalThis.__reclaimRefs.filter(r => r.deref() !== undefined).length;
          globalThis.__reclaimRefs = null;
          globalThis.__reusedAc.abort('later');
          return { alivePayloads, reason: globalThis.__reusedAc.signal.reason };
        })()
        """,
        to_py=True,
    )
    assert alive_after_reused_signal == {
        "alivePayloads": 0,
        "reason": "later",
    }

    # 4. [P2] Preserve exact user error identity when detail getter throws "structuredClone: ..."
    getter_err = page.eval(
        """
        (() => {
          const customErr = new TypeError('structuredClone: user getter boom');
          const results = [];
          for (const fn of [
            () => performance.mark('m', { detail: { get x() { throw customErr; } } }),
            () => new PerformanceMark('m', { detail: { get x() { throw customErr; } } }),
            () => performance.measure('m', { detail: { get x() { throw customErr; } }, start: 0, end: 1 }),
          ]) {
            try {
              fn();
              results.push({ same: false });
            } catch (e) {
              results.push({ same: e === customErr, name: e.name, msg: e.message });
            }
          }
          return results;
        })()
        """,
        to_py=True,
    )
    assert getter_err == [
        {"same": True, "name": "TypeError", "msg": "structuredClone: user getter boom"},
        {"same": True, "name": "TypeError", "msg": "structuredClone: user getter boom"},
        {"same": True, "name": "TypeError", "msg": "structuredClone: user getter boom"},
    ]

