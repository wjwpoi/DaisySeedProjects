#!/usr/bin/python3
"""Generate the portable reference mainboard package and the routed UART adapter.

Run with KiCad 9's system Python (pcbnew), never the SDK's Python environment.
"""
import csv,json,re,shutil,uuid,xml.etree.ElementTree as ET
from pathlib import Path
import pcbnew as pcb
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT.parent/'Hardware/GuitarPedal125b/pcb'
DEST=ROOT/'hardware/mainboard'
ADAPTER=ROOT/'hardware/adapter'
def uid():return str(uuid.uuid4())
def vec(x,y):return pcb.VECTOR2I(pcb.FromMM(x),pcb.FromMM(y))
def block(text,start):
    pos=start;depth=0;quoted=False;escaped=False
    while pos<len(text):
        c=text[pos]
        if quoted:
            if escaped:escaped=False
            elif c=='\\':escaped=True
            elif c=='"':quoted=False
        elif c=='"':quoted=True
        elif c=='(':depth+=1
        elif c==')':
            depth-=1
            if depth==0:return text[start:pos+1]
        pos+=1
    raise ValueError('Unbalanced KiCad source')
def cached_mainboard():
    DEST.mkdir(parents=True,exist_ok=True)
    for p in SOURCE.iterdir():
        if p.is_file() and p.suffix in ('.kicad_pcb','.kicad_sch','.kicad_pro','.kicad_sym'):shutil.copy2(p,DEST/p.name)
    board=pcb.LoadBoard(str(DEST/'DaisySeedPedal125b.kicad_pcb'))
    tables={};seen=set()
    for item in board.GetDrawings():
        if item.GetLayer() in (pcb.F_SilkS,pcb.B_SilkS):item.SetLayer(pcb.Dwgs_User)
    for fp in board.GetFootprints():
        for item in fp.GraphicalItems():
            if item.GetLayer()==pcb.F_SilkS:item.SetLayer(pcb.F_Fab)
            elif item.GetLayer()==pcb.B_SilkS:item.SetLayer(pcb.B_Fab)
        fp.Reference().SetLayer(pcb.B_Fab if fp.IsFlipped() else pcb.F_Fab)
        fp.Value().SetLayer(pcb.B_Fab if fp.IsFlipped() else pcb.F_Fab)
        lib=str(fp.GetFPID().GetLibNickname());name=str(fp.GetFPID().GetLibItemName())
        folder=DEST/'Libraries'/f'{lib}.pretty';folder.mkdir(parents=True,exist_ok=True);tables[lib]=folder
        if (lib,name) not in seen:
            seen.add((lib,name))
            copy=fp.Duplicate()
            if copy.IsFlipped():copy.Flip(copy.GetPosition(),False)
            copy.SetOrientationDegrees(0);copy.SetPosition(vec(0,0))
            pcb.PCB_IO_MGR.PluginFind(pcb.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(folder),copy)
    pcb.SaveBoard(str(DEST/'DaisySeedPedal125b.kicad_pcb'),board)
    text='(fp_lib_table\n  (version 7)\n'
    for name,folder in tables.items():text+=f'  (lib (name "{name}")(type "KiCad")(uri "${{KIPRJMOD}}/Libraries/{folder.name}")(options "")(descr "Pinned reference footprint"))\n'
    (DEST/'fp-lib-table').write_text(text+')\n')
    symbols={}
    for sheet in DEST.glob('*.kicad_sch'):
        data=sheet.read_text();start=data.index('(lib_symbols');cache=block(data,start)
        for match in re.finditer(r'\(symbol "([^":]+):([^"]+)"',cache):
            lib,name=match.groups();symbols.setdefault(lib,{})[name]=block(cache,match.start()).replace(f'(symbol "{lib}:{name}"',f'(symbol "{name}"',1)
    symbol_dir=DEST/'Libraries/Symbols';symbol_dir.mkdir(exist_ok=True)
    for lib,entries in symbols.items():
        (symbol_dir/f'{lib}.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "kicad_symbol_editor")\n'+'\n'.join(entries.values())+')\n')
    (DEST/'sym-lib-table').write_text('(sym_lib_table (version 7)\n'+''.join(f'(lib (name "{lib}")(type "KiCad")(uri "${{KIPRJMOD}}/Libraries/Symbols/{lib}.kicad_sym")(options "")(descr "Pinned reference symbols"))\n' for lib in symbols)+')\n')
    print(f'Packed {len(tables)} footprint libraries for the reference board')
