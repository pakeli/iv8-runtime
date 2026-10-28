"""M11-5 acceptance tests: MutationObserver.

Records are captured natively and delivered by the existing Page.run_jobs()
microtask checkpoint. There is no new Python API and no new pump.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://mutation.test/"
DOC = "<html><body><div id='root'><span id='a'>A</span></div></body></html>"

# Field order of the summary string produced by the observer below.
TYPE, TARGET, ADDED, REMOVED, PREV, NEXT, ATTR, OLD, NS = range(9)

SETUP = r"""
globalThis.obsLog = [];
globalThis.obsRecords = [];
globalThis.obsArgSame = null;
globalThis.obsThisSame = null;
globalThis.obs = new MutationObserver(function (records, observer) {
    globalThis.obsRecords.push(records);
    globalThis.obsArgSame = (observer === globalThis.obs);
    globalThis.obsThisSame = (this === globalThis.obs);
    var summaries = [];
    for (var i = 0; i < records.length; i++) {
        var r = records[i];
        var added = Array.prototype.map.call(r.addedNodes, function (n) {
            return n.id || n.tagName;
        }).join('+');
        var removed = Array.prototype.map.call(r.removedNodes, function (n) {
            return n.id || n.tagName;
        }).join('+');
        var prev = r.previousSibling
            ? (r.previousSibling.id || r.previousSibling.tagName) : '';
        var next = r.nextSibling
            ? (r.nextSibling.id || r.nextSibling.tagName) : '';
        var old = (r.oldValue === null) ? 'null' : String(r.oldValue);
        var ns = (r.attributeNamespace === null)
            ? 'null' : String(r.attributeNamespace);
        summaries.push([r.type, r.target.id || r.target.tagName, added, removed,
                        prev, next, r.attributeName || '', old, ns].join('|'));
    }
    globalThis.obsLog.push(summaries.join(';'));
});
globalThis.root = document.getElementById('root');
"""


def _setup(page):
    page.load(DOC, BASE)
    page.eval(SETUP)


def _records(page, index=0):
    text = page.eval(f"obsLog[{index}]")
    return [part.split("|") for part in text.split(";")] if text else []


def _record_count(page):
    return page.eval("obsLog.length")


# --- shape guards (both build modes) -----------------------------------------

def test_no_new_python_surface():
    for name in ("MutationObserver", "MutationRecord"):
        assert not hasattr(iv8, name)
    for name in ("observe", "disconnect", "takeRecords"):
        assert not hasattr(iv8.Page, name)
    for pump in ("run_timers", "run_jobs", "run_animation_frame",
                 "run_idle_callbacks"):
        assert callable(getattr(iv8.Page, pump))


# --- global / constructor -----------------------------------------------------

@on_only
def test_global_shape():
    with iv8.Page() as page:
        assert page.eval("typeof MutationObserver") == "function"
        assert page.eval("window.MutationObserver === MutationObserver") is True
        assert page.eval("globalThis.MutationObserver === MutationObserver") is True
        assert page.eval("typeof MutationRecord") == "undefined"
        for method in ("observe", "disconnect", "takeRecords"):
            assert page.eval(
                f"typeof MutationObserver.prototype.{method}") == "function"
        assert page.eval(
            "new MutationObserver(function () {}) instanceof MutationObserver"
        ) is True


@on_only
@pytest.mark.parametrize("expr", [
    "MutationObserver(function () {})",
    "new MutationObserver()",
    "new MutationObserver(1)",
    "new MutationObserver(null)",
    "new MutationObserver({})",
])
def test_constructor_requires_new_and_callable(expr):
    with iv8.Page() as page:
        with pytest.raises(iv8.JSError) as info:
            page.eval(expr)
        assert info.value.name == "TypeError"
        assert page.eval("1 + 1") == 2


@on_only
@pytest.mark.parametrize("expr", [
    "o.observe()",
    "o.observe(null, {childList: true})",
    "o.observe({}, {childList: true})",
    "o.observe(window, {childList: true})",
    "o.observe('x', {childList: true})",
    "o.observe(root, null)",
    "o.observe(root, 1)",
    "o.observe(root, {})",
    "o.observe(root, {childList: false, attributes: false})",
    "o.observe(root, {attributeOldValue: true})",
    "o.observe(root, {attributes: true, attributeFilter: 'x'})",
    "o.observe(root, {attributes: true, characterDataOldValue: true})",
    "MutationObserver.prototype.observe.call({}, root, {childList: true})",
    "MutationObserver.prototype.disconnect.call({})",
    "MutationObserver.prototype.takeRecords.call({})",
])
def test_observe_and_brand_validation(expr):
    with iv8.Page() as page:
        page.load(DOC, BASE)
        page.eval(
            "globalThis.o = new MutationObserver(function () {});"
            "globalThis.root = document.getElementById('root');"
        )
        with pytest.raises(iv8.JSError) as info:
            page.eval(expr)
        assert info.value.name == "TypeError"
        assert page.eval("1 + 1") == 2


# --- childList records --------------------------------------------------------

@on_only
def test_append_child_record():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true});
            var c = document.createElement('div');
            c.id = 'c1';
            root.appendChild(c);
        """)
        assert _record_count(page) == 0
        page.run_jobs()
        assert _record_count(page) == 1
        assert page.eval("obsArgSame") is True
        assert page.eval("obsThisSame") is True
        record = _records(page)[0]
        assert record[TYPE] == "childList"
        assert record[TARGET] == "root"
        assert record[ADDED] == "c1"
        assert record[REMOVED] == ""
        assert record[PREV] == "a"
        assert record[NEXT] == ""
        assert record[ATTR] == ""
        assert record[OLD] == "null"
        assert record[NS] == "null"


