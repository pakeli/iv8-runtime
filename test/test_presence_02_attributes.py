"""Attr inherits Node without accepting child-tree mutations."""

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")


@pytest.mark.parametrize("operation,arguments,error_name", [
    ("appendChild", "child", "HierarchyRequestError"),
    ("insertBefore", "child, null", "HierarchyRequestError"),
    ("replaceChild", "child, child", "HierarchyRequestError"),
    ("removeChild", "child", "NotFoundError"),
])
def test_attribute_node_tree_mutations_fail_explicitly(operation, arguments, error_name):
    with iv8.Page() as page:
        page.eval("""globalThis.attribute = document.createAttribute('data-probe');
            attribute.value = 'retained';
            globalThis.child = document.createTextNode('child');""")
        with pytest.raises(iv8.JSError) as caught:
            page.eval(f"Node.prototype.{operation}.call(attribute, {arguments})")
        assert caught.value.name == error_name
        assert page.eval("""attribute instanceof Node && attribute.value === 'retained' &&
            attribute.ownerElement === null && !attribute.hasChildNodes() && child.parentNode === null""")
