"""Step 4: missing-property probes on HostObjects."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-probe.test/"
HTML = "<html><body><div id='app'>hello</div></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _probes(page):
    return [
        event for event in page.read_access_trace(clear=False)["events"]
        if event["operation"] == "get_probe"
    ]


@on_only
def test_missing_host_property_records_missing_probe():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval("navigator.someMissingProperty === undefined",
                         name="probe.js") is True
        probes = _probes(page)
        assert len(probes) == 1
        probe = probes[0]
        assert probe["object_type"] == "navigator"
        assert probe["label"] == "navigator"
        assert probe["member"] == "someMissingProperty"
        assert probe["operation"] == "get_probe"
        assert probe["phase"] == "enter"
        assert probe["resolution"] == "missing"
        assert probe["source_kind"] == "eval"
        assert probe["resource_name"] == "probe.js"
        assert probe["generation"] == 2


@on_only
def test_existing_undefined_own_and_prototype_properties_are_not_missing():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval("""
            navigator.custom = undefined;
            navigator.custom === undefined;
        """, name="probe.js") is True
        assert page.eval("""
            Object.setPrototypeOf(navigator, {fromPrototype: 123});
            navigator.fromPrototype === 123;
        """, name="probe.js") is True
        probes = _probes(page)
        assert not [probe for probe in probes
                    if probe["member"] in ("custom", "fromPrototype")]


@on_only
def test_deleted_property_becomes_missing_again():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval("""
            navigator.temporary = 1;
            delete navigator.temporary;
            navigator.temporary === undefined;
        """, name="probe.js") is True
        probes = _probes(page)
        assert [(probe["member"], probe["resolution"]) for probe in probes] == [
            ("temporary", "missing")]


@on_only
def test_has_in_enumeration_and_descriptors_do_not_probe():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval("""
            ('notThere' in navigator);
            navigator.hasOwnProperty('notThere');
            Object.keys(navigator);
            Object.getOwnPropertyDescriptor(navigator, 'notThere');
            true;
        """, name="probe.js") is True
        assert _probes(page) == []


@on_only
def test_symbol_and_numeric_reads_are_out_of_scope():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval("navigator[Symbol('probe')]; navigator[12345];",
                  name="probe.js")
        assert _probes(page) == []


@on_only
def test_trace_off_and_on_preserve_missing_lookup_behavior():
    with iv8.Page() as page:
        _load(page)
        expression = """
            [navigator.neverThere === undefined,
             'neverThere' in navigator,
             navigator.hasOwnProperty('neverThere')].join(',')
        """
        assert page.eval(expression) == "true,false,false"
        page.start_access_trace()
        assert page.eval(expression) == "true,false,false"
        page.stop_access_trace()
        assert page.eval(expression) == "true,false,false"


@on_only
def test_missing_method_reference_probes_then_throws_on_call():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        with pytest.raises(iv8.JSError):
            page.eval("navigator.notAFunction();", name="probe.js")
        probes = _probes(page)
        assert [(probe["member"], probe["resolution"]) for probe in probes] == [
            ("notAFunction", "missing")]
        assert page.eval("typeof navigator.notAFunction") == "undefined"


@on_only
def test_existing_native_getter_is_a_get_not_a_probe():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval("navigator.userAgent.length > 0") is True
        events = page.read_access_trace(clear=False)["events"]
        assert [event["operation"] for event in events] == ["get"]
        assert events[0]["member"] == "userAgent"


@on_only
def test_probe_object_filter_and_capacity():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace(max_events=1, object_types=["document"])
        page.eval("navigator.firstMissing; document.secondMissing;",
                  name="probe.js")
        report = page.read_access_trace(clear=False)
        assert [(event["object_type"], event["member"], event["resolution"])
                for event in report["events"]] == [
            ("document", "secondMissing", "missing")]
        assert report["dropped_events"] == 0


@on_only
def test_probe_generation_and_identity_follow_page_generation():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval("navigator.oneMissing;", name="probe.js")
        first = _probes(page)[0]
        page.load(HTML, BASE)
        page.eval("navigator.oneMissing;", name="probe.js")
        probes = _probes(page)
        assert len(probes) == 2
        assert probes[0]["generation"] == 2
        assert probes[1]["generation"] == 3
        assert probes[0]["object_id"] != probes[1]["object_id"]
        assert probes[1]["resolution"] == "missing"
