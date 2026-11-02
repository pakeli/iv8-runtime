"""Step 6f: target association on the existing Reflect watch channels."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://reflect-target.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


@on_only
def test_reflect_get_has_and_ownkeys_report_host_object_targets():
    with iv8.Page() as page:
        _load(page)
        page.watch_reflect_get(enabled=True)
        page.watch_reflect_has(enabled=True)
        page.watch_reflect_ownkeys(enabled=True)
        page.eval(
            "Reflect.get(navigator, 'userAgent');"
            "Reflect.has(screen, 'width');"
            "Reflect.ownKeys(document);"
            "Reflect.get({x: 1}, 'x');",
            name="targets.js"
        )

        get_hits = page.read_reflect_get_hits()
        has_hits = page.read_reflect_has_hits()
        ownkeys_hits = page.read_reflect_ownkeys_hits()
        assert [(hit["target_type"], hit["target_label"], hit["property"])
                for hit in get_hits] == [
            ("navigator", "navigator", "userAgent"),
            ("unknown", "", "x"),
        ]
        assert [(hit["target_type"], hit["target_label"], hit["property"])
                for hit in has_hits] == [
            ("screen", "screen", "width"),
        ]
        assert [(hit["target_type"], hit["target_label"])
                for hit in ownkeys_hits] == [
            ("document", "document"),
        ]


@on_only
def test_reflect_targets_distinguish_same_property_and_preserve_channels():
    with iv8.Page() as page:
        _load(page)
        page.watch_reflect_get(enabled=True)
        page.watch_reflect_has(enabled=True)
        page.eval(
            "Reflect.get(navigator, 'same');"
            "Reflect.get(screen, 'same');"
            "Reflect.has(navigator, 'same');"
            "Reflect.has(screen, 'same');",
            name="same.js"
        )
        get_hits = page.read_reflect_get_hits()
        has_hits = page.read_reflect_has_hits()
        assert [(hit["target_type"], hit["target_label"])
                for hit in get_hits] == [
            ("navigator", "navigator"), ("screen", "screen")]
        assert [(hit["target_type"], hit["target_label"])
                for hit in has_hits] == [
            ("navigator", "navigator"), ("screen", "screen")]
        assert get_hits[0]["property"] == get_hits[1]["property"] == "same"
        assert has_hits[0]["property"] == has_hits[1]["property"] == "same"
        # The channels remain independent and read-and-clear.
        assert page.read_reflect_get_hits() == []
        assert page.read_reflect_has_hits() == []


@on_only
def test_reflect_target_metadata_survives_load_and_unknown_targets():
    with iv8.Page() as page:
        _load(page)
        page.watch_reflect_get(enabled=True)
        page.eval("Reflect.get(navigator, 'userAgent');", name="one.js")
        assert page.read_reflect_get_hits()[0]["target_label"] == "navigator"

        page.load(HTML, BASE)
        page.eval("Reflect.get({x: 1}, 'x');", name="two.js")
        hits = page.read_reflect_get_hits()
        assert hits[0]["target_type"] == "unknown"
        assert hits[0]["target_label"] == ""
        assert hits[0]["resource_name"] == "two.js"