@on_only
def test_remove_child_record():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true});
            root.removeChild(document.getElementById('a'));
        """)
        page.run_jobs()
        record = _records(page)[0]
        assert record[TYPE] == "childList"
        assert record[TARGET] == "root"
        assert record[ADDED] == ""
        assert record[REMOVED] == "a"
        assert record[PREV] == ""
        assert record[NEXT] == ""


@on_only
def test_insert_before_record():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true});
            var c = document.createElement('b');
            c.id = 'c1';
            root.insertBefore(c, document.getElementById('a'));
        """)
        page.run_jobs()
        record = _records(page)[0]
        assert record[ADDED] == "c1"
        assert record[PREV] == ""
        assert record[NEXT] == "a"


@on_only
def test_same_parent_reorder_is_removal_plus_addition():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            var c = document.createElement('b');
            c.id = 'c1';
            root.appendChild(c);
            obs.observe(root, {childList: true});
            root.appendChild(document.getElementById('a'));
        """)
        page.run_jobs()
        records = _records(page)
        assert len(records) == 2
        assert records[0][TYPE] == "childList"
        assert records[0][REMOVED] == "a"
        assert records[0][PREV] == ""
        assert records[0][NEXT] == "c1"
        assert records[1][ADDED] == "a"
        assert records[1][PREV] == "c1"
        assert records[1][NEXT] == ""


@on_only
def test_cross_parent_move_records_both_parents():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            var box = document.createElement('div');
            box.id = 'box';
            document.body.appendChild(box);
            obs.observe(root, {childList: true});
            obs.observe(box, {childList: true});
            box.appendChild(document.getElementById('a'));
        """)
        page.run_jobs()
        records = _records(page)
        assert len(records) == 2
        assert records[0][TARGET] == "root"
        assert records[0][REMOVED] == "a"
        assert records[1][TARGET] == "box"
        assert records[1][ADDED] == "a"


@on_only
def test_insert_before_self_is_a_noop():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true});
            root.insertBefore(document.getElementById('a'),
                              document.getElementById('a'));
        """)
        page.run_jobs()
        assert _record_count(page) == 0


@on_only
def test_text_content_removes_all_children():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            var c = document.createElement('b');
            c.id = 'c1';
            root.appendChild(c);
            obs.observe(root, {childList: true});
            root.textContent = 'x';
        """)
        page.run_jobs()
        record = _records(page)[0]
        assert record[TARGET] == "root"
        assert record[ADDED] == ""
        assert record[REMOVED] == "a+c1"
        assert record[PREV] == ""
        assert record[NEXT] == ""
        assert page.eval("root.children.length") == 0


@on_only
def test_text_content_without_children_has_no_record():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            var c = document.createElement('b');
            c.id = 'c1';
            obs.observe(root, {childList: true});
            c.textContent = 'x';
        """)
        page.run_jobs()
        assert _record_count(page) == 0


# --- attribute records --------------------------------------------------------

@on_only
def test_attribute_records_and_old_values():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {attributes: true, attributeOldValue: true});
            root.setAttribute('data-x', '1');
            root.setAttribute('data-x', '1');
            root.setAttribute('data-x', '2');
            root.removeAttribute('data-x');
            root.removeAttribute('data-x');
        """)
        page.run_jobs()
        records = _records(page)
        assert len(records) == 4
        assert all(r[TYPE] == "attributes" for r in records)
        assert all(r[TARGET] == "root" for r in records)
        assert all(r[ATTR] == "data-x" for r in records)
        assert all(r[NS] == "null" for r in records)
        assert [r[OLD] for r in records] == ["null", "1", "1", "2"]


