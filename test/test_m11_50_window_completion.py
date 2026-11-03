"""M11-50: deterministic window scroll / focus / UI metadata completion."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-50.test/"
HTML = "<html><body></body></html>"


def test_no_new_python_surface():
    for attr in ("scrollX", "scrollY", "pageXOffset", "pageYOffset",
                 "scroll", "scrollTo", "scrollBy", "focus", "blur", "status",
                 "locationbar", "clientInformation"):
        assert not hasattr(iv8.Page, attr)


@on_only
def test_scroll_position_defaults_and_read_only():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval(
            "[scrollX, scrollY, pageXOffset, pageYOffset].join(',')"
        ) == "0,0,0,0"
        assert page.eval(
            "window.scrollX === 0 && window.scrollY === 0 && "
            "window.pageXOffset === 0 && window.pageYOffset === 0"
        ) is True
        assert page.eval(
            "(scrollX = 9, scrollY = 8, pageXOffset = 7, pageYOffset = 6, "
            "[scrollX, scrollY, pageXOffset, pageYOffset].join(','))"
        ) == "0,0,0,0"
        with pytest.raises(iv8.JSError) as info:
            page.eval("'use strict'; scrollX = 1;")
        assert info.value.name == "TypeError"


@on_only
def test_scroll_methods_accept_arguments_and_stay_at_zero():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval(
            "typeof window.scroll === 'function' && "
            "typeof window.scrollTo === 'function' && "
            "typeof window.scrollBy === 'function'"
        ) is True
        assert page.eval(
            "(() => {"
            "  const a = scroll(1, 2);"
            "  const b = scrollTo({left: 5, top: 10, behavior: 'smooth'});"
            "  const c = scrollBy(3, 4);"
            "  const d = scrollTo(0, 0);"
            "  return [String(a), String(b), String(c), String(d)].join('|');"
            "})()"
        ) == "undefined|undefined|undefined|undefined"
        assert page.eval("[scrollX, scrollY].join(',')") == "0,0"


@on_only
def test_focus_blur_are_noops_and_has_focus_stays_false():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval(
            "typeof window.focus === 'function' && "
            "typeof window.blur === 'function'"
        ) is True
        assert page.eval(
            "(() => {"
            "  const a = window.focus();"
            "  const b = window.blur();"
            "  return [String(a), String(b), document.hasFocus()].join('|');"
            "})()"
        ) == "undefined|undefined|false"


@on_only
def test_move_resize_and_stop_are_noops():
    env = {"innerWidth": 800, "innerHeight": 600,
           "outerWidth": 900, "outerHeight": 700,
           "screenX": 10, "screenY": 20}
    with iv8.Page(environment=env) as page:
        page.load(HTML, BASE)
        assert page.eval(
            "(() => {"
            "  const calls = ["
            "    moveBy(1, 2), moveTo(3, 4), resizeBy(5, 6), resizeTo(7, 8),"
            "    stop()"
            "  ];"
            "  return [calls.every(v => v === undefined),"
            "          [innerWidth, innerHeight, outerWidth, outerHeight,"
            "           screenX, screenY].join(',')].join('|');"
            "})()"
        ) == "true|800,600,900,700,10,20"


@on_only
def test_status_is_a_writable_string_and_resets_on_load():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval("window.status") == ""
        assert page.eval("window.status = 42; window.status") == "42"
        assert page.eval("window.status = null; window.status") == "null"
        assert page.eval(
            "window.status = {toString: () => 'ready'}; window.status"
        ) == "ready"
        page.load(HTML, BASE)
        assert page.eval("window.status") == ""
        assert page.eval("window.status = 'x'; window.status") == "x"
    with iv8.Page() as other:
        assert other.eval("window.status") == ""


@on_only
def test_bar_props_are_read_only_visible_metadata():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval(
            "(() => {"
            "  const names = ['locationbar', 'menubar', 'personalbar',"
            "                 'scrollbars', 'statusbar', 'toolbar'];"
            "  return names.map(n =>"
            "      typeof window[n] + ':' + window[n].visible).join('|');"
            "})()"
        ) == "|".join(["object:true"] * 6)
        assert page.eval(
            "window.locationbar !== window.menubar && "
            "locationbar.visible === true"
        ) is True
        assert page.eval(
            "(window.locationbar = null, window.locationbar.visible)"
        ) is True
        assert page.eval(
            "(locationbar.visible = false, locationbar.visible)"
        ) is True
        with pytest.raises(iv8.JSError) as info:
            page.eval("'use strict'; window.locationbar = null;")
        assert info.value.name == "TypeError"
        with pytest.raises(iv8.JSError) as info:
            page.eval("'use strict'; locationbar.visible = false;")
        assert info.value.name == "TypeError"


@on_only
def test_client_information_aliases_navigator():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval("window.clientInformation === navigator") is True
        assert page.eval(
            "clientInformation.userAgent === navigator.userAgent"
        ) is True
        assert page.eval(
            "(window.clientInformation = null, "
            "window.clientInformation === navigator)"
        ) is True


@on_only
def test_scroll_and_status_reads_are_access_traced():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        page.start_access_trace()
        assert page.eval(
            "[scrollX, scrollY, pageXOffset, pageYOffset].join('|')",
            name="probe.js"
        ) == "0|0|0|0"
        events = page.read_access_trace()["events"]
        assert [event["member"] for event in events] == [
            "scrollX", "scrollY", "pageXOffset", "pageYOffset"]
        assert all(event["object_type"] == "window" for event in events)
        assert all(event["operation"] == "get" for event in events)

        page.start_access_trace()
        assert page.eval(
            "window.status = 'ready'; window.status;", name="probe.js"
        ) == "ready"
        events = page.read_access_trace()["events"]
        assert [(event["member"], event["operation"])
                for event in events] == [("status", "set"),
                                         ("status", "get")]


@on_only
def test_new_window_methods_are_watchable():
    paths = ["window.scroll", "window.scrollTo", "window.scrollBy",
             "window.focus", "window.blur", "window.moveBy", "window.moveTo",
             "window.resizeBy", "window.resizeTo", "window.stop"]
    with iv8.Page() as page:
        page.load(HTML, BASE)
        page.watch_apis(paths)
        page.eval(
            "scroll(1, 2); scrollTo(3, 4); scrollBy(5, 6);"
            "focus(); blur(); moveBy(1, 1); moveTo(2, 2);"
            "resizeBy(3, 3); resizeTo(4, 4); stop();"
        )
        hits = page.read_watch_api_hits()
        assert [hit["path"] for hit in hits] == paths
