// End-to-end UI checks against an explicit telemetry fixture, never a fake DSP benchmark.
const {chromium}=require('playwright');
const http=require('node:http'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../web');
const schema=JSON.parse(fs.readFileSync(path.join(root,'schema.json')));
let state={connected:true,simulated:true,values:schema.parameters.map(p=>p.default),knobs:[...schema.defaultKnobs],preset:0,bypassed:false,tuning:false,dirty:false,busy:false,clipped:false,pitch:0,cpuAverage:0,cpuMaximum:0,inputPeak:0,outputPeak:0};
const commands=[];let saveDelay=400;
const server=http.createServer((req,res)=>{
  if(req.url==='/api/state'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify(state));return;}
  if(req.url==='/api/command'){let body='';req.on('data',b=>body+=b);req.on('end',()=>{const c=JSON.parse(body);commands.push(c);if(c.op==='set'){state.values[c.id]=c.value;state.dirty=true;}if(c.op==='knobs')state.knobs=c.knobs;if(c.op==='tuner')state.tuning=c.enabled;if(c.op==='bypass')state.bypassed=c.enabled;if(c.op==='load')state.preset=c.preset;const reply=()=>{if(c.op==='save'){state.preset=c.preset;state.dirty=false;}res.setHeader('Content-Type','application/json');res.end('{"ok":true}');};setTimeout(reply,c.op==='save'?saveDelay:0);});return;}
  const filename=req.url==='/'?'index.html':req.url.slice(1);
  if(filename.includes('..')){res.writeHead(404);res.end();return;}
  try{res.setHeader('Content-Type',filename.endsWith('.js')?'text/javascript':filename.endsWith('.css')?'text/css':filename.endsWith('.json')?'application/json':'text/html');res.end(fs.readFileSync(path.join(root,filename)));}catch{res.writeHead(404);res.end();}
});
(async()=>{
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
  try{
    const page=await browser.newPage({viewport:{width:1040,height:1100}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>document.querySelector('#connection').textContent.includes('模拟'));
    assert.equal(await page.locator('input[type=range]').count(),15);
    await page.locator('#p4').selectOption('3');await page.waitForFunction(()=>document.querySelector('#modelName').textContent==='Mesa');
    await page.locator('summary').click();await page.locator('#k0').selectOption('2');await page.locator('#k1').selectOption('3');
    await page.waitForTimeout(500);assert.equal(await page.locator('#k0').inputValue(),'2');assert.equal(await page.locator('#k1').inputValue(),'3');
    await page.locator('#applyMaps').click();await page.waitForFunction(()=>document.querySelector('#notice').textContent==='设置已确认');assert.equal(state.knobs[0],2);
    await page.locator('#save').click();await page.waitForTimeout(100);assert.notEqual(await page.locator('#notice').textContent(),'预设已写入并校验');
    await page.waitForFunction(()=>document.querySelector('#notice').textContent==='预设已写入并校验');
    state.tuning=true;state.pitch=110;await page.waitForFunction(()=>document.querySelector('#note').textContent==='A2');
    state.tuning=false;await page.waitForTimeout(300);
    fs.mkdirSync(path.resolve(__dirname,'../docs/images'),{recursive:true});
    await page.screenshot({path:path.resolve(__dirname,'../docs/images/web-desktop.png'),fullPage:true});
    await page.setViewportSize({width:390,height:844});await page.waitForTimeout(300);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth),true);
    await page.screenshot({path:path.resolve(__dirname,'../docs/images/web-mobile.png'),fullPage:true});
    state.connected=false;await page.waitForFunction(()=>document.querySelector('#load').disabled);
    assert.equal(await page.locator('#connection').textContent(),'音频主板未连接');assert.deepEqual(errors,[]);
    console.log(`Browser checks passed: model selection, multi-knob editing, confirmed save, tuner, 390px layout, offline lockout. ${commands.length} fixture commands.`);
  }finally{await browser.close();await new Promise(r=>server.close(r));}
})().catch(e=>{console.error(e);server.close();process.exitCode=1;});
