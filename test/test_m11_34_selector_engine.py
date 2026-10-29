"""M11-34 acceptance tests: CSS selector engine expansion."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-34.test/"

DOC = """
<html><body>
<div id='root' class='box main' data-kind='container'>
  <p id='p1' class='item first' data-tags='a b' lang='en-US'>one</p>
  <p id='p2' class='item' data-tags='b c' lang='en'>two</p>
  <span id='s1' class='item special' data-note='hello world'>three</span>
  <section id='sec'>
    <a id='a1' href='https://example.com/x.png' class='item link'>link</a>
    <a id='a2' href='/local' class='link'>local</a>
  </section>
  <ul id='list'><li id='l1'>1</li><li id='l2'>2</li><li id='l3'>3</li></ul>
</div>
<div id='outside' class='box'><p id='p3' class='item'>out</p></div>
</body></html>
"""


def _load(page):
    page.load(DOC, BASE)
    page.eval("""
        for (const id of ['root', 'p1', 'p2', 's1', 'sec', 'a1', 'a2',
                          'list', 'outside', 'p3']) {
          globalThis[id] = document.getElementById(id);
        }
    """)


# --- selector lists / order ----------------------------------------------------

@on_only
def test_selector_lists_preserve_document_order_and_dedupe():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            "document.querySelectorAll('p, span').map(e => e.id).join(',')"
        ) == "p1,p2,s1,p3"
        # p1/p2/p3 occur in both branches but only once in the result.
        assert page.eval(
            "document.querySelectorAll('p, .item').map(e => e.id).join(',')"
        ) == "p1,p2,s1,a1,p3"


# --- combinators ---------------------------------------------------------------

@on_only
def test_descendant_and_child_combinators():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            "document.querySelectorAll('#root .link').map(e => e.id).join(',')"
        ) == "a1,a2"
        assert page.eval(
            "document.querySelectorAll('#root > p').map(e => e.id).join(',')"
        ) == "p1,p2"
        assert page.eval(
            "document.querySelectorAll('#root > section > a').map(e => e.id).join(',')"
        ) == "a1,a2"
        assert page.eval("document.querySelectorAll('#root > a').length") == 0
        assert page.eval(
            "document.querySelector('body > #root').id") == "root"


@on_only
def test_sibling_combinators():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            "document.querySelectorAll('p.first + p').map(e => e.id).join(',')"
        ) == "p2"
        assert page.eval(
            "document.querySelectorAll('#p2 + span').map(e => e.id).join(',')"
        ) == "s1"
        assert page.eval(
            "document.querySelectorAll('p.first ~ span').map(e => e.id).join(',')"
        ) == "s1"
        assert page.eval(
            "document.querySelectorAll('p.first ~ ul').map(e => e.id).join(',')"
        ) == "list"
        assert page.eval("document.querySelectorAll('p.first ~ a').length") == 0
        assert page.eval("document.querySelectorAll('#p2 + p').length") == 0


@on_only
def test_compound_selectors():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            "document.querySelectorAll('p.item#p1[data-tags~=\"a\"]').length"
        ) == 1
        assert page.eval(
            "document.querySelector('div#root.box.main[data-kind=container]').id"
        ) == "root"
        assert page.eval(
            "document.querySelector('*.special').id") == "s1"
        assert page.eval(
            "document.querySelectorAll('p.item.first + p.item').map(e => e.id).join(',')"
        ) == "p2"
        assert page.eval(
            "document.querySelectorAll('#root.box > p.item').map(e => e.id).join(',')"
        ) == "p1,p2"


# --- attribute selectors --------------------------------------------------------

@on_only
def test_attribute_selectors():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("document.querySelector('[data-kind]').id") == "root"
        assert page.eval(
            "document.querySelector('[data-kind=container]').id") == "root"
        assert page.eval(
            "document.querySelector('[DATA-KIND=container]').id") == "root"
        assert page.eval("document.querySelectorAll('[data-kind=CONTAINER]').length") == 0
        assert page.eval(
            "document.querySelectorAll('[data-tags~=b]').map(e => e.id).join(',')"
        ) == "p1,p2"
        assert page.eval(
            "document.querySelectorAll('[lang|=en]').map(e => e.id).join(',')"
        ) == "p1,p2"
        assert page.eval(
            "document.querySelectorAll('[href^=\"https://\"]').map(e => e.id).join(',')"
        ) == "a1"
        assert page.eval(
            "document.querySelectorAll('[href$=\".png\"]').map(e => e.id).join(',')"
        ) == "a1"
        assert page.eval(
            "document.querySelectorAll('[href*=\"example\"]').map(e => e.id).join(',')"
        ) == "a1"
        assert page.eval(
            "document.querySelector('[data-note=\"hello world\"]').id") == "s1"
        assert page.eval("document.querySelectorAll('[href^=\"\"]').length") == 0
        assert page.eval(
            "document.querySelectorAll('[class~=item]').map(e => e.id).join(',')"
        ) == "p1,p2,s1,a1,p3"
        assert page.eval("document.querySelectorAll('[id]').length") == 13
        assert page.eval("document.querySelectorAll('[class]').length") == 8


@on_only
def test_universal_selector():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            "document.querySelectorAll('*').length === "
            "document.getElementsByTagName('*').length"
        ) is True
        assert page.eval("root.querySelector('*').id") == "root"
        assert page.eval("root.querySelectorAll('*').length") == 11
        assert page.eval(
            "document.querySelectorAll('*.box').map(e => e.id).join(',')"
        ) == "root,outside"


# --- matches / closest ----------------------------------------------------------

@on_only
def test_matches_combinators_and_attributes():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              return [p1.matches('div > p'),
                      p1.matches('div > span'),
                      s1.matches('p + span'),
                      s1.matches('p ~ span'),
                      p2.matches('p.first + p'),
                      p2.matches('p.first ~ p'),
                      a1.matches('#sec a[href^="https"]'),
                      a2.matches('a[href^="https"]'),
                      p1.matches('[data-tags~=a][lang|=en]'),
                      root.matches('div#root.box[data-kind=container]')].join(',');
            })();
            """
        ) == "true,false,true,true,true,true,true,false,true,true"


