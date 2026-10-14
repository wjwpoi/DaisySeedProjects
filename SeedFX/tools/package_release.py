#!/usr/bin/env python3
"""Package checked firmware, fabrication data and editable project for review."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import zipfile
ROOT=Path(__file__).resolve().parents[1]
RELEASE=ROOT/'releases'
REF='8e9f468ab0d35b87fafa33b9da2f017a0e6a865b'
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def product_files():
    for path in sorted(ROOT.rglob('*')):
        if not path.is_file():continue
        parts=path.relative_to(ROOT).parts
        if any(p in ('build','__pycache__','.git') for p in parts) or path.suffix in ('.pyc','.kicad_prl'):continue
        if path.name in ('sdkconfig','sdkconfig.old','manifest.json'):continue
        if path.name.startswith('_autosave-'):continue
        yield path

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=Path('/workspace/SeedFX-v1-engineering.zip'));args=parser.parse_args()
    if args.output.resolve().is_relative_to(ROOT):raise RuntimeError('Write the archive outside the project to avoid recursively packaging it')
    validation=json.loads((ROOT/'docs/validation.json').read_text())
    if validation.get('status')=='failed' or not validation.get('checks') or not all(c['passed'] for c in validation['checks']):raise RuntimeError('Release checks have not passed')
    checked_at=datetime.datetime.fromisoformat(validation['generatedAt']).timestamp()
    sources=[p for folder in ('firmware','web','tests') for p in (ROOT/folder).rglob('*') if p.is_file() and 'build' not in p.relative_to(ROOT).parts and '__pycache__' not in p.relative_to(ROOT).parts and p.name not in ('sdkconfig','sdkconfig.old')]
    sources += [ROOT/'Makefile',ROOT/'tools/hardware.py',ROOT/'tools/mechanical.py']
    if any(p.stat().st_mtime>checked_at for p in sources):raise RuntimeError('Sources changed after validation; run tools/validate.py again')
    for check in validation['checks']:

        if digest(ROOT/check['log'])!=check['logSHA256']:raise RuntimeError('Validation log changed: '+check['name'])
    assets={
      'daisy/seedfx.bin':ROOT/'firmware/audio/build/seedfx.bin',
      'daisy/seedfx.hex':ROOT/'firmware/audio/build/seedfx.hex',
      'daisy/seedfx.elf':ROOT/'firmware/audio/build/seedfx.elf',
      'daisy/dsy_bootloader_v6_4-intdfu-2000ms.bin':ROOT/'../Software/GuitarPedal/dependencies/libDaisy/core/dsy_bootloader_v6_4-intdfu-2000ms.bin',
      'esp32c3/bootloader.bin':ROOT/'firmware/wireless/build/bootloader/bootloader.bin',
      'esp32c3/partition-table.bin':ROOT/'firmware/wireless/build/partition_table/partition-table.bin',
      'esp32c3/seedfx_wireless.bin':ROOT/'firmware/wireless/build/seedfx_wireless.bin',
      'esp32c3/seedfx_wireless.elf':ROOT/'firmware/wireless/build/seedfx_wireless.elf'}
    for destination,source in assets.items():
        target=RELEASE/'firmware'/destination;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    license_roots={'libDaisy':ROOT/'../Software/GuitarPedal/dependencies/libDaisy','esp-idf':Path('/workspace/daisy-environment/esp-idf')}
    for label,origin in license_roots.items():
        for source in origin.rglob('*'):
            if not source.is_file() or any(part in ('.git','build') for part in source.relative_to(origin).parts):continue
            if not source.name.upper().startswith(('LICENSE','COPYING','NOTICE')):continue
            target=RELEASE/'licenses'/label/source.relative_to(origin)
            target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    for board in ('mainboard','adapter'):

        cam=ROOT/f'hardware/manufacturing/{board}'
        with zipfile.ZipFile(RELEASE/f'{board}-pcb.zip','w',zipfile.ZIP_DEFLATED) as z:
            for folder in ('gerbers','drill'):
                for p in sorted((cam/folder).glob('*')):
                    if p.is_file():z.write(p,p.relative_to(cam))
    refs=[ROOT/'../Software/GuitarPedal/Effect-Modules/Nam/nam_a2_runtime.h',ROOT/'../Software/GuitarPedal/Effect-Modules/Nam/model_data_nam_a2.h',ROOT/'../Software/GuitarPedal/Effect-Modules/ImpulseResponse/ir_data.h',ROOT/'../Software/GuitarPedal/Effect-Modules/ImpulseResponse/ir_data.cpp']
    manifest=dict(version='v1-engineering',generatedAt=datetime.datetime.now(datetime.timezone.utc).isoformat(),referenceRepository='wjwpoi/DaisySeedProjects',referenceCommit=REF,physicalTestsExecuted=False,
                  buildQuantity=json.loads((ROOT/'hardware/cost-plan.json').read_text())['buildQuantity'],
                  flash=dict(daisyBootloaderAddress='0x08000000',daisyAppAddress='0x90040000',presetBanks=['0x907f0000','0x907f1000'],esp32c3=dict(bootloader='0x0',partitionTable='0x8000',application='0x10000',flashSize='4MB',mode='DIO',frequency='80MHz')),
                  files={str(p.relative_to(ROOT)):dict(bytes=p.stat().st_size,sha256=digest(p)) for p in product_files()},referenceFiles={str(p.resolve().relative_to(ROOT.parent)):dict(bytes=p.stat().st_size,sha256=digest(p)) for p in refs})
    (RELEASE/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(args.output,'w',zipfile.ZIP_DEFLATED) as z:
        for p in product_files():z.write(p,'SeedFX/'+str(p.relative_to(ROOT)))
        z.write(RELEASE/'manifest.json','SeedFX/releases/manifest.json')
        # Captured source excerpts give a reviewer the exact shipped runtime and weights.
        for p in refs:z.write(p,'reference/'+str(p.resolve().relative_to(ROOT.parent)))
        if z.testzip() is not None:raise RuntimeError('Archive checksum error')
        for name,meta in manifest['files'].items():
            if hashlib.sha256(z.read('SeedFX/'+name)).hexdigest()!=meta['sha256']:raise RuntimeError('Archive/manifest mismatch: '+name)
    args.output.with_suffix(args.output.suffix+'.sha256').write_text(digest(args.output)+'  '+args.output.name+'\n')
    print(f'Created {args.output}: {args.output.stat().st_size} bytes; {len(manifest["files"])} hashed project files')
if __name__=='__main__':main()
