"""PRES-03 Window ordinary-name gate, without browser subsystem backends."""

from hashlib import sha256
import json
from pathlib import Path

import pytest

import iv8


FIXTURES = Path(__file__).parent / "fixtures" / "presence_03_window"
PROBE = (FIXTURES / "probe.js").read_text(encoding="utf-8")
REFERENCE = json.loads((FIXTURES / "chrome.json").read_text(encoding="utf-8"))
BASE = "https://presence-03-window.test/"
HTML = "<html><head></head><body></body></html>"
on_only = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
SYNC_UNSUPPORTED = (
    "open()", "getComputedStyle(document.body)", "getSelection()", "find('text')",
    "fetchLater('https://presence-03-window.test/never-sent')", "when('click')",
    "webkitRequestFileSystem(TEMPORARY,0,()=>{},()=>{})",
    "webkitResolveLocalFileSystemURL('filesystem:https://presence-03-window.test/temporary/',()=>{},()=>{})",
)
PROMISE_UNSUPPORTED = (
    "createImageBitmap(document.createElement('canvas'))", "getScreenDetails()", "queryLocalFonts()",
    "showDirectoryPicker()", "showOpenFilePicker()", "showSaveFilePicker()",
)


def test_window_reference_is_pinned_and_contains_only_the_49_ordinary_names():
    assert REFERENCE["probe_sha256"] == sha256(PROBE.encode("utf-8")).hexdigest()
    assert REFERENCE["browser"] == "Google Chrome 157.0.8103.51"
    assert REFERENCE["conditions"]["fresh_profile"] is True
    assert REFERENCE["conditions"]["profile_removed_after_capture"] is True
    assert len(REFERENCE["members"]) == 49
    assert all(item["present"] and "error" not in item for item in REFERENCE["members"].values())


@pytest.fixture(scope="module", params=[False, True], ids=["untraced", "traced"])
def snapshot(request):
    if not getattr(iv8, "_v8_linked", False):
        pytest.skip("requires V8")
    with iv8.Page() as page:
        page.load(HTML, BASE)
        if request.param:
            page.start_access_trace()
        yield json.loads(page.eval(PROBE, name="presence-03-window-probe.js"))["members"]


@pytest.fixture
def page():
    if not getattr(iv8, "_v8_linked", False):
        pytest.skip("requires V8")
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


@on_only
def test_all_49_keys_match_reference_types_and_basic_values(snapshot):
    for name, reference in REFERENCE["members"].items():
        actual = snapshot[name]
        assert actual["present"] is True, name
        assert "error" not in actual, name
        assert actual["type"] == reference["type"], name
        if "value" in reference:
            assert actual["value"] == reference["value"], name


@on_only
def test_global_functions_match_captured_native_shapes(snapshot):
    for name, reference in REFERENCE["members"].items():
        if "function" in reference:
            assert snapshot[name]["function"] == reference["function"], name


@on_only
def test_namespace_shell_tags_and_identity_match_reference(snapshot):
    for name, reference in REFERENCE["members"].items():
        if "namespace" not in reference:
            continue
        actual = snapshot[name]["namespace"]
        assert actual["tag"] == reference["namespace"]["tag"], name
        assert actual["stable"] == reference["namespace"]["stable"], name


@on_only
def test_gpu_and_node_filter_constant_namespaces_keep_numeric_values(page):
    for name in ("GPUBufferUsage", "GPUColorWrite", "GPUMapMode", "GPUShaderStage", "GPUTextureUsage", "NodeFilter"):
        reference = REFERENCE["members"][name]
        members = reference["static_members"] if name == "NodeFilter" else reference["namespace"]["own_members"]
        constants = {item["name"]:item["descriptor"]["value"] for item in members
                     if item["descriptor"].get("type") == "number" and item["name"] != "length"}
        actual = json.loads(page.eval("JSON.stringify(Object.fromEntries(" + json.dumps(list(constants)) +
                                     ".map(key => [key,window[" + json.dumps(name) + "][key]])))"))
        assert actual == constants, name


@on_only
def test_namespace_state_is_generation_owned_and_pages_are_independent(page):
    old_namespace = page.eval("chrome")
    assert old_namespace.context_alive
    page.eval("chrome.presenceOnlyMarker = 7")
    with iv8.Page() as other:
        assert other.eval("chrome.presenceOnlyMarker === undefined")
    page.load(HTML, BASE)
    assert old_namespace.context_alive is False
    with pytest.raises(iv8.JSContextDisposedError):
        _ = old_namespace.type_name
    assert page.eval("chrome.presenceOnlyMarker === undefined")
    names = json.dumps(list(REFERENCE["members"]))
    assert page.eval(names + ".every(name => name in window)")


@on_only
def test_prefixed_animation_frame_aliases_share_existing_queue(page):
    page.eval("""globalThis.frameHits=[];
        const canceled=webkitRequestAnimationFrame(() => frameHits.push('canceled'));
        webkitCancelAnimationFrame(canceled);
        webkitRequestAnimationFrame(timestamp => frameHits.push(['alias',Number.isFinite(timestamp)]));
        requestAnimationFrame(timestamp => frameHits.push(['standard',Number.isFinite(timestamp)]));""")
    assert page.eval("frameHits.length") == 0
    page.run_animation_frame()
    assert json.loads(page.eval("JSON.stringify(frameHits)")) == [["alias", True], ["standard", True]]


@on_only
@pytest.mark.parametrize("expression", SYNC_UNSUPPORTED)
def test_backend_operations_fail_explicitly_without_starting_work(page, expression):
    with pytest.raises(iv8.JSError) as error:
        page.eval(expression)
    assert error.value.name == "NotSupportedError"
    assert page.read_navigation_requests()["requests"] == []


@on_only
@pytest.mark.parametrize("expression", PROMISE_UNSUPPORTED)
def test_async_backend_operations_reject_through_promise(page, expression):
    assert page.eval("""(() => {
        const promise=""" + expression + """;
        promise.then(() => {globalThis.backendOutcome='fulfilled';},
                     error => {globalThis.backendOutcome=error.name;});
        return promise instanceof Promise;
    })()""")
    page.run_jobs()
    assert page.eval("backendOutcome") == "NotSupportedError"


@on_only
@pytest.mark.parametrize("name", ["captureEvents", "releaseEvents", "close", "requestResize"])
def test_bounded_noop_methods_do_not_close_or_navigate(page, name):
    assert page.eval(name + "()") is iv8.JSUndefined
    assert page.eval("window.closed") is False
    assert page.eval("1+1") == 2
    assert page.read_navigation_requests()["requests"] == []
