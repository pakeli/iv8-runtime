"""Native StorageManager and bounded in-memory filesystem conformance."""

import gc
import hashlib
import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8-linked build")
FIXTURES = Path(__file__).resolve().parent / "fixtures" / "bom_10"
CONTEXTS = [
    ("secure", "http://127.0.0.1:8080/reference"),
    ("insecure", "http://bom-fixture.invalid:8080/reference"),
    ("opaque", "about:blank"),
]


def evaluate_async(page, expression):
    page.eval("globalThis.__storageDone=false;globalThis.__storageError=null;globalThis.__storageResult=undefined;"
              "(async()=>" + expression + ")().then(v=>{__storageResult=v;__storageDone=true},e=>{__storageError={name:e.name,message:e.message};__storageDone=true});")
    page.run_until_idle(max_steps=100)
    assert page.eval("__storageDone") is True
    assert page.eval("__storageError", to_py=True) is None
    return page.eval("__storageResult", to_py=True)


@pytest.mark.parametrize("context,url", CONTEXTS)
def test_storage_tree_matches_browser_reference(context, url):
    source = (FIXTURES / "storage_tree_probe.js").read_bytes()
    fixture = json.loads((FIXTURES / f"storage_tree-{context}.json").read_text())
    assert fixture["probe_sha256"] == hashlib.sha256(source).hexdigest()
    with iv8.Page() as page:
        page.load("<html></html>", url)
        evaluate_async(page, source.decode())
        actual = json.loads(page.eval("JSON.stringify(__storageResult)"))
    assert actual == fixture["result"]


@pytest.mark.parametrize("context,url", CONTEXTS)
def test_storage_native_shapes_match_reference_without_stream_execution(context, url):
    source = (FIXTURES / "storage_probe.js").read_text()
    # Keep the existing browser shape probe byte-for-byte through shape collection;
    # stop before its later write-stream discovery, which is not in this slice.
    marker = "      const fn = StorageManager.prototype.getDirectory;"
    assert source.count(marker) == 1
    source = source[:source.index(marker)] + "return out; } } return out; })()"
    reference = json.loads((FIXTURES / f"storage-{context}.json").read_text())["result"]
    with iv8.Page() as page:
        page.load("<html></html>", url)
        actual = evaluate_async(page, source)
    actual.pop("behavior"); reference.pop("behavior")
    # Streams and worker-only sync handles remain a separately tracked slice.
    for result in (actual, reference):
        result["interfaces"].pop("FileSystemWritableFileStream")
        result["interfaces"].pop("FileSystemSyncAccessHandle")
    assert actual == reference


@pytest.mark.parametrize("first_url,second_url", [
    ("https://EXAMPLE.test:443/a", "https://example.test/b"),
    ("http://LOCALHOST:080/a", "http://localhost/b"),
    ("https://same.test:0443/a", "https://same.test/b"),
])
def test_same_origin_reload_preserves_tree_but_invalidates_old_wrappers(first_url, second_url):
    with iv8.Page() as page:
        page.load("<html></html>", first_url)
        assert evaluate_async(page, "{globalThis.root=await navigator.storage.getDirectory();await root.getDirectoryHandle('kept',{create:true});return true;}")
        previous = page.eval("root")
        page.load("<html></html>", second_url)
        with pytest.raises(iv8.JSContextDisposedError):
            previous.to_py()
        assert evaluate_async(page, "{const root=await navigator.storage.getDirectory();return (await root.getDirectoryHandle('kept')).name;}") == "kept"
        del previous
        gc.collect()


@pytest.mark.parametrize("other_url", ["https://other.test/", "https://first.test:444/", "http://localhost/"])
def test_origin_partitions_and_return_navigation(other_url):
    with iv8.Page() as page:
        page.load("<html></html>", "https://first.test/")
        evaluate_async(page, "{const r=await navigator.storage.getDirectory();await r.getFileHandle('only-first',{create:true});}")
        page.load("<html></html>", other_url)
        assert evaluate_async(page, "{const r=await navigator.storage.getDirectory();return Array.fromAsync(r.keys());}") == []
        page.load("<html></html>", "https://first.test/again")
        assert evaluate_async(page, "{const r=await navigator.storage.getDirectory();return (await r.getFileHandle('only-first')).name;}") == "only-first"


def test_pages_do_not_share_storage_and_disposal_is_terminal():
    first = iv8.Page()
    with iv8.Page() as second:
        first.load("<html></html>", "https://shared.test/")
        second.load("<html></html>", "https://shared.test/")
        evaluate_async(first, "{const r=await navigator.storage.getDirectory();await r.getFileHandle('first',{create:true});}")
        assert evaluate_async(second, "{const r=await navigator.storage.getDirectory();return Array.fromAsync(r.keys());}") == []
        first.dispose(); first.dispose()
        with pytest.raises(iv8.JSContextDisposedError):
            first.load("<html></html>", "https://shared.test/")
        assert evaluate_async(second, "{return (await navigator.storage.getDirectory()).kind;}") == "directory"


