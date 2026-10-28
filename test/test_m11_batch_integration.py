"""M11-6..M11-10 integration acceptance test.

Everything here is Page-only: JSContext has no window/document/event/storage/
Blob surface, and the Page Python API gained no new members.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-batch.test/"
DOC = ("<html><body><div id='root' class='a b'>"
       "<span id='inner'></span></div></body></html>")


def test_no_new_python_surface():
    for name in ("localStorage", "sessionStorage", "Blob", "FileReader",
                 "CustomEvent", "classList", "toggleAttribute"):
        assert not hasattr(iv8, name)
        assert not hasattr(iv8.Page, name)


@on_only
def test_jscontext_has_no_browser_surface():
    with iv8.JSContext() as ctx:
        surface = ctx.eval("""
            [typeof window, typeof document, typeof localStorage,
             typeof CustomEvent, typeof Blob, typeof Event].join(',')
        """)
    assert surface == "undefined,undefined,undefined,undefined,undefined,undefined"


@on_only
def test_m11_batch_end_to_end():
    with iv8.Page() as page:
        page.load(DOC, BASE)

        # M11-6 / M11-9 / M11-10 / M11-5 cooperation:
        # - CustomEvent carries detail and cancels via preventDefault
        # - stopImmediatePropagation blocks the later same-target listener
        # - classList / toggleAttribute mutate the live tree and queue
        #   MutationObserver attribute records (delivered only by run_jobs)
        result = page.eval("""
            (function () {
                globalThis.log = [];
                var root = document.getElementById('root');
                globalThis.root = root;
                var observer = new MutationObserver(function (records) {
                    for (var i = 0; i < records.length; i++) {
                        log.push(records[i].attributeName + '=' +
                                 String(records[i].oldValue));
                    }
                });
                observer.observe(root, {attributes: true, attributeOldValue: true});
                root.addEventListener('ping', function (e) {
                    log.push('ping:' + e.detail.x + ':' + (e.target.id === 'root'));
                    e.preventDefault();
                    e.stopImmediatePropagation();
                });
                root.addEventListener('ping', function () { log.push('after'); });
                root.classList.add('c');
                root.classList.remove('a');
                root.toggleAttribute('hidden');
                var canceled = root.dispatchEvent(new CustomEvent('ping', {
                    bubbles: true, cancelable: true, detail: {x: 7}
                }));
                return [canceled, root.className, root.classList.length,
                        root.classList.contains('c'),
                        root.hasAttribute('hidden'),
                        log.join('|')].join(';');
            })()
        """)
        assert result == "false;b c;2;true;true;ping:7:true"

        # The three attribute records are queued but not delivered yet.
        page.run_jobs()
        assert page.eval("log.join('|')") == (
            "ping:7:true|class=a b|class=a b c|hidden=null")
        assert "after" not in page.eval("log.join('|')")

        # M11-7 / M11-8: storage + Blob/File/FileReader on the same page.
        page.eval("""
            localStorage.setItem('k', 'v');
            sessionStorage.setItem('s', '1');
            globalThis.blob = new Blob(['héllo'], {type: 'TEXT/PLAIN'});
            globalThis.file = new File(['abc'], 'a.txt', {type: 'text/plain'});
            globalThis.readDone = [];
            globalThis.reader = new FileReader();
            reader.addEventListener('load', function () {
                readDone.push(reader.result + ':' + reader.readyState);
            });
            reader.addEventListener('loadend', function () {
                readDone.push('end');
            });
            reader.readAsText(blob);
        """)
        assert page.eval("blob.size") == 6
        assert page.eval("blob.type") == "text/plain"
        assert page.eval("file.name") == "a.txt"
        assert page.eval("file instanceof Blob") is True
        assert page.eval("reader.readyState") == 1
        page.run_jobs()
        assert page.eval("readDone.join('|')") == "héllo:2|end"

        # Storage lives in PageState and survives the same Page's load().
        page.load(DOC, BASE)
        assert page.eval("localStorage.getItem('k')") == "v"
        assert page.eval("sessionStorage.getItem('s')") == "1"
        assert page.eval("typeof CustomEvent") == "function"
        assert page.eval(
            "document.getElementById('root').className") == "a b"
