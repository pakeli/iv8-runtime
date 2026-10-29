"""M11-32 acceptance tests: minimal form constraint-validation API."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-32.test/"


def _load(page, body=""):
    page.load(f"<html><body>{body}</body></html>", BASE)
    # This runtime has no id-to-global shortcut; expose the ids used by the tests.
    page.eval("""
        for (const id of ['f', 'i', 't', 's', 'b', 'd', 'c', 'e', 'u',
                          'p', 'n', 'a', 'h', 'r', 'rs', 'bt']) {
          const el = document.getElementById(id);
          if (el) globalThis[id] = el;
        }
    """)


# --- shape guards ---------------------------------------------------------------

def test_no_new_python_surface():
    for name in ("ValidityState", "HTMLInputElement", "HTMLFormElement"):
        assert not hasattr(iv8, name)
    for name in ("checkValidity", "reportValidity", "validity",
                 "willValidate", "setCustomValidity"):
        assert not hasattr(iv8.Page, name)


@on_only
def test_surface_shape_on_controls_and_form():
    _load_html = (
        "<form id='f'>"
        "<input id='i'><textarea id='t'></textarea>"
        "<select id='s'></select><button id='b'></button>"
        "</form><div id='d'></div>")
    with iv8.Page() as page:
        _load(page, _load_html)
        assert page.eval("typeof f.checkValidity") == "function"
        assert page.eval("typeof f.reportValidity") == "function"
        for name in ("willValidate", "validity", "validationMessage",
                     "setCustomValidity"):
            assert page.eval(f"typeof f.{name}") == "undefined", name
        for eid in ("i", "t", "s", "b"):
            for name in ("checkValidity", "reportValidity", "setCustomValidity"):
                assert page.eval(f"typeof {eid}.{name}") == "function", (eid, name)
            for name in ("willValidate", "validity", "validationMessage"):
                assert page.eval(f"typeof {eid}.{name}") != "undefined", (eid, name)
        for name in ("checkValidity", "reportValidity", "validity",
                     "willValidate", "validationMessage", "setCustomValidity"):
            assert page.eval(f"typeof d.{name}") == "undefined", name
        assert page.eval("typeof globalThis.ValidityState") == "undefined"


@on_only
def test_default_control_is_valid_and_will_validate():
    with iv8.Page() as page:
        _load(page, "<input id='i'>")
        assert page.eval("i.willValidate") is True
        assert page.eval("i.validity.valid") is True
        assert page.eval("i.checkValidity()") is True
        assert page.eval("i.reportValidity()") is True
        assert page.eval("i.validationMessage") == ""
        for flag in ("valueMissing", "typeMismatch", "patternMismatch",
                     "tooLong", "tooShort", "rangeUnderflow", "rangeOverflow",
                     "stepMismatch", "badInput", "customError"):
            assert page.eval(f"i.validity.{flag}") is False, flag


# --- required -------------------------------------------------------------------

@on_only
def test_required_text_input():
    with iv8.Page() as page:
        _load(page, "<input id='i' required>")
        assert page.eval("i.validity.valueMissing") is True
        assert page.eval("i.validity.valid") is False
        assert page.eval("i.validationMessage") == "Please fill out this field."
        assert page.eval("i.checkValidity()") is False
        page.eval("i.value = 'x'")
        assert page.eval("i.validity.valid") is True
        assert page.eval("i.checkValidity()") is True


@on_only
def test_required_checkbox_and_select():
    with iv8.Page() as page:
        _load(page, """
          <input id='c' type='checkbox' required>
          <select id='s' required>
            <option value=''>--</option>
            <option value='x' selected>x</option>
          </select>
        """)
        assert page.eval("c.validity.valueMissing") is True
        page.eval("c.checked = true")
        assert page.eval("c.validity.valid") is True
        assert page.eval("s.validity.valid") is True   # selected value is "x"
        page.eval("s.selectedIndex = 0")
        assert page.eval("s.validity.valueMissing") is True
        assert page.eval("s.checkValidity()") is False


@on_only
def test_disabled_and_readonly_are_barred():
    with iv8.Page() as page:
        _load(page, """
          <form id='f'>
            <input id='d' required disabled>
            <input id='r' required readonly>
            <textarea id='t' required readonly></textarea>
          </form>
        """)
        for eid in ("d", "r", "t"):
            assert page.eval(f"{eid}.willValidate") is False
            assert page.eval(f"{eid}.validity.valid") is True
            assert page.eval(f"{eid}.checkValidity()") is True, eid
        assert page.eval("f.checkValidity()") is True


@on_only
def test_barred_input_types():
    with iv8.Page() as page:
        _load(page, """
          <input id='h' type='hidden' required>
          <input id='rs' type='reset' required>
          <input id='bt' type='button' required>
        """)
        for eid in ("h", "rs", "bt"):
            assert page.eval(f"{eid}.willValidate") is False
            assert page.eval(f"{eid}.checkValidity()") is True


# --- custom validity ------------------------------------------------------------

@on_only
def test_custom_validity():
    with iv8.Page() as page:
        _load(page, "<form id='f'><input id='i'></form>")
        assert page.eval("i.checkValidity()") is True
        page.eval("i.setCustomValidity('bad value')")
        assert page.eval("i.validity.customError") is True
        assert page.eval("i.validity.valid") is False
        assert page.eval("i.validationMessage") == "bad value"
        assert page.eval("i.checkValidity()") is False
        assert page.eval("f.checkValidity()") is False
        page.eval("i.setCustomValidity('')")
        assert page.eval("i.validity.valid") is True
        assert page.eval("f.checkValidity()") is True
        # String coercion (non-string values are stringified).
        page.eval("i.setCustomValidity(123)")
        assert page.eval("i.validationMessage") == "123"


# --- length and type constraints ------------------------------------------------

@on_only
def test_minlength_and_maxlength():
    with iv8.Page() as page:
        _load(page, "<input id='i' minlength='3' maxlength='5'>")
        assert page.eval("i.validity.valid") is True  # empty is not tooShort
        page.eval("i.value = 'ab'")
        assert page.eval("i.validity.tooShort") is True
        assert page.eval("i.validity.valid") is False
        assert "3 characters" in page.eval("i.validationMessage")
        page.eval("i.value = 'abc'")
        assert page.eval("i.validity.valid") is True
        page.eval("i.value = 'abcdef'")
        assert page.eval("i.validity.tooLong") is True
        assert "5 characters" in page.eval("i.validationMessage")
        page.eval("i.value = 'abcde'")
        assert page.eval("i.validity.valid") is True


@on_only
def test_email_and_url_type_mismatch():
    with iv8.Page() as page:
        _load(page, """
          <input id='e' type='email'>
          <input id='u' type='url'>
        """)
        assert page.eval("e.validity.valid") is True  # empty
        page.eval("e.value = 'nope'")
        assert page.eval("e.validity.typeMismatch") is True
        assert "email" in page.eval("e.validationMessage").lower()
        page.eval("e.value = 'a@b'")
        assert page.eval("e.validity.valid") is True
        page.eval("u.value = 'not a url'")
        assert page.eval("u.validity.typeMismatch") is True
        page.eval("u.value = 'https://example.com/x'")
        assert page.eval("u.validity.valid") is True


@on_only
def test_unsupported_constraints_stay_valid():
    with iv8.Page() as page:
        _load(page, """
          <input id='p' pattern='[0-9]+' value='abc'>
          <input id='n' type='number' min='5' max='10' step='2' value='7'>
        """)
        assert page.eval("p.validity.patternMismatch") is False
        assert page.eval("p.validity.valid") is True
        assert page.eval("n.validity.rangeUnderflow") is False
        assert page.eval("n.validity.rangeOverflow") is False
        assert page.eval("n.validity.stepMismatch") is False
        assert page.eval("n.validity.valid") is True


# --- validity object shape ------------------------------------------------------

@on_only
def test_validity_object_is_fresh_and_read_only():
    with iv8.Page() as page:
        _load(page, "<input id='i' required>")
        assert page.eval("i.validity !== i.validity") is True
        assert page.eval(
            """
            (() => {
              const v = i.validity;
              v.valid = true;
              v.valueMissing = false;
              return [v.valid, v.valueMissing].join(',');
            })();
            """
        ) == "false,true"
        # The element's live view is still invalid.
        assert page.eval("i.validity.valid") is False


# --- invalid events -------------------------------------------------------------

@on_only
def test_control_check_validity_fires_invalid_event():
    with iv8.Page() as page:
        _load(page, "<form id='f'><input id='i' required></form>")
        page.eval("""
            globalThis.hits = [];
            globalThis.formHits = 0;
            i.addEventListener('invalid', e => {
                hits.push([e.type, e.target.id, e.bubbles, e.cancelable]);
            });
            f.addEventListener('invalid', () => { formHits++; });
        """)
        assert page.eval("i.checkValidity()") is False
        assert page.eval("hits.length") == 1
        assert page.eval("hits[0].join(',')") == "invalid,i,false,true"
        # invalid does not bubble to the form.
        assert page.eval("formHits") == 0


@on_only
def test_form_check_validity_aggregates_controls():
    with iv8.Page() as page:
        _load(page, """
          <form id='f'>
            <input id='a' required>
            <input id='b' required>
            <input id='c' required disabled>
            <div id='d'></div>
          </form>
        """)
        page.eval("""
            globalThis.hits = [];
            for (const el of [a, b, c]) {
              el.addEventListener('invalid', e => hits.push(e.target.id));
            }
        """)
        assert page.eval("f.checkValidity()") is False
        assert page.eval("hits.join(',')") == "a,b"
        page.eval("a.value = '1'; b.value = '2'")
        page.eval("hits.length = 0")
        assert page.eval("f.reportValidity()") is True
        assert page.eval("hits.length") == 0


@on_only
def test_no_validate_does_not_skip_explicit_check():
    with iv8.Page() as page:
        _load(page, "<form id='f' novalidate><input id='i' required></form>")
        assert page.eval("f.noValidate") is True
        assert page.eval("f.checkValidity()") is False
        assert page.eval("i.checkValidity()") is False


@on_only
def test_detached_control_and_custom_validity_survive_reset():
    with iv8.Page() as page:
        _load(page, "<form id='f'><input id='i' value='seed' required></form>")
        page.eval("i.setCustomValidity('keep me')")
        page.eval("f.reset()")
        assert page.eval("i.validity.customError") is True
        assert page.eval("i.validationMessage") == "keep me"
        page.eval("""
            const d = document.createElement('input');
            d.setAttribute('required', '');
            globalThis.detached = d;
        """)
        assert page.eval("detached.checkValidity()") is False
        page.eval("detached.value = 'x'")
        assert page.eval("detached.checkValidity()") is True


@on_only
def test_load_resets_validation_state():
    with iv8.Page() as page:
        _load(page, "<input id='i'>")
        page.eval("i.setCustomValidity('old')")
        assert page.eval("i.validity.customError") is True
        _load(page, "<input id='i'>")
        assert page.eval("i.validity.customError") is False
        assert page.eval("i.validity.valid") is True
