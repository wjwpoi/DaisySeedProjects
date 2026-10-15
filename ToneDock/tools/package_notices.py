#!/usr/bin/env python3
"""Include the dependency notices alongside development binary artifacts."""
import argparse
import shutil
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--build',type=Path,default=Path('ToneDock/build'))
parser.add_argument('--juce',type=Path)
parser.add_argument('--nam',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
sources={'JUCE':args.juce or args.build/'_deps/juce-src',
         'NeuralAmpModelerCore':args.nam or args.build/'_deps/nam-src',
         'TONE3000':root/'third_party/t3k'}
destination=args.build/'notices'
destination.mkdir(parents=True,exist_ok=True)
shutil.copyfile(root/'LICENSE',destination/'ToneDock-AGPL.txt')
shutil.copyfile(root/'docs/INTEGRATION.md',destination/'SOURCES-AND-LICENSES.md')
count=0
for name,folder in sources.items():
    if not folder.is_dir():raise SystemExit(f'Missing dependency source: {folder}')
    for source in folder.rglob('*'):
        if not source.is_file() or '.git' in source.relative_to(folder).parts:continue
        if not source.name.upper().startswith(('LICENSE','LICENCE','COPYING','NOTICE')):continue
        target=destination/name/source.relative_to(folder)
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,target)
        count+=1
print(f'Packaged {count} framework/dependency notices in {destination}')
