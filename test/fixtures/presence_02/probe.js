(async () => {
  const results = {};
  const exception = callback => { try { callback(); return null; } catch (error) { return error.name; } };
  const run = async (name, callback) => {
    try { results[name] = {value:await callback()}; }
    catch (error) { results[name] = {error:{name:error.name, message:error.message}}; }
  };
  const otherDocument = () => new DOMParser().parseFromString('<html><head></head><body></body></html>', 'text/html');
  await run('dataset', () => {
    const element = document.createElement('div');
    element.setAttribute('data-foo-bar', 'one'); element.setAttribute('data-foo--bar', 'two');
    const data = element.dataset;
    const initial = {fooBar:data.fooBar, repeatedDash:data['foo-Bar'], keys:Object.keys(data).sort()};
    data.fooBar = 7; data.emptyValue = null;
    const set = [element.getAttribute('data-foo-bar'), element.getAttribute('data-empty-value')];
    delete data.fooBar;
    return {initial, set, deleted:!element.hasAttribute('data-foo-bar'),
      stable:data === element.dataset, prototype:Object.getPrototypeOf(data) === DOMStringMap.prototype,
      invalidName:exception(() => {data['bad-name'] = 'value';})};
  });
  await run('dataset_definition', () => {
    const element=document.createElement('div'),data=element.dataset;
    const ordinary=exception(() => Object.defineProperty(data,'foo',{value:'x'}));
    const restrictive=exception(() => Object.defineProperty(data,'bar',{
      value:'y',writable:false,enumerable:false,configurable:false}));
    const accessor=exception(() => Object.defineProperty(data,'accessor',{get(){return 'getter';}}));
    const descriptor=Object.getOwnPropertyDescriptor(data,'foo');
    const prototype=Object.getPrototypeOf(data);
    data.__proto__={marker:true};
    return {ordinary,restrictive,accessor,
      descriptor:{value:descriptor.value,writable:descriptor.writable,enumerable:descriptor.enumerable,configurable:descriptor.configurable},
      bar:element.getAttribute('data-bar'),protoAttribute:element.getAttribute('data-__proto__'),
      protoValue:data.__proto__,unchangedPrototype:Object.getPrototypeOf(data) === prototype};
  });
  await run('attr', () => {
    const attr = document.createAttribute('DATA-Value');
    const element = document.createElement('div');
    const initial = {name:attr.name, localName:attr.localName, prefix:attr.prefix, namespaceURI:attr.namespaceURI,
      value:attr.value, specified:attr.specified, nodeType:attr.nodeType, nodeName:attr.nodeName,
      ownerElement:attr.ownerElement === null, ownerDocument:attr.ownerDocument === document,
      parentNode:attr.parentNode === null, prototype:Object.getPrototypeOf(Attr.prototype) === Node.prototype};
    attr.value = 'first';
    const previous = element.setAttributeNode(attr);
    attr.nodeValue = 'second';
    const attached = {previous:previous === null, owner:attr.ownerElement === element,
      identity:element.getAttributeNode('data-value') === attr, reflected:element.getAttribute('data-value')};
    element.setAttribute('data-value', 'third');
    const retained = element.getAttributeNode('data-value') === attr && attr.value === 'third';
    const removed = element.removeAttributeNode(attr);
    return {initial, attached, retained, removed:removed === attr, detached:attr.ownerElement === null,
      retainedValue:attr.value, missing:element.getAttributeNode('missing') === null};
  });
  await run('named_node_map', () => {
    const element = document.createElement('div'); element.setAttribute('title','old');
    const attributes = element.attributes;
    const old = attributes.getNamedItem('title');
    const replacement = document.createAttribute('title'); replacement.value = 'new';
    const returned = attributes.setNamedItem(replacement);
    const inUse = exception(() => document.createElement('span').attributes.setNamedItem(replacement));
    const attached = {stable:attributes === element.attributes,
      prototype:Object.getPrototypeOf(attributes) === NamedNodeMap.prototype,
      replaced:returned === old, oldDetached:old.ownerElement === null,
      owner:replacement.ownerElement === element, identity:attributes.item(0) === replacement,
      indexed:attributes[0] === replacement, length:attributes.length,
      missing:attributes.getNamedItem('missing') === null, outOfRange:attributes.item(99) === null,
      reflected:element.getAttribute('title'), inUse};
    const removed = attributes.removeNamedItem('title');
    return {attached, removed:removed === replacement, detached:replacement.ownerElement === null,
      finalLength:attributes.length, missingRemoval:exception(() => attributes.removeNamedItem('title'))};
  });
  await run('namespace_attributes', () => {
    const element = document.createElement('div');
    element.setAttributeNS('urn:presence', 'p:flag', 'one');
    const attr = element.getAttributeNodeNS('urn:presence','flag');
    const initial = {namespaceURI:attr.namespaceURI, prefix:attr.prefix, localName:attr.localName,
      name:attr.name, value:element.getAttributeNS('urn:presence','flag'),
      has:element.hasAttributeNS('urn:presence','flag'), ordinary:element.getAttribute('p:flag')};
    element.setAttributeNS('urn:presence','q:flag','two');
    const replacement = {same:element.getAttributeNodeNS('urn:presence','flag') === attr,
      prefix:attr.prefix, name:attr.name, value:attr.value, count:element.attributes.length};
    const external = document.createAttributeNS('urn:other','o:value'); external.value = 'extra';
    element.setAttributeNodeNS(external);
    const mapLookup = element.attributes.getNamedItemNS('urn:other','value') === external;
    const removed = element.attributes.removeNamedItemNS('urn:other','value');
    element.removeAttributeNS('urn:presence','flag');
    return {initial, replacement, mapLookup, removed:removed === external,
      missing:element.getAttributeNS('urn:presence','flag') === null,
      detached:attr.ownerElement === null && external.ownerElement === null};
  });
  await run('namespace_creation', () => {
    const element = document.createElementNS('urn:presence', 'p:widget');
    const attr = document.createAttributeNS('urn:presence','p:flag');
    return {element:{namespaceURI:element.namespaceURI, prefix:element.prefix,
      localName:element.localName, nodeName:element.nodeName, tagName:element.tagName,
      owner:element.ownerDocument === document, element:element instanceof Element},
      attr:{namespaceURI:attr.namespaceURI, prefix:attr.prefix, localName:attr.localName, name:attr.name},
      emptyNamespace:document.createElementNS('', 'plain').namespaceURI,
      invalidPrefix:exception(() => document.createElementNS(null,'p:widget')),
      invalidXml:exception(() => document.createAttributeNS('urn:wrong','xml:lang')),
      invalidName:exception(() => document.createAttribute('bad name'))};
  });
  await run('namespace_collection', () => {
    const root=document.createElementNS('urn:presence','p:Item');
    const first=document.createElementNS('urn:presence','p:Item');
    const other=document.createElementNS('urn:other','q:Item');
    root.appendChild(first);root.appendChild(other);
    const byLocal=root.getElementsByTagNameNS('*','Item');
    const byNamespace=root.getElementsByTagNameNS('urn:presence','*');
    const lower=root.getElementsByTagNameNS('*','item');
    const documentItems=document.getElementsByTagNameNS('urn:presence','Item');
    const initial={local:byLocal.length,namespace:byNamespace.length,lower:lower.length,
      excludesReceiver:byLocal[0] === first && byNamespace[0] === first,document:documentItems.length};
    const lowerNode=document.createElementNS('urn:presence','p:item');root.appendChild(lowerNode);
    const appended=[byLocal.length,byNamespace.length,lower.length,lower[0] === lowerNode];
    first.remove();document.body.appendChild(root);
    const connected={local:byLocal.length,namespace:byNamespace.length,lower:lower.length,
      document:documentItems.length,documentRoot:documentItems[0] === root};
    root.remove();
    return {initial,appended,connected,removedFromDocument:documentItems.length};
  });
  await run('attribute_observer', async () => {
    const element = document.createElement('div'); const records=[];
    const observer = new MutationObserver(batch => records.push(...batch));
    observer.observe(element,{attributes:true,attributeOldValue:true});
    element.dataset.flag = 'one'; element.getAttributeNode('data-flag').value = 'two';
    element.attributes.removeNamedItem('data-flag');
    await Promise.resolve(); observer.disconnect();
    return records.map(record => ({type:record.type, attributeName:record.attributeName,
      oldValue:record.oldValue, target:record.target === element}));
  });
  await run('node_constants', () => {
    const nodeNames=['ELEMENT_NODE','ATTRIBUTE_NODE','TEXT_NODE','CDATA_SECTION_NODE','ENTITY_REFERENCE_NODE',
      'ENTITY_NODE','PROCESSING_INSTRUCTION_NODE','COMMENT_NODE','DOCUMENT_NODE','DOCUMENT_TYPE_NODE',
      'DOCUMENT_FRAGMENT_NODE','NOTATION_NODE'];
    const positionNames=['DOCUMENT_POSITION_DISCONNECTED','DOCUMENT_POSITION_PRECEDING','DOCUMENT_POSITION_FOLLOWING',
      'DOCUMENT_POSITION_CONTAINS','DOCUMENT_POSITION_CONTAINED_BY','DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC'];
    return Object.fromEntries([...nodeNames,...positionNames].map(name => [name,[Node[name],Node.prototype[name]]]));
  });
  await run('get_root_node', () => {
    const parent=document.createElement('div'),child=document.createElement('span'); parent.appendChild(child);
    const detached=child.getRootNode() === parent;
    const fragment=document.createDocumentFragment(); fragment.appendChild(parent);
    const inFragment=child.getRootNode() === fragment && child.getRootNode({composed:true}) === fragment;
    document.body.appendChild(fragment);
    const connected=child.getRootNode() === document && document.getRootNode() === document;
    parent.remove();
    return {detached,inFragment,connected,removed:child.getRootNode() === parent};
  });
  await run('normalize', () => {
    const parent=document.createElement('div'), nested=document.createElement('em');
    const empty=document.createTextNode(''), first=document.createTextNode('A'), second=document.createTextNode('B');
    const third=document.createTextNode('C'),fourth=document.createTextNode('D');
    parent.appendChild(empty);parent.appendChild(first);parent.appendChild(document.createTextNode(''));parent.appendChild(second);
    parent.appendChild(document.createComment('separator'));parent.appendChild(third);parent.appendChild(fourth);parent.appendChild(nested);
    nested.appendChild(document.createTextNode(''));nested.appendChild(document.createTextNode('x'));nested.appendChild(document.createTextNode('y'));
    parent.normalize(); parent.normalize();
    return {types:Array.from(parent.childNodes,node => node.nodeType), texts:[first.data,third.data,nested.textContent],
      retainedFirst:parent.firstChild === first, retainedThird:parent.childNodes[2] === third,
      detached:empty.parentNode === null && second.parentNode === null && fourth.parentNode === null,
      nestedCount:nested.childNodes.length};
  });
  await run('normalize_observer', async () => {
    const parent=document.createElement('div'),first=document.createTextNode('A');parent.appendChild(first);
    const records=[];const observer=new MutationObserver(batch => records.push(...batch));
    observer.observe(parent,{childList:true,characterData:true,subtree:true,characterDataOldValue:true});
    const read=() => records.splice(0).map(record => ({type:record.type,
      target:record.target === parent ? 'parent' : record.target === first ? 'first' : 'other',
      oldValue:record.oldValue,removed:Array.from(record.removedNodes,node => node.data)}));
    parent.normalize();await Promise.resolve();const single=read();
    observer.disconnect();
    parent.removeChild(first);parent.appendChild(document.createTextNode(''));
    parent.appendChild(first);parent.appendChild(document.createTextNode(''));parent.appendChild(document.createTextNode('B'));
    observer.observe(parent,{childList:true,characterData:true,subtree:true,characterDataOldValue:true});
    parent.normalize();await Promise.resolve();const merged=read();
    parent.normalize();await Promise.resolve();const repeated=read();observer.disconnect();
    const sequence = async values => {
      const element=document.createElement('div');
      for (const value of values) element.appendChild(document.createTextNode(value));
      const output=[];const watch=new MutationObserver(batch => output.push(...batch));
      watch.observe(element,{childList:true,characterData:true,subtree:true,characterDataOldValue:true});
      element.normalize();await Promise.resolve();watch.disconnect();
      return output.map(record => ({type:record.type,oldValue:record.oldValue,
        removed:Array.from(record.removedNodes,node => node.data)}));
    };
    return {single,merged,repeated,text:parent.textContent,retained:parent.firstChild === first,
      multiple:await sequence(['A','B','C']),withEmpty:await sequence(['A','B','','C'])};
  });
  await run('compare_document_position', () => {
    const parent=document.createElement('div'),first=document.createElement('span'),second=document.createElement('em');
    parent.appendChild(first); parent.appendChild(second);
    const detached=document.createElement('section');
    const forward=first.compareDocumentPosition(detached), backward=detached.compareDocumentPosition(first);
    return {same:first.compareDocumentPosition(first), parentChild:parent.compareDocumentPosition(first),
      childParent:first.compareDocumentPosition(parent), next:first.compareDocumentPosition(second),
      previous:second.compareDocumentPosition(first), disconnected:!!(forward&1), implementationSpecific:!!(forward&32),
      opposite:!!((forward&2)&&(backward&4) || (forward&4)&&(backward&2)),
      stable:first.compareDocumentPosition(detached) === forward};
  });
  await run('import_node', () => {
    const target=otherDocument(),source=document.createElement('div');
    source.setAttribute('data-value','original');source.appendChild(document.createTextNode('text'));
    const shallow=target.importNode(source),deep=target.importNode(source,true);
    deep.firstChild.data='copy';
    return {shallow:shallow !== source && shallow.childNodes.length === 0 && shallow.ownerDocument === target,
      deep:deep !== source && deep.ownerDocument === target && deep.firstChild.ownerDocument === target,
      detached:deep.parentNode === null, attribute:deep.getAttribute('data-value'),
      independent:source.textContent === 'text' && deep.textContent === 'copy',
      documentError:exception(() => target.importNode(document,true))};
  });
  await run('adopt_node', () => {
    const target=otherDocument(),source=document.createElement('div'); source.id='adoptedProbe';
    source.appendChild(document.createElement('span')); source.setAttribute('data-value','one');
    const child=source.firstChild,attr=source.getAttributeNode('data-value');
    let listener=0,handler=0;
    source.addEventListener('probe',() => listener++);source.onclick=() => handler++;
    document.body.appendChild(source);
    const adopted=target.adoptNode(source);
    adopted.dispatchEvent(new Event('probe'));adopted.dispatchEvent(new Event('click'));
    const result={identity:adopted === source, detached:source.parentNode === null,
      owner:source.ownerDocument === target && child.ownerDocument === target && attr.ownerDocument === target,
      oldQuery:document.getElementById('adoptedProbe') === null,listener,handler,
      documentError:exception(() => target.adoptNode(document))};
    target.body.appendChild(adopted);
    result.newQuery=target.getElementById('adoptedProbe') === source;
    return result;
  });
  await run('adopted_observer', async () => {
    const target=otherDocument(),source=document.createElement('div');
    const records=[];const observer=new MutationObserver(batch => records.push(...batch));
    observer.observe(source,{childList:true,subtree:true});
    const before=document.createElement('span');source.appendChild(before);
    target.adoptNode(source);target.body.appendChild(source);
    const after=target.createElement('b');source.appendChild(after);source.removeChild(after);
    await Promise.resolve();observer.disconnect();
    return {count:records.length,targets:records.every(record => record.target === source),
      previous:records[0].addedNodes[0] === before,newNode:records[1].addedNodes[0] === after,
      removed:records[2].removedNodes[0] === after,
      owners:before.ownerDocument === target && after.ownerDocument === target};
  });
  await run('dom_parser_html', () => {
    const parsed=new DOMParser().parseFromString('<html><head><title>Parsed</title></head><body><p id="parsed" data-x="one">A &amp; B</p><script>globalThis.parserExecuted=true</script></body></html>', 'text/html');
    return {tag:Object.prototype.toString.call(parsed),prototype:Object.getPrototypeOf(parsed) === HTMLDocument.prototype,
      contentType:parsed.contentType,defaultView:parsed.defaultView === null,ownerDocument:parsed.ownerDocument === null,
      urlMatches:parsed.URL === document.URL,uriMatches:parsed.documentURI === document.documentURI,
      baseMatches:parsed.baseURI === document.baseURI,title:parsed.title,
      root:parsed.documentElement.tagName,head:parsed.head.tagName,body:parsed.body.tagName,
      text:parsed.getElementById('parsed').textContent,attribute:parsed.getElementById('parsed').getAttribute('data-x'),
      inert:globalThis.parserExecuted === undefined, connectedToParsedDocument:parsed.getElementById('parsed').isConnected};
  });
  await run('dom_parser_fragment', () => {
    const parsed=new DOMParser().parseFromString('<title>Fragment</title><p id="fragment">one <b>two</b></p>','text/html');
    return {root:parsed.documentElement.tagName,head:parsed.head.tagName,body:parsed.body.tagName,
      title:parsed.title,text:parsed.body.textContent,bodyChildren:parsed.body.children.length,
      first:parsed.body.firstChild.tagName,owner:parsed.getElementById('fragment').ownerDocument === parsed,
      isolated:document.getElementById('fragment') === null};
  });
  await run('dom_parser_mime', () => ({
    invalid:exception(() => new DOMParser().parseFromString('<p>x</p>','invalid/type')),
    xml:new DOMParser().parseFromString('<root/>','application/xml').documentElement.nodeName,
    htmlCase:exception(() => new DOMParser().parseFromString('<p>x</p>','TEXT/HTML'))
  }));
  await run('xml_serializer', () => {
    const element=document.createElementNS('urn:presence','p:root'); element.setAttribute('value','a&b');
    element.appendChild(document.createTextNode('<text>'));
    const serializer=new XMLSerializer();
    return {serialized:serializer.serializeToString(element),
      text:serializer.serializeToString(document.createTextNode('<&>')),
      invalid:exception(() => serializer.serializeToString({}))};
  });
  await run('function_shapes', () => {
    const map=document.createElement('div').attributes;
    const functions={
      'document.createAttribute':document.createAttribute,'document.createAttributeNS':document.createAttributeNS,
      'document.createElementNS':document.createElementNS,'Element.getAttributeNS':Element.prototype.getAttributeNS,
      'document.getElementsByTagNameNS':document.getElementsByTagNameNS,
      'Element.getElementsByTagNameNS':Element.prototype.getElementsByTagNameNS,
      'Element.setAttributeNS':Element.prototype.setAttributeNS,'Element.hasAttributeNS':Element.prototype.hasAttributeNS,
      'Element.removeAttributeNS':Element.prototype.removeAttributeNS,'Element.getAttributeNode':Element.prototype.getAttributeNode,
      'Element.setAttributeNode':Element.prototype.setAttributeNode,'Element.removeAttributeNode':Element.prototype.removeAttributeNode,
      'Element.getAttributeNodeNS':Element.prototype.getAttributeNodeNS,'Element.setAttributeNodeNS':Element.prototype.setAttributeNodeNS,
      'NamedNodeMap.item':map.item,'NamedNodeMap.getNamedItem':map.getNamedItem,'NamedNodeMap.setNamedItem':map.setNamedItem,
      'NamedNodeMap.removeNamedItem':map.removeNamedItem,'NamedNodeMap.getNamedItemNS':map.getNamedItemNS,
      'NamedNodeMap.setNamedItemNS':map.setNamedItemNS,'NamedNodeMap.removeNamedItemNS':map.removeNamedItemNS,
      'Node.getRootNode':Node.prototype.getRootNode,'Node.normalize':Node.prototype.normalize,
      'Node.compareDocumentPosition':Node.prototype.compareDocumentPosition,'document.importNode':document.importNode,
      'document.adoptNode':document.adoptNode,'DOMParser.parseFromString':DOMParser.prototype.parseFromString,
      'XMLSerializer.serializeToString':XMLSerializer.prototype.serializeToString};
    return Object.fromEntries(Object.entries(functions).map(([name,fn]) => [name,{
      name:fn.name,length:fn.length,source:Function.prototype.toString.call(fn),prototype:Object.hasOwn(fn,'prototype'),
      constructable:(() => {try {Reflect.construct(String,[],fn);return true;} catch {return false;}})()
    }]));
  });
  const output=document.createElement('pre');output.textContent=JSON.stringify(results);document.body.appendChild(output);
  return JSON.stringify(results);
})()
