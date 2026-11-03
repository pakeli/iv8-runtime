"""M11-49: HTML string APIs, entity decoding, and variadic tree methods."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-49.test/"


def _load(page, html, base=BASE):
    page.load(html, base)


@on_only
def test_inner_html_getter_setter_and_attribute_order():
    html = "<div id='host'><span id='a' class='c'>A</span>tail</div>"
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval("document.getElementById('host').innerHTML") == \
            '<span id="a" class="c">A</span>tail'
        assert page.eval(
            """
            (() => {
              const host = document.getElementById('host');
              host.innerHTML =
                  "<b data-z='1' data-a='2'>B</b><!--note-->text";
              return [host.innerHTML,
                      host.childNodes.length,
                      host.childNodes[0].nodeType,
                      host.childNodes[1].nodeType,
                      host.childNodes[2].nodeType,
                      host.querySelector('b').textContent,
                      host.childNodes[1].data].join('|');
            })();
            """
        ) == ('<b data-a="2" data-z="1">B</b><!--note-->text|3|1|8|3|B|'
              'note')


@on_only
def test_entity_decoding_and_serialization_escaping():
    with iv8.Page() as page:
        _load(page, "<html><body></body></html>")
        assert page.eval(
            """
            (() => {
              const d = document.createElement('div');
              d.innerHTML =
                  'a &amp; b &lt;c&gt; &nbsp; &#65; &#x1F600; &bogus;';
              const text = d.textContent;
              const html = d.innerHTML;
              const a = document.createElement('div');
              a.setAttribute('data-x', 'a"b&c');
              a.textContent = 'x';
              const attr = a.innerHTML;
              a.innerHTML = '<i title="a&amp;b&quot;c">x</i>';
              return [text, html, attr,
                      a.getAttribute('title'), a.innerHTML].join('|');
            })();
            """
        ) == ('a & b <c>   A 😀 &bogus;|'
              'a &amp; b &lt;c&gt; &nbsp; A 😀 &amp;bogus;|'
              'data-x="a&quot;b&amp;c"x|a&b"c|'
              '<i title="a&amp;b&quot;c">x</i>')


@on_only
def test_loaded_document_entities_and_outer_html():
    html = ("<div id='d' data-x='a&amp;b' title='q&quot;r'>"
            "x &lt; y &amp; z</div>")
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const d = document.getElementById('d');
              return [d.getAttribute('data-x'), d.getAttribute('title'),
                      d.textContent, d.outerHTML].join('|');
            })();
            """
        ) == ('a&b|q"r|x < y & z|'
              '<div id="d" data-x="a&amp;b" title="q&quot;r">'
              'x &lt; y &amp; z</div>')


@on_only
def test_inner_html_initializes_controls_and_keeps_scripts_inert():
    with iv8.Page() as page:
        _load(page, "<div id='host'></div>")
        assert page.eval(
            """
            (() => {
              const host = document.getElementById('host');
              host.innerHTML =
                  "<script>globalThis.ran = 1;</script>" +
                  "<select id='s'><option selected>1</option>" +
                  "<option selected>2</option></select>" +
                  "<textarea id='ta'>hi</textarea>" +
                  "<input id='in' value='v'>";
              const s = document.getElementById('s');
              return [typeof globalThis.ran, document.scripts.length,
                      s.value, s.selectedIndex, s.options.length,
                      document.getElementById('ta').value,
                      document.getElementById('in').value,
                      document.currentScript === null].join('|');
            })();
            """
        ) == "undefined|1|1|0|2|hi|v|true"


@on_only
def test_outer_html_getter_and_setter():
    html = "<div id='host'><span id='a'>A</span></div>"
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const a = document.getElementById('a');
              const before = a.outerHTML;
              a.outerHTML = '<b id="b">B</b><!--c-->';
              const host = document.getElementById('host');
              const after = [host.innerHTML, document.getElementById('a'),
                             document.getElementById('b').textContent,
                             host.childNodes[1].data].join('|');
              const detached = document.createElement('div');
              detached.outerHTML = '<p>x</p>';
              return [before, after, detached.tagName,
                      detached.childNodes.length].join('/');
            })();
            """
        ) == ('<span id="a">A</span>/'
              '<b id="b">B</b><!--c-->|null|B|c/DIV/0')


@on_only
def test_insert_adjacent_html_element_and_text():
    html = "<div id='host'><span id='ref'>R</span></div>"
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const ref = document.getElementById('ref');
              ref.insertAdjacentHTML('beforebegin', '<b>bb</b>');
              ref.insertAdjacentHTML('afterbegin', '<i>ab</i>');
              ref.insertAdjacentHTML('beforeend', '<u>be</u>');
              ref.insertAdjacentHTML('afterend', '<em>ae</em>');
              const el = document.createElement('s');
              el.id = 'el';
              const returned = ref.insertAdjacentElement('beforeend', el) === el;
              ref.insertAdjacentText('afterbegin', 'T');
              return [document.getElementById('host').innerHTML,
                      returned].join('|');
            })();
            """
        ) == ('<b>bb</b><span id="ref">T<i>ab</i>R<u>be</u><s id="el"></s>'
              '</span><em>ae</em>|true')
        with pytest.raises(iv8.JSError) as info:
            page.eval("document.getElementById('ref').insertAdjacentHTML("
                      "'nope', '<b>x</b>')")
        assert info.value.name == "SyntaxError"
        with pytest.raises(iv8.JSError) as info:
            page.eval("document.createElement('div').insertAdjacentHTML("
                      "'beforebegin', '<b>x</b>')")
        assert info.value.name == "TypeError"
        with pytest.raises(iv8.JSError) as info:
            page.eval("document.getElementById('ref').insertAdjacentElement("
                      "'beforeend', document.createTextNode('x'))")
        assert info.value.name == "TypeError"


