#!/usr/bin/env python3
"""Generate the parametric 125B case, radio window, panel artwork and drill data.

Coordinate convention: front view, X right, Y down, Z from front towards lid.
Pot/encoder shaft centers are derived from Rev6 PCB fabrication circles.
"""
import csv,json,math
from pathlib import Path
import cadquery as cq
import ezdxf
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'mechanical';OUT.mkdir(exist_ok=True)
W,H,D,T=66.0,121.5,42.0,1.5
holes=[('POT1',13,17,7.2,'INPUT'),('POT2',33,17,7.2,'BASS'),('POT3',53,17,7.2,'MID'),
       ('POT4',13,40.5,7.2,'TREBLE'),('POT5',33,40.5,7.2,'LEVEL'),('POT6',53,40.5,7.2,'USER'),
       ('ENC1',33,81.275,7.2,'EDIT'),('LED1',15,87.5,3.2,'ON'),('LED2',51,87.5,3.2,'CLIP'),
       ('FS1',15,106.5,12.2,'BYPASS / TUNER'),('FS2',51,106.5,12.2,'TAP / PRESET')]
window=dict(x=21.5,y=53.0,width=23.0,height=12.0)
body_depth=D-T-2
case=cq.Workplane('XY').center(W/2,H/2).rect(W,H).extrude(body_depth).edges('|Z').fillet(4)
cavity=cq.Workplane('XY').workplane(offset=T).center(W/2,H/2).rect(W-2*T,H-2*T).extrude(D).edges('|Z').fillet(2.5)
case=case.cut(cavity)
for ref,x,y,d,label in holes:case=case.cut(cq.Workplane('XY').center(x,y).circle(d/2).extrude(T+1))
case=case.cut(cq.Workplane('XY').center(window['x']+window['width']/2,window['y']+window['height']/2).rect(window['width'],window['height']).extrude(T+1))
# Audio sockets: reuse the Rev6 enclosure's center lines, 13 mm bores.
for x in [-1,W-2*T]:case=case.cut(cq.Workplane('YZ',origin=(x,0,0)).center(87.5,22).circle(6.5).extrude(2*T+2))
# PCB-mounted DC-005 inlet: rectangular clearance, not a panel-mount substitute.
case=case.cut(cq.Workplane('XZ',origin=(0,2,0)).center(33,22).rect(10.0,9.0).extrude(4))
# Service access on input side for Daisy's USB/reset, separate from audio jack.
case=case.cut(cq.Workplane('YZ',origin=(-1,0,0)).center(47,22).rect(12,8).extrude(T+2))
# Concealed side radio window: non-metallic patch plus external IPEX antenna.
case=case.cut(cq.Workplane('YZ',origin=(W-T-1,0,0)).center(44,28.5).rect(40,17).extrude(T+3))
lid=cq.Workplane('XY').center(W/2,H/2).rect(W,H).extrude(T).edges('|Z').fillet(4)
# Four stand-offs clear the PCB outline; use tapped M3 bosses in the metal body.
boss_positions=[(5.5,5.5),(60.5,5.5),(5.5,116),(60.5,116)]
for x,y in boss_positions:
    boss=cq.Workplane('XY').center(x,y).circle(3).extrude(body_depth)
    bore=cq.Workplane('XY').workplane(offset=body_depth-8).center(x,y).circle(1.25).extrude(9)
    case=case.union(boss).cut(bore)
    lid=lid.cut(cq.Workplane('XY').center(x,y).circle(1.65).extrude(T+1))
# Clip-in/adhesive dielectric window with a 1 mm flange. No metallic ink here.
rf=cq.Workplane('XY').rect(42,19).extrude(1).union(cq.Workplane('XY').workplane(offset=1).rect(39.7,16.7).extrude(T))
# A 2 mm low-cost lid spacer preserves a 40 mm nominal body/lid envelope.
spacer=cq.Workplane('XY').center(W/2,H/2).rect(W,H).extrude(2).edges('|Z').fillet(4)
spacer=spacer.cut(cq.Workplane('XY').center(W/2,H/2).rect(W-2*T,H-2*T).extrude(3).edges('|Z').fillet(2.5))
for x,y in boss_positions:
    spacer=spacer.union(cq.Workplane('XY').center(x,y).circle(3).extrude(2))
    spacer=spacer.cut(cq.Workplane('XY').center(x,y).circle(1.65).extrude(3))