@on_only
def test_attribute_old_value_is_off_by_default():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {attributes: true});
            root.setAttribute('data-x', '1');
            root.setAttribute('data-x', '2');
        """)
        page.run_jobs()
        records = _records(page)
        assert len(records) == 2
        assert [r[OLD] for r in records] == ["null", "null"]


@on_only
def test_remove_missing_attribute_has_no_record():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {attributes: true});
            root.removeAttribute('data-nope');
        """)
        page.run_jobs()
        assert _record_count(page) == 0


@on_only
def test_id_and_class_via_set_attribute_but_not_property_writes():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {attributes: true, attributeOldValue: true});
            root.setAttribute('id', 'new-id');
            root.setAttribute('class', 'x y');
            root.id = 'ignored';
            root.className = 'ignored';
        """)
        page.run_jobs()
        records = _records(page)
        assert len(records) == 2
        assert records[0][ATTR] == "id"
        assert records[0][OLD] == "root"
        assert records[1][ATTR] == "class"
        assert records[1][OLD] == "null"
        assert page.eval("root.id") == "new-id"
        assert page.eval("root.className") == "x y"


@on_only
def test_attribute_filter_is_case_insensitive_and_can_be_empty():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {attributes: true,
                               attributeFilter: ['DATA-X']});
            root.setAttribute('data-x', '1');
            root.setAttribute('data-y', '2');
        """)
        page.run_jobs()
        records = _records(page)
        assert len(records) == 1
        assert records[0][ATTR] == "data-x"
        page.eval("""
            obs.observe(root, {attributes: true, attributeFilter: []});
            root.setAttribute('data-z', '3');
        """)
        page.run_jobs()
        assert _record_count(page) == 1


@on_only
def test_character_data_option_is_inert():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {characterData: true});
            root.textContent = 'x';
        """)
        page.run_jobs()
        assert _record_count(page) == 0


# --- subtree / document -------------------------------------------------------

@on_only
def test_subtree_matches_descendant_mutations():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true, subtree: true});
            var c = document.createElement('b');
            c.id = 'c1';
            document.getElementById('a').appendChild(c);
        """)
        page.run_jobs()
        record = _records(page)[0]
        assert record[TARGET] == "a"
        assert record[ADDED] == "c1"


@on_only
def test_without_subtree_descendant_mutations_are_ignored():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true});
            var c = document.createElement('b');
            c.id = 'c1';
            document.getElementById('a').appendChild(c);
        """)
        page.run_jobs()
        assert _record_count(page) == 0


@on_only
def test_document_target_with_subtree_observes_descendants():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(document, {childList: true, subtree: true,
                                   attributes: true});
            var c = document.createElement('b');
            c.id = 'c1';
            root.appendChild(c);
            root.setAttribute('data-x', '1');
        """)
        page.run_jobs()
        records = _records(page)
        assert len(records) == 2
        assert records[0][TYPE] == "childList"
        assert records[0][TARGET] == "root"
        assert records[1][TYPE] == "attributes"
        assert records[1][TARGET] == "root"


@on_only
def test_document_target_without_subtree_is_inert():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(document, {childList: true});
            var c = document.createElement('b');
            c.id = 'c1';
            root.appendChild(c);
        """)
        page.run_jobs()
        assert _record_count(page) == 0


# --- delivery ----------------------------------------------------------------

@on_only
def test_only_run_jobs_delivers():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true});
            var c = document.createElement('b');
            c.id = 'c1';
            root.appendChild(c);
        """)
        page.run_timers()
        page.run_animation_frame()
        page.run_idle_callbacks()
        assert _record_count(page) == 0
        page.run_jobs()
        assert _record_count(page) == 1


@on_only
def test_records_are_delivered_in_mutation_order():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true});
            var c1 = document.createElement('b');
            c1.id = 'c1';
            var c2 = document.createElement('b');
            c2.id = 'c2';
            root.appendChild(c1);
            root.appendChild(c2);
        """)
        page.run_jobs()
        records = _records(page)
        assert len(records) == 2
        assert records[0][ADDED] == "c1"
        assert records[1][ADDED] == "c2"


@on_only
def test_observer_creation_order_is_delivery_order():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.order = [];
            globalThis.o1 = new MutationObserver(function () {
                order.push('o1');
            });
            globalThis.o2 = new MutationObserver(function () {
                order.push('o2');
            });
            o1.observe(root, {childList: true});
            o2.observe(root, {childList: true});
            var c = document.createElement('b');
            c.id = 'c1';
            root.appendChild(c);
        """)
        page.run_jobs()
        assert page.eval("order.join(',')") == "o1,o2"


