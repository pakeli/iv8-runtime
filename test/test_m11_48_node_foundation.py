"""M11-48: text/comment/DocumentFragment node foundation and live textContent."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-48.test/"


def _load(page, html, base=BASE):
    page.load(html, base)


@on_only
def test_parser_emits_text_comments_and_element_only_document_children():
    html = (
        "top<!--outside--><html><head><title>T</title></head>"
        "<body> A <div id='d'>x<!--inside-->y</div> B </body></html>tail"
    )
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const body = document.body;
              const d = document.getElementById('d');
              return [
                document.children.length,
                document.children[0].tagName,
                typeof document.childNodes,
                document.querySelectorAll('*').length,
                body.childNodes.length, body.children.length,
                body.firstChild.nodeType, body.firstChild.data,
                body.lastChild.nodeType, body.lastChild.data,
                d.childNodes.length,
                [1, 2, 3].map(i => d.childNodes[i - 1].nodeType).join(','),
                d.textContent,
                d.childNodes[1].data,
                document.documentElement.textContent
              ].join('|');
            })();
            """
        ) == ("1|HTML|object|5|3|1|3| A |3| B tail|3|3,8,3|xy|inside|"
              "T A xy B tail")
        assert page.eval("document.title") == "T"


@on_only
def test_creation_shape_for_text_comment_and_fragment():
    with iv8.Page() as page:
        _load(page, "<html><body></body></html>")
        assert page.eval(
            """
            (() => {
              const t = document.createTextNode('a😀');
              const c = document.createComment('note');
              const f = document.createDocumentFragment();
              return [
                t.nodeType, t.nodeName, t.nodeValue, t.data, t.length,
                t.textContent, String(t.parentNode), String(t.parentElement),
                t.childNodes.length, String(t.firstChild), String(t.lastChild),
                t.ownerDocument === document, t.isConnected,
                c.nodeType, c.nodeName, c.nodeValue,
                f.nodeType, f.nodeName, String(f.nodeValue),
                f.childNodes.length, f.children.length,
                String(f.parentNode), String(f.parentElement), f.isConnected,
                typeof t.appendChild, typeof t.querySelector,
                typeof t.addEventListener
              ].join('|');
            })();
            """
        ) == ("3|#text|a😀|a😀|3|a😀|null|null|0|"
              "null|null|true|false|8|#comment|note|11|#document-fragment|"
              "null|0|0|null|null|false|function|undefined|function")


