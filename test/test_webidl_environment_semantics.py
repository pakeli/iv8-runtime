"""Offline acceptance for the approved ENV-01 through ENV-06 repair."""

import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")


def evaluate(page, expression):
    return json.loads(page.eval("JSON.stringify(" + expression + ")"))


@pytest.mark.parametrize("expression,name,length", [
    ("document.createElement", "createElement", 1),
    ("document.getElementById", "getElementById", 1),
    ("document.getElementsByTagName", "getElementsByTagName", 1),
    ("document.addEventListener", "addEventListener", 2),
    ("document.createElement('div').removeChild", "removeChild", 1),
    ("document.createElement('div').getAttribute", "getAttribute", 1),
    ("location.replace", "replace", 1),
    ("localStorage.getItem", "getItem", 1),
    ("localStorage.setItem", "setItem", 2),
    ("window.setTimeout", "setTimeout", 1),
    ("window.addEventListener", "addEventListener", 2),
])
def test_native_method_metadata(expression, name, length):
    with iv8.Page() as page:
        result = evaluate(page, """(() => {
          const method = (%s);
          let construction;
          try { Reflect.construct(method, []); }
          catch (error) { construction = error.name; }
          return [method.name, method.length, Function.prototype.toString.call(method),
                  Object.getOwnPropertyNames(method).sort(), construction];
        })()""" % expression)
        assert result == [name, length, "function " + name + "() { [native code] }",
                          ["length", "name"], "TypeError"]


@pytest.mark.parametrize("expression", [
    "document.createElement.call({}, 'div')",
    "document.createElement.call(navigator, 'div')",
    "document.getElementsByTagName.call(location, 'div')",
    "document.createElement('div').getAttribute.call(document, 'id')",
    "localStorage.getItem.call({}, 'x')",
    "window.addEventListener.call({}, 'test', () => {})",
])
def test_native_method_brand(expression):
    with iv8.Page() as page:
        assert evaluate(page, """(() => {
          try { %s; return null; }
          catch (error) { return [error.name, error.message]; }
        })()""" % expression) == ["TypeError", "Illegal invocation"]


def test_borrowed_methods_use_actual_receiver():
    with iv8.Page() as page:
        assert evaluate(page, """(() => {
          const div = document.createElement('div');
          const form = document.createElement('form');
          form.id = 'form';
          localStorage.setItem('x', 'local');
          sessionStorage.setItem('x', 'session');
          let count = 0;
          window.addEventListener.call(div, 'test', () => count++);
          div.dispatchEvent(new Event('test'));
          return [div.getAttribute.call(form, 'id'),
                  localStorage.getItem.call(sessionStorage, 'x'), count,
                  div.getAttribute === div.getAttribute,
                  document.createElement === document.createElement];
        })()""") == ["form", "session", 1, True, True]


@pytest.fixture(scope="module")
def chrome_probe():
    fixtures = Path(__file__).parent / "fixtures" / "webidl_environment"
    expected = json.loads((fixtures / "chrome.json").read_text())
    source = (fixtures / "probe.js").read_text()
    with iv8.Page() as page:
        page.load("<html><head></head><body></body></html>", "https://idl.test/")
        actual = evaluate(page, "(" + source + ")()")
    return expected, actual


@pytest.mark.parametrize("key", [
    *[name + suffix for name in ("window", "document", "navigator", "screen", "location",
        "div", "form", "script", "meta") for suffix in (".tag", ".constructor", ".chain")],
    *["global." + name for name in ("Window", "Document", "HTMLDocument", "Navigator", "Screen",
        "Location", "EventTarget", "Node", "Element", "HTMLElement", "HTMLDivElement", "HTMLFormElement",
        "HTMLScriptElement", "HTMLMetaElement", "XMLHttpRequest", "Worker", "fetch", "open")],
    *["method." + name for name in ("createElement", "getElementById", "getElementsByTagName",
        "addEventListener", "removeChild", "getAttribute", "replace", "storageGet")],
    *["descriptor." + name for name in ("document.cookie", "document.readyState", "document.referrer",
        "navigator.userAgent", "navigator.webdriver", "div.id", "form.action", "script.src", "meta.content", "location.href")],
    "keys.document", "keys.navigator", "ownNames.document", "brand.createElement.wrongThis",
    "brand.getElementById.wrongThis", "brand.getAttribute.wrongThis", "brand.getElementsByTagName.wrongThis",
    "construct.createElement", "construct.getAttribute", "storage.get.identity", "document.method.identity", "node.method.identity",
])
def test_chrome_environment_probe(chrome_probe, key):
    expected, actual = chrome_probe
    if key == "global.open":
        pytest.xfail("Window opening is unavailable in the offline Page profile")
    assert actual[key] == expected[key]


