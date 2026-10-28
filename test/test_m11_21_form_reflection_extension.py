"""M11-21 acceptance tests: extended form-control reflection."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-21.test/"
DOC = """
<html><body>
<form id='f'>
  <input id='in' name='i' required>
  <textarea id='ta' name='t' required disabled readonly placeholder='p'></textarea>
  <select id='se' name='s' required disabled><option>x</option></select>
  <button id='bt' name='b' type='button'>B</button>
  <div id='dv'></div>
</form>
</body></html>
"""


def _setup(page):
    page.load(DOC, BASE)
    page.eval("""
        globalThis.f = document.getElementById('f');
        globalThis.inp = document.getElementById('in');
        globalThis.ta = document.getElementById('ta');
        globalThis.se = document.getElementById('se');
        globalThis.bt = document.getElementById('bt');
        globalThis.dv = document.getElementById('dv');
    """)


@on_only
def test_initial_reflection():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("inp.name") == "i"
        assert page.eval("inp.required") is True
        assert page.eval("ta.name") == "t"
        assert page.eval("ta.required") is True
        assert page.eval("ta.disabled") is True
        assert page.eval("ta.readOnly") is True
        assert page.eval("ta.placeholder") == "p"
        assert page.eval("se.name") == "s"
        assert page.eval("se.required") is True
        assert page.eval("se.disabled") is True
        assert page.eval("bt.name") == "b"


@on_only
def test_create_element_defaults():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("document.createElement('input').name") == ""
        assert page.eval("document.createElement('input').required") is False
        assert page.eval("document.createElement('textarea').name") == ""
        assert page.eval("document.createElement('textarea').required") is False
        assert page.eval("document.createElement('textarea').disabled") is False
        assert page.eval("document.createElement('textarea').readOnly") is False
        assert page.eval("document.createElement('textarea').placeholder") == ""
        assert page.eval("document.createElement('select').name") == ""
        assert page.eval("document.createElement('select').required") is False
        assert page.eval("document.createElement('select').disabled") is False
        assert page.eval("document.createElement('button').name") == ""


@on_only
def test_round_trip_with_attributes():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            inp.name = 'n';
            inp.required = false;
            ta.disabled = false;
            ta.readOnly = false;
            ta.placeholder = 'q';
            ta.name = 'u';
            ta.required = false;
            se.disabled = false;
            se.required = false;
            se.name = 'v';
            bt.name = 'w';
        """)
        assert page.eval("inp.getAttribute('name')") == "n"
        assert page.eval("inp.required") is False
        assert page.eval("inp.hasAttribute('required')") is False
        assert page.eval("ta.getAttribute('disabled') === null") is True
        assert page.eval("ta.getAttribute('readonly') === null") is True
        assert page.eval("ta.getAttribute('placeholder')") == "q"
        assert page.eval("ta.getAttribute('name')") == "u"
        assert page.eval("ta.required") is False
        assert page.eval("se.getAttribute('disabled') === null") is True
        assert page.eval("se.required") is False
        assert page.eval("se.getAttribute('name')") == "v"
        assert page.eval("bt.getAttribute('name')") == "w"
        # A truthy boolean write creates an empty attribute.
        page.eval("inp.required = 1; se.disabled = 'yes';")
        assert page.eval("inp.getAttribute('required')") == ""
        assert page.eval("se.getAttribute('disabled')") == ""
        assert page.eval("inp.required") is True
        assert page.eval("se.disabled") is True


@on_only
def test_reflection_feeds_observer():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.log = [];
            globalThis.obs = new MutationObserver(function (records) {
                for (var i = 0; i < records.length; i++) {
                    log.push(records[i].attributeName + ':' +
                             String(records[i].oldValue));
                }
            });
            obs.observe(f, {subtree: true, attributes: true,
                            attributeOldValue: true});
            inp.name = 'n';
            inp.required = false;
            ta.disabled = false;
            se.name = 'z';
            bt.name = 'w';
        """)
        page.run_jobs()
        assert page.eval("log.join(',')") == (
            "name:i,required:,disabled:,name:s,name:b")


@on_only
def test_reflection_surface_is_scoped():
    with iv8.Page() as page:
        _setup(page)
        for member in ("name", "required", "disabled", "readOnly",
                       "placeholder"):
            assert page.eval(f"typeof dv.{member}") == "undefined"
        assert page.eval("typeof bt.required") == "undefined"
        assert page.eval("typeof se.readOnly") == "undefined"
        assert page.eval("typeof bt.placeholder") == "undefined"
        assert page.eval("typeof inp.name") != "undefined"
        assert page.eval("typeof ta.name") != "undefined"
        assert page.eval("typeof se.name") != "undefined"
        assert page.eval("typeof bt.name") != "undefined"


def test_no_new_python_surface():
    for attr in ("name", "required"):
        assert not hasattr(iv8.Page, attr)
