"""Browser-referenced, bounded same-document History; no network transport."""

import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, '_v8_linked', False), reason='requires V8')
BASE = 'https://history.test/start?seed=1#old'


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load('<html><head><title>unchanged</title></head><body><a href="relative"></a></body></html>', BASE)
        yield value


def test_native_surface_matches_captured_chrome(page):
    expected = json.loads((Path(__file__).parent / 'fixtures/chrome157_history_surface.json').read_text())
    expected.pop('reference')
    actual = json.loads(page.eval(r'''JSON.stringify((()=>{
      const fn=value=>({name:value.name,length:value.length,source:Function.prototype.toString.call(value),
        ownKeys:Reflect.ownKeys(value).map(String),constructible:(()=>{try{Reflect.construct(function(){},[],value);return true}catch{return false}})()});
      const desc=value=>{const out={enumerable:value.enumerable,configurable:value.configurable};
        if('value'in value){out.writable=value.writable;out.type=typeof value.value;
          if(typeof value.value==='function')out.function=fn(value.value);else if(typeof value.value!=='object')out.value=value.value;}
        else{out.get=value.get&&fn(value.get);out.set=value.set&&fn(value.set);}return out;};
      const out={historyTag:Object.prototype.toString.call(history),instanceKeys:Reflect.ownKeys(history).map(String),
        global:desc(Object.getOwnPropertyDescriptor(window,'history')),constructor:fn(History),
        prototypeKeys:Reflect.ownKeys(History.prototype).map(String),prototype:{}};
      for(const key of Reflect.ownKeys(History.prototype))out.prototype[String(key)]=desc(Object.getOwnPropertyDescriptor(History.prototype,key));
      return out;
    })())'''))
    assert actual == expected


def test_fresh_generation_and_stable_wrapper(page):
    assert page.eval('history.length') == 1
    assert page.eval('history.state') is None
    assert page.eval('history.scrollRestoration') == 'auto'
    assert page.eval('history === window.history && history instanceof History')


def test_push_replace_clone_and_url_consistency(page):
    assert page.eval("history.pushState({nested:{value:1}},'ignored','/next?q=2#new')") is iv8.JSUndefined
    assert page.eval('history.length') == 2
    assert page.eval('history.state.nested.value') == 1
    for expression in ['location.href', 'document.URL', 'document.documentURI', 'document.baseURI', 'document.body.baseURI']:
        assert page.eval(expression) == 'https://history.test/next?q=2#new'
    assert page.eval("document.querySelector('a').href") == 'https://history.test/relative'
    assert page.eval('document.title') == 'unchanged'
    page.eval("history.replaceState({replaced:true},'', '?q=3#three')")
    assert page.eval('history.length') == 2
    assert page.eval('history.state.replaced')
    assert page.eval('location.href') == 'https://history.test/next?q=3#three'
    assert page.read_navigation_requests()['requests'] == []


def test_clone_identity_and_cycles(page):
    assert page.eval("(()=>{const s={value:1};s.self=s;history.pushState(s,'');s.value=9;return history.state!==s&&history.state.self===history.state&&history.state.value===1&&history.state===history.state})()")
    page.eval('history.state.value=7')
    assert page.eval('history.state.value') == 7


def test_clone_builtin_types_and_no_transfer(page):
    assert page.eval("(()=>{const bytes=new Uint8Array([1,2]);history.pushState({date:new Date(123),map:new Map([['x',2]]),bytes},'');bytes[0]=9;return history.state.date.getTime()===123&&history.state.map.get('x')===2&&history.state.bytes[0]===1&&bytes.buffer.byteLength===2})()")


@pytest.mark.parametrize('value', ['undefined', 'null', "''"])
def test_null_empty_undefined_url_preserves_url(page, value):
    page.eval(f"history.pushState(undefined,'',{value})")
    assert page.eval('typeof history.state') == 'undefined'
    assert page.eval('location.href') == BASE


@pytest.mark.parametrize('method', ['pushState', 'replaceState'])
@pytest.mark.parametrize('arguments,count', [('', 0), ('{}', 1)])
def test_required_arguments(page, method, arguments, count):
    with pytest.raises(iv8.JSError) as error:
        page.eval(f'history.{method}({arguments})')
    assert error.value.name == 'TypeError'
    assert f'2 arguments required, but only {count} present.' in error.value.message
    assert page.eval('history.length') == 1


