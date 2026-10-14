import {requestCommand,pitchNote} from './api.js';
const $=id=>document.getElementById(id);const schema=await (await fetch('/schema.json')).json();
const groups=[['输入 / 动态',[0,1,2],[18,19]],['过载 / NAM',[3,4],[20,17]],['音色 / 箱体',[5,6,7,8],[21,22]],['合唱',[9,10],[23]],['延迟',[11,12,13],[24]],['混响 / 输出',[14,15,16],[25]]];
let state=null,online=false,editing=new Set(),queue=new Map(),sending=false,mappingDirty=false,pendingMapping=null;
function enable(value){online=value;for(const el of document.querySelectorAll('button,input,select'))el.disabled=!value;}
function notice(message){$('notice').textContent=message;}
async function command(value){if(!online)return false;try{await requestCommand(value,schema);notice(value.op==='save'?'预设已写入并校验':value.op==='load'?'正在载入预设':'设置已确认');return true;}catch(e){notice(e.message);return false;}}
async function drain(){if(sending)return;sending=true;while(queue.size){const [key,value]=queue.entries().next().value;queue.delete(key);await command(value);}sending=false;}
function schedule(id,value){queue.set(id,{op:'set',id,value});drain();}
function option(value,label){const o=document.createElement('option');o.value=value;o.textContent=label;return o;}
for(let i=0;i<16;i++)$('preset').append(option(i,`P${String(i+1).padStart(2,'0')}`));
for(const [name,ids,toggles] of groups){const panel=document.createElement('section');panel.className='effect';const h=document.createElement('h3');h.textContent=name;panel.append(h);
  for(const id of toggles){const p=schema.parameters[id],label=document.createElement('label');label.title=p.label;const input=document.createElement('input');input.type='checkbox';input.id=`p${id}`;input.setAttribute('aria-label',`${p.label} 开关`);input.onchange=()=>schedule(id,input.checked?1:0);label.append(input,document.createTextNode(p.label));h.append(label);}
  for(const id of ids){const p=schema.parameters[id],row=document.createElement('div');row.className='control';const label=document.createElement('label');label.htmlFor=`p${id}`;label.textContent=p.label;row.append(label);
    if(id===4||id===8){const select=document.createElement('select');select.id=`p${id}`;(id===4?schema.models:schema.cabinets).forEach((n,i)=>select.append(option(i,n)));select.onchange=()=>schedule(id,Number(select.value));row.append(select);}
    else{const input=document.createElement('input');input.type='range';input.id=`p${id}`;input.min=p.min;input.max=p.max;input.step=(p.max-p.min)/100;const output=document.createElement('output');output.id=`v${id}`;output.htmlFor=input.id;const update=()=>{output.textContent=`${Number(input.value).toFixed(1)} ${p.unit}`;schedule(id,Number(input.value));};
      input.onpointerdown=()=>editing.add(id);input.onpointerup=()=>editing.delete(id);input.onpointercancel=()=>editing.delete(id);input.onblur=()=>editing.delete(id);input.oninput=update;row.append(input,output);}
    panel.append(row);}
  $('effects').append(panel);}
for(let i=0;i<6;i++){const label=document.createElement('label');label.textContent=`旋钮 ${i+1}`;const select=document.createElement('select');select.id=`k${i}`;for(const p of schema.parameters)if(p.id<=16&&p.id!==4&&p.id!==8)select.append(option(p.id,p.label));select.onchange=()=>{mappingDirty=true;};label.append(select);$('knobMaps').append(label);}
$('load').onclick=()=>command({op:'load',preset:Number($('preset').value)});$('save').onclick=()=>command({op:'save',preset:Number($('preset').value)});
$('bypass').onclick=()=>command({op:'bypass',enabled:!state?.bypassed});$('tuner').onclick=()=>command({op:'tuner',enabled:!state?.tuning});$('tap').onclick=()=>command({op:'tap'});
$('applyMaps').onclick=async()=>{const mapping=Array.from({length:6},(_,i)=>Number($(`k${i}`).value));if(await command({op:'knobs',knobs:mapping}))pendingMapping=mapping;};
$('resetMaps').onclick=()=>{mappingDirty=false;pendingMapping=null;};
async function poll(){try{const r=await fetch('/api/state',{cache:'no-store',signal:AbortSignal.timeout(1200)});if(!r.ok)throw new Error('连接失败');const next=await r.json();enable(next.connected);$('connection').textContent=next.connected?(next.simulated?'界面预览 · 模拟状态':'效果器已连接'):'音频主板未连接';$('connection').className=next.connected?'online':'';if(!next.connected)return;state=next;
  $('modelName').textContent=schema.models[next.values[4]];$('presetName').textContent=`P${String(next.preset+1).padStart(2,'0')} ${next.dirty?'· 未保存':''} ${next.busy?'· 正在切换':''} ${next.storageError?'· 保存失败，请重试':''}`;
  for(const p of schema.parameters){const el=$(`p${p.id}`);if(!el||editing.has(p.id)||queue.has(p.id)||document.activeElement===el)continue;if(p.id>=17)el.checked=!!next.values[p.id];else el.value=next.values[p.id];const output=$(`v${p.id}`);if(output)output.textContent=`${next.values[p.id].toFixed(1)} ${p.unit}`;}
  if(pendingMapping&&pendingMapping.every((id,i)=>id===next.knobs[i])){mappingDirty=false;pendingMapping=null;}
  for(let i=0;i<6;i++)if(!mappingDirty&&document.activeElement!==$(`k${i}`))$(`k${i}`).value=next.knobs[i];
  $('bypass').setAttribute('aria-pressed',next.bypassed);$('tuner').setAttribute('aria-pressed',next.tuning);$('tunerPanel').hidden=!next.tuning;
  const note=pitchNote(next.pitch);$('note').textContent=note?.name??'—';$('cents').textContent=note?`${note.cents>0?'+':''}${note.cents} cents`:'弹奏单音开始调音';
  $('inputMeter').value=next.inputPeak;$('outputMeter').value=next.outputPeak;$('cpu').textContent=next.simulated?'CPU 尚未实测':`CPU ${(next.cpuAverage*100).toFixed(0)}% · PEAK ${(next.cpuMaximum*100).toFixed(0)}%`;$('clip').textContent=next.clipped?'电平过载：降低 Input 或 Output':'';
}catch(e){enable(false);$('connection').textContent='连接已断开';$('connection').className='';}finally{setTimeout(poll,250);}}
enable(false);poll();