# Lid-mounted tray and two M2 adapter pillars, global front-view datum.
# With the 42 mm case, a 6 mm connector envelope clears the Seed keepout.
tray_z=D-T-1.2
tray=cq.Workplane('XY').workplane(offset=tray_z).center(33,74.5).rect(58,42).extrude(1.2)
adapter_holes=[(34,57.5),(15,81.5)]
for x,y in adapter_holes:
    tray=tray.union(cq.Workplane('XY').workplane(offset=tray_z-2).center(x,y).circle(2).extrude(2))
    tray=tray.cut(cq.Workplane('XY').workplane(offset=tray_z-3).center(x,y).circle(1.05).extrude(5))
placements=[dict(name='radio_adapter',x=5,y=55.5,z=tray_z-9.6,width=32,length=28,height=7.6),
            dict(name='xiao_esp32c3',x=38.5,y=55.5,z=tray_z-5,width=21,length=17.5,height=4),
            dict(name='mini360',x=39.5,y=76,z=tray_z-5,width=18,length=14,height=4)]
for name,obj in [('case',case),('lid',lid),('lid-spacer',spacer),('rf-window',rf),('insulating-tray',tray)]:
    cq.exporters.export(obj,str(OUT/f'seedfx-{name}.step'));cq.exporters.export(obj,str(OUT/f'seedfx-{name}.stl'))
assembly=cq.Assembly(name='SeedFX');assembly.add(spacer,name='lid_spacer',loc=cq.Location(cq.Vector(0,0,body_depth)),color=cq.Color(.12,.16,.13));assembly.add(tray,name='insulating_tray',color=cq.Color(.8,.8,.8));assembly.add(rf.rotate((0,0,0),(0,0,1),90).rotate((0,0,0),(0,1,0),90).translate((W-T-1,44,28.5)),name='radio_window',color=cq.Color(.2,.25,.2));assembly.add(case,name='case',color=cq.Color(.12,.16,.13));assembly.add(lid,name='lid',loc=cq.Location(cq.Vector(0,0,D-T)),color=cq.Color(.12,.16,.13))
if (OUT/'mainboard.step').exists():
    board=cq.importers.importStep(str(OUT/'mainboard.step')).val()
    board=board.rotate((0,0,0),(0,1,0),180).rotate((0,0,0),(1,0,0),180).translate((175,-37.5,14.5))
    overlap=board.intersect(case.val()).Volume()
    if overlap>1e-5:raise ValueError(f'Board/case interference: {overlap} mm3')
    assembly.add(board,name='mainboard_body',color=cq.Color(.1,.3,.15))
    body_lid_overlap=case.val().intersect(lid.val().translate((0,0,D-T))).Volume()
    spacer_lid_overlap=spacer.val().translate((0,0,body_depth)).intersect(lid.val().translate((0,0,D-T))).Volume()
    if body_lid_overlap+spacer_lid_overlap>1e-5:raise ValueError('Lid/body collision')
    fit={'lidBodyInterference_mm3':body_lid_overlap,'lidSpacerInterference_mm3':spacer_lid_overlap,'boardCaseInterference_mm3':overlap,'scope':'bare board and conservative accessory envelopes; main components use a procurement keepout, not measured CAD','pcbThickness_mm':board.BoundingBox().zlen,'mainElectronicsDepthLimit_mm':28,'accessoryChecks':[]}
    envelopes=[]
    for item in placements:
        box=cq.Workplane('XY').workplane(offset=item['z']).center(item['x']+item['width']/2,item['y']+item['length']/2).rect(item['width'],item['length']).extrude(item['height']).val()
        interference=box.intersect(case.val()).Volume()+box.intersect(lid.val().translate((0,0,D-T))).Volume()+box.intersect(board).Volume()+box.intersect(spacer.val().translate((0,0,body_depth))).Volume()
        if interference>1e-5 or item['z']<28:raise ValueError('Accessory envelope collision: '+item['name'])
        for previous in envelopes:
            if box.intersect(previous).Volume()>1e-5:raise ValueError('Accessory envelopes overlap')
        envelopes.append(box);assembly.add(box,name=item['name']+'_envelope',color=cq.Color(.2,.3,.4,.55))
        fit['accessoryChecks'].append(dict(name=item['name'],caseBoardLidInterference_mm3=interference,clearanceToMainKeepout_mm=item['z']-28))
    (OUT/'fit-check.json').write_text(json.dumps(fit,indent=2)+'\n')
assembly.save(str(OUT/'seedfx-enclosure.step'))
# Drawing layer names distinguish through cuts from print/keep-out information.
doc=ezdxf.new('R2010');doc.units=4;space=doc.modelspace()
for name in ['FRONT_OUTLINE','DRILL','DISPLAY_CUT','PRINT_SAFE','RADIO_WINDOW']:doc.layers.new(name)
space.add_lwpolyline([(0,0),(W,0),(W,H),(0,H)],close=True,dxfattribs={'layer':'FRONT_OUTLINE'})
for ref,x,y,d,label in holes:
    space.add_circle((x,y),d/2,dxfattribs={'layer':'DRILL'})
    space.add_text(ref,height=1.5,dxfattribs={'insert':(x+4,y),'layer':'PRINT_SAFE'})