@on_only
def test_parser_raw_text_rcdata_and_comments():
    html = (
        "<style>a < b { content: '>' }</style>"
        "<script>var s = '<div>';</script>"
        "<title>t<b>x</b></title>"
        "<textarea>v<b>y</b></textarea>"
        "<div id='d'>a<!--x>y-->b</div>"
        "<div id='e'>a < b</div>"
    )
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const style = document.querySelector('style');
              const script = document.querySelector('script');
              const title = document.querySelector('title');
              const ta = document.getElementById('ta') || document.querySelector('textarea');
              const d = document.getElementById('d');
              const e = document.getElementById('e');
              return [
                style.firstChild.nodeType, style.textContent,
                script.firstChild.nodeType, script.textContent,
                document.scripts.length,
                title.textContent, document.title,
                ta.value, ta.textContent, ta.firstChild.nodeType,
                d.textContent, d.childNodes.length,
                d.childNodes[1].nodeType, d.childNodes[1].data,
                e.textContent
              ].join('|');
            })();
            """
        ) == ("3|a < b { content: '>' }|3|var s = '<div>';|1|"
              "t<b>x</b>|t<b>x</b>|v<b>y</b>|v<b>y</b>|3|ab|3|8|x>y|a < b")


@on_only
def test_live_text_content_reads_and_writes():
    html = "<div id='d'>A<p id='p'>hi</p>B</div>"
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval("document.getElementById('d').textContent") == "AhiB"
        assert page.eval(
            """
            (() => {
              const d = document.getElementById('d');
              d.appendChild(document.createTextNode('!'));
              const appended = d.textContent;
              document.getElementById('p').textContent = 'x';
              const nested = d.textContent;
              d.textContent = 'z';
              const replaced = [d.textContent, d.childNodes.length,
                                d.children.length,
                                d.firstChild.nodeType].join('|');
              d.textContent = '';
              const cleared = [d.textContent, d.childNodes.length,
                               d.children.length].join('|');
              return [appended, nested, replaced, cleared].join('/');
            })();
            """
        ) == "AhiB!/AxB!/z|1|0|3/|0|0"


@on_only
def test_fragment_queries_and_insertion_semantics():
    with iv8.Page() as page:
        _load(page, "<div id='host'></div>")
        assert page.eval(
            """
            (() => {
              const f = document.createDocumentFragment();
              const a = document.createElement('span');
              a.id = 'a';
              a.appendChild(document.createTextNode('A'));
              const b = document.createElement('b');
              b.appendChild(document.createTextNode('B'));
              f.appendChild(a);
              f.appendChild(b);
              f.appendChild(document.createTextNode('T'));
              const before = [f.childNodes.length, f.textContent,
                              f.querySelector('#a').id,
                              f.getElementById('a').id,
                              f.querySelectorAll('*').length,
                              String(a.parentElement)].join('|');
              const host = document.getElementById('host');
              const returned = host.appendChild(f) === f;
              const after = [f.childNodes.length, host.childNodes.length,
                             host.textContent, host.firstChild.id,
                             host.lastChild.nodeType,
                             a.parentElement.id].join('|');
              const empty = document.createDocumentFragment();
              const emptyReturned = host.appendChild(empty) === empty;
              const unchanged = host.childNodes.length;
              return [before, returned, after, emptyReturned,
                      unchanged].join('/');
            })();
            """
        ) == ("3|ABT|a|a|2|null/true/0|3|ABT|a|3|host/true/3")


@on_only
def test_navigation_distinguishes_nodes_from_elements():
    html = (
        "<div id='root'>\n"
        "<span id='a'>A</span>\n"
        "<span id='b'>B</span>\n"
        "</div>"
    )
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const root = document.getElementById('root');
              const a = document.getElementById('a');
              const b = document.getElementById('b');
              const f = document.createDocumentFragment();
              const ft = document.createTextNode('t');
              f.appendChild(ft);
              return [
                root.childNodes.length, root.children.length,
                root.firstChild.nodeType, root.lastChild.nodeType,
                root.firstElementChild.id, root.lastElementChild.id,
                root.childElementCount,
                a.nextElementSibling.id, b.previousElementSibling.id,
                a.previousSibling.nodeType, a.nextSibling.nodeType,
                b.previousSibling.nodeType, b.nextSibling.nodeType,
                a.parentElement.id,
                String(ft.parentElement), ft.parentNode.nodeType,
                ft.isConnected
              ].join('|');
            })();
            """
        ) == ("5|2|3|3|a|b|2|b|a|3|3|3|3|root|null|11|false")


@on_only
def test_selectors_match_elements_only_and_skip_text_siblings():
    html = (
        "<div id='p'>x<span id='a'>A</span> "
        "<span id='b'>B</span>y</div><div id='q'>Q</div>"
    )
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const ids = list => list.map(n => n.id || n.tagName).join(',');
              return [
                document.querySelectorAll('*').length,
                document.querySelectorAll('span').length,
                ids(document.querySelectorAll('#p > span + span')),
                ids(document.querySelectorAll('#p > span ~ span')),
                document.querySelectorAll('#p > span').length,
                document.getElementById('p').querySelectorAll('*').length,
                document.querySelectorAll('*').every(n => n.nodeType === 1)
              ].join('|');
            })();
            """
        ) == "4|2|b|b|2|3|true"


@on_only
def test_clone_equality_contains_and_connectivity_across_kinds():
    html = "<div id='d'>A<p id='p'>hi</p>B</div>"
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const d = document.getElementById('d');
              const t = d.firstChild;
              const clone = d.cloneNode(true);
              const textClone = t.cloneNode();
              const f = document.createDocumentFragment();
              f.appendChild(d.cloneNode(true));
              const fClone = f.cloneNode(true);
              return [
                clone.nodeType, clone.isEqualNode(d), clone.isSameNode(d),
                clone.textContent,
                textClone.nodeType, textClone.isEqualNode(t),
                fClone.nodeType, fClone.isEqualNode(f),
                d.contains(t), t.contains(t), t.contains(d),
                document.contains(t),
                t.isConnected, f.firstChild.isConnected
              ].join('|');
            })();
            """
        ) == "1|true|false|AhiB|3|true|11|true|true|true|false|true|true|false"


