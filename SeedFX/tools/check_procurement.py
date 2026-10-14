#!/usr/bin/env python3
"""Compare manufacturing descriptions with independently exported PCB data.

Run with KiCad's system Python, after mechanical generation.
"""
import csv,json
from pathlib import Path
import pcbnew
ROOT=Path(__file__).resolve().parents[1]
board=pcbnew.LoadBoard(str(ROOT/'hardware/mainboard/DaisySeedPedal125b.kicad_pcb'))
jack=next(fp for fp in board.GetFootprints() if fp.GetReference()=='J1')
body=[item for item in jack.GraphicalItems() if item.GetLayer()==pcbnew.F_Fab and hasattr(item,'GetStart')]
if len(body)!=1:raise ValueError('Review changed J1 fabrication outline before deriving the inlet axis')
start,end=body[0].GetStart(),body[0].GetEnd()
axis=175-(pcbnew.ToMM(start.x)+pcbnew.ToMM(end.x))/2
layout=json.loads((ROOT/'mechanical/layout.json').read_text())
if abs(layout['powerInlet']['x']-axis)>0.001:raise ValueError('Power inlet axis differs from J1 body')
rows=list(csv.DictReader((ROOT/'mechanical/side-drill.csv').open()))
inlet=next(row for row in rows if row['Feature']=='DC-005 power inlet')
if abs(float(inlet['Along_axis_mm'])-axis)>0.001:raise ValueError('Side drill power axis differs from J1 body')
for ref,side,description in [('J2','right','Audio input (J2)'),('J4','left','Audio output (J4)')]:
    fp=next(fp for fp in board.GetFootprints() if fp.GetReference()==ref)
    x=175-pcbnew.ToMM(fp.GetPosition().x)
    if (x>33)!=(side=='right'):raise ValueError('Jack side differs from PCB placement')
    if next(row for row in rows if row['Feature']==description)['Face']!=side:raise ValueError('Side drill audio labeling mismatch')
print(f'PCB-derived power axis {axis:.3f}mm and audio-side labels agree with mechanical exports.')
print('Scope: XY/datums only; complete component heights, tolerances and procurement release remain unverified.')
