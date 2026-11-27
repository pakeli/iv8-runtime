"""BOM-10 digest-only native execution and browser-shape conformance."""

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


def reference(group, context):
    return json.loads((FIXTURES / f"{group}-{context}.json").read_text(encoding="utf-8"))


def run_probe(page, group, result_name):
    source = (FIXTURES / f"{group}_probe.js").read_bytes()
    page.eval(source.decode(), name=f"{group}_probe.js")
    page.run_until_idle(max_steps=100)
    page.run_jobs()
    result = page.eval(f"globalThis.{result_name}", to_py=True)
    assert result.get("status") != "probe-failed", result
    return result


@pytest.mark.parametrize("context,url", CONTEXTS)
def test_native_crypto_shape_and_digest_match_browser(context, url):
    fixture = reference("crypto", context)
    assert fixture["probe_sha256"] == hashlib.sha256((FIXTURES / "crypto_probe.js").read_bytes()).hexdigest()
    with iv8.Page() as page:
        page.load("<html></html>", url)
        actual = run_probe(page, "crypto", "__bom10CryptoResult")
    expected = fixture["result"]
    # CryptoKey import execution is a later slice, not part of digest acceptance.
    # V8's pre-existing SAB global differs from non-isolated Chrome; shared input
    # rejection is checked independently below, without changing global exposure.
    for result in (actual, expected):
        result.pop("keys")
        result["digest"].pop("sharedBuffer", None)
    assert actual == expected


@pytest.mark.parametrize("context,url", CONTEXTS)
def test_random_and_digest_conversion_edges_match_browser(context, url):
    fixture = reference("digest_edges", context)
    assert fixture["probe_sha256"] == hashlib.sha256((FIXTURES / "digest_edges_probe.js").read_bytes()).hexdigest()
    with iv8.Page() as page:
        page.load("<html></html>", url)
        assert run_probe(page, "digest_edges", "__bom10DigestEdgesResult") == fixture["result"]


@pytest.mark.parametrize("algorithm", ["SHA-1", "SHA-256", "SHA-384", "SHA-512"])
@pytest.mark.parametrize("size", [0, 55, 56, 63, 64, 65, 111, 112, 127, 128, 129, 1048576])
def test_digest_block_boundaries_and_large_inputs(algorithm, size):
    expected = hashlib.new(algorithm.lower().replace("-", ""), bytes(i % 251 for i in range(size))).hexdigest()
    with iv8.Page() as page:
        page.eval(f"globalThis.digestOutput=null;crypto.subtle.digest({json.dumps(algorithm)},Uint8Array.from({{length:{size}}},(_,i)=>i%251)).then(v=>{{digestOutput=Array.from(new Uint8Array(v),b=>b.toString(16).padStart(2,'0')).join('')}})")
        assert page.eval("digestOutput") is None
        page.run_jobs()
        assert page.eval("digestOutput") == expected


def test_shared_buffers_reject_and_user_exception_identity_is_preserved():
    with iv8.Page() as page:
        page.eval("""
        globalThis.sharedResults=[];
        for(const value of [new SharedArrayBuffer(3),new Uint8Array(new SharedArrayBuffer(3))]){
          crypto.subtle.digest('SHA-256',value).then(()=>sharedResults.push('unexpected success'),e=>sharedResults.push(e.name));
        }
        globalThis.errorIdentity=[];
        for(const original of [undefined,null,7,Symbol('original'),new Error('original'),{}]){
          crypto.subtle.digest({get name(){throw original}},new Uint8Array()).then(()=>errorIdentity.push(false),e=>errorIdentity.push(e===original));
        }
        """)
        page.run_jobs()
        assert page.eval("sharedResults", to_py=True) == ["TypeError", "TypeError"]
        assert page.eval("errorIdentity", to_py=True) == [True] * 6


def test_key_operations_reject_explicitly_without_fake_results():
    with iv8.Page() as page:
        assert page.eval("SubtleCrypto.supports('digest','SHA-256')") is True
        assert page.eval("SubtleCrypto.supports('encrypt','AES-GCM')") is False
        page.eval("""
        globalThis.unsupported=[];
        const cases=[()=>crypto.subtle.importKey('raw',new Uint8Array(16),'AES-GCM',true,['encrypt']),
          ()=>crypto.subtle.generateKey({name:'HMAC',hash:'SHA-256'},true,['sign']),
          ()=>crypto.subtle.encrypt('AES-GCM',{},new Uint8Array())];
        for(const call of cases)call().then(()=>unsupported.push('unexpected success'),e=>unsupported.push([e.name,e instanceof DOMException,e.message]));
        """)
        page.run_jobs()
        results = page.eval("unsupported", to_py=True)
        assert len(results) == 3
        assert all(result[0] == "NotSupportedError" and result[1] for result in results)
        assert all("digest" in result[2].lower() for result in results)


def test_static_supports_is_native_and_monitored_without_coercion_replay():
    with iv8.Page() as page:
        page.watch_apis(["SubtleCrypto.supports"])
        page.start_access_trace()
        assert page.eval("(()=>{let reads=0;const supported=SubtleCrypto.supports('digest',{get name(){reads++;return 'SHA-384'}});return supported&&reads===1})()")
        calls = [item for item in page.read_access_trace()["events"]
                 if item["operation"] == "call" and item["member"] == "supports"]
        assert len(calls) == 1
        assert calls[0]["outcome"] == "returned"
        assert calls[0]["argument_count"] == 2
        assert [hit["path"] for hit in page.read_watch_api_hits()] == ["SubtleCrypto.supports"]


def test_digest_monitoring_preserves_inputs_identity_and_call_results():
    with iv8.Page() as page:
        page.eval("globalThis.cachedSubtle=crypto.subtle;globalThis.cachedDigest=crypto.subtle.digest;globalThis.algorithmReads=0")
        page.watch_apis(["crypto.subtle.digest"])
        page.start_access_trace()
        page.eval("cachedDigest.call(cachedSubtle,{get name(){algorithmReads++;return 'SHA-256'}},new Uint8Array([1])).then(v=>globalThis.traceDigest=v.byteLength)")
        page.run_jobs()
        assert page.eval("algorithmReads") == 1
        assert page.eval("traceDigest") == 32
        records = page.read_access_trace()["events"]
        calls = [item for item in records if item["member"] == "digest" and item["operation"] == "call"]
        assert len(calls) == 1
        assert calls[0]["label"] == "crypto.subtle"
        assert calls[0]["outcome"] == "returned"
        assert calls[0]["argument_count"] == 2
        assert [hit["path"] for hit in page.read_watch_api_hits()] == ["crypto.subtle.digest"]
        page.stop_access_trace()
        assert page.eval("cachedDigest===crypto.subtle.digest&&cachedSubtle===crypto.subtle")


def test_digest_reload_disposal_and_isolation():
    with iv8.Page() as first, iv8.Page() as second:
        handle = first.eval("crypto.subtle")
        first.eval("globalThis.pendingResult='pending';crypto.subtle.digest('SHA-256',new Uint8Array()).then(()=>pendingResult='done')")
        second.eval("globalThis.pendingResult='second'")
        first.load("<html></html>", "https://new.test/")
        first.run_jobs()
        assert first.eval("typeof pendingResult") == "undefined"
        assert second.eval("pendingResult") == "second"
        with pytest.raises(iv8.JSContextDisposedError):
            handle.to_py()
        del handle
        gc.collect()
        first.eval("crypto.subtle.digest('SHA-512',new Uint8Array()).then(v=>globalThis.newDigest=v.byteLength)")
        first.run_jobs()
        assert first.eval("newDigest") == 64