@pytest.mark.parametrize("name", ["Window", "HTMLDocument", "XMLDocument", "Navigator", "Screen",
    "Location", "Node", "Element", "HTMLElement", "HTMLDivElement", "HTMLFormElement", "HTMLScriptElement", "HTMLMetaElement"])
def test_illegal_interface_constructors(name):
    with iv8.Page() as page:
        assert evaluate(page, """(() => {
          try { new %s(); return null; }
          catch (error) { return [error.name, error.message]; }
        })()""" % name) == ["TypeError", "Failed to construct '" + name + "': Illegal constructor"]


def test_standalone_document_and_node_constructors():
    with iv8.Page() as page:
        assert evaluate(page, """(() => {
          const doc = new Document(), root = doc.createElement('Root');
          doc.appendChild(root);
          const text = doc.createTextNode('content'); root.appendChild(text);
          const clone = doc.cloneNode(true);
          const direct = new Text('a\\0b');
          return [Object.prototype.toString.call(doc), doc.constructor.name,
            doc.contentType, doc.URL, doc.baseURI, doc.defaultView, doc.location,
            doc.ownerDocument, root.tagName, root.constructor.name, root.ownerDocument === doc,
            root.parentNode === doc, doc.documentElement === root, doc.contains(text),
            clone.isEqualNode(doc), clone.documentElement.ownerDocument === clone,
            direct.data, direct.ownerDocument === document,
            new Comment('note').data, new DocumentFragment().nodeType,
            new Text(undefined).data, new Comment(undefined).data];
        })()""") == ["[object Document]", "Document", "application/xml", "about:blank", "about:blank",
            None, None, None, "Root", "Element", True, True, True, True, True, True,
            "a\0b", True, "note", 11, "", ""]


def test_event_target_state_inheritance_and_native_brands():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", "https://idl.test/")
        assert evaluate(page, """(() => {
          class CustomTarget extends EventTarget {}
          const target = new CustomTarget(), text = document.createTextNode('x');
          document.body.appendChild(text);
          let calls = 0, bubbles = 0, screenCalls = 0;
          EventTarget.prototype.addEventListener.call(target, 'test', () => calls++);
          target.dispatchEvent(new Event('test'));
          document.addEventListener('text', () => bubbles++);
          text.dispatchEvent(new Event('text', {bubbles:true}));
          screen.addEventListener('screen', () => screenCalls++);
          screen.dispatchEvent(new Event('screen'));
          let forged;
          try { EventTarget.prototype.dispatchEvent.call(Object.create(EventTarget.prototype), new Event('test')); }
          catch (error) { forged = error.message; }
          return [target instanceof CustomTarget, target instanceof EventTarget, calls, bubbles,
            screenCalls, forged, document instanceof Node, document instanceof EventTarget,
            document.documentElement.parentNode === document, document.documentElement.parentElement];
        })()""") == [True, True, 1, 1, 1, "Illegal invocation", True, True, True, None]


def test_shared_methods_override_delete_and_reload():
    with iv8.Page() as page, iv8.Page() as other:
        assert evaluate(page, """(() => {
          const div = document.createElement('div'), form = document.createElement('form');
          const original = Element.prototype.getAttribute;
          const shared = div.getAttribute === form.getAttribute;
          div.getAttribute = () => 'shadow';
          const shadow = Object.hasOwn(div, 'getAttribute') && div.getAttribute() === 'shadow'
            && form.getAttribute === original;
          delete div.getAttribute;
          delete Element.prototype.getAttribute;
          const deleted = document.createElement('span').getAttribute === undefined;
          Element.prototype.getAttribute = original;
          return [shared, shadow, deleted, div.getAttribute === original,
                  div.appendChild === Node.prototype.appendChild,
                  localStorage.getItem === sessionStorage.getItem,
                  window.addEventListener === EventTarget.prototype.addEventListener];
        })()""") == [True] * 7
        page.eval("Element.prototype.getAttribute = () => 'changed'")
        assert other.eval("document.createElement('div').getAttribute('id') === null") is True
        page.load("<html><body></body></html>", "https://idl.test/")
        assert page.eval("document.createElement('div').getAttribute('id') === null") is True