x,y,a,b=window['x'],window['y'],window['width'],window['height'];space.add_lwpolyline([(x,y),(x+a,y),(x+a,y+b),(x,y+b)],close=True,dxfattribs={'layer':'DISPLAY_CUT'})
doc.saveas(OUT/'front-drill.dxf')
with (OUT/'front-drill.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['Reference','X_mm_from_left','Y_mm_from_top','Through_diameter_mm','Label'])
    w.writerows(holes)
layout={'units':'mm','body':{'width':W,'height':H,'depth':D,'wall':T,'bareBodyDepth':body_depth,'lidThickness':T,'lidSpacerThickness':2},'frontHoles':[dict(ref=r,x=x,y=y,diameter=d,label=l) for r,x,y,d,l in holes],
        'display':window,'radioWindow':{'side':'right','y':24,'z':20,'length':40,'height':17},
        'pcbTransform':{'x':'175 - pcb_x','y':'pcb_y - 37.5','z':14.5},
        'accessoryEnvelopes':placements,'adapterMountHoles':adapter_holes,'trayDepth':tray_z,
        'assemblyAssumptions':{'seedKeepoutDepthMax':28,'accessoryLayerDepthMin':29.7,'needsPhysicalFitCheck':True}}
(OUT/'layout.json').write_text(json.dumps(layout,indent=2)+'\n')
def artwork(template=False):
    svg=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">']
    svg.append('<g id="background"><rect width="66" height="121.5" rx="4" fill="#1d2822"/></g>')
    svg.append('<g id="custom-artwork">'+('<!-- Replace this layer with the user supplied artwork; preserve cut/keepout layers. -->' if template else '<path d="M-8 48 C16 16 44 82 77 42 M-8 54 C16 22 44 88 77 48 M-8 60 C16 28 44 94 77 54" fill="none" stroke="#314333" stroke-width="1.5"/>')+'</g>')
    svg.append('<g id="labels" fill="#edf0df" font-family="sans-serif" text-anchor="middle">')
    for ref,x,y,d,label in holes:
        if ref.startswith('POT'):svg.append(f'<text x="{x}" y="{y-8}" font-size="2.3" letter-spacing=".25">{label}</text>')
    svg.append('<text x="33" y="73" font-size="4.5" font-weight="bold">SeedFX.</text><text x="33" y="91.5" font-size="2">EDIT</text><text x="33" y="95" font-size="1.8" letter-spacing=".6">NAM A2 · MULTI EFFECTS</text>')
    svg.append('<text x="15" y="117" font-size="1.7">BYPASS / TUNER</text><text x="51" y="117" font-size="1.7">TAP / PRESET</text></g>')
    svg.append('<g id="hardware-keepouts" fill="#101511" stroke="#c6e891" stroke-width=".3">')
    for ref,x,y,d,label in holes:
        radius=6.25 if ref.startswith('POT') or ref=='ENC1' else d/2
        svg.append(f'<circle cx="{x}" cy="{y}" r="{radius}"/>')
    svg.append('<rect x="21.5" y="53" width="23" height="12" rx="1"/></g>')
    svg.append('<g id="drill-guides" fill="none" stroke="#ef766b" stroke-width=".12" opacity=".5">')
    for ref,x,y,d,label in holes:svg.append(f'<circle cx="{x}" cy="{y}" r="{d/2}"/><path d="M{x-1} {y}h2 M{x} {y-1}v2"/>')
    svg.append('</g></svg>');return '\n'.join(svg)
(OUT/'panel-design.svg').write_text(artwork());(OUT/'panel-print.svg').write_text(artwork().replace('id="drill-guides"','id="drill-guides" style="display:none"'));(OUT/'panel-artwork-template.svg').write_text(artwork(True))
with (OUT/'side-drill.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['Face','Feature','Along_axis_mm','Depth_from_front_mm','Shape','Diameter_or_width_mm','Height_mm'])
    w.writerows([['left','Audio input',87.5,22,'circle',13,13],['right','Audio output',87.5,22,'circle',13,13],['top','DC-005 power inlet',33,22,'rectangle',10,9],['left','USB service access',47,22,'rectangle',12,8],['right','RF dielectric window',44,28.5,'rectangle',40,17]])
with (OUT/'accessory-placement.csv').open('w') as f:
    w=csv.DictWriter(f,fieldnames=placements[0].keys());w.writeheader();w.writerows(placements)
print('Generated enclosure STEP/STL, RF insert, tray, drill DXF/CSV and editable panel SVGs')