@on_only
def test_append_and_prepend_accept_strings_nodes_and_fragments():
    html = "<div id='host'><b id='b'>B</b></div>"
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const host = document.getElementById('host');
              const f = document.createDocumentFragment();
              f.append('f1');
              const result = host.append('a', document.createElement('i'),
                                         document.createTextNode('c'), f);
              const after = [String(result), host.childNodes.length,
                             Array.from(host.childNodes).map(n => n.nodeType)
                                 .join(','),
                             host.textContent, f.childNodes.length].join('|');
              host.prepend('z');
              return [after, host.innerHTML, host.firstChild.data].join('/');
            })();
            """
        ) == ('undefined|5|1,3,1,3,3|Bacf1|0/'
              'z<b id="b">B</b>a<i></i>cf1/z')


@on_only
def test_before_after_replace_with_and_replace_children():
    html = "<div id='host'><b id='b'>B</b></div>"
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const host = document.getElementById('host');
              const b = document.getElementById('b');
              b.before('x');
              b.after(document.createComment('c'));
              b.replaceWith('r1', document.createElement('i'));
              const afterReplace = host.innerHTML;
              const detached = document.createElement('span');
              detached.before('nope');
              const detachedEmpty = detached.parentNode === null;
              host.replaceChildren('p', document.createElement('u'));
              const replaced = [host.innerHTML, host.childNodes.length,
                                host.firstChild.nodeType].join('|');
              host.replaceChildren();
              return [afterReplace, detachedEmpty, replaced,
                      host.childNodes.length, host.innerHTML].join('/');
            })();
            """
        ) == ('xr1<i></i><!--c-->/true/p<u></u>|2|3/0/')


@on_only
def test_variadic_string_conversion_and_child_node_methods():
    html = "<div id='host'>t</div>"
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              const host = document.getElementById('host');
              host.append(null, undefined, 5, true, {toString: () => 'o'});
              const text = host.textContent;
              const t = host.firstChild;
              t.after('!');
              t.replaceWith('N');
              return [text, host.innerHTML, host.firstChild.data,
                      typeof t.before].join('|');
            })();
            """
        ) == "tnullundefined5trueo|N!nullundefined5trueo|N|function"
        with pytest.raises(iv8.JSError) as info:
            page.eval("document.getElementById('host').append(Symbol('x'))")
        assert info.value.name == "TypeError"


@on_only
def test_script_style_raw_and_title_textarea_escaping():
    with iv8.Page() as page:
        _load(page, "<html><body></body></html>")
        assert page.eval(
            """
            (() => {
              const d = document.createElement('div');
              d.innerHTML =
                  '<script>if (a < b) x();</script>' +
                  '<style>a > b {}</style>' +
                  '<title>a<b</title><textarea>v&lt;y</textarea>';
              return [d.querySelector('script').textContent, d.innerHTML]
                  .join('|');
            })();
            """
        ) == ('if (a < b) x();|'
              '<script>if (a < b) x();</script><style>a > b {}</style>'
              '<title>a&lt;b</title><textarea>v&lt;y</textarea>')


@on_only
def test_inner_html_and_append_mutation_records():
    html = "<div id='host'><b id='b'>B</b>tail</div>"
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              window.__records = [];
              const host = document.getElementById('host');
              const summary = r => [r.type,
                r.addedNodes.map(n => n.nodeType).join(','),
                r.removedNodes.map(n => n.nodeType).join(','),
                r.previousSibling ? r.previousSibling.nodeType : '',
                r.nextSibling ? r.nextSibling.nodeType : ''].join('|');
              new MutationObserver(rs => {
                for (const r of rs) window.__records.push(summary(r));
              }).observe(host, {childList: true});
              host.innerHTML = '<i>I</i>';
              return host.childNodes.length;
            })();
            """
        ) == 1
        page.run_jobs()
        assert page.eval("window.__records.join('/')") == "childList|1|1,3||"
        assert page.eval(
            """
            (() => {
              const host = document.getElementById('host');
              host.append('x', 'y');
              return host.childNodes.length;
            })();
            """
        ) == 3
        page.run_jobs()
        assert page.eval("window.__records.join('/')") == \
            "childList|1|1,3||/childList|3,3||1|"


@on_only
def test_outer_html_setter_mutation_record_order():
    html = "<div id='host'><span id='a'>A</span></div>"
    with iv8.Page() as page:
        _load(page, html)
        assert page.eval(
            """
            (() => {
              window.__records = [];
              const summary = r => [r.type,
                r.addedNodes.map(n => n.tagName || n.nodeType).join(','),
                r.removedNodes.map(n => n.tagName || n.nodeType).join(',')]
                .join('|');
              new MutationObserver(rs => {
                for (const r of rs) window.__records.push(summary(r));
              }).observe(document.getElementById('host'),
                         {childList: true});
              document.getElementById('a').outerHTML =
                  '<b id="b">B</b><i></i>';
              return document.getElementById('host').childNodes.length;
            })();
            """
        ) == 2
        page.run_jobs()
        assert page.eval("window.__records.join('/')") == \
            "childList||SPAN/childList|B,I|"