@on_only
def test_tree_editing_accepts_text_nodes_and_fragments():
    with iv8.Page() as page:
        _load(page, "<div id='d'></div>")
        assert page.eval(
            """
            (() => {
              const d = document.getElementById('d');
              const a = document.createTextNode('a');
              const b = document.createTextNode('b');
              const f = document.createDocumentFragment();
              f.appendChild(document.createTextNode('x'));
              f.appendChild(document.createTextNode('y'));
              d.appendChild(a);
              d.insertBefore(b, a);
              const order = [d.textContent, d.childNodes.length].join('|');
              d.replaceChild(f, a);
              const replaced = [d.textContent,
                                d.childNodes.map(n => n.nodeType).join(',')].join('|');
              d.removeChild(b);
              const removed = [d.textContent, d.childNodes.length].join('|');
              let error = '';
              try { d.appendChild(42); } catch (e) { error = e.name; }
              return [order, replaced, removed, error,
                      typeof a.appendChild].join('/');
            })();
            """
        ) == "ba|2/bxy|3,3,3/xy|2/TypeError/function"


@on_only
def test_character_data_mutation_observer_records():
    html = "<div id='host'><span id='s'>old</span></div>"
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              window.__direct = [];
              window.__subtree = [];
              window.__doc = [];
              const text = document.getElementById('s').firstChild;
              new MutationObserver(rs => {
                for (const r of rs) {
                  window.__direct.push(
                    [r.type, r.target.nodeType, r.oldValue].join(','));
                }
              }).observe(text, {characterData: true, characterDataOldValue: true});
              new MutationObserver(rs => {
                for (const r of rs) {
                  window.__subtree.push(
                    [r.type, r.target.nodeType, r.oldValue].join(','));
                }
              }).observe(document.getElementById('host'),
                         {characterData: true, subtree: true});
              new MutationObserver(rs => {
                for (const r of rs) {
                  window.__doc.push(r.type + ',' + r.target.nodeType);
                }
              }).observe(document, {characterData: true, subtree: true});
              text.data = 'new';
              text.nodeValue = 'x';
              return [text.textContent, text.length].join('|');
            })();
            """
        ) == "x|1"
        page.run_jobs()
        assert page.eval("window.__direct.join('/')") ==             "characterData,3,old/characterData,3,new"
        assert page.eval("window.__subtree.join('/')") ==             "characterData,3,/characterData,3,"
        assert page.eval("window.__doc.join('/')") ==             "characterData,3/characterData,3"
        assert page.eval(
            """
            (() => {
              window.__text_content = [];
              const text = document.getElementById('s').firstChild;
              new MutationObserver(rs => {
                for (const r of rs) {
                  window.__text_content.push(
                    [r.type, r.target.nodeType, r.oldValue].join(','));
                }
              }).observe(text, {characterData: true, characterDataOldValue: true});
              text.textContent = 'z';
              return text.textContent;
            })();
            """
        ) == "z"
        page.run_jobs()
        assert page.eval("window.__text_content.join('/')") ==             "characterData,3,x"


@on_only
def test_child_list_records_include_text_nodes_and_fragments():
    with iv8.Page() as page:
        _load(page, "<div id='host'></div>")
        assert page.eval(
            """
            (() => {
              window.__host_records = [];
              window.__frag_records = [];
              const host = document.getElementById('host');
              const f = document.createDocumentFragment();
              const i = document.createElement('i');
              f.appendChild(i);
              f.appendChild(document.createTextNode('t'));
              const summary = r => [r.type,
                r.addedNodes.map(n => n.nodeType).join(','),
                r.removedNodes.map(n => n.nodeType).join(',')].join('|');
              new MutationObserver(rs => {
                for (const r of rs) window.__host_records.push(summary(r));
              }).observe(host, {childList: true});
              new MutationObserver(rs => {
                for (const r of rs) window.__frag_records.push(summary(r));
              }).observe(f, {childList: true});
              host.appendChild(f);
              return [f.childNodes.length, host.childNodes.length].join('|');
            })();
            """
        ) == "0|2"
        page.run_jobs()
        assert page.eval("window.__frag_records.join('/')") ==             "childList||1,3"
        assert page.eval("window.__host_records.join('/')") ==             "childList|1,3|"
        assert page.eval(
            """
            (() => {
              const host = document.getElementById('host');
              host.textContent = 'y';
              return host.childNodes[0].nodeType;
            })();
            """
        ) == 3
        page.run_jobs()
        assert page.eval("window.__host_records.join('/')") ==             "childList|1,3|/childList|3|1,3"


@on_only
def test_title_textarea_and_option_text_are_live():
    html = (
        "<html><head><title id='t'>Old</title></head><body>"
        "<select><option id='o'>A<span>B</span></option></select>"
        "<textarea id='ta'>v<b>y</b></textarea>"
        "</body></html>"
    )
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const title = document.getElementById('t');
              const o = document.getElementById('o');
              const ta = document.getElementById('ta');
              const before = [document.title, title.childNodes.length,
                              title.firstChild.nodeType,
                              o.text, o.value, o.label,
                              ta.value, ta.defaultValue,
                              ta.textContent].join('|');
              document.title = 'New';
              const after = [document.title, title.childNodes.length,
                             title.firstChild.data].join('|');
              return before + '/' + after;
            })();
            """
        ) == ("Old|1|3|AB|AB|AB|v<b>y</b>|v<b>y</b>|v<b>y</b>/"
              "New|1|New")
        with pytest.raises(iv8.JSError):
            page.eval(
                "document.title = {toString() { throw new Error('bad'); }};"
            )
        assert page.eval("document.title") == "New"