def symbol(library,name):
    text=(Path('/usr/share/kicad/symbols')/f'{library}.kicad_sym').read_text()
    match=re.search(r'\(symbol "'+re.escape(name)+r'"\s',text)
    if not match:raise ValueError(f'Missing {library}:{name}')
    return block(text,match.start()).replace(f'(symbol "{name}"',f'(symbol "{library}:{name}"',1)
def schematic(parts):
    rootid=uid();content=f'(kicad_sch (version 20250114) (generator "eeschema") (uuid {rootid}) (paper "A4")\n'
    content+='(title_block (title "SeedFX wireless power / UART adapter") (rev "1.0") (comment 1 "230400 baud; 3.3 V logic; disconnect UART harness during independent USB flashing"))\n'
    unique={(p['lib'],p['symbol']) for p in parts};content+='(lib_symbols\n'+'\n'.join(symbol(*x) for x in sorted(unique))+')\n'
    for p in parts:
        sym=symbol(p['lib'],p['symbol']);x,y=round(p['sx']/1.27)*1.27,round(p['sy']/1.27)*1.27;component_id=uid()
        content+=f'(symbol (lib_id "{p["lib"]}:{p["symbol"]}") (at {x} {y} 0) (unit 1) (in_bom {"no" if p["lib"]=="Mechanical" else "yes"}) (on_board yes) (uuid {component_id})\n'
        for key,value,px,py in [('Reference',p['ref'],x,y-10),('Value',p['value'],x,y+10),('Footprint',p['footprint'],x,y)]:
            content+=f'(property "{key}" "{value}" (at {px} {py} 0) (effects (font (size 1.27 1.27))'+(' hide' if key=='Footprint' else '')+'))\n'
        for num in p['nets']:content+=f'(pin "{num}" (uuid {uid()}))\n'
        content+=f'(instances (project "seedfx-adapter" (path "/{rootid}" (reference "{p["ref"]}") (unit 1)))) )\n'
        for match in re.finditer(r'\(pin\s+\w+\s+\w+\s*\(at\s+(-?[\d.]+)\s+(-?[\d.]+)\s+([\d.]+)\)',sym):
            pin=block(sym,match.start());number=re.search(r'\(number "([^"]+)"',pin)
            if not number or number.group(1) not in p['nets']:continue
            px=x+float(match.group(1));py=y-float(match.group(2));net=p['nets'][number.group(1)]
            endpoint=px-5.08
            content+=f'(wire (pts (xy {px} {py}) (xy {endpoint} {py})) (stroke (width 0) (type default)) (uuid {uid()}))\n'
            content+=f'(label "{net}" (at {endpoint} {py} 0) (effects (font (size 1.0 1.0)) (justify left bottom)) (uuid {uid()}))\n'
    (ADAPTER/'seedfx-adapter.kicad_sch').write_text(content+')\n')
