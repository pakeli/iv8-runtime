"""M11-8 acceptance tests: Blob / File / FileReader.

Blob/File are immutable byte value objects; FileReader completes reads on the
existing Page.run_jobs() microtask checkpoint. There is no Python API, no new
pump, and no network or filesystem access.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://blob.test/"
DOC = "<html><body><div id='root'></div></body></html>"


def _setup(page):
    page.load(DOC, BASE)


def _blob_text(page, expr):
    page.eval(f"globalThis._text = null; ({expr}).text().then("
              "function (v) { _text = v; });")
    page.run_jobs()
    return page.eval("_text")


def _blob_bytes(page, expr):
    page.eval(f"globalThis._bytes = null; ({expr}).arrayBuffer().then("
              "function (v) { _bytes = new Uint8Array(v).join(','); });")
    page.run_jobs()
    return page.eval("_bytes")


def test_no_new_python_surface():
    for name in ("Blob", "File", "FileReader"):
        assert not hasattr(iv8, name)
    for name in ("Blob", "File", "FileReader", "readAsText"):
        assert not hasattr(iv8.Page, name)


@on_only
def test_global_shape():
    with iv8.Page() as page:
        _setup(page)
        for name in ("Blob", "File", "FileReader"):
            assert page.eval(f"typeof {name}") == "function"
            assert page.eval(f"window.{name} === {name}") is True
            assert page.eval(f"globalThis.{name} === {name}") is True
        assert page.eval(
            "Object.getPrototypeOf(File.prototype) === Blob.prototype") is True
        for name in ("slice", "text", "arrayBuffer"):
            assert page.eval(f"typeof Blob.prototype.{name}") == "function"
        for name in ("readAsText", "readAsArrayBuffer", "abort",
                     "addEventListener", "removeEventListener", "dispatchEvent"):
            assert page.eval(
                f"typeof FileReader.prototype.{name}") == "function"
        assert page.eval(
            "typeof FileReader.prototype.readAsDataURL") == "undefined"


@on_only
@pytest.mark.parametrize("expr", [
    "Blob([])",
    "File([], 'x')",
    "FileReader()",
])
def test_constructor_requires_new(expr):
    with iv8.Page() as page:
        _setup(page)
        with pytest.raises(iv8.JSError) as info:
            page.eval(expr)
        assert info.value.name == "TypeError"
        assert page.eval("1 + 1") == 2


# --- Blob ---------------------------------------------------------------------

@on_only
def test_blob_defaults_and_promises():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("new Blob().size") == 0
        assert page.eval("new Blob().type") == ""
        assert page.eval("new Blob(null).size") == 0
        assert page.eval("""
            (function () {
                globalThis.got = null;
                globalThis.p = new Blob(['hi']).text();
                p.then(function (v) { got = v; });
                return p instanceof Promise;
            })()
        """) is True
        assert page.eval("got === null") is True
        page.run_jobs()
        assert page.eval("got") == "hi"
        assert _blob_text(page, "new Blob()") == ""
        assert _blob_bytes(page, "new Blob([])") == ""


@on_only
def test_blob_parts_and_type():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.b = new Blob([
                'a',
                new Blob(['xy']),
                new Uint8Array([1, 2, 3]),
                new Uint8Array([4, 5]).buffer,
                7
            ], {type: 'TEXT/Plain'});
        """)
        assert page.eval("b.size") == 9
        assert page.eval("b.type") == "text/plain"
        assert _blob_bytes(page, "b") == "97,120,121,1,2,3,4,5,55"
        assert page.eval("new Blob(['héllo']).size") == 6
        assert page.eval("new Blob([1]).size") == 1


@on_only
def test_blob_parts_must_be_an_array():
    with iv8.Page() as page:
        _setup(page)
        for expr in ("new Blob('abc')", "new Blob(5)", "new Blob({})"):
            with pytest.raises(iv8.JSError) as info:
                page.eval(expr)
            assert info.value.name == "TypeError"
        assert page.eval("new Blob(undefined).size") == 0
        assert page.eval("new Blob(null).size") == 0


@on_only
def test_blob_slice():
    with iv8.Page() as page:
        _setup(page)
        assert _blob_text(page, "new Blob(['abcdef']).slice(1, 4)") == "bcd"
        assert _blob_text(page, "new Blob(['abcdef']).slice(-2)") == "ef"
        assert _blob_text(page, "new Blob(['abcdef']).slice(2, -1)") == "cde"
        assert _blob_text(page, "new Blob(['abcdef']).slice(4, 2)") == ""
        assert _blob_text(page, "new Blob(['abcdef']).slice()") == "abcdef"
        assert page.eval(
            "new Blob(['abc']).slice(0, 1, 'TEXT/Plain').type") == "text/plain"
        assert page.eval(
            "new Blob(['abc']).slice(0, 1) instanceof Blob") is True


@on_only
def test_blob_array_buffer_returns_a_copy():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var b = new Blob([new Uint8Array([1, 2, 3])]);
                globalThis.got = null;
                globalThis.p = b.arrayBuffer();
                p.then(function (v) { got = new Uint8Array(v).join(','); });
                return p instanceof Promise;
            })()
        """) is True
        assert page.eval("got === null") is True
        page.run_jobs()
        assert page.eval("got") == "1,2,3"


# --- File ---------------------------------------------------------------------

@on_only
def test_file_fields():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.f = new File(['abc'], 'a.txt', {
                type: 'TEXT/PLAIN', lastModified: 123.9
            });
        """)
        assert page.eval("f instanceof File") is True
        assert page.eval("f instanceof Blob") is True
        assert page.eval("f.size") == 3
        assert page.eval("f.type") == "text/plain"
        assert page.eval("f.name") == "a.txt"
        assert page.eval("f.lastModified") == 123
        assert _blob_text(page, "f") == "abc"


