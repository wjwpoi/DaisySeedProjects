#!/usr/bin/env python3
"""Run digital release checks; never substitute host results for hardware tests."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[1]
TOOLS=Path('/workspace/daisy-environment')
LOGS=ROOT/'releases/verification-logs'
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_CACHE_HOME','cache'),('XDG_DATA_HOME','data')]:
    os.environ.setdefault(key,str(TOOLS/'kicad'/folder))
LOGS.mkdir(parents=True,exist_ok=True)
CHECKS=[]
def run(name,command,env=None):
    result=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (LOGS/f'{name}.log').write_bytes(result.stdout)
    CHECKS.append(dict(name=name,command=command,passed=result.returncode==0,log=f'releases/verification-logs/{name}.log',logSHA256=hashlib.sha256(result.stdout).hexdigest()))
    print(f'{name}: {"PASS" if result.returncode==0 else "FAIL"}',flush=True)
    if result.returncode:raise RuntimeError(result.stdout.decode(errors='replace')[-5000:])
    return result.stdout.decode(errors='replace')
def version(command):
    return subprocess.check_output(command,cwd=ROOT,text=True).strip()
def main():
    run('hardware',['bash','tools/export_hardware.sh'])
    cad_env=dict(os.environ,XDG_CACHE_HOME=str(TOOLS/'kicad/cache'))
    run('mechanical',[str(TOOLS/'dev-python/bin/python'),'tools/mechanical.py'],cad_env)
    run('procurement-datums',['/usr/bin/python3','tools/check_procurement.py'])
    output=run('audio-and-core',['make','test','audio'])
    run('browser',['node','tests/browser.cjs'])
    sdk=TOOLS/'esp-idf';env=dict(os.environ,IDF_TOOLS_PATH=str(TOOLS/'esp-tools'),IDF_PYTHON_CHECK_CONSTRAINTS='no')
    env['PATH']=str(TOOLS/'dev-python/bin')+os.pathsep+env['PATH']
    run('wireless',['bash','-c','source "$1/export.sh" && idf.py -C "$2" build','seedfx-build',str(sdk),str(ROOT/'firmware/wireless')],env)
    cmsis=ROOT/'../Software/GuitarPedal/dependencies/libDaisy/Drivers/CMSIS-DSP'
    sanitize=ROOT/'build/sanitized';sanitize.mkdir(exist_ok=True)
    flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-D__GNUC_PYTHON__','-DARM_MATH_LOOPUNROLL','-I'+str(cmsis/'Include')]
    for name in ('arm_fir_f32','arm_fir_init_f32'):
        run('sanitize-build-'+name,['gcc',*flags,'-c',str(cmsis/f'Source/FilteringFunctions/{name}.c'),'-o',str(sanitize/f'{name}.o')])
    run('sanitize-build-core',['g++',*flags,'-std=c++17','-DNAM_A2_HOT_DATA=','-DNAM_A2_HOT_STATE_DATA=','-DNAM_A2_STATE_DATA=','tests/core_test.cpp','../Software/GuitarPedal/Effect-Modules/ImpulseResponse/ir_data.cpp',str(sanitize/'arm_fir_f32.o'),str(sanitize/'arm_fir_init_f32.o'),'-o',str(sanitize/'tests')])
    run('sanitizers',[str(sanitize/'tests')],dict(os.environ,ASAN_OPTIONS='detect_leaks=1'))
    board_results={}
    for name in ('mainboard','adapter'):
        drc=json.loads((ROOT/f'hardware/reports/{name}-drc.json').read_text())
        erc=json.loads((ROOT/f'hardware/reports/{name}-erc.json').read_text())
        issues=[v for sheet in erc['sheets'] for v in sheet['violations']]
        board_results[name]=dict(drcViolations=len(drc['violations']),unconnected=len(drc['unconnected_items']),schematicParity=len(drc['schematic_parity']),ercErrors=sum(v['severity']=='error' for v in issues),ercWarnings=sum(v['severity']=='warning' for v in issues))
    # Account both runtime VMA allocations and .data initialization images in SRAM.
    prefix=TOOLS/'toolchains/xpack-arm-none-eabi-gcc-10.3.1-2.3/bin/arm-none-eabi-'
    section_output=version([str(prefix)+'objdump','-h','firmware/audio/build/seedfx.elf']).splitlines()
    regions={'DTCMRAM':(0x20000000,128*1024),'SRAM':(0x24000000,480*1024),'RAM_D2_DMA':(0x30000000,32*1024),'RAM_D2':(0x30008000,256*1024),'SDRAM':(0xc0000000,64*1024*1024)}
    used={name:0 for name in regions}
    for i,line in enumerate(section_output[:-1]):
        match=re.match(r'\s*\d+\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s',line)
        if not match or 'ALLOC' not in section_output[i+1]:continue
        size,vma,lma=(int(n,16) for n in match.groups())
        addresses={vma}
        if 'LOAD' in section_output[i+1]:addresses.add(lma)
        for address in addresses:
            for name,(origin,capacity) in regions.items():
                if size and origin<=address<origin+capacity:used[name]=max(used[name],address+size-origin)
    memory={name:dict(usedBytes=used[name],capacityBytes=capacity,percent=round(used[name]/capacity*100,2)) for name,(_,capacity) in regions.items()}
    for row in memory.values():
        if row['usedBytes']>row['capacityBytes']:raise RuntimeError('Memory overflow')
    evidence=dict(schemaVersion=1,generatedAt=datetime.datetime.now(datetime.timezone.utc).isoformat(),referenceBase='8e9f468ab0d35b87fafa33b9da2f017a0e6a865b',
                  tools=dict(armGcc=version([str(prefix)+'gcc','-dumpfullversion']),espIdf=version(['git','-C',str(sdk),'describe','--tags','--exact-match']),espIdfCommit=version(['git','-C',str(sdk),'rev-parse','HEAD']),kicad=version(['kicad-cli','version']),node=version(['node','--version']),cadquery=version([str(TOOLS/'dev-python/bin/python'),'-c','import cadquery;print(cadquery.__version__)'])),
                  checks=CHECKS,coreAssertions=int(re.search(r'Passed (\d+) checks',output).group(1)),pythonTests=int(re.search(r'Ran (\d+) tests',output).group(1)),jsTests=int(re.search(r'\btests (\d+)',output).group(1)),
                  warmStart=dict(models=5,samples=15360,maxAbsoluteError=float(re.search(r'max absolute error ([\deE.+-]+)',output).group(1).rstrip('.'))),
                  daisyMemory=memory,espApp=dict(sizeBytes=(ROOT/'firmware/wireless/build/seedfx_wireless.bin').stat().st_size,partitionBytes=0x100000),
                  pcb=board_results,mechanical=json.loads((ROOT/'mechanical/fit-check.json').read_text()),cost=json.loads((ROOT/'hardware/cost-plan.json').read_text()),
                  physicalTestsExecuted=False,procurementReady=False,procurementStatus='See docs/PROCUREMENT_READINESS.md: complete component fit, ERC annotation review and supplier quotes pending',unverified=['USB flashing','analog audio and listening','round-trip latency and callback time','switch time','power ripple and thermal behavior','RF range/noise','actual supplier component fit and tolerances','power-cut during physical flash write'])
    (ROOT/'docs/validation.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n')
    print('Saved docs/validation.json. Physical tests are explicitly unexecuted.',flush=True)
if __name__=='__main__':
    try:main()
    except Exception:
        (ROOT/'docs/validation.json').write_text(json.dumps(dict(status='failed',checks=CHECKS,physicalTestsExecuted=False),indent=2)+'\n')
        raise
