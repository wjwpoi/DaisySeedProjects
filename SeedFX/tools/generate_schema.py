#!/usr/bin/env python3
"""Generate browser metadata from the firmware's single parameter registry."""
import json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'firmware/common/parameters.hpp').read_text()
rows=re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,\s*([^,]+),\s*([^,]+),\s*([^}]+)\}',text)
def number(value):return float(value.strip().removesuffix('f'))
params=[dict(id=i,key=k,label=l,unit=u,min=number(a),max=number(b),default=number(c),integer=i in (4,8) or i>=17) for i,(k,l,u,a,b,c) in enumerate(rows)]
assert len(params)==26
models=['BE-100','JCM800','Ampeg','Mesa','1959BJA']
schema=dict(version=1,sampleRate=48000,blockSize=48,presetCount=16,parameters=params,models=models,cabinets=['Rhythm','Lead'],defaultKnobs=[0,5,6,7,16,11])
(ROOT/'web/schema.json').write_text(json.dumps(schema,ensure_ascii=False,indent=2)+'\n')
print(f'Generated {len(params)} parameters')
