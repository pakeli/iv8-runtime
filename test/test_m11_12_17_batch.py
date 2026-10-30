"""M11-12..M11-17 batch acceptance tests.

M11-12 attribute API, M11-13 tree editing conveniences, M11-14 clone/equality,
M11-15 URL.searchParams snapshot, M11-16 select extension, M11-17 nested
environment + get_defaults.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-batch2.test/"
DOC = (
    "<html><body>"
    "<div id='root' class='a b' data-x='1'><span id='inner'></span></div>"
    "<select id='sel'><option value='1'>One</option>"
    "<option value='2' selected>Two</option>"
    "<option value='3'>Three</option></select>"
    "</body></html>"
)


def _setup(page):
    page.load(DOC, BASE)
    page.eval("""
        globalThis.root = document.getElementById('root');
        globalThis.inner = document.getElementById('inner');
        globalThis.sel = document.getElementById('sel');
    """)


# --- M11-12: element attribute API -------------------------------------------

@on_only
def test_attribute_api():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("root.hasAttributes()") is True
        assert page.eval("root.getAttributeNames().join(',')") == (
            "id,class,data-x")
        assert page.eval("""
            (function () {
                globalThis.log = [];
                var observer = new MutationObserver(function (records) {
                    for (var i = 0; i < records.length; i++) {
                        log.push(records[i].attributeName + ':' +
                                 String(records[i].oldValue));
                    }
                });
                observer.observe(root, {attributes: true, attributeOldValue: true});
                root.id = 'new-id';
                root.className = 'x y';
                globalThis.observer = observer;
                return [root.id, root.getAttribute('id'), root.className,
                        document.getElementById('new-id').isSameNode(root),
                        root.matches('.x'), root.classList.length].join('|');
            })()
        """) == "new-id|new-id|x y|true|true|2"
        page.run_jobs()
        assert page.eval("log.join(',')") == "id:root,class:a b"
        assert page.eval(
            "document.createElement('div').hasAttributes()") is False
        assert page.eval(
            "document.createElement('div').getAttributeNames().length") == 0


# --- M11-13: tree editing conveniences ----------------------------------------

@on_only
def test_tree_editing_conveniences():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("root.hasChildNodes()") is True
        assert page.eval("root.firstChild.id") == "inner"
        assert page.eval("root.lastChild.id") == "inner"
        assert page.eval("inner.previousSibling === null") is True
        assert page.eval("inner.nextSibling === null") is True
        assert page.eval("""
            (function () {
                var replacement = document.createElement('b');
                replacement.id = 'replacement';
                var removed = root.replaceChild(replacement, inner);
                var first = [removed.id, root.firstChild.id,
                             document.getElementById('inner') === null,
                             root.hasChildNodes()].join('|');
                replacement.remove();
                return [first, root.hasChildNodes(),
                        replacement.isConnected].join('|');
            })()
        """) == "inner|replacement|true|true|false|false"


# --- M11-14: cloneNode / equality --------------------------------------------

@on_only
def test_clone_node_and_equality():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var copy = root.cloneNode(true);
                var shallow = root.cloneNode(false);
                var same = root.isSameNode(root);
                var equal = root.isEqualNode(copy);
                return [copy.firstChild.id, copy.isConnected,
                        equal, shallow.hasChildNodes(),
                        root.isSameNode(copy), root.isEqualNode(shallow),
                        document.querySelectorAll('#inner').length].join('|');
            })()
        """) == "inner|false|true|false|false|false|1"
        assert page.eval("""
            (function () {
                var copy = root.cloneNode(true);
                copy.className = 'changed';
                return [root.className, copy.className,
                        root.isEqualNode(copy)].join('|');
            })()
        """) == "a b|changed|false"


# --- M11-15: URL.searchParams snapshot ---------------------------------------

