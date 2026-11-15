"""PRES-02 offline DOM behavior against an isolated Chrome 157 reference.

The probe also records XML parsing/serialization for future work. Those browser
capabilities are excluded from this batch's required comparisons.
"""

from hashlib import sha256
import json
from pathlib import Path

import pytest

import iv8


FIXTURES = Path(__file__).parent / "fixtures" / "presence_02"
PROBE = (FIXTURES / "probe.js").read_text(encoding="utf-8")
REFERENCE = json.loads((FIXTURES / "chrome.json").read_text(encoding="utf-8"))
BASE = "https://presence-02.test/"
HTML = '<html><head></head><body><div id="root" data-start="one"></div></body></html>'
on_only = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
CASES = (
    "dataset", "dataset_definition", "attr", "named_node_map", "namespace_attributes", "namespace_creation",
    "namespace_collection", "attribute_observer", "node_constants", "get_root_node", "normalize", "normalize_observer",
    "compare_document_position", "import_node", "adopt_node", "adopted_observer", "dom_parser_html", "dom_parser_fragment",
)


def evaluate(page, body):
    return json.loads(page.eval("JSON.stringify((() => {" + body + "})())"))


def test_reference_probe_hash_and_capture_conditions():
    assert REFERENCE["probe_sha256"] == sha256(PROBE.encode("utf-8")).hexdigest()
    assert REFERENCE["browser"] == "Google Chrome 157.0.8103.51"
    assert REFERENCE["conditions"]["profile_removed_after_capture"] is True
    assert REFERENCE["conditions"]["target_network"] == "none"
    for case in CASES:
        assert "value" in REFERENCE["results"][case], case


@pytest.fixture(scope="module")
def observed_cases():
    if not getattr(iv8, "_v8_linked", False):
        pytest.skip("requires V8")
    with iv8.Page() as page:
        page.load('<html><head></head><body></body></html>', BASE)
        page.eval("(" + PROBE + ").then(value => {globalThis.presence02Results = value;}, "
                  "error => {globalThis.presence02Failure = String(error);});", name="presence-02-probe.js")
        page.run_jobs()
        assert page.eval("typeof presence02Failure") == "undefined"
        serialized = page.eval("presence02Results")
        assert isinstance(serialized, str), "The complete probe must settle through Page.run_jobs()"
        yield json.loads(serialized)


@pytest.fixture
def page():
    if not getattr(iv8, "_v8_linked", False):
        pytest.skip("requires V8")
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


@on_only
@pytest.mark.parametrize("case", CASES)
def test_dom_behavior_matches_real_browser(case, observed_cases):
    assert observed_cases[case] == REFERENCE["results"][case]


@on_only
def test_native_dom_methods_match_browser_shapes(page):
    expected = REFERENCE["results"]["function_shapes"]["value"]
    for name, captured in expected.items():
        if name.startswith("XMLSerializer."):
            continue
        expression = name if name.startswith("document.") else name.replace(".", ".prototype.", 1)
        actual = evaluate(page, """
            const method = """ + expression + """;
            return {name:method.name,length:method.length,source:Function.prototype.toString.call(method),
                prototype:Object.hasOwn(method,'prototype'),constructable:(() => {
                    try {Reflect.construct(String,[],method);return true;} catch(error) {
                        if (!(error instanceof TypeError)) throw error;return false;
                    }
                })()};
        """)
        assert actual == captured, name


@on_only
def test_dom_properties_and_methods_live_on_defining_prototypes(page):
    assert page.eval("""(() => {
        const own = (prototype,name) => Object.getOwnPropertyDescriptor(prototype,name) !== undefined;
        const element = document.getElementById('root');
        return own(HTMLElement.prototype,'dataset') && own(Element.prototype,'attributes') &&
            !Object.hasOwn(element,'dataset') && !Object.hasOwn(element,'attributes') &&
            ['getAttributeNode','setAttributeNode','removeAttributeNode','getAttributeNS','setAttributeNS',
             'removeAttributeNS','hasAttributeNS','getAttributeNodeNS','setAttributeNodeNS'].every(name => own(Element.prototype,name)) &&
            ['getRootNode','normalize','compareDocumentPosition'].every(name => own(Node.prototype,name)) &&
            ['createAttribute','createAttributeNS','createElementNS','importNode','adoptNode'].every(name => own(Document.prototype,name)) &&
            Object.getPrototypeOf(Attr.prototype) === Node.prototype &&
            Object.getPrototypeOf(element.attributes) === NamedNodeMap.prototype &&
            Object.getPrototypeOf(element.dataset) === DOMStringMap.prototype &&
            Object.getPrototypeOf(new DOMParser()) === DOMParser.prototype;
    })()""")