def test_handle_and_iterator_gc_keeps_store_entries_alive():
    with iv8.Page() as page:
        page._native.collect_garbage_for_testing()
        baseline = page._native.ephemeral_host_object_count()
        evaluate_async(page, "{for(let i=0;i<200;i++){const r=await navigator.storage.getDirectory();await r.getFileHandle('same',{create:true});const it=r.entries();await it.next();}return true;}")
        page._native.collect_garbage_for_testing()
        assert page._native.ephemeral_host_object_count() == baseline
        assert evaluate_async(page, "{globalThis.savedRoot=await navigator.storage.getDirectory();return (await savedRoot.getFileHandle('same')).name;}") == "same"
        page._native.collect_garbage_for_testing()
        assert page._native.ephemeral_host_object_count() == baseline + 1
        assert evaluate_async(page, "{return (await savedRoot.getFileHandle('same')).name;}") == "same"
        page.eval("savedRoot=null")
        page._native.collect_garbage_for_testing()
        assert page._native.ephemeral_host_object_count() == baseline


def test_get_file_uses_internal_factory_and_snapshot_survives_deletion():
    with iv8.Page() as page:
        result = evaluate_async(page, """{
          const r=await navigator.storage.getDirectory();const h=await r.getFileHandle('sample.txt',{create:true});
          const FileCtor=File, text=Blob.prototype.text, buffer=Blob.prototype.arrayBuffer;
          let touched=0;globalThis.File=function(){touched++;throw new Error('public File called')};
          globalThis.Blob=function(){touched++;throw new Error('public Blob called')};
          const old=Object.getOwnPropertyDescriptor(FileCtor.prototype,'name');
          Object.defineProperty(FileCtor.prototype,'name',{configurable:true,set(){touched++}});
          const f=await h.getFile();await h.remove();
          const result=[f instanceof FileCtor,f.name,f.type,f.size,f.lastModified,await text.call(f),(await buffer.call(f)).byteLength,touched];
          if(old)Object.defineProperty(FileCtor.prototype,'name',old);else delete FileCtor.prototype.name;
          return result;
        }""")
        assert result == [True, "sample.txt", "text/plain", 0, 0, "", 0, 0]


def test_unimplemented_writes_and_move_reject_explicit_profile():
    with iv8.Page() as page:
        result = evaluate_async(page, """{
          const r=await navigator.storage.getDirectory();const f=await r.getFileHandle('x',{create:true});const out=[];
          for(const fn of [()=>f.createWritable(),()=>f.move('other')]){try{await fn();out.push('unexpected')}catch(e){out.push([e.name,e instanceof DOMException])}}
          return out;
        }""")
        assert result == [["NotSupportedError", True], ["NotSupportedError", True]]
        assert page.eval("typeof FileSystemWritableFileStream") == "undefined"


def test_storage_watch_and_access_trace_preserve_host_identity():
    with iv8.Page() as page:
        page.eval("globalThis.manager=navigator.storage")
        page.watch_apis(["navigator.storage.getDirectory"])
        page.start_access_trace()
        assert evaluate_async(page, "{const a=await manager.getDirectory();const b=await manager.getDirectory();return [a===b,await a.isSameEntry(b)];}") == [False, True]
        records = page.read_access_trace()["events"]
        calls = [r for r in records if r["operation"] == "call" and r["member"] == "getDirectory"]
        assert len(calls) == 2
        assert len({r["object_id"] for r in calls}) == 1
        assert all(r["object_type"] == "storage_manager" and r["outcome"] == "returned" for r in calls)
        assert [r["path"] for r in page.read_watch_api_hits()] == ["navigator.storage.getDirectory"] * 2


def test_non_http_secure_origin_rejects_virtual_storage_profile():
    with iv8.Page() as page:
        page.load("<html></html>", "file:///virtual-only/index.html")
        assert evaluate_async(page, "{try{await navigator.storage.getDirectory();return 'unexpected'}catch(e){return e.name}}") == "SecurityError"


def test_cached_iterator_prototype_and_promise_constructor_are_not_user_hooks():
    with iv8.Page() as page:
        result = evaluate_async(page, """{
          const root=await navigator.storage.getDirectory();await root.getFileHandle('present',{create:true});
          const intrinsicAsync=Object.getPrototypeOf(Object.getPrototypeOf(Object.getPrototypeOf((async function*(){})())));
          const originalPromise=Promise;let hits=0;
          globalThis.Promise=function(){hits++;throw new Error('global Promise used')};
          const originalIterator=Array.prototype[Symbol.iterator];
          Array.prototype[Symbol.iterator]=function(){hits++;throw new Error('array iterator used')};
          const iterator=root.entries();const item=await iterator.next();
          Array.prototype[Symbol.iterator]=originalIterator;globalThis.Promise=originalPromise;
          return [Object.getPrototypeOf(Object.getPrototypeOf(iterator))===intrinsicAsync,item.done,item.value[0],item.value[1].name,hits];
        }""")
        assert result == [True, False, "present", "present", 0]
