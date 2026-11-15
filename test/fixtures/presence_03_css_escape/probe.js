
(() => {
  const inputs = ['0a', '-', '\0', 'a b', '-1x', '\x7f', 'é', '\ud83d\ude00', '\ud800'];
  const members = {};
  for (let index = 0; index < inputs.length; index++) {
    members['case-' + index] = {type:'json_string', input:JSON.stringify(inputs[index]),
      value:JSON.stringify(CSS.escape(inputs[index]))};
  }
  const output = document.createElement('pre');
  output.textContent = JSON.stringify({conditions:{
    target_invocation:'Only CSS.escape pure string conversion; no system/network backend or Cookie access'
  }, members});
  document.body.appendChild(output);
})();