@on_only
def test_file_defaults():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("new File([], 'x').name") == "x"
        assert page.eval("new File([], 'x').type") == ""
        assert page.eval("new File([], 'x').lastModified") == 0
        assert page.eval("new File([], undefined).name") == ""


# --- FileReader ---------------------------------------------------------------

@on_only
def test_filereader_defaults():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("new FileReader() instanceof FileReader") is True
        assert page.eval("new FileReader().readyState") == 0
        assert page.eval("new FileReader().result === null") is True
        assert page.eval("new FileReader().error === null") is True
        assert page.eval("typeof new FileReader().onload") == "undefined"


@on_only
def test_filereader_read_as_text_events():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.log = [];
            globalThis.reader = new FileReader();
            reader.addEventListener('loadstart', function (e) {
                log.push('start:' + reader.readyState + ':' +
                         (e.target === reader) + ':' +
                         (e.currentTarget === reader));
            });
            reader.addEventListener('load', function (e) {
                log.push('load:' + reader.readyState + ':' + reader.result +
                         ':' + (e.target === reader));
            });
            reader.addEventListener('loadend', function () {
                log.push('end:' + reader.readyState);
            });
            globalThis.blob = new Blob(['héllo']);
            reader.readAsText(blob);
        """)
        assert page.eval("reader.readyState") == 1
        assert page.eval("reader.result === null") is True
        assert page.eval("log.join(',')") == "start:1:true:true"
        page.run_timers()
        page.run_animation_frame()
        page.run_idle_callbacks()
        assert page.eval("reader.readyState") == 1
        page.run_jobs()
        assert page.eval("reader.readyState") == 2
        assert page.eval("reader.result") == "héllo"
        assert page.eval("log.join(',')") == (
            "start:1:true:true,load:2:héllo:true,end:2")


@on_only
def test_filereader_read_as_array_buffer():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.reader = new FileReader();
            reader.readAsArrayBuffer(new Blob([new Uint8Array([1, 2, 255])]));
        """)
        assert page.eval("reader.readyState") == 1
        page.run_jobs()
        assert page.eval("reader.readyState") == 2
        assert page.eval("reader.result instanceof ArrayBuffer") is True
        assert page.eval(
            "new Uint8Array(reader.result).join(',')") == "1,2,255"


@on_only
def test_filereader_accepts_file():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.reader = new FileReader();
            reader.readAsText(new File(['abc'], 'a.txt', {type: 'text/plain'}));
        """)
        page.run_jobs()
        assert page.eval("reader.result") == "abc"


@on_only
def test_filereader_read_while_loading_throws():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            globalThis.reader = new FileReader();
            reader.readAsText(new Blob(['a']));
            try { reader.readAsText(new Blob(['b'])); return 'no'; }
            catch (err) { return err.name; }
        """) == "TypeError"
        page.run_jobs()
        assert page.eval("reader.result") == "a"
        assert page.eval("1 + 1") == 2


@on_only
def test_filereader_invalid_argument_throws():
    with iv8.Page() as page:
        _setup(page)
        for expr in ("new FileReader().readAsText({})",
                     "new FileReader().readAsText()",
                     "new FileReader().readAsArrayBuffer('x')"):
            with pytest.raises(iv8.JSError) as info:
                page.eval(expr)
            assert info.value.name == "TypeError"
        assert page.eval("1 + 1") == 2


@on_only
def test_filereader_abort():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.log = [];
            globalThis.reader = new FileReader();
            reader.addEventListener('load', function () { log.push('load'); });
            reader.addEventListener('abort', function () { log.push('abort'); });
            reader.addEventListener('loadend', function () { log.push('end'); });
            reader.readAsText(new Blob(['abc']));
            reader.abort();
        """)
        assert page.eval("reader.readyState") == 2
        assert page.eval("reader.result === null") is True
        assert page.eval("log.join(',')") == "abort,end"
        page.run_jobs()
        assert page.eval("log.join(',')") == "abort,end"


@on_only
def test_filereader_multiple_reads():
    with iv8.Page() as page:
        _setup(page)
        page.eval("globalThis.reader = new FileReader();"
                  "reader.readAsText(new Blob(['a']));")
        page.run_jobs()
        assert page.eval("reader.result") == "a"
        page.eval("reader.readAsText(new Blob(['b']));")
        assert page.eval("reader.readyState") == 1
        page.run_jobs()
        assert page.eval("reader.result") == "b"
        assert page.eval("reader.readyState") == 2


@on_only
def test_filereader_remove_listener():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.log = [];
            globalThis.reader = new FileReader();
            globalThis.fn = function () { log.push('x'); };
            reader.addEventListener('load', fn);
            reader.removeEventListener('load', fn);
            reader.readAsText(new Blob(['a']));
        """)
        page.run_jobs()
        assert page.eval("log.join(',')") == ""
        assert page.eval("reader.result") == "a"


@on_only
def test_load_discards_pending_reader():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.reader = new FileReader();
            reader.readAsText(new Blob(['abc']));
        """)
        page.load(DOC, BASE)
        page.run_jobs()
        assert page.eval("typeof reader") == "undefined"
        assert page.eval("new FileReader().readyState") == 0
        assert page.eval("new Blob(['x']).size") == 1
