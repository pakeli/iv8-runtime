"""Adoption keeps live handles and queued records valid across document cycles."""

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")


def test_clone_and_import_equality_includes_element_and_attribute_namespaces():
    with iv8.Page() as page:
        assert page.eval("""(() => {
            const source = document.createElementNS('urn:element', 'item');
            source.setAttributeNS('urn:attribute', 'a:flag', 'original');
            const clone = source.cloneNode(true);
            const otherDocument = new DOMParser().parseFromString('<html><body></body></html>', 'text/html');
            const imported = otherDocument.importNode(source, true);
            if (!clone.isEqualNode(source) || !imported.isEqualNode(source) ||
                clone.getAttributeNodeNS('urn:attribute', 'flag') === source.getAttributeNodeNS('urn:attribute', 'flag')) return false;
            clone.setAttributeNS('urn:attribute', 'a:flag', 'changed');
            if (clone.isEqualNode(source) || source.getAttributeNS('urn:attribute', 'flag') !== 'original') return false;
            const otherNamespace = document.createElementNS('urn:other-element', 'item');
            otherNamespace.setAttributeNS('urn:attribute', 'a:flag', 'original');
            const otherAttributeNamespace = document.createElementNS('urn:element', 'item');
            otherAttributeNamespace.setAttributeNS('urn:other-attribute', 'a:flag', 'original');
            return !source.isEqualNode(otherNamespace) && !source.isEqualNode(otherAttributeNamespace);
        })()""")


def test_repeated_adoption_preserves_observer_records_handlers_and_cached_views():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", "https://node-lifecycle.test/")
        page.eval("""
            const parser = new DOMParser();
            const first = parser.parseFromString('<html><body></body></html>', 'text/html');
            const second = parser.parseFromString('<html><body></body></html>', 'text/html');
            const moving = document.createElement('section'); moving.id = 'roundTripNode';
            const control = document.createElement('button');
            control.className = 'seed'; control.setAttribute('data-state', 'ready');
            moving.appendChild(control); document.body.appendChild(moving);
            const collection = moving.getElementsByTagName('button');
            const classList = control.classList;
            const attributes = control.attributes;
            const attribute = control.getAttributeNode('data-state');
            const received = [];
            const observer = new MutationObserver(records => received.push(...records));
            observer.observe(moving, {subtree:true, attributes:true, childList:true});
            let clicks = 0;
            control.addEventListener('click', () => clicks++);
            control.onclick = () => { clicks += 10; };
            const original = document.createElement('em'); moving.appendChild(original);
            const owners = [first, second, document, first, document];
            const detached = [];
            let identities = true;
            for (let index = 0; index < owners.length; index++) {
                const owner = owners[index];
                const previous = moving.ownerDocument;
                const adopted = owner.adoptNode(moving);
                identities &&= adopted === moving && moving.parentNode === null &&
                    moving.ownerDocument === owner && control.ownerDocument === owner &&
                    original.ownerDocument === owner && attribute.ownerDocument === owner &&
                    previous.getElementById('roundTripNode') === null;
                owner.body.appendChild(moving);
                identities &&= owner.getElementById('roundTripNode') === moving &&
                    collection.length === 1 && collection[0] === control &&
                    control.classList.contains('seed') && control.attributes === attributes &&
                    control.getAttributeNode('data-state') === attribute;
                control.setAttribute('data-pass', String(index));
                const transient = owner.createElement('i'); detached.push(transient);
                moving.appendChild(transient); moving.removeChild(transient);
                control.dispatchEvent(new Event('click'));
            }
            classList.add('final');
            globalThis.adoptionResult = {moving, control, original, attribute, owners,
                detached, received, identities, clicks,
                expectedRecords:1 + owners.length * 3 + 1};
        """)
        page.run_jobs()
        assert page.eval("adoptionResult.identities")
        assert page.eval("adoptionResult.clicks") == 55
        assert page.eval("adoptionResult.received.length === adoptionResult.expectedRecords")
        assert page.eval("""(() => {
            const {moving, control, original, attribute, owners, detached, received} = adoptionResult;
            const additions = received.filter(record => record.type === 'childList' && record.addedNodes.length);
            const removals = received.filter(record => record.type === 'childList' && record.removedNodes.length);
            return additions.length === owners.length + 1 && removals.length === owners.length &&
                additions[0].addedNodes[0] === original &&
                additions.slice(1).every((record, index) => record.addedNodes[0] === detached[index]) &&
                removals.every((record, index) => record.removedNodes[0] === detached[index]) &&
                received.every(record => record.target === (record.type === 'attributes' ? control : moving)) &&
                detached.every((node, index) => node.ownerDocument === owners[index] && node.parentNode === null) &&
                moving.ownerDocument === document && control.ownerDocument === document &&
                original.ownerDocument === document && attribute.ownerDocument === document &&
                control.classList.contains('final');
        })()""")
