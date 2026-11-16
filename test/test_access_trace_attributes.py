"""Attribute containers retain their native behavior while recording API use."""

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")

HTML = """<html><body>
<main id="first" data-seed="one" title="first"></main>
<aside id="second" data-seed="two"></aside>
</body></html>"""
BASE = "https://attribute-trace.test/"


def _bind_containers(page):
    page.eval("""
        globalThis.attributeElement = document.getElementById('first');
        globalThis.datasetObject = attributeElement.dataset;
        globalThis.attributesObject = attributeElement.attributes;
        globalThis.firstAttribute = attributesObject.item(0);
        globalThis.attributePrototype = Object.getPrototypeOf(attributesObject);
        globalThis.savedAttributeItem = attributesObject.item;
        globalThis.secondDataset = document.getElementById('second').dataset;
    """)


@pytest.fixture
def attribute_page():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        _bind_containers(page)
        yield page


def _events(page):
    return page.read_access_trace()["events"]


def _operations(events):
    return [(event["object_type"], event["label"], event["member"], event["operation"])
            for event in events]


def test_dynamic_named_and_index_reads_writes_and_deletes(attribute_page):
    page = attribute_page
    page.start_access_trace()
    assert page.eval("""[
        datasetObject.seed,
        datasetObject.extra = 'two',
        delete datasetObject.seed,
        attributesObject[0] === firstAttribute,
        attributesObject.id === firstAttribute
    ]""", to_py=True, name="attribute-operations.js") == ["one", "two", True, True, True]
    events = _events(page)
    assert _operations(events) == [
        ("dom_string_map", "DOMStringMap", "seed", "get"),
        ("dom_string_map", "DOMStringMap", "extra", "set"),
        ("dom_string_map", "DOMStringMap", "seed", "delete"),
        ("named_node_map", "NamedNodeMap", "0", "get"),
        ("named_node_map", "NamedNodeMap", "id", "get"),
    ]
    assert events[0]["value_summary"] == "string:3:one"
    assert events[1]["value_summary"] == "string:3:two"
    assert events[2]["return_summary"] == "boolean:true"
    assert all(event["value_summary"] == "host_object" for event in events[3:])
    assert events[0]["object_id"] == events[1]["object_id"] == events[2]["object_id"]
    assert events[3]["object_id"] == events[4]["object_id"] != events[0]["object_id"]
    assert all(event["outcome"] == "returned" for event in events)
    assert all(event["resource_name"] == "attribute-operations.js" for event in events)
    page.stop_access_trace()
    assert page.eval("attributeElement.getAttribute('data-extra') === 'two' && !attributeElement.hasAttribute('data-seed')")


def test_numeric_dataset_and_missing_container_properties(attribute_page):
    page = attribute_page
    page.start_access_trace()
    assert page.eval("""(() => {
        datasetObject[0] = 'numeric';
        return datasetObject[0] === 'numeric' && datasetObject.notThere === undefined &&
            attributesObject[999] === undefined;
    })()""")
    events = _events(page)
    assert _operations(events) == [
        ("dom_string_map", "DOMStringMap", "0", "set"),
        ("dom_string_map", "DOMStringMap", "0", "get"),
        ("dom_string_map", "DOMStringMap", "notThere", "get_probe"),
        ("named_node_map", "NamedNodeMap", "999", "get_probe"),
    ]
    assert events[0]["value_summary"] == events[1]["value_summary"] == "string:7:numeric"
    assert [event["resolution"] for event in events[2:]] == ["missing", "missing"]


def test_definition_queries_and_enumeration_do_not_double_record(attribute_page):
    page = attribute_page
    page.start_access_trace()
    page.eval("Object.defineProperty(datasetObject, 'added', {value:'new',writable:true,enumerable:true,configurable:true})")
    defined = _events(page)
    assert _operations(defined) == [("dom_string_map", "DOMStringMap", "added", "define_property")]
    assert defined[0]["value_summary"] == "string:3:new"
    assert defined[0]["outcome"] == "returned"
    dataset_id = defined[0]["object_id"]

    for expression in ("Reflect.has(datasetObject, 'added')", "Object.hasOwn(datasetObject, 'added')"):
        assert page.eval(expression)
        query = _events(page)
        assert _operations(query) == [("dom_string_map", "DOMStringMap", "added", "has")]
        assert query[0]["object_id"] == dataset_id
        assert query[0]["outcome"] == "returned"

    assert page.eval("Object.getOwnPropertyDescriptor(datasetObject, 'added').value") == "new"
    described = _events(page)
    assert _operations(described) == [
        ("dom_string_map", "DOMStringMap", "added", "get_own_property_descriptor"),
        ("dom_string_map", "DOMStringMap", "added", "get"),
    ]
    assert all(event["object_id"] == dataset_id for event in described)

    assert page.eval("Object.keys(datasetObject).join(',')") == "seed,added"
    enumerated = _events(page)
    assert _operations(enumerated) == [("dom_string_map", "DOMStringMap", "", "own_keys")]
    assert enumerated[0]["object_id"] == dataset_id
    assert page.eval("Reflect.ownKeys(attributesObject).includes('id')")
    assert _operations(_events(page)) == [("named_node_map", "NamedNodeMap", "", "own_keys")]


