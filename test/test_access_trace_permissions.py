"""Step 5f: access trace for navigator.permissions and PermissionStatus."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-permissions.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


@on_only
def test_permissions_query_and_status_accesses_are_traced():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            """
            globalThis.geo = null;
            navigator.permissions.query({name: 'geolocation'})
                .then(s => { geo = s; });
            """,
            name="probe.js"
        )
        page.run_jobs()
        assert page.eval("geo.name + '|' + geo.state", name="read.js") == (
            "geolocation|prompt")
        assert page.eval(
            """
            geo.addEventListener('change', () => {});
            geo.dispatchEvent({type: 'change'});
            """,
            name="read.js"
        ) is False

        events = _events(page)
        assert [(event["object_type"], event["label"], event["member"],
                 event["operation"]) for event in events] == [
            ("navigator", "navigator", "permissions", "get"),
            ("permissions", "navigator.permissions", "query", "call"),
            ("permission_status", "permissionStatus", "name", "get"),
            ("permission_status", "permissionStatus", "state", "get"),
            ("permission_status", "permissionStatus", "addEventListener", "call"),
            ("permission_status", "permissionStatus", "dispatchEvent", "call"),
        ]
        assert [event["source_kind"] for event in events] == [
            "eval", "eval", "eval", "eval", "eval", "eval"]
        assert [event["resource_name"] for event in events] == [
            "probe.js", "probe.js", "read.js", "read.js", "read.js", "read.js"]
        assert len({event["object_id"] for event in events
                    if event["object_type"] == "permission_status"}) == 1


@on_only
def test_permission_status_keeps_plain_shape_and_identity_is_per_name():
    with iv8.Page() as page:
        _load(page)
        page.eval(
            """
            globalThis.s1 = null;
            globalThis.s2 = null;
            globalThis.cam = null;
            navigator.permissions.query({name: 'geolocation'})
                .then(s => { s1 = s; });
            navigator.permissions.query({name: 'geolocation'})
                .then(s => { s2 = s; });
            navigator.permissions.query({name: 'camera'})
                .then(s => { cam = s; });
            """
        )
        page.run_jobs()
        assert page.eval("s1 !== s2 && s1 !== cam") is True
        assert page.eval(
            """
            (() => {
              const name = Object.getOwnPropertyDescriptor(s1, 'name');
              const state = Object.getOwnPropertyDescriptor(s1, 'state');
              const onchange = Object.getOwnPropertyDescriptor(s1, 'onchange');
              const add = Object.getOwnPropertyDescriptor(
                  s1, 'addEventListener');
              s1.name = 'changed';
              s1.onchange = function () {};
              return [
                s1.name, s1.state,
                Object.getPrototypeOf(s1) === Object.prototype,
                name.value, name.writable, name.enumerable, name.configurable,
                state.value, String(onchange.value), onchange.writable,
                onchange.enumerable, onchange.configurable,
                typeof add.value, add.writable, add.enumerable,
                add.configurable, typeof s1.onchange
              ].join('|');
            })();
            """,
            name="shape.js"
        ) == ("geolocation|prompt|true|geolocation|false|true|false|prompt|"
              "null|true|true|true|function|false|true|false|function")

        page.start_access_trace(object_types=["permission_status"])
        page.eval("s1.name; s2.name; cam.name;", name="ids.js")
        events = _events(page)
        assert [event["member"] for event in events] == ["name", "name", "name"]
        assert events[0]["object_id"] == events[1]["object_id"]
        assert events[0]["object_id"] != events[2]["object_id"]


@on_only
def test_permissions_query_errors_match_the_existing_contract():
    with iv8.Page() as page:
        _load(page)
        page.eval(
            """
            globalThis.errs = [];
            const rec = p => p.then(
                () => errs.push('resolved'),
                e => errs.push(e.name + ':' + e.message));
            rec(navigator.permissions.query());
            rec(navigator.permissions.query(null));
            rec(navigator.permissions.query('x'));
            rec(navigator.permissions.query({name: 1}));
            rec(navigator.permissions.query({name: 'not-a-permission'}));
            """
        )
        page.run_jobs()
        assert page.eval("errs.join('\\n')").split("\n") == [
            "TypeError:permissions.query: descriptor must be an object",
            "TypeError:permissions.query: descriptor must be an object",
            "TypeError:permissions.query: descriptor must be an object",
            "TypeError:permissions.query: name must be a string",
            "TypeError:permissions.query: unknown permission name",
        ]
        with pytest.raises(iv8.JSError):
            page.eval(
                "navigator.permissions.query("
                "{get name() { throw new Error('boom'); }});")


@on_only
def test_permissions_manager_keeps_plain_object_shape():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            "navigator.permissions === navigator.permissions") is True
        assert page.eval(
            "Object.getPrototypeOf(navigator.permissions) === Object.prototype"
        ) is True
        assert page.eval("Object.keys(navigator.permissions).join(',')") == (
            "query")
        assert page.eval(
            """
            JSON.stringify(
                Object.getOwnPropertyDescriptor(navigator.permissions, 'query'))
            """
        ) == '{"writable":false,"enumerable":true,"configurable":false}'
        assert page.eval(
            """
            navigator.permissions = null;
            navigator.permissions.query = 1;
            [typeof navigator.permissions,
             typeof navigator.permissions.query].join('|');
            """
        ) == "object|function"


@on_only
def test_permissions_filter_and_generation():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace(object_types=["permissions"])
        page.eval("navigator.permissions.query({name: 'camera'});",
                  name="probe.js")
        events = _events(page)
        assert [(event["object_type"], event["member"]) for event in events] == [
            ("permissions", "query")]
        assert events[0]["generation"] == 2

        page.start_access_trace(object_types=["permission_status"])
        page.eval(
            """
            globalThis.cam = null;
            navigator.permissions.query({name: 'camera'})
                .then(s => { cam = s; });
            """,
            name="probe.js"
        )
        page.run_jobs()
        page.eval("cam.name;", name="probe.js")
        events = _events(page)
        assert [(event["object_type"], event["member"]) for event in events] == [
            ("permission_status", "name")]
        first_id = events[0]["object_id"]

        page.load(HTML, BASE)
        page.eval(
            """
            globalThis.cam = null;
            navigator.permissions.query({name: 'camera'})
                .then(s => { cam = s; });
            """,
            name="probe.js"
        )
        page.run_jobs()
        page.eval("cam.name;", name="probe.js")
        events = [
            event for event in _events(page)
            if event["object_type"] == "permission_status"
        ]
        assert [event["generation"] for event in events] == [2, 3]
        assert events[0]["object_id"] == first_id
        assert events[1]["object_id"] != first_id
