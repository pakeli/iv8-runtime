"""M11-18 acceptance tests: form-control attribute reflection.

The reflected surface is deliberately small: input.type/disabled/readOnly/
placeholder, button.type/disabled, form.encoding (an enctype alias), and
option.label/index. Property writes are real attribute writes and feed the
MutationObserver attribute hooks.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-18.test/"
DOC = (
    "<html><body>"
    "<form id='f' enctype='text/plain'>"
    "<input id='plain'>"
    "<input id='mail' type='EMAIL'>"
    "<input id='odd' type='weird' disabled readonly placeholder='hint'>"
    "<button id='btn' type='RESET' disabled>Go</button>"
    "<select id='sel'>"
    "<option id='o0'>Zero</option>"
    "<option id='o1' label='One'>Uno</option>"
    "<option id='o2'>Two</option>"
    "</select>"
    "</form>"
    "<div id='dv'></div>"
    "</body></html>"
)


def _setup(page):
    page.load(DOC, BASE)
    page.eval("""
        globalThis.f = document.getElementById('f');
        globalThis.plain = document.getElementById('plain');
        globalThis.mail = document.getElementById('mail');
        globalThis.odd = document.getElementById('odd');
        globalThis.btn = document.getElementById('btn');
        globalThis.sel = document.getElementById('sel');
        globalThis.o0 = document.getElementById('o0');
        globalThis.o1 = document.getElementById('o1');
        globalThis.o2 = document.getElementById('o2');
        globalThis.dv = document.getElementById('dv');
    """)


# --- input.type ---------------------------------------------------------------

@on_only
def test_input_type_default_and_normalization():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("plain.type") == "text"
        assert page.eval("document.createElement('input').type") == "text"
        assert page.eval("mail.type") == "email"       # lower-cased known value
        assert page.eval("odd.type") == "text"         # unknown -> default
        assert page.eval("mail.getAttribute('type')") == "EMAIL"
        assert page.eval("""
            (() => {
                plain.type = 'TEL';
                return [plain.type, plain.getAttribute('type')].join('|');
            })()
        """) == "tel|TEL"
        # The property writes the raw string; the getter normalizes it.
        assert page.eval("(plain.type = 'nope', plain.type)") == "text"
        assert page.eval("plain.hasAttribute('type')") is True


# --- boolean + string reflection on input ------------------------------------

@on_only
def test_input_boolean_and_string_reflection():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("odd.disabled") is True
        assert page.eval("odd.readOnly") is True
        assert page.eval("odd.placeholder") == "hint"
        assert page.eval("plain.disabled") is False
        assert page.eval("plain.readOnly") is False
        assert page.eval("plain.placeholder") == ""
        assert page.eval("""
            (() => {
                odd.disabled = false;
                odd.readOnly = false;
                odd.placeholder = 'changed';
                return [odd.hasAttribute('disabled'),
                        odd.hasAttribute('readonly'),
                        odd.getAttribute('placeholder')].join('|');
            })()
        """) == "false|false|changed"
        assert page.eval("""
            (() => {
                plain.disabled = true;
                plain.readOnly = 1;
                return [plain.getAttribute('disabled'),
                        plain.getAttribute('readonly'),
                        plain.disabled, plain.readOnly].join('|');
            })()
        """) == "||true|true"
        # A falsey write removes only an existing boolean attribute.
        assert page.eval("(plain.disabled = false, plain.hasAttribute('disabled'))") is False


# --- button.type / disabled --------------------------------------------------

@on_only
def test_button_type_and_disabled():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("btn.type") == "reset"
        assert page.eval("btn.disabled") is True
        assert page.eval("document.createElement('button').type") == "submit"
        assert page.eval("""
            (() => {
                btn.type = 'menu';
                btn.disabled = false;
                return [btn.type, btn.getAttribute('type'),
                        btn.disabled, btn.hasAttribute('disabled')].join('|');
            })()
        """) == "submit|menu|false|false"
        assert page.eval("(btn.type = 'BUTTON', btn.type)") == "button"


# --- form.encoding alias -----------------------------------------------------

@on_only
def test_form_encoding_alias():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("f.enctype") == "text/plain"
        assert page.eval("f.encoding") == "text/plain"
        assert page.eval("""
            (() => {
                f.encoding = 'multipart/form-data';
                return [f.enctype, f.encoding, f.getAttribute('enctype')].join('|');
            })()
        """) == "multipart/form-data|multipart/form-data|text/plain"
        assert page.eval("""
            (() => {
                f.enctype = 'nope';
                return [f.enctype, f.encoding].join('|');
            })()
        """) == "application/x-www-form-urlencoded|application/x-www-form-urlencoded"
        assert page.eval("document.createElement('form').encoding") == (
            "application/x-www-form-urlencoded")


# --- option.label / index ----------------------------------------------------

@on_only
def test_option_label_and_index():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("o0.label") == "Zero"         # falls back to text
        assert page.eval("o1.label") == "One"          # label attribute wins
        assert page.eval("o0.index") == 0
        assert page.eval("o1.index") == 1
        assert page.eval("o2.index") == 2
        assert page.eval("document.createElement('option').index") == 0
        assert page.eval("""
            (() => {
                o0.label = 'Zero2';
                o1.label = '';
                return [o0.getAttribute('label'), o0.label,
                        o1.getAttribute('label'), o1.label].join('|');
            })()
        """) == "Zero2|Zero2||"  # an explicit empty label stays empty
        assert page.eval("""
            (() => {
                const detached = document.createElement('option');
                return detached.index;
            })()
        """) == 0


# --- MutationObserver integration --------------------------------------------

@on_only
def test_reflection_feeds_attribute_observer():
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
            plain.type = 'TEL';
            plain.disabled = true;
            plain.placeholder = 'x';
            o0.label = 'L';
            f.encoding = 'text/plain';
            plain.disabled = true;          // true again: still a setAttribute-style write
            plain.disabled = false;
            plain.disabled = false;         // absent -> no record
        """)
        page.run_jobs()
        assert page.eval("log.join(',')") == (
            "type:null,disabled:null,placeholder:null,label:null,"
            "disabled:,disabled:")


# --- shape / scope guards -----------------------------------------------------

@on_only
def test_reflection_surface_is_scoped():
    with iv8.Page() as page:
        _setup(page)
        for member in ("type", "disabled", "readOnly", "placeholder",
                       "encoding", "label", "index"):
            assert page.eval(f"typeof dv.{member}") == "undefined"
        assert page.eval("typeof plain.encoding") == "undefined"
        assert page.eval("typeof btn.readOnly") == "undefined"
        assert page.eval("typeof btn.placeholder") == "undefined"
        assert page.eval("typeof o0.disabled") == "undefined"
        # M11-21 later extends reflection to these controls.
        assert page.eval("typeof plain.name") != "undefined"
        assert page.eval("typeof plain.required") != "undefined"
        assert page.eval("typeof document.createElement('textarea').disabled") != "undefined"
        assert page.eval("typeof document.createElement('select').disabled") != "undefined"


def test_no_new_python_surface():
    for name in ("HTMLInputElement", "HTMLButtonElement", "HTMLSelectElement",
                 "HTMLOptionElement"):
        assert not hasattr(iv8, name)
    for attr in ("type", "disabled", "readOnly", "placeholder", "encoding",
                 "label", "index"):
        assert not hasattr(iv8.Page, attr)