@on_only
def test_node_hosts_are_access_traced():
    with iv8.Page() as page:
        _load(page, "<html><body></body></html>")
        page.start_access_trace()
        assert page.eval(
            """
            (() => {
              const t = document.createTextNode('x');
              const kind = t.nodeType;
              const data = t.data;
              t.data = 'y';
              const f = document.createDocumentFragment();
              const name = f.nodeName;
              return kind + data + name;
            })();
            """,
            name="nodes.js",
        ) == "3x#document-fragment"
        events = page.read_access_trace()["events"]
        node_events = [event for event in events
                       if event["object_type"] in ("text", "comment",
                                                   "document_fragment")]
        assert [(event["object_type"], event["member"], event["operation"])
                for event in node_events] == [
            ("text", "nodeType", "get"),
            ("text", "data", "get"),
            ("text", "data", "set"),
            ("document_fragment", "nodeName", "get"),
        ]
        assert node_events[2]["value_summary"] == "string:1:y"

        # The new node kinds are valid object-type filters and appear in the
        # coverage list.
        page.start_access_trace(object_types=["text"])
        assert page.eval(
            """
            (() => {
              const t = document.createTextNode('z');
              t.nodeType;
              document.body.nodeType;
              return t.data;
            })();
            """
        ) == "z"
        filtered = page.read_access_trace()
        assert [event["object_type"] for event in filtered["events"]] == [
            "text", "text"]
        assert [event["member"] for event in filtered["events"]] == [
            "nodeType", "data"]
        assert "text" in filtered["coverage"]["object_types"]