@pytest.mark.parametrize('value', ['()=>{}', 'Symbol("x")', 'document.body', 'history', 'new Proxy({}, {})'])
def test_uncloneable_state_is_atomic(page, value):
    assert page.eval(f"(()=>{{try{{history.pushState({value},'', '/changed')}}catch(e){{return e.name}}}})()") == 'DataCloneError'
    assert page.eval('history.length') == 1
    assert page.eval('history.state') is None
    assert page.eval('location.href') == BASE


@pytest.mark.parametrize('url', ['https://outside.test/', 'http://history.test/', 'https://', 'https://history.test:444/path', 'https://user:pass@history.test/path'])
def test_cross_origin_or_invalid_url_is_atomic(page, url):
    assert page.eval(f"(()=>{{try{{history.pushState({{}},'',{json.dumps(url)})}}catch(e){{return e.name}}}})()") == 'SecurityError'
    assert page.eval('history.length') == 1
    assert page.eval('location.href') == BASE


def test_same_origin_normalization(page):
    page.eval("history.pushState(null,'','https://HISTORY.test:443/ok')")
    assert page.eval('location.href') == 'https://history.test/ok'


def test_conversion_order_and_throwing_getters(page):
    assert json.loads(page.eval("JSON.stringify((()=>{const order=[];history.pushState({get x(){order.push('state');return 1;}},{toString(){order.push('title');return '';}},{toString(){order.push('url');return '#new';}});return order})())")) == ['title', 'url', 'state']
    assert page.eval("(()=>{try{history.pushState({get x(){throw Error('state-getter')}},'')}catch(e){return e.message}})()") == 'state-getter'
    assert page.eval('history.length') == 2


def test_scroll_restoration_conversion_and_invalid_values(page):
    page.eval("history.scrollRestoration='manual'")
    assert page.eval('history.scrollRestoration') == 'manual'
    page.eval("history.scrollRestoration='invalid'; history.scrollRestoration=123")
    assert page.eval('history.scrollRestoration') == 'manual'
    page.eval("history.scrollRestoration={toString(){return 'auto'}}")
    assert page.eval('history.scrollRestoration') == 'auto'


def test_navigation_intents_do_not_execute_traversal(page):
    page.eval('history.back();history.forward();history.go(99)')
    assert page.read_navigation_requests()['requests'] == []
    page.eval("history.pushState({x:1},'', '/next');history.back();history.go(0)")
    requests = page.read_navigation_requests()['requests']
    assert [(item['operation'], item['url']) for item in requests] == [('history.back', BASE), ('history.go', 'https://history.test/next')]
    assert page.eval('history.state.x') == 1
    assert page.eval('location.pathname') == '/next'


def test_reset_and_isolation(page):
    page.eval("history.pushState({x:1},'', '/next');history.scrollRestoration='manual'")
    with iv8.Page() as other:
        assert other.eval('history.length') == 1
        assert other.eval('history.state') is None
    page.load('<html></html>', BASE)
    assert page.eval('history.length') == 1
    assert page.eval('history.state') is None
    assert page.eval('history.scrollRestoration') == 'auto'


def test_methods_and_attributes_reject_other_receivers(page):
    assert page.eval("(()=>{try{History.prototype.pushState.call({},null,'')}catch(e){return e.message}})()") == 'Illegal invocation'
    assert page.eval("(()=>{try{Object.getOwnPropertyDescriptor(History.prototype,'state').get.call({})}catch(e){return e.message}})()") == 'Illegal invocation'
    assert page.eval("(()=>{try{new History()}catch(e){return e.name}})()") == 'TypeError'


def test_native_access_trace_history_group(page):
    page.start_access_trace(object_types=['history'])
    page.eval("history.pushState({x:1},'');history.state;history.scrollRestoration='manual'")
    events = page.read_access_trace()['events']
    assert any(item['member']=='pushState' and item['operation']=='call' for item in events)
    assert any(item['member']=='state' and item['operation']=='get' for item in events)


def test_bare_context_stays_browser_free():
    with iv8.JSContext() as context:
        assert context.eval('typeof history') == 'undefined'
        assert context.eval('typeof History') == 'undefined'
