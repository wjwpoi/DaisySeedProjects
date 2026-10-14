#!/usr/bin/env python3
"""Produce variant-aware purchasing and assembly lists from the reference BOM."""
import csv, json, xml.etree.ElementTree as ET, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
XLSX=ROOT.parent/'Hardware/GuitarPedal125b/pcb/BOM_JLCSMT_DaisySeedGuitarPedal125b.xlsx'
DNP={'J3','J5','U4','D2','R17','R18','R19','R20','C17'}
ns={'s':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
with zipfile.ZipFile(XLSX) as z:
    strings=[''.join(t.text or '' for t in x.findall('.//s:t',ns)) for x in ET.fromstring(z.read('xl/sharedStrings.xml')).findall('s:si',ns)]
    rows=[]
    for row in ET.fromstring(z.read('xl/worksheets/sheet1.xml')).findall('.//s:row',ns):
        fields=[]
        for c in row.findall('s:c',ns):
            value=c.findtext('s:v',default='',namespaces=ns)
            if c.get('t')=='s':value=strings[int(value)]
            elif c.get('t')=='inlineStr':value=''.join(t.text or '' for t in c.findall('.//s:t',ns))
            fields.append(value)
        if fields:rows.append(fields)
with (ROOT/'hardware/mainboard/assembly-bom.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['Comment','Designator','Footprint','LCSC_part','Populate_SeedFX'])
    for row in rows[1:]:
        row=(row+['']*4)[:4];refs={r.strip() for r in row[1].split(',')};populate=not refs<=DNP
        # Reference rows can contain multiple parts; don't suppress needed parts.
        kept=sorted(refs-DNP)
        if kept:w.writerow([row[0],','.join(kept),row[2],row[3],'yes'])
        omitted=sorted(refs&DNP)
        if omitted:w.writerow([row[0],','.join(omitted),row[2],row[3],'DNP'])
# These are planning allowances, not live prices or supplier quotations.
parts=[
 ['Daisy Seed Rev7',1,'Electro-Smith Daisy Seed, fitted 2 x 20 pin headers','https://electro-smith.com/products/daisy-seed',240,360,'Audio MCU, codec, SDRAM and QSPI included'],
 ['Main PCB',1,'125B Rev6-derived, FR4 1.6mm, 2 layers, 1oz copper','hardware/mainboard',20,60,'Per board when a 5-board order is shared; setup/assembly separate'],
 ['Main SMT components',1,'Use assembly-bom.csv and original LCSC selections','hardware/mainboard/assembly-bom.csv',65,120,'DNP MIDI circuit for this UART variant'],
 ['Potentiometers',6,'Alpha RD901F-40-15R1-B10K','https://www.taydaelectronics.com/10k-ohm-linear-taper-potentiometer-round-shaft-pcb-9mm.html',4,8,'B10K linear; round shaft; follow electrical isolation note'],
 ['Encoder',1,'Bourns PEC11R-4220K-S24','https://www.mouser.com/ProductDetail/652-PEC11R-4220K-S24',10,25,'Momentary push switch; 24 detents; 6mm shaft'],
 ['OLED',1,'HiLetgo B01MQPQF24 reference form factor; 0.96 inch 128x64, 7-pin SPI, 3.3V SSD1306','https://www.amazon.com/dp/B01MQPQF24',20,45,'27.5x27.75mm PCB, 22x11mm active area; GND,VCC,SCK,MOSI,RESET,DC,CS; match footprint'],
 ['Audio sockets',2,'Neutrik NMJ6HCD2','https://www.neutrik.com/en/product/nmj6hcd2',10,20,'Fits the existing mainboard footprint; use mono TS patch leads'],
 ['Footswitches',2,'SPST normally-open momentary, M12 thread, 2 solder lugs','',8,18,'12.2mm panel bore; do not use latching 3PDT switches'],
 ['LEDs',2,'3mm diffused green / amber, through-hole','',.2,1,'Flush 3.2mm holes; isolated leads; no metal holders needed'],
 ['Knobs',7,'6 x 12.5mm OD for 6.35mm round shafts; 1 x 12.5mm OD for 6mm encoder','',2,5,'Set-screw type; select correct bore sizes'],
 ['Seed sockets',2,'1x20 female, 2.54mm pitch, low profile','',2,5,'Match the included footprint; no tall stacking sockets'],
 ['Wi-Fi MCU',1,'Seeed Studio XIAO ESP32-C3 with supplied IPEX antenna','https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html',25,50,'USB-C, 4MB flash; D6 GPIO21 TX / D7 GPIO20 RX'],
 ['Wi-Fi buck supply',1,'Mini-360 MP2307DN module, adjusted and locked to 5V; >=0.5A tested continuous output','',5,15,'18x14mm envelope; protected +9V input; commission before attaching XIAO. Fixed 5V Pololu D24V10F5 #2831 is the drop-in premium option'],
 ['UART adapter',1,'32x28mm FR4 board, see adapter BOM','hardware/adapter/bom.csv',10,25,'Shared PCB order allowance; 3.3V logic, 1k series resistors and supply filtering'],
 ['Case and finish',1,'125B family 66x121.5x40mm black/olive powder-coated metal shell','mechanical/seedfx-case.step',45,90,'Check the blank dimensions before drilling; RF window machining separate'],
 ['Dielectric inserts and tray',1,'PETG/ABS RF insert + insulating tray + 2mm lid spacer + M2 hardware','mechanical',10,30,'No metallic paint/foil over the radio window'],
 ['Panel printing',1,'UV print from panel-print.svg, full-size 66x121.5mm','mechanical/panel-print.svg',25,60,'Small-batch allowance; user artwork can replace the artwork layer'],
 ['Harness and small hardware',1,'26-28AWG wire, heatshrink, removable headers, M3x8 lid screws, ground lug','hardware/wiring.csv',10,25,'Short twisted UART/GND pairs; no bare wire near pot tabs'],
]
with (ROOT/'hardware/purchase-bom.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['Item','Quantity','Selected_part_or_specification','Source','Unit_CNY_low_estimate','Unit_CNY_high_estimate','Notes']);w.writerows(parts)
low=sum(p[1]*p[4] for p in parts);high=sum(p[1]*p[5] for p in parts)
(ROOT/'hardware/cost-plan.json').write_text(json.dumps({'currency':'CNY','basis':'engineering planning estimate, not current quotations','partsLow':low,'partsHigh':high,'excluded':['shipping','PCB/SMT setup not shared across units','manual assembly','RF window machining','repeated prototypes','external 9V adapter','development labor'],'costDown':['share five-board setup','retain standard enclosure and generic knobs','fixed 5V >=1A buck substitute only after voltage/ripple/transient validation','omit the MIDI population']},ensure_ascii=False,indent=2)+'\n')
print(f'Planning materials allowance: CNY {low:.0f}–{high:.0f}, exclusions recorded')
