"""M11-33 acceptance tests: OS-backed crypto.getRandomValues / randomUUID."""

import re

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-33.test/"
UUID_RE = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")


def _load(page):
    page.load("<html><body></body></html>", BASE)


# --- shape guards ---------------------------------------------------------------

def test_no_new_python_surface():
    for name in ("crypto", "Crypto", "CryptoKey"):
        assert not hasattr(iv8, name)
    for name in ("crypto", "getRandomValues", "randomUUID"):
        assert not hasattr(iv8.Page, name)


@on_only
def test_surface_shape():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("typeof crypto") == "object"
        assert page.eval("typeof crypto.getRandomValues") == "function"
        assert page.eval("typeof crypto.randomUUID") == "function"
        assert page.eval("window.crypto === crypto") is True
        # This milestone deliberately has no SubtleCrypto surface.
        assert page.eval("typeof crypto.subtle") == "undefined"
        assert page.eval("typeof globalThis.Crypto") == "undefined"


@on_only
def test_surface_is_read_only():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              crypto.getRandomValues = 1;
              crypto.randomUUID = 1;
              crypto = null;
              return [typeof crypto.getRandomValues,
                      typeof crypto.randomUUID,
                      typeof crypto].join(',');
            })();
            """
        ) == "function,function,object"


# --- getRandomValues ------------------------------------------------------------

@on_only
def test_get_random_values_fills_and_returns_the_same_view():
    with iv8.Page() as page:
        _load(page)
        result = page.eval(
            """
            (() => {
              const a = new Uint8Array(32);
              const same = crypto.getRandomValues(a) === a;
              return [same, a.length, a.every(v => v === 0)];
            })();
            """,
            to_py=True,
        )
        assert result[0] is True
        assert result[1] == 32
        assert result[2] is False  # 32 zero bytes would be astronomically unlikely


@on_only
def test_get_random_values_accepts_integer_typed_arrays():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              const views = [
                new Int8Array(8), new Uint8Array(8), new Uint8ClampedArray(8),
                new Int16Array(4), new Uint16Array(4),
                new Int32Array(2), new Uint32Array(2),
                new BigInt64Array(1), new BigUint64Array(1),
              ];
              return views.every(v => crypto.getRandomValues(v) === v);
            })();
            """
        ) is True


@on_only
def test_get_random_values_respects_the_view_byte_range():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              const a = new Uint8Array(8).fill(0xAA);
              crypto.getRandomValues(a.subarray(2, 5));
              return [a[0], a[1], a[5], a[6], a[7]].join(',');
            })();
            """
        ) == "170,170,170,170,170"


@on_only
def test_zero_length_view_is_unchanged():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("const z = new Uint8Array(0); crypto.getRandomValues(z) === z") is True


@on_only
def test_quota_is_65536_bytes():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            "const big = new Uint8Array(65536); "
            "crypto.getRandomValues(big) === big"
        ) is True
        with pytest.raises(iv8.JSError) as info:
            page.eval("crypto.getRandomValues(new Uint8Array(65537))")
        assert info.value.name == "RangeError"


@on_only
def test_non_integer_views_are_rejected():
    with iv8.Page() as page:
        _load(page)
        bad = (
            "new ArrayBuffer(8)",
            "new DataView(new ArrayBuffer(8))",
            "new Float32Array(2)",
            "new Float64Array(1)",
            "[]",
            "'text'",
            "null",
            "undefined",
        )
        for expression in bad:
            with pytest.raises(iv8.JSError) as info:
                page.eval(f"crypto.getRandomValues({expression})")
            assert info.value.name == "TypeError", expression
        with pytest.raises(iv8.JSError) as info:
            page.eval("crypto.getRandomValues()")
        assert info.value.name == "TypeError"


@on_only
def test_two_fills_differ():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              const a = new Uint8Array(32);
              const b = new Uint8Array(32);
              crypto.getRandomValues(a);
              crypto.getRandomValues(b);
              return a.every((v, i) => v === b[i]);
            })();
            """
        ) is False


# --- randomUUID -----------------------------------------------------------------

@on_only
def test_random_uuid_format_and_uniqueness():
    with iv8.Page() as page:
        _load(page)
        values = page.eval(
            "Array.from({length: 8}, () => crypto.randomUUID())", to_py=True
        )
        assert len(values) == 8
        assert len(set(values)) == 8
        for value in values:
            assert isinstance(value, str)
            assert UUID_RE.match(value), value


# --- monitoring / lifecycle -----------------------------------------------------

@on_only
def test_watch_apis_records_crypto_calls():
    with iv8.Page() as page:
        _load(page)
        page.watch_apis(["crypto.getRandomValues", "crypto.randomUUID"])
        page.eval("crypto.getRandomValues(new Uint8Array(4)); crypto.randomUUID();")
        hits = page.read_watch_api_hits()
        assert [h["path"] for h in hits] == [
            "crypto.getRandomValues", "crypto.randomUUID"]
        assert all(h["type"] == "call" for h in hits)


@on_only
def test_crypto_is_present_after_load():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            "typeof crypto.getRandomValues === 'function' && "
            "typeof crypto.randomUUID === 'function'"
        ) is True