def test_method_references_preserve_identity_and_calls_use_the_real_map(attribute_page):
    page = attribute_page
    page.start_access_trace()
    assert page.eval("""(() => {
        const reference = attributesObject.item;
        return reference === savedAttributeItem && reference.call(attributesObject, 0) === firstAttribute &&
            attributesObject.length === 3;
    })()""")
    events = _events(page)
    assert _operations(events) == [
        ("host_object", "NamedNodeMap.prototype", "item", "get"),
        ("named_node_map", "NamedNodeMap", "item", "call"),
        ("named_node_map", "NamedNodeMap", "length", "get"),
    ]
    assert events[0]["value_summary"] == "function"
    assert events[0]["object_id"] != events[1]["object_id"] == events[2]["object_id"]
    assert events[1]["argument_summaries"] == ["number:0"]
    assert events[1]["return_summary"] == "host_object"
    assert page.eval("attributePrototype.item === savedAttributeItem")
    assert _operations(_events(page)) == [("host_object", "NamedNodeMap.prototype", "item", "get")]
    page.stop_access_trace()
    assert page.eval("""(() => {
        const descriptor = Object.getOwnPropertyDescriptor(attributePrototype, 'item');
        return 'value' in descriptor && descriptor.value === savedAttributeItem &&
            descriptor.writable && descriptor.enumerable && descriptor.configurable &&
            descriptor.value.name === 'item' && descriptor.value.length === 1 &&
            !Object.hasOwn(descriptor.value, 'prototype') &&
            Function.prototype.toString.call(descriptor.value) === 'function item() { [native code] }';
    })()""")


def test_dataset_coercion_throws_the_original_error_once_per_operation(attribute_page):
    page = attribute_page
    page.eval("""
        globalThis.attributeSentinel = new Error('attribute conversion sentinel');
        globalThis.attributeConversions = 0;
        globalThis.attributePayload = {toString() {attributeConversions++; throw attributeSentinel;}};
    """)
    page.start_access_trace()
    assert page.eval("""(() => {
        let assigned = false, defined = false;
        try {datasetObject.failedSet = attributePayload;} catch (error) {assigned = error === attributeSentinel;}
        try {Object.defineProperty(datasetObject, 'failedDefine', {value:attributePayload});}
        catch (error) {defined = error === attributeSentinel;}
        return assigned && defined && attributeConversions === 2;
    })()""", name="attribute-error.js")
    events = _events(page)
    assert _operations(events) == [
        ("dom_string_map", "DOMStringMap", "failedSet", "set"),
        ("dom_string_map", "DOMStringMap", "failedDefine", "define_property"),
    ]
    for event in events:
        assert event["outcome"] == "threw"
        assert event["value_summary"] == "object"
        assert event["exception_name"] == "Error"
        assert "attribute conversion sentinel" in event["exception_message"]
        assert event["resource_name"] == "attribute-error.js"
    assert events[0]["object_id"] == events[1]["object_id"]
    page.stop_access_trace()
    assert page.eval("!attributeElement.hasAttribute('data-failed-set') && !attributeElement.hasAttribute('data-failed-define')")


def test_attribute_container_identities_are_per_object_and_follow_generation(attribute_page):
    page = attribute_page
    page.start_access_trace()
    assert page.eval("[datasetObject.seed, secondDataset.seed, datasetObject.seed].join(',')") == "one,two,one"
    first = _events(page)
    assert _operations(first) == [("dom_string_map", "DOMStringMap", "seed", "get")] * 3
    assert first[0]["object_id"] == first[2]["object_id"] != first[1]["object_id"]
    page.load(HTML, BASE)
    _bind_containers(page)
    _events(page)
    assert page.eval("datasetObject.seed") == "one"
    second = _events(page)
    assert _operations(second) == [("dom_string_map", "DOMStringMap", "seed", "get")]
    assert second[0]["generation"] == first[0]["generation"] + 1
    assert second[0]["object_id"] not in {event["object_id"] for event in first}