@on_only
def test_mutation_inside_callback_delivers_in_same_run_jobs():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.order = [];
            globalThis.o = new MutationObserver(function (records) {
                order.push('cb' + order.length + ':' + records.length);
                if (order.length === 1) {
                    var c = document.createElement('span');
                    c.id = 'c2';
                    root.appendChild(c);
                }
            });
            o.observe(root, {childList: true});
            var c = document.createElement('span');
            c.id = 'c1';
            root.appendChild(c);
        """)
        page.run_jobs()
        assert page.eval("order.join(',')") == "cb0:1,cb1:1"


@on_only
def test_interleaves_with_queue_microtask():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.order = [];
            globalThis.o = new MutationObserver(function () {
                order.push('obs');
            });
            o.observe(root, {childList: true});
            var c = document.createElement('b');
            c.id = 'c1';
            root.appendChild(c);
            queueMicrotask(function () { order.push('micro'); });
        """)
        page.run_jobs()
        assert page.eval("order.join(',')") == "obs,micro"


# --- takeRecords / disconnect / errors ---------------------------------------

@on_only
def test_take_records_returns_and_clears():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true});
            var c = document.createElement('b');
            c.id = 'c1';
            root.appendChild(c);
            globalThis.taken = obs.takeRecords();
        """)
        assert page.eval("taken.length") == 1
        assert page.eval("taken[0].type") == "childList"
        page.run_jobs()
        assert _record_count(page) == 0
        assert page.eval("obs.takeRecords().length") == 0


@on_only
def test_disconnect_discards_pending_and_future_records():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true});
            var c1 = document.createElement('b');
            c1.id = 'c1';
            root.appendChild(c1);
            obs.disconnect();
        """)
        page.run_jobs()
        assert _record_count(page) == 0
        page.eval("""
            var c2 = document.createElement('b');
            c2.id = 'c2';
            root.appendChild(c2);
        """)
        page.run_jobs()
        assert _record_count(page) == 0


@on_only
def test_throwing_callback_is_isolated():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            globalThis.order = [];
            globalThis.bad = new MutationObserver(function () {
                order.push('bad');
                throw new Error('boom');
            });
            globalThis.good = new MutationObserver(function () {
                order.push('good');
            });
            bad.observe(root, {childList: true});
            good.observe(root, {childList: true});
            var c = document.createElement('b');
            c.id = 'c1';
            root.appendChild(c);
        """)
        page.run_jobs()
        assert page.eval("order.join(',')") == "bad,good"
        assert page.eval("1 + 1") == 2


@on_only
def test_reobserve_replaces_options():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true});
            obs.observe(root, {attributes: true});
            var c = document.createElement('b');
            c.id = 'c1';
            root.appendChild(c);
            root.setAttribute('data-x', '1');
        """)
        page.run_jobs()
        records = _records(page)
        assert len(records) == 1
        assert records[0][TYPE] == "attributes"
        assert records[0][ATTR] == "data-x"


# --- generation / lifecycle ---------------------------------------------------

@on_only
def test_load_discards_observers_and_pending_records():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            obs.observe(root, {childList: true});
            var c = document.createElement('b');
            c.id = 'c1';
            root.appendChild(c);
        """)
        page.load("<html><body><div id='root'></div></body></html>", BASE)
        page.run_jobs()
        assert page.eval("typeof MutationObserver") == "function"
        assert page.eval("typeof obsLog") == "undefined"
        page.eval("""
            globalThis.log2 = [];
            globalThis.o2 = new MutationObserver(function () {
                log2.push('new');
            });
            o2.observe(document.getElementById('root'), {childList: true});
            var c = document.createElement('b');
            c.id = 'c1';
            document.getElementById('root').appendChild(c);
        """)
        page.run_jobs()
        assert page.eval("log2.join(',')") == "new"


@on_only
def test_dispose_with_pending_observer_is_clean_and_run_jobs_raises():
    page = iv8.Page()
    page.load(DOC, BASE)
    page.eval("""
        globalThis.o = new MutationObserver(function () {});
        o.observe(document.getElementById('root'), {childList: true});
        var c = document.createElement('b');
        c.id = 'c1';
        document.getElementById('root').appendChild(c);
    """)
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.run_jobs()


@on_only
def test_detached_subtree_is_observable():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            var parent = document.createElement('div');
            parent.id = 'det';
            var child = document.createElement('span');
            child.id = 'c1';
            parent.appendChild(child);
            obs.observe(parent, {childList: true});
            parent.removeChild(child);
        """)
        page.run_jobs()
        record = _records(page)[0]
        assert record[TARGET] == "det"
        assert record[REMOVED] == "c1"