def adapter():
    ADAPTER.mkdir(parents=True,exist_ok=True);b=pcb.BOARD();nets={};fps={}
    for name in ['GND','5V_SOURCE','5V_WIFI','SEED_TX','SEED_RX','ESP_TX','ESP_RX']:
        n=pcb.NETINFO_ITEM(b,"/"+name);b.Add(n);nets[name]=n
    parts=[
      dict(ref='J1',value='5V BUCK INPUT',lib='Connector_Generic',symbol='Conn_01x02',footprint='Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical',x=4,y=4,sx=35,sy=35,nets={'1':'5V_SOURCE','2':'GND'}),
      dict(ref='J2',value='XIAO HARNESS',lib='Connector_Generic',symbol='Conn_01x04',footprint='Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical',x=4,y=15,sx=35,sy=85,nets={'1':'5V_WIFI','2':'GND','3':'ESP_TX','4':'ESP_RX'}),
      dict(ref='J3',value='DAISY HARNESS',lib='Connector_Generic',symbol='Conn_01x03',footprint='Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical',x=26,y=18,sx=120,sy=85,nets={'1':'GND','2':'SEED_TX','3':'SEED_RX'}),
      dict(ref='FB1',value='BLM21PG601SN1D',lib='Device',symbol='FerriteBead',footprint='Inductor_SMD:L_0805_2012Metric',x=13,y=4,sx=85,sy=35,nets={'1':'5V_SOURCE','2':'5V_WIFI'}),
      dict(ref='C1',value='22u 10V X5R',lib='Device',symbol='C',footprint='Capacitor_SMD:C_1206_3216Metric',x=20,y=8,sx=130,sy=35,nets={'1':'5V_WIFI','2':'GND'}),
      dict(ref='C2',value='22u 10V X5R',lib='Device',symbol='C',footprint='Capacitor_SMD:C_1206_3216Metric',x=13,y=10,sx=175,sy=35,nets={'1':'5V_WIFI','2':'GND'}),
      dict(ref='C3',value='100n 50V X7R',lib='Device',symbol='C',footprint='Capacitor_SMD:C_0805_2012Metric',x=7,y=12,sx=220,sy=35,nets={'1':'5V_WIFI','2':'GND'}),
      dict(ref='R1',value='1k 1%',lib='Device',symbol='R',footprint='Resistor_SMD:R_0805_2012Metric',x=19,y=21,sx=175,sy=85,nets={'1':'SEED_TX','2':'ESP_RX'}),
      dict(ref='R2',value='1k 1%',lib='Device',symbol='R',footprint='Resistor_SMD:R_0805_2012Metric',x=16,y=15,sx=220,sy=85,nets={'1':'ESP_TX','2':'SEED_RX'}),
    ]
    for p in parts:
        lib,name=p['footprint'].split(':');fp=pcb.FootprintLoad(f'/usr/share/kicad/footprints/{lib}.pretty',name)
        if not fp:raise ValueError(p['footprint'])
        fp.SetFPID(pcb.LIB_ID(lib,name));fp.SetReference(p['ref']);fp.SetValue(p['value']);fp.SetPosition(vec(p['x'],p['y']));b.Add(fp)
        fp.Reference().SetLayer(pcb.F_Fab);fp.Value().SetLayer(pcb.F_Fab)
        for pad in fp.Pads():pad.SetNet(nets[p['nets'][pad.GetNumber()]])
        fps[p['ref']]=fp
    for i,(x,y) in enumerate([(29,2),(10,26)]):
        f=pcb.FootprintLoad('/usr/share/kicad/footprints/MountingHole.pretty','MountingHole_2.2mm_M2');f.SetFPID(pcb.LIB_ID('MountingHole','MountingHole_2.2mm_M2'));f.SetReference(f'H{i+1}');f.SetValue('M2 NPTH');f.SetPosition(vec(x,y));f.Reference().SetLayer(pcb.F_Fab);f.Value().SetLayer(pcb.F_Fab);b.Add(f)
    for a,z in [((0,0),(32,0)),((32,0),(32,28)),((32,28),(0,28)),((0,28),(0,0))]:
        s=pcb.PCB_SHAPE();s.SetShape(pcb.SHAPE_T_SEGMENT);s.SetLayer(pcb.Edge_Cuts);s.SetStart(vec(*a));s.SetEnd(vec(*z));s.SetWidth(pcb.FromMM(.1));b.Add(s)
    def point(ref,pin):return next(x.GetPosition() for x in fps[ref].Pads() if x.GetNumber()==str(pin))
    def route(name,points,layer=pcb.F_Cu,width=.3):
        points=[point(*p) if isinstance(p[0],str) else vec(*p) for p in points]
        for a,z in zip(points,points[1:]):
            t=pcb.PCB_TRACK(b);t.SetStart(a);t.SetEnd(z);t.SetWidth(pcb.FromMM(width));t.SetLayer(layer);t.SetNet(nets[name]);b.Add(t)
    def via(name,x,y):
        v=pcb.PCB_VIA(b);v.SetPosition(vec(x,y));v.SetWidth(pcb.F_Cu,pcb.FromMM(.7));v.SetWidth(pcb.B_Cu,pcb.FromMM(.7));v.SetDrill(pcb.FromMM(.3));v.SetViaType(pcb.VIATYPE_THROUGH);v.SetLayerPair(pcb.F_Cu,pcb.B_Cu);v.SetNet(nets[name]);b.Add(v)
    route('5V_SOURCE',[('J1',1),('FB1',1)],width=.8)
    route('5V_WIFI',[('FB1',2),('C1',1)],width=.7)
    route('5V_WIFI',[('FB1',2),(11.5,6),('C2',1)],width=.7)
    route('5V_WIFI',[('C2',1),(6.05,10),('C3',1),('J2',1)],width=.7)
    route('ESP_TX',[('J2',3),(7,20.08),(7,15),('R2',1)])
    route('ESP_RX',[('J2',4),(5.5,24.5),(20.025,24.5),('R1',2)])
    route('SEED_TX',[('J3',2),(23,20.54),(23,18.5),(17.975,18.5),('R1',1)])
    route('SEED_RX',[('R2',2),(18.5,15)]);via('SEED_RX',18.5,15)
    route('SEED_RX',[(18.5,15),('J3',3)],layer=pcb.B_Cu)
    for ref,x,y in [('C1',22,8),('C2',15.5,10),('C3',9,12)]:route('GND',[(ref,2),(x,y)]);via('GND',x,y)
    route('GND',[('J1',2),(2,6.54),(2,17.54),('J2',2)],layer=pcb.B_Cu,width=.8)
    route('GND',[('J1',2),(4,7.2),(28,7.2),(28,18),('J3',1)],layer=pcb.B_Cu,width=.8)
    for x,y in [(22,8),(15.5,10),(9,12)]:route('GND',[(x,y),(x,7.2)],layer=pcb.B_Cu,width=.8)
    label=pcb.PCB_TEXT(b);label.SetText('SeedFX UART / 5V');label.SetPosition(vec(19,26.4));label.SetTextSize(vec(.85,.85));label.SetTextThickness(pcb.FromMM(.15));label.SetLayer(pcb.F_SilkS);b.Add(label)
    pcb.SaveBoard(str(ADAPTER/'seedfx-adapter.kicad_pcb'),b);schematic(parts+[dict(ref='H1',value='M2 NPTH',lib='Mechanical',symbol='MountingHole',footprint='MountingHole:MountingHole_2.2mm_M2',sx=35,sy=135,nets={}),dict(ref='H2',value='M2 NPTH',lib='Mechanical',symbol='MountingHole',footprint='MountingHole:MountingHole_2.2mm_M2',sx=85,sy=135,nets={})])
    # Project-local standard libraries ensure CLI ERC can resolve footprints.
    libs=sorted({p['footprint'].split(':')[0] for p in parts}|{'MountingHole'})
    (ADAPTER/'fp-lib-table').write_text('(fp_lib_table (version 7)\n'+''.join(f'(lib (name "{n}")(type "KiCad")(uri "${{KICAD9_FOOTPRINT_DIR}}/{n}.pretty")(options "")(descr ""))\n' for n in libs)+')\n')
    with (ADAPTER/'bom.csv').open('w') as f:
        w=csv.writer(f);w.writerow(['Reference','Value','Footprint','Quantity'])
        for p in parts:w.writerow([p['ref'],p['value'],p['footprint'],1])
    print('Generated routed 32 x 28 mm power/UART adapter')
if __name__=='__main__':cached_mainboard();adapter()