def test_descriptor_queries_do_not_invoke_idl_getters():
    with iv8.Page() as page:
        page.start_access_trace()
        assert evaluate(page, """(() => {
          const own = Object.getOwnPropertyDescriptor(navigator, 'userAgent');
          const descriptor = Object.getOwnPropertyDescriptor(Navigator.prototype, 'userAgent');
          return [own === undefined, descriptor.get.name, descriptor.get.length,
            descriptor.enumerable, descriptor.configurable, Object.keys(navigator),
            Object.getOwnPropertyNames(document), Object.keys(document)];
        })()""") == [True, "get userAgent", 0, True, True, [], ["location"], ["location"]]
        events = page.read_access_trace()["events"]
        assert not [event for event in events if event["operation"] == "get"]


def test_unavailable_execution_is_explicit():
    with iv8.Page() as page:
        assert evaluate(page, """(() => {
          const failures = [];
          for (const create of [() => new XMLHttpRequest(), () => new Worker('worker.js')]) {
            try { create(); } catch (error) { failures.push([error.name, /offline/.test(error.message)]); }
          }
          globalThis.fetchResults = [];
          fetch('/resource').catch(error => fetchResults.push(error.name));
          fetch().catch(error => fetchResults.push(error.name));
          return [typeof XMLHttpRequest, typeof Worker, typeof fetch, typeof open,
            fetch.name, fetch.length, failures,
            Object.getPrototypeOf(Worker.prototype) === EventTarget.prototype];
        })()""") == ["function", "function", "function", "undefined", "fetch", 1,
            [["NotSupportedError", True], ["NotSupportedError", True]], True]
        page.run_jobs()
        assert evaluate(page, "fetchResults") == ["NotSupportedError", "TypeError"]


def test_location_assignments_preserve_identity_and_record_intents():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", "https://idl.test/path?q=1")
        assert evaluate(page, """(() => {
          const original = location;
          location.href = '/one'; window.location = '/two'; document.location = '/three';
          location.hash = 'hash';
          const d = Object.getOwnPropertyDescriptor(location, 'href');
          return [location === original, document.location === original, location.href,
            d.configurable, d.enumerable, d.get.name, d.set.name, delete location.href];
        })()""") == [True, True, "https://idl.test/path?q=1", False, True, "get href", "set href", False]
        requests = page.read_navigation_requests()["requests"]
        assert [request["url"] for request in requests] == ["https://idl.test/one", "https://idl.test/two",
            "https://idl.test/three", "https://idl.test/path?q=1#hash"]
        assert [request["operation"] for request in requests] == ["assign"] * 4


def test_inherited_node_edits_validate_before_mutation():
    with iv8.Page() as page:
        assert evaluate(page, """(() => {
          const text = document.createTextNode('x'), child = document.createElement('div');
          const foreign = new Document().createElement('root');
          const errors = [];
          for (const apply of [() => text.appendChild(child), () => child.appendChild(document),
              () => child.appendChild(foreign)]) {
            try { apply(); } catch (error) { errors.push(error.name); }
          }
          return [errors, text.childNodes.length, child.parentNode, foreign.parentNode];
        })()""") == [["HierarchyRequestError", "HierarchyRequestError", "WrongDocumentError"], 0, None, None]


def test_node_constants_and_tag_interface_selection():
    with iv8.Page() as page:
        assert evaluate(page, """(() => {
          const descriptor = Object.getOwnPropertyDescriptor(Node.prototype, 'DOCUMENT_NODE');
          return [Node.DOCUMENT_NODE, Node.ELEMENT_NODE, document.DOCUMENT_NODE,
            descriptor.value, descriptor.writable, descriptor.enumerable, descriptor.configurable,
            ...['section', 'x-thing', 'unknown', 'tbody', 'embed'].map(tag => document.createElement(tag).constructor.name)];
        })()""") == [9, 1, 9, 9, False, True, False, "HTMLElement", "HTMLElement",
                      "HTMLUnknownElement", "HTMLTableSectionElement", "HTMLEmbedElement"]


