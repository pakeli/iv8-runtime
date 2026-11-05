"""Offline browser semantics from the user's 27-expression Chrome comparison.

Synthetic documents only: no challenge code, live site, cookies, or network.
Additional cases exercise the same root causes beyond the reported expressions.
"""

import json

import pytest

import iv8


on_only = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://www.nmpa.gov.cn/datasearch/home-index.html"
HTML = ('<html><head><meta id="m" content="seed">'
        '<script type="text/plain" src="/asset.js"></script></head><body></body></html>'
        '<script type="text/plain">probe();</script>')
CASES = [('DOM-01a', 'meta.content reflection', "document.getElementById('m').content", 'seed'), ('DOM-01b', 'meta content setter reflection', "(()=>{const m=document.getElementById('m');m.content='changed';return m.getAttribute('content')})()", 'changed'), ('URL-01', 'location.port', 'location.port', ''), ('URL-02a', 'anchor default protocol', "document.createElement('a').protocol", ':'), ('URL-02b', 'anchor href URL resolution', "(()=>{const a=document.createElement('a');a.href='/probe?q=1#x';return a.href})()", 'https://www.nmpa.gov.cn/probe?q=1#x'), ('URL-02c', 'anchor href attribute reflection', "(()=>{const a=document.createElement('a');a.href='/probe';return a.getAttribute('href')})()", '/probe'), ('DOM-02a', 'script.src', "document.getElementsByTagName('script')[0].src", 'https://www.nmpa.gov.cn/asset.js'), ('DOM-02b', 'script.innerText', "document.getElementsByTagName('script')[1].innerText", 'probe();'), ('DOM-03', 'trailing script parent', "document.getElementsByTagName('script')[1].parentElement?.tagName", 'BODY'), ('DOM-04a', 'node identity', "document.getElementById('m')===document.getElementById('m')", True), ('DOM-04b', 'expando survives node lookup', "(()=>{document.getElementById('m').testValue=1;return document.getElementById('m').testValue})()", 1), ('DOM-05a', 'collection type', "Object.prototype.toString.call(document.getElementsByTagName('script'))", '[object HTMLCollection]'), ('DOM-05b', 'live collection', "(()=>{const d=document.createElement('div');const c=d.getElementsByTagName('i');d.appendChild(document.createElement('i'));return c.length})()", 1), ('DOM-06a', 'document.appendChild', 'typeof document.appendChild', 'function'), ('DOM-06b', 'document.removeChild', 'typeof document.removeChild', 'function'), ('IDL-01a', 'form.action accessor on prototype', "typeof Object.getOwnPropertyDescriptor(Object.getPrototypeOf(document.createElement('form')),'action')?.get", 'function'), ('IDL-01b', 'form.action not own property', "Object.hasOwn(document.createElement('form'),'action')", False), ('IDL-02', 'navigator.webdriver accessor on prototype', "typeof Object.getOwnPropertyDescriptor(Object.getPrototypeOf(navigator),'webdriver')?.get", 'function'), ('NAV-01', 'location.replace exists', 'typeof location.replace', 'function'), ('SURFACE-01', 'style object', 'Object.prototype.toString.call(document.documentElement.style)', '[object CSSStyleDeclaration]'), ('SURFACE-02', 'canvas.getContext', "typeof document.createElement('canvas').getContext", 'function'), ('SURFACE-03', 'navigator.sendBeacon', 'typeof navigator.sendBeacon', 'function'), ('SURFACE-04', 'navigator.getBattery', 'typeof navigator.getBattery', 'function'), ('LEGACY-01', 'document.all exotic identity', 'document.all===undefined', False), ('CONTROL-01', 'documentMode remains absent', 'typeof document.documentMode', 'undefined'), ('CONTROL-02', 'navigator.battery remains absent', 'typeof navigator.battery', 'undefined'), ('CONTROL-03', 'connection.type remains absent', 'typeof navigator.connection.type', 'undefined')]


def evaluate(page, expression):
    return json.loads(page.eval("JSON.stringify(" + expression + ")"))


