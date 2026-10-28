"""M11-10 acceptance tests: element.classList and toggleAttribute."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://classlist.test/"
DOC = ("<html><body><div id='root' class='a b'>"
       "<span id='inner'></span></div></body></html>")


def _setup(page):
    page.load(DOC, BASE)
    page.eval("globalThis.root = document.getElementById('root');")


def test_no_new_python_surface():
    for name in ("DOMTokenList", "classList", "toggleAttribute"):
        assert not hasattr(iv8, name)
    for name in ("classList", "toggleAttribute"):
        assert not hasattr(iv8.Page, name)


@on_only
def test_shape_and_initial_tokens():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("typeof root.classList") == "object"
        assert page.eval("typeof document.classList") == "undefined"
        for name in ("contains", "add", "remove", "toggle", "replace", "item"):
            assert page.eval(f"typeof root.classList.{name}") == "function"
        assert page.eval("root.classList.length") == 2
        assert page.eval("root.classList.value") == "a b"
        assert page.eval("root.classList.item(0)") == "a"
        assert page.eval("root.classList.item(1)") == "b"
        assert page.eval("root.classList.item(2) === null") is True
        assert page.eval("root.classList.contains('a')") is True
        assert page.eval("root.classList.contains('c')") is False


@on_only
def test_add_and_dynamic_length():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var cl = root.classList;
                cl.add('c');
                cl.add('c');
                cl.add('d', 'e');
                return [cl.length, cl.value, root.className,
                        cl.contains('e'),
                        document.querySelectorAll('.c').length,
                        document.getElementsByClassName('d').length].join('|');
            })()
        """) == "5|a b c d e|a b c d e|true|1|1"


@on_only
def test_remove_and_empty_class_attribute():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var cl = root.classList;
                cl.remove('a');
                var mid = [root.className, cl.length].join(':');
                cl.remove('b');
                return [mid, root.className, root.hasAttribute('class'),
                        cl.length, document.querySelectorAll('.a').length]
                    .join('|');
            })()
        """) == "b:1||false|0|0"


@on_only
def test_toggle():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("root.classList.toggle('a')") is False
        assert page.eval("root.classList.contains('a')") is False
        assert page.eval("root.classList.toggle('a')") is True
        assert page.eval("root.classList.toggle('a', false)") is False
        assert page.eval("root.classList.toggle('a', true)") is True
        assert page.eval("root.classList.toggle('a', true)") is True
        assert page.eval("root.classList.contains('a')") is True


@on_only
def test_replace():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("root.classList.replace('a', 'x')") is True
        assert page.eval("root.className") == "x b"
        assert page.eval("root.classList.replace('missing', 'y')") is False
        assert page.eval("root.classList.replace('x', 'b')") is True
        assert page.eval("root.className") == "b"
        assert page.eval("root.classList.length") == 1


@on_only
def test_value_setter():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var cl = root.classList;
                cl.value = 'x  y z';
                var first = [root.className, cl.length, cl.item(0), cl.item(2)]
                    .join('|');
                cl.value = '';
                return [first, root.className, cl.length,
                        root.getAttribute('class')].join('|');
            })()
        """) == "x  y z|3|x|z||0|"


@on_only
def test_invalid_tokens_throw():
    with iv8.Page() as page:
        _setup(page)
        for expr in ("root.classList.add('')", "root.classList.add('a b')",
                     "root.classList.contains(' ')",
                     "root.classList.toggle('a\tb')"):
            with pytest.raises(iv8.JSError) as info:
                page.eval(expr)
            assert info.value.name == "TypeError"
        assert page.eval("1 + 1") == 2


@on_only
def test_toggle_attribute():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("root.toggleAttribute('disabled')") is True
        assert page.eval("root.hasAttribute('disabled')") is True
        assert page.eval("root.getAttribute('disabled')") == ""
        assert page.eval("root.toggleAttribute('disabled')") is False
        assert page.eval("root.hasAttribute('disabled')") is False
        assert page.eval("root.toggleAttribute('data-x', true)") is True
        assert page.eval("root.toggleAttribute('data-x', true)") is True
        assert page.eval("root.toggleAttribute('data-x', false)") is False
        assert page.eval("root.hasAttribute('data-x')") is False


@on_only
def test_toggle_attribute_id_and_class():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("root.toggleAttribute('id')") is False
        assert page.eval("root.hasAttribute('id')") is False
        assert page.eval("document.getElementById('root') === null") is True
        assert page.eval("root.toggleAttribute('id')") is True
        assert page.eval("root.getAttribute('id')") == ""
        assert page.eval("root.toggleAttribute('class')") is False
        assert page.eval("root.className") == ""
        assert page.eval("root.hasAttribute('class')") is False
        assert page.eval("root.classList.length") == 0


@on_only
def test_classlist_queues_attribute_records():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                globalThis.log = [];
                globalThis.o = new MutationObserver(function (records) {
                    for (var i = 0; i < records.length; i++) {
                        log.push(records[i].attributeName + ':' +
                                 String(records[i].oldValue));
                    }
                });
                o.observe(root, {attributes: true, attributeOldValue: true});
                root.classList.add('c');
                root.classList.remove('a');
                root.toggleAttribute('data-x');
                root.setAttribute('data-y', '1');
                return 'ok';
            })()
        """) == "ok"
        page.run_jobs()
        assert page.eval("log.join(',')") == "class:a b,class:a b c,data-x:null,data-y:null"


@on_only
def test_class_changes_update_queries():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                root.classList.add('c');
                var byClass = document.getElementsByClassName('c').length;
                var bySelector = document.querySelectorAll('.c').length;
                root.classList.remove('a');
                return [byClass, bySelector,
                        document.querySelectorAll('.a').length,
                        root.matches('.c')].join('|');
            })()
        """) == "1|1|0|true"


@on_only
def test_detached_element_class_list():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var el = document.createElement('div');
                el.classList.add('x');
                el.toggleAttribute('hidden');
                return [el.className, el.classList.contains('x'),
                        el.hasAttribute('hidden'),
                        document.querySelectorAll('.x').length].join('|');
            })()
        """) == "x|true|true|0"