@on_only
def test_url_search_params_snapshot():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var url = new URL('https://example.com/p?x=1&y=2#h');
                var params = url.searchParams;
                var before = [params.get('x'), params.get('y'), url.search]
                    .join('|');
                params.set('x', '9');
                return [before, params.get('x'), url.search].join('|');
            })()
        """) == "1|2|?x=1&y=2|9|?x=1&y=2"


# --- M11-16: select extension ------------------------------------------------

@on_only
def test_select_extension():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("sel.options.length") == 3
        assert page.eval("sel.options[0].value") == "1"
        assert page.eval("sel.selectedIndex") == 1
        assert page.eval("sel.value") == "2"
        assert page.eval("""
            (function () {
                sel.selectedIndex = 2;
                var high = [sel.value, sel.selectedIndex,
                            sel.options[2].selected,
                            sel.options[0].selected].join('|');
                sel.selectedIndex = -1;
                var none = [sel.value, sel.selectedIndex,
                            sel.options[2].selected].join('|');
                sel.selectedIndex = 99;  // out of range: ignored
                return [high, none, sel.selectedIndex].join('|');
            })()
        """) == "3|2|true|false||-1|false|-1"
        assert page.eval("sel.multiple") is False
        assert page.eval("sel.size") == 0
        page.eval("sel.setAttribute('multiple', ''); sel.setAttribute('size', '4');")
        assert page.eval("sel.multiple") is True
        assert page.eval("sel.size") == 4


# --- M11-17: nested environment + get_defaults --------------------------------

def test_nested_environment_defaults_shape():
    defaults = iv8.Page.get_defaults()
    assert set(defaults) == {"navigator", "screen", "window"}
    assert set(defaults["navigator"]) == {
        "userAgent", "platform", "language", "webdriver", "languages",
        "onLine", "cookieEnabled", "hardwareConcurrency", "appName",
        "appCodeName", "product", "vendor", "vendorSub", "productSub",
        "maxTouchPoints", "pdfViewerEnabled", "doNotTrack",
        "connectionEffectiveType", "connectionRtt", "connectionDownlink",
        "connectionSaveData"}
    assert set(defaults["screen"]) == {
        "width", "height", "availWidth", "availHeight", "availLeft",
        "availTop", "colorDepth", "pixelDepth"}
    assert set(defaults["window"]) == {
        "devicePixelRatio", "innerWidth", "innerHeight", "outerWidth",
        "outerHeight", "screenX", "screenY"}
    defaults["navigator"]["userAgent"] = "mutated"
    defaults["navigator"]["languages"].append("mutated")
    assert iv8.Page.get_defaults()["navigator"]["userAgent"] != "mutated"
    assert "mutated" not in iv8.Page.get_defaults()["navigator"]["languages"]
    assert "userAgent" in iv8.Page.get_default_environment()


@on_only
def test_nested_environment_applied():
    environment = {
        "navigator": {"userAgent": "nested-UA", "language": "fr",
                      "languages": ["fr-FR", "fr"], "onLine": False,
                      "hardwareConcurrency": 6, "appName": "NestedApp"},
        "screen": {"width": 800, "height": 600, "colorDepth": 30},
        "window": {"devicePixelRatio": 2, "innerWidth": 640,
                   "outerWidth": 900, "screenX": 5},
    }
    with iv8.Page(environment=environment) as page:
        page.load("<html><body></body></html>", BASE)
        assert page.eval("navigator.userAgent") == "nested-UA"
        assert page.eval("navigator.language") == "fr"
        assert page.eval("navigator.languages.join(',')") == "fr-FR,fr"
        assert page.eval("navigator.onLine") is False
        assert page.eval("navigator.hardwareConcurrency") == 6
        assert page.eval("navigator.appName") == "NestedApp"
        assert page.eval("navigator.platform") == "iv8"  # default inherited
        assert page.eval("screen.width") == 800
        assert page.eval("screen.colorDepth") == 30
        assert page.eval("screen.availHeight") == 1040  # default inherited
        assert page.eval("window.innerWidth") == 640
        assert page.eval("window.outerWidth") == 900
        assert page.eval("window.screenX") == 5
        assert page.eval("window.devicePixelRatio") == 2


def test_nested_environment_validation():
    with pytest.raises(TypeError):
        iv8.Page(environment={"navigator": {}, "userAgent": "x"})
    with pytest.raises(TypeError):
        iv8.Page(environment={"navigator": {"unknown": "x"}})
    with pytest.raises(TypeError):
        iv8.Page(environment={"screen": "not-a-mapping"})
    with pytest.raises(TypeError):
        iv8.Page(environment={"navigator": {"webdriver": "yes"}})
    with pytest.raises(TypeError):
        iv8.Page(environment={"navigator": {"languages": []}})