@on_only
@pytest.mark.parametrize("issue,name,expression,expected", CASES, ids=[case[0] for case in CASES])
def test_reported_browser_expression(issue, name, expression, expected):
    with iv8.Page() as page:
        page.load(HTML, BASE)
        actual = evaluate(page, "({value:(" + expression + ")})").get("value", "<undefined>")
        assert type(actual) is type(expected)
        assert actual == expected, name


@on_only
def test_reflection_identity_mutation_and_coercion():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert evaluate(page, """(() => {
          const meta = document.getElementById('m');
          meta.content = 42; meta.httpEquiv = 'refresh';
          meta.marker = 7;
          const again = document.querySelector('#m');
          const identity = meta === again && meta.parentElement === document.head;
          meta.setAttribute('content', 'new');
          const reflected = meta.content;
          meta.removeAttribute('content');
          let threw = false;
          try { meta.content = {toString() {throw Error('coercion')}}; } catch {threw = true;}
          return [identity, again.marker, reflected, meta.content,
                  meta.getAttribute('http-equiv'), threw];
        })()""") == [True, 7, "new", "", "refresh", True]


@on_only
def test_script_reflection_and_dom_wrapper_identity():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert evaluate(page, """(() => {
          const script = document.getElementsByTagName('script')[0];
          script.src = '../next.js'; script.type = 'application/json';
          script.text = 'value();';
          const text = script.firstChild;
          return [script.src, script.getAttribute('src'), script.type,
            script.text, script.innerText, text === script.firstChild,
            text.parentNode === script,
            script === document.head.getElementsByTagName('script')[0]];
        })()""") == ["https://www.nmpa.gov.cn/next.js", "../next.js", "application/json",
                     "value();", "value();", True, True, True]


@on_only
@pytest.mark.parametrize("html", [
    '<html><head></head><body></body></html><script type="text/plain">x</script>',
    '<html><head></head></html><script type="text/plain">x</script>',
    '<html><body></body><script type="text/plain">x</script></html>',
])
def test_trailing_document_content_belongs_to_body(html):
    with iv8.Page() as page:
        page.load(html, BASE)
        assert page.eval("document.getElementsByTagName('script')[0].parentElement === document.body")


@on_only
def test_idl_accessors_are_shared_brand_checked_and_configurable():
    with iv8.Page() as page:
        assert evaluate(page, """(() => {
          const first = document.createElement('form'), second = document.createElement('form');
          const prototype = Object.getPrototypeOf(first);
          const action = Object.getOwnPropertyDescriptor(prototype, 'action');
          const webdriver = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(navigator), 'webdriver');
          let failures = 0;
          for (const fn of [() => action.get.call({}), () => action.set.call({}, 'x'),
                           () => action.get.call(document.createElement('a')),
                           () => webdriver.get.call({})]) {
            try {fn();} catch (error) {if (error instanceof TypeError) failures++;}
          }
          action.set.call(first, 'x');
          return [prototype === Object.getPrototypeOf(second), typeof action.get,
            typeof action.set, action.enumerable, action.configurable,
            Object.hasOwn(first, 'action'), action.get.call(first), failures,
            typeof webdriver.set, webdriver.configurable];
        })()""") == [True, "function", "function", True, True, False, "x", 4, "undefined", True]


@on_only
def test_live_collections_track_changes_and_snapshot_queries_do_not():
    with iv8.Page() as page:
        page.load('<html><body><div id="root" class="x"><i id="one" class="x"></i></div></body></html>', BASE)
        assert evaluate(page, """(() => {
          const root = document.getElementById('root');
          const tags = root.getElementsByTagName('i'), classes = document.getElementsByClassName('x');
          const snapshot = root.querySelectorAll('i');
          const second = document.createElement('i'); second.id = 'two'; second.className = 'x';
          root.appendChild(second);
          const before = [tags.length, classes.length, snapshot.length,
            tags.item(1) === second, tags.namedItem('two') === second, tags.two === second,
            Array.from(tags).map(node => node.id).join(','), Array.isArray(tags)];
          second.remove(); root.className = '';
          return [...before, tags.length, classes.length, tags.item(9), tags[9] === undefined,
            root.getElementsByTagName('div').length];
        })()""") == [2, 3, 1, True, True, True, "one,two", False, 1, 1, None, True, 0]


