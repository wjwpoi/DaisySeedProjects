export function validateCommand(command,schema){
  if(!command||typeof command!=='object')throw new Error('无效指令');
  const {op}=command;
  if(op==='set'){const p=schema.parameters[command.id];if(!Number.isInteger(command.id)||!p||!Number.isFinite(command.value)||command.value<p.min||command.value>p.max||(p.integer&&!Number.isInteger(command.value)))throw new Error('参数超出范围');}
  else if(op==='load'||op==='save'){if(!Number.isInteger(command.preset)||command.preset<0||command.preset>=schema.presetCount)throw new Error('无效预设');}
  else if(op==='knobs'){if(!Array.isArray(command.knobs)||command.knobs.length!==6||command.knobs.some(id=>!Number.isInteger(id)||id<0||id>16||id===4||id===8))throw new Error('无效旋钮映射');}
  else if(op==='bypass'||op==='tuner'){if(typeof command.enabled!=='boolean')throw new Error('无效开关');}
  else if(op!=='tap')throw new Error('未知指令');
  return command;
}
export function pitchNote(frequency){if(!Number.isFinite(frequency)||frequency<=0)return null;const midi=69+12*Math.log2(frequency/440),rounded=Math.round(midi);const names=['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];return {name:names[((rounded%12)+12)%12]+(Math.floor(rounded/12)-1),cents:Math.round(100*(midi-rounded))};}
export async function requestCommand(command,schema,fetcher=fetch){validateCommand(command,schema);const response=await fetcher('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(command),signal:AbortSignal.timeout(3000)});if(!response.ok)throw new Error(response.status===503?'音频主板未连接':response.status===504?'指令确认超时':'指令未完成，请重试');const result=await response.json();if(!result.ok)throw new Error('效果器拒绝了指令');return result;}