@on_only
def test_closest_complex_selectors():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              return [a1.closest('#root > section').id,
                      a1.closest('div.box a').id,
                      a1.closest('body > div > section > a').id,
                      list.closest('section + ul').id,
                      p3.closest('#root') === null].join(',');
            })();
            """
        ) == "sec,a1,a1,list,true"


# --- element-scoped queries -----------------------------------------------------

@on_only
def test_element_scoped_queries():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("root.querySelector('section a').id") == "a1"
        assert page.eval(
            "root.querySelectorAll('p, a').map(e => e.id).join(',')"
        ) == "p1,p2,a1,a2"
        assert page.eval("root.querySelectorAll('div').length") == 1  # self
        assert page.eval("root.querySelectorAll('#outside').length") == 0
        assert page.eval("root.querySelectorAll('div, span').length") == 2


# --- live tree / invalid selectors ---------------------------------------------

@on_only
def test_selector_results_follow_live_tree_updates():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("document.querySelectorAll('[data-kind=container]').length") == 1
        root = "document.getElementById('root')"
        page.eval(f"{root}.setAttribute('data-kind', 'changed')")
        assert page.eval("document.querySelectorAll('[data-kind=container]').length") == 0
        assert page.eval("document.querySelectorAll('[data-kind=changed]').length") == 1
        page.eval("p1.setAttribute('class', 'other')")
        assert page.eval("p1.matches('.first')") is False
        assert page.eval("document.querySelectorAll('.item').length") == 4
        page.eval("sec.remove()")
        assert page.eval("document.querySelectorAll('#root .link').length") == 0
        page.eval("root.appendChild(sec)")
        assert page.eval("document.querySelectorAll('#root .link').length") == 2


@on_only
def test_detached_tree_combinators():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              const box = document.createElement('div');
              const inner = document.createElement('span');
              box.appendChild(inner);
              return [inner.matches('div > span'),
                      inner.matches('div span'),
                      inner.matches('div + span')].join(',');
            })();
            """
        ) == "true,true,false"


@on_only
def test_invalid_and_unsupported_selectors_match_nothing():
    with iv8.Page() as page:
        _load(page)
        selectors = ("", ":first-child", "::before", "div >", "div,,span",
                     "div#", "[attr=value i]", "div + + span", "div:not(.x)")
        for selector in selectors:
            expr = selector.replace("\\", "\\\\").replace("'", "\\'")
            assert page.eval(
                f"document.querySelectorAll('{expr}').length") == 0, selector
            assert page.eval(
                f"document.querySelector('{expr}') === null") is True, selector
            assert page.eval(
                f"p1.matches('{expr}')") is False, selector
            assert page.eval(
                f"p1.closest('{expr}') === null") is True, selector