@on_only
def test_document_all_is_undetectable_live_and_callable():
    with iv8.Page() as page:
        page.load('<html><body><div id="one"></div></body></html>', BASE)
        assert evaluate(page, """(() => {
          const all = document.all, before = all.length;
          const element = document.createElement('p'); element.id = 'two'; document.body.appendChild(element);
          return [typeof all, Boolean(all), all == null, all === undefined,
            'all' in document, all === document.all, Object.prototype.toString.call(all),
            all.length - before, all.namedItem('two') === element, all('two') === element];
        })()""") == ["undefined", False, True, False, True, True, "[object HTMLAllCollection]", 1, True, True]


@on_only
def test_anchor_components_resolve_reflect_and_coordinate():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert evaluate(page, """(() => {
          const anchor = document.createElement('a');
          anchor.href = 'HTTPS://EXAMPLE.TEST:443/a/../b?q=1#x';
          const initial = [anchor.href, anchor.hostname, anchor.port, anchor.pathname, anchor.origin];
          anchor.port = '8443'; anchor.pathname = '/new'; anchor.search = 'n=2'; anchor.hash = 'end';
          const changed = anchor.href;
          anchor.setAttribute('href', '/other');
          return [...initial, changed, anchor.href, anchor.getAttribute('href')];
        })()""") == ["https://example.test/b?q=1#x", "example.test", "", "/b", "https://example.test",
                     "https://example.test:8443/new?n=2#end", "https://www.nmpa.gov.cn/other", "/other"]


@on_only
@pytest.mark.parametrize("url,port,hostname", [
    ("https://example.test:8443/x", "8443", "example.test"),
    ("https://[::1]:9443/x", "9443", "[::1]"),
    ("https://example.test:443/x", "", "example.test"),
])
def test_location_port_and_anchor_ipv6(url, port, hostname):
    with iv8.Page() as page:
        page.load('<html></html>', url)
        assert page.eval("location.port") == port
        assert evaluate(page, "(() => {const a=document.createElement('a');a.href=" + json.dumps(url) + ";return [a.port,a.hostname]})()") == [port, hostname]


@on_only
def test_navigation_requests_are_observable_without_loading_or_network():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        page.eval("location.replace('/next?q=1#x'); location.assign('../other'); location.reload()", name="navigation.js")
        report = page.read_navigation_requests(clear=False)
        assert [entry["operation"] for entry in report["requests"]] == ["replace", "assign", "reload"]
        assert [entry["url"] for entry in report["requests"]] == [
            "https://www.nmpa.gov.cn/next?q=1#x", "https://www.nmpa.gov.cn/other", BASE]
        assert all(entry["resource_name"] == "navigation.js" for entry in report["requests"])
        assert page.eval("location.href") == BASE
        assert page.eval("document.getElementById('m').content") == "seed"
        assert page.read_navigation_requests() == report
        assert page.read_navigation_requests()["requests"] == []


@on_only
def test_document_tree_edits_validate_before_mutating():
    with iv8.Page() as page:
        page.load('<html><body></body></html>', BASE)
        assert evaluate(page, """(() => {
          const html = document.documentElement;
          const failures = [];
          for (const fn of [() => document.appendChild(document.createTextNode('x')),
                           () => document.appendChild(document.createElement('p')),
                           () => document.removeChild(document.createElement('p'))]) {
            try { fn(); } catch(error) { failures.push(error.name); }
          }
          const removed = document.removeChild(html);
          const added = document.appendChild(html);
          return [...failures, removed === html, added === html, document.documentElement === html];
        })()""") == ["HierarchyRequestError", "HierarchyRequestError", "NotFoundError", True, True, True]


