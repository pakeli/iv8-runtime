(async()=>{
  const original=Object.getOwnPropertyDescriptor(Array.prototype,'then');
  const sentinel={sentinel:'related-then'};
  const output={secure:isSecureContext,reads:0,status:'pending',sameSentinel:false,synchronousThrow:false,order:[]};
  const started=performance.now();
  Object.defineProperty(Array.prototype,'then',{configurable:true,get(){output.reads++;output.order.push('then lookup');throw sentinel;}});
  try{
    try{
      const pending=navigator.getInstalledRelatedApps();
      output.promise=pending instanceof Promise;
      output.order.push('after call');
      pending.then(value=>{output.status='resolved';output.valueIsArray=Array.isArray(value);},error=>{
        output.status='rejected';output.sameSentinel=error===sentinel;output.error={name:error?.name??null,message:error?.message??null};output.order.push('rejected');
      });
    }catch(error){output.synchronousThrow=true;output.status='threw';output.sameSentinel=error===sentinel;}
    await new Promise(resolve=>{
      const until=performance.now()+2000;
      const poll=()=>{if(output.status!=='pending'||performance.now()>=until){resolve();return;}setTimeout(poll,10);};poll();
    });
  }finally{
    if(original)Object.defineProperty(Array.prototype,'then',original);else delete Array.prototype.then;
  }
  output.elapsedMs=performance.now()-started;
  const pre=document.createElement('pre');pre.id='bom02-reference';pre.textContent=JSON.stringify(output);document.body.appendChild(pre);
  return output;
})();