@on_only
@pytest.mark.parametrize("mime", ["invalid/type", "TEXT/HTML"])
def test_dom_parser_rejects_invalid_mime_like_browser(page, mime):
    with pytest.raises(iv8.JSError) as error:
        page.eval("new DOMParser().parseFromString('<p>text</p>'," + json.dumps(mime) + ")")
    assert error.value.name == "TypeError"


@on_only
@pytest.mark.parametrize("mime", ["text/xml", "application/xml", "application/xhtml+xml", "image/svg+xml"])
def test_valid_xml_mime_has_explicit_unsupported_boundary(page, mime):
    with pytest.raises(iv8.JSError) as error:
        page.eval("new DOMParser().parseFromString('<root/>'," + json.dumps(mime) + ")")
    assert error.value.name == "NotSupportedError"


@on_only
def test_normalize_delivers_existing_mutation_channels(page):
    page.eval("""globalThis.normalizedRecords = [];
        const parent = document.getElementById('root');
        parent.appendChild(document.createTextNode('A')); parent.appendChild(document.createTextNode('B'));
        const observer = new MutationObserver(records => normalizedRecords.push(...records));
        observer.observe(parent,{childList:true,characterData:true,subtree:true,characterDataOldValue:true});
        parent.normalize();""")
    page.run_jobs()
    assert page.eval("""normalizedRecords.some(record => record.type === 'characterData' && record.oldValue === 'A') &&
        normalizedRecords.some(record => record.type === 'childList' && record.removedNodes.length === 1) &&
        document.getElementById('root').textContent === 'AB'""")


@on_only
def test_adoption_keeps_live_collection_routing_and_moves_attribute_owner(page):
    assert page.eval("""(() => {
        const target = new DOMParser().parseFromString('<html><head></head><body></body></html>','text/html');
        const form = document.createElement('form');
        for (const tag of ['input','button']) {
            const control = document.createElement(tag); control.name = 'shared'; form.appendChild(control);
        }
        const collection = form.shared;
        const attribute = document.createAttribute('data-label');attribute.value = 'one';form.setAttributeNode(attribute);
        const adopted = target.adoptNode(form);target.body.appendChild(adopted);
        if (adopted !== form || attribute.ownerDocument !== target || collection.length !== 2) return false;
        const third = target.createElement('input');third.name = 'shared';adopted.appendChild(third);
        if (collection.length !== 3 || collection[2] !== third) return false;
        adopted.firstChild.remove();
        return collection.length === 2 && adopted.attributes.getNamedItem('data-label') === attribute;
    })()""")


@on_only
def test_import_does_not_copy_listeners_or_handler_slots(page):
    assert page.eval("""(() => {
        const target = new DOMParser().parseFromString('<html><head></head><body></body></html>','text/html');
        const source = document.createElement('button');let calls=0;
        source.onclick = () => calls++;source.addEventListener('click',() => calls++);
        const copy = target.importNode(source,true);copy.dispatchEvent(new Event('click'));
        source.dispatchEvent(new Event('click'));
        return copy.onclick === null && calls === 2 && copy.ownerDocument === target;
    })()""")


@on_only
def test_attribute_wrappers_are_generation_scoped_and_pages_are_isolated(page):
    old_attribute = page.eval("document.getElementById('root').getAttributeNode('data-start')")
    assert old_attribute.context_alive
    page.eval("document.getElementById('root').dataset.start = 'changed'; document.getElementById('root').dataset.onlyHere = 'private'")
    with iv8.Page() as other:
        other.load(HTML, BASE)
        assert other.eval("document.getElementById('root').dataset.start === 'one' && document.getElementById('root').dataset.onlyHere === undefined")
    page.load(HTML, BASE)
    assert old_attribute.context_alive is False
    with pytest.raises(iv8.JSContextDisposedError):
        _ = old_attribute.type_name
    assert page.eval("document.getElementById('root').dataset.start === 'one' && document.getElementById('root').dataset.onlyHere === undefined")