@on_only
def test_inline_style_has_identity_reflection_priority_and_methods():
    with iv8.Page() as page:
        page.load('<html><body><div id="d" style="color: red"></div></body></html>', BASE)
        assert evaluate(page, """(() => {
          const element = document.getElementById('d'), style = element.style;
          style.backgroundColor = 'blue'; style.setProperty('width', '20px', 'important');
          const before = [style === element.style, Object.prototype.toString.call(style),
            style.color, style.backgroundColor, style.getPropertyPriority('width'), style.length,
            element.getAttribute('style').includes('width: 20px !important')];
          element.setAttribute('style', 'height: 30px');
          const height = style.height; const removed = style.removeProperty('height');
          element.style = 'color: green';
          return [...before, height, removed, style.color, style.cssText];
        })()""") == [True, "[object CSSStyleDeclaration]", "red", "blue", "important", 3, True,
                     "30px", "30px", "green", "color: green;"]


@on_only
def test_canvas_explicitly_cannot_allocate_a_rendering_context():
    with iv8.Page() as page:
        assert evaluate(page, """(() => {
          const canvas = document.createElement('canvas'); canvas.width = 20;
          return [canvas.width, canvas.height, canvas.getAttribute('width'), canvas.getContext('2d'),
            canvas.getContext('webgl'), canvas.getContext('unknown')];
        })()""") == [20, 150, "20", None, None, None]


@on_only
def test_beacon_refuses_transmission_and_checks_receiver_and_scheme():
    with iv8.Page() as page:
        assert page.eval("navigator.sendBeacon('/collect', 'data')") is False
        for expression in ("navigator.sendBeacon('ftp://example.test')", "navigator.sendBeacon.call({}, '/x')", "navigator.sendBeacon()"):
            with pytest.raises(iv8.JSError) as caught:
                page.eval(expression)
            assert caught.value.name == "TypeError"
        assert page.read_navigation_requests()["requests"] == []


@on_only
def test_battery_and_deprecated_quota_use_jobs_and_defined_baselines():
    with iv8.Page() as page:
        page.eval("globalThis.battery = null; globalThis.usage = null; globalThis.quotaError = null; navigator.getBattery().then(value => battery = value); navigator.webkitPersistentStorage.queryUsageAndQuota((used, quota) => usage=[used,quota]); navigator.webkitPersistentStorage.requestQuota(1, () => {}, error => quotaError=error.name)")
        assert page.eval("battery === null")
        page.run_jobs()
        assert evaluate(page, "[battery.charging, battery.level, battery.chargingTime, battery.dischargingTime === Infinity, Object.prototype.toString.call(battery), usage, quotaError]") == [True, 1, 0, True, "[object BatteryManager]", [0, 0], "NotSupportedError"]


@on_only
def test_async_errors_preserve_timer_and_listener_progress_and_source():
    with iv8.Page() as page:
        page.capture_async_errors()
        page.eval("globalThis.progress=0; setTimeout(() => {throw new RangeError('timer failure')}); setTimeout(() => progress++); document.addEventListener('probe', () => {throw new TypeError('listener failure')}); document.addEventListener('probe', () => progress++); document.dispatchEvent(new Event('probe'));", name="async-probe.js")
        page.run_timers()
        assert page.eval("progress") == 2
        errors = page.read_async_errors(clear=False)["errors"]
        assert [(entry["kind"], entry["name"]) for entry in errors] == [("listener", "TypeError"), ("timer", "RangeError")]
        assert all("async-probe.js" in entry["stack"] for entry in errors)
        assert page.read_async_errors()["errors"] == errors
        assert page.read_async_errors()["errors"] == []


@on_only
def test_async_capture_is_optional_and_handled_rejections_are_removed():
    with iv8.Page() as page:
        page.eval("setTimeout(() => {throw Error('ignored')})")
        page.run_timers()
        assert page.read_async_errors()["errors"] == []
        page.capture_async_errors()
        page.eval("globalThis.rejected = Promise.reject(new Error('later handled'))")
        assert page.read_async_errors(clear=False)["errors"][0]["kind"] == "promise"
        page.eval("rejected.catch(() => {})")
        page.run_jobs()
        assert page.read_async_errors()["errors"] == []
        page.load('<html></html>', BASE)
        page.eval("setTimeout(() => {throw Error('after load')})")
        page.run_timers()
        assert len(page.read_async_errors()["errors"]) == 1
