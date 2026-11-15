(() => {
const originalDescriptor=Object.getOwnPropertyDescriptor(window,'event');const shape=()=>{const d=Object.getOwnPropertyDescriptor(window,'event');return d?{get:d.get&&d.get.name,set:d.set&&d.set.name,data:'value'in d,configurable:d.configurable,enumerable:d.enumerable}:null};
const output={initial:{type:typeof window.event,descriptor:shape()},stages:[]};
window.event=42;output.assigned={value:window.event,descriptor:shape()};
const outer=new Event('synthetic-outer'),inner=new Event('synthetic-inner');
window.addEventListener('synthetic-inner',()=>output.stages.push({stage:'inner',isInner:window.event===inner,value:typeof window.event}));
window.addEventListener('synthetic-outer',()=>{output.stages.push({stage:'outer',isOuter:window.event===outer});window.event='assigned-inside';output.stages.push({stage:'assigned-inside',value:window.event});window.dispatchEvent(inner);output.stages.push({stage:'after-inner',value:window.event,isOuter:window.event===outer});});
window.dispatchEvent(outer);output.after={value:window.event,descriptor:shape()};
Object.defineProperty(window,'event',originalDescriptor);const scopedOuter=new Event('scope-outer'),scopedInner=new Event('scope-inner');output.scoped=[];window.addEventListener('scope-inner',()=>output.scoped.push({stage:'inner',current:window.event===scopedInner}));window.addEventListener('scope-outer',()=>{output.scoped.push({stage:'outer',current:window.event===scopedOuter});window.dispatchEvent(scopedInner);output.scoped.push({stage:'restored-outer',current:window.event===scopedOuter});});window.dispatchEvent(scopedOuter);output.scopedAfter=typeof window.event;try{originalDescriptor.get.call({});output.invalidReceiver='returned'}catch(error){output.invalidReceiver=error.name};
const error=new Error('synthetic-report');let activeError;
window.addEventListener('error',event=>{activeError=event;output.report={isEvent:event instanceof Event,type:event.type,message:event.message,errorIdentity:event.error===error,bubbles:event.bubbles,cancelable:event.cancelable,composed:event.composed,current:window.event===event};event.preventDefault();});
window.onerror=function(...args){output.onerror={count:args.length,message:args[0],errorIdentity:args[4]===error,current:window.event===activeError};return true;};
try{output.reportReturn=typeof reportError(error);}catch(e){output.reportThrew=e.name;}
output.reportAfter={type:typeof window.event};
try{reportError();output.missingArgument='returned';}catch(e){output.missingArgument=e.name;}
const pre=document.createElement('pre');pre.textContent=JSON.stringify(output);document.body.replaceChildren(pre);
})()
