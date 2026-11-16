(() => {
  const outcome = operation => {
    try {const value=operation();return {returned:value===undefined?'<undefined>':value};}
    catch(error){return {name:error.name,message:error.message,code:error.code};}
  };
  const responseTypes={};
  for(const value of ['', 'text','json','document','arraybuffer','blob','madeup',null,undefined]) {
    const xhr=new XMLHttpRequest();xhr.responseType='text';
    const row={set:outcome(()=>{xhr.responseType=value;return xhr.responseType;})};
    for(const field of ['response','responseText','responseXML'])row[field]=outcome(()=>xhr[field]);
    responseTypes[String(value)]=row;
  }
  const timeout={};
  for(const [label,value] of [['negative',-1],['wrap',4294967296],['fraction',1.9],['nan',NaN],['infinity',Infinity],['undefined',undefined],['null',null],['string','12'],['bigint',1n]]) {
    const xhr=new XMLHttpRequest();timeout[label]=outcome(()=>{xhr.timeout=value;return xhr.timeout;});
  }
  let timeoutCoercions=0;
  timeout.object=outcome(()=>{const xhr=new XMLHttpRequest();xhr.timeout={valueOf(){timeoutCoercions++;return 3.9;}};return xhr.timeout;});
  timeout.object.coercions=timeoutCoercions;
  const handlerValues={};
  for(const [label,value] of [['object',{}],['array',[]],['number',42],['string','callback'],['null',null],['undefined',undefined]]) {
    const xhr=new XMLHttpRequest();xhr.onload=value;
    handlerValues[label]={same:xhr.onload===value,isNull:xhr.onload===null,type:typeof xhr.onload};
  }
  const arity={};
  for(const [name,args] of [['open',[]],['open',['GET']],['setRequestHeader',[]],['setRequestHeader',['X-Probe']],['getResponseHeader',[]],['overrideMimeType',[]],['setAttributionReporting',[]],['setPrivateToken',[]]]) {
    arity[name+':'+args.length]=outcome(()=>XMLHttpRequest.prototype[name].apply(new XMLHttpRequest(),args));
  }
  const coercionBeforeState={};
  for(const name of ['send','setRequestHeader','getResponseHeader','overrideMimeType']) {
    const effects=[];const value={toString(){effects.push('string');return 'X-Probe';}};
    coercionBeforeState[name]=outcome(()=>new XMLHttpRequest()[name](value,...(name==='setRequestHeader'?[value]:[])));
    coercionBeforeState[name].effects=effects;
  }
  const result={responseTypes,timeout,handlerValues,arity,coercionBeforeState};
  const output=document.createElement('pre');output.id='xhr-reference';output.textContent=JSON.stringify(result);document.body.replaceChildren(output);
})();