def test_document_root_mutations_preserve_constraints_and_identity():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", "https://idl.test/")
        assert evaluate(page, """(() => {
          const root = document.documentElement, other = document.createElement('other');
          const comment = document.createComment('before');
          root.before(comment, root);
          const valid = root.parentNode === document && document.firstChild === comment;
          const failures = [];
          for (const change of [() => root.before(other), () => root.replaceWith('text'),
              () => root.insertAdjacentElement('beforebegin', other),
              () => {root.outerHTML = '<div></div>';}]) {
            try { change(); } catch (error) { failures.push(error.name); }
          }
          return [valid, failures, root.parentNode === document, document.documentElement === root,
            other.parentNode, comment.parentNode === document];
        })()""") == [True, ["HierarchyRequestError", "HierarchyRequestError", "HierarchyRequestError",
            "NoModificationAllowedError"], True, True, None, True]


def test_variadic_mutation_converts_all_arguments_before_moving_nodes():
    with iv8.Page() as page:
        assert evaluate(page, """(() => {
          const parent = document.createElement('div'), child = document.createElement('span');
          parent.appendChild(child);
          const target = document.createElement('div');
          let failure;
          try { target.append(child, {toString() {throw Error('conversion');}}); }
          catch (error) {failure = error.message;}
          return [failure, child.parentNode === parent, target.childNodes.length,
            parent.firstChild === child];
        })()""") == ["conversion", True, 0, True]


def test_interface_state_tears_down_on_reload_and_disposal():
    page = iv8.Page()
    for _ in range(3):
        page.eval("""
          globalThis.detachedDocument = new Document();
          globalThis.target = new EventTarget();
          target.addEventListener('test', () => detachedDocument.title = 'called');
          target.dispatchEvent(new Event('test'));
        """)
        assert page.eval("detachedDocument.title") == "called"
        assert page.eval("typeof HTMLTableElement") == "function"
        page.load("<html><body></body></html>", "https://idl.test/")
        assert page.eval("typeof detachedDocument") == "undefined"
        assert page.eval("Element.prototype.hasAttributes === document.createElement('div').hasAttributes") is True
    page.dispose()
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.eval("new Document()")


def test_inherited_nullable_node_setters_keep_node_kind_semantics():
    with iv8.Page() as page:
        page.start_access_trace()
        assert evaluate(page, """(() => {
          const element = document.createElement('div'), text = document.createTextNode('x');
          element.appendChild(text);
          element.nodeValue = 'ignored';
          const kept = element.firstChild === text && element.nodeValue === null;
          text.nodeValue = null;
          const cleared = text.data;
          text.textContent = undefined;
          let failure;
          try { element.nodeValue = {toString() {throw Error('conversion');}}; }
          catch (error) {failure = error.message;}
          return [kept, cleared, text.data, failure, element.firstChild === text];
        })()""") == [True, "", "", "conversion", True]
        events = page.read_access_trace()["events"]
        node_value_sets = [event for event in events if event["object_type"] == "text"
                           and event["member"] == "nodeValue" and event["operation"] == "set"]
        assert node_value_sets[0]["value_summary"] == "null"


def test_document_node_mutations_reach_existing_observers():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", "https://idl.test/")
        page.eval("""
          globalThis.rootRecords = [];
          globalThis.rootObserver = new MutationObserver(records => {
            for (const record of records) rootRecords.push([record.target === document,
              record.addedNodes.length, record.removedNodes.length]);
          });
          rootObserver.observe(document, {childList:true});
          const marker = document.createComment('root');
          document.appendChild(marker);
          document.removeChild(marker);
        """)
        page.run_jobs()
        assert evaluate(page, "rootRecords") == [[True, 1, 0], [True, 0, 1]]


def test_unavailable_window_open_is_absent_from_the_profile():
    with iv8.Page() as page:
        assert evaluate(page, "[typeof open, typeof window.open, Object.hasOwn(window, 'open'), 'open' in window]") == ["undefined", "undefined", False, False]
        page.load("<html><body></body></html>", "https://idl.test/")
        assert page.eval("typeof open") == "undefined"
