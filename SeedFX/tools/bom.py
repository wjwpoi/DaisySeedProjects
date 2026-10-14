#!/usr/bin/env python3
"""Produce variant-aware purchasing and assembly lists from the reference BOM."""
import csv, json, re, xml.etree.ElementTree as ET, zipfile
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
# Source spreadsheet uses ranges (R9-12); assembly houses need explicit refs.
def expand_designators(value):
    refs=set()
    for token in value.split(','):
        token=token.strip()
        match=re.fullmatch(r'([A-Za-z]+)(\d+)-(?:[A-Za-z]+)?(\d+)',token)
        if match:
            prefix,start,end=match.groups()
            if int(end)<int(start):raise ValueError('Reversed reference range: '+token)
            refs.update(prefix+str(number) for number in range(int(start),int(end)+1))
        elif token:refs.add(token)
    return refs
with (ROOT/'hardware/mainboard/assembly-bom.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['Comment','Designator','Footprint','LCSC_part','Populate_SeedFX','Quantity'])
    for row in rows[1:]:
        row=(row+['']*4)[:4];refs=expand_designators(row[1])
        # Reference rows can contain multiple parts; don't suppress needed parts.
        kept=sorted(refs-DNP)
        if kept:w.writerow([row[0],','.join(kept),row[2],row[3],'yes',len(kept)])
        omitted=sorted(refs&DNP)
        if omitted:w.writerow([row[0],','.join(omitted),row[2],row[3],'DNP',len(omitted)])
# These are planning allowances, not live prices or supplier quotations.
parts=[
 ['Daisy Seed Rev7',1,'Electro-Smith Daisy Seed, fitted 2 x 20 pin headers','https://electro-smith.com/products/daisy-seed',240,360,'Audio MCU, codec, SDRAM and QSPI included'],
 ['Main PCB',1,'125B Rev6-derived, FR4 1.6mm, 2 layers, 1oz copper','hardware/mainboard',20,60,'Allowance for one minimum bare-board order; quantity depends on supplier quote; extra boards are repair spares'],
 ['Main SMT components',1,'Use assembly-bom.csv and original LCSC selections','hardware/mainboard/assembly-bom.csv',65,120,'SMD material set only; excludes DC/audio sockets, Seed sockets and assembly service; omit MIDI DNP'],
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
 ['UART adapter PCB',1,'32x28mm bare FR4 board; 2 layers','hardware/adapter/seedfx-adapter.kicad_pcb',10,30,'Allowance for one minimum bare-board order; excludes components and assembly; extra boards are repair spares'],
 ['UART adapter small components',1,'One installed set: FB1, C1-C3, R1-R2 (6 SMD components)','hardware/adapter/bom.csv',3,8,'Parts only; excludes the three headers and soldering; minimum material packages are accounted separately'],
 ['UART adapter headers',3,'PCB headers: one 1x02, one 1x03, one 1x04; 2.54mm pitch','hardware/adapter/bom.csv',0.5,1.5,'PCB headers only; mating wire housings are included in the harness allowance'],
 ['DC power socket',1,'DC-005-2.5A-2.0; J1; reference LCSC C136744','hardware/mainboard/assembly-bom.csv',1,3,'Through-hole item; not included in the SMD component allowance'],
 ['External 9V supply',1,'9V regulated, center-negative, >=1A; suitable guitar-pedal supply','',30,80,'Power-supply purchase included here; actual ripple/noise will be tested on the single device'],
 ['Case and finish',1,'125B family 66x121.5x40mm black/olive powder-coated metal shell','mechanical/seedfx-case.step',45,90,'Check the blank dimensions before drilling; RF window machining separate'],
 ['Dielectric inserts and tray',1,'PETG RF insert + insulating tray + 2mm lid spacer','mechanical',20,60,'No metallic paint/foil over the radio window'],
 ['Panel printing',1,'UV print from panel-print.svg, full-size 66x121.5mm','mechanical/panel-print.svg',25,60,'One panel for the final device; user artwork can replace the artwork layer; confirm any one-time print setup fee'],
 ['Harness and small hardware',1,'26-28AWG wire, heatshrink, removable headers, M3x8 lid screws, ground lug','hardware/wiring.csv',10,25,'Short twisted UART/GND pairs; no bare wire near pot tabs'],
]
# Prices are engineering allowances in CNY, not fetched storefront quotations.
# Count the complete minimum bare-board orders in this one-device project.
channels={
 'Main PCB':'嘉立创打板','UART adapter PCB':'嘉立创打板',
 'Main SMT components':'嘉立创代采 / 立创商城',
 'UART adapter small components':'立创商城','UART adapter headers':'淘宝 / 立创商城',
 'Case and finish':'淘宝壳体店','Dielectric inserts and tray':'3D打印服务',
 'Panel printing':'UV印刷 / UV DTF服务',
 'Daisy Seed Rev7':'淘宝 / 京东 / 原厂代理','Wi-Fi MCU':'淘宝 / 京东 / 原厂代理',
 'OLED':'淘宝 / 京东','External 9V supply':'淘宝 / 京东'
}
labels={
 'Daisy Seed Rev7':'Daisy Seed 主控','Main PCB':'主板裸板最低订单','Main SMT components':'主板贴片元件一套',
 'Potentiometers':'B10K 电位器','Encoder':'按压编码器','OLED':'七针 SPI OLED',
 'Audio sockets':'Neutrik 音频插座','Footswitches':'瞬时常开脚踏','LEDs':'3mm LED',
 'Knobs':'旋钮帽','Seed sockets':'Seed 1×20 排母','Wi-Fi MCU':'XIAO ESP32-C3 含天线',
 'Wi-Fi buck supply':'Mini-360 降压模块','UART adapter PCB':'适配板裸板最低订单',
 'UART adapter small components':'适配板六颗贴片元件','UART adapter headers':'适配板排针',
 'DC power socket':'DC-005 电源座','External 9V supply':'外接 9V 电源',
 'Case and finish':'标准金属壳含基础涂装','Dielectric inserts and tray':'RF窗口 / 托盘 / 增高圈',
 'Panel printing':'单台封面印刷','Harness and small hardware':'线束 / 螺丝 / 绝缘小件'
}
units={'Main PCB':'单','UART adapter PCB':'单','Main SMT components':'套','UART adapter small components':'套','Harness and small hardware':'套','Dielectric inserts and tray':'套','Panel printing':'次'}
price_basis='工程预算预估，未获取实时店铺或服务商报价'
def subtotal(quantity,price):return round(quantity*price,2)
with (ROOT/'hardware/purchase-bom.csv').open('w') as f:
    w=csv.writer(f)
    w.writerow(['Item','Quantity','Selected_part_or_specification','Source','Unit_CNY_low_estimate','Unit_CNY_high_estimate','Notes','Item_ZH','Purchase_channel','Subtotal_CNY_low_estimate','Subtotal_CNY_high_estimate','Price_basis','Quantity_unit'])
    for item,quantity,spec,source,lo,hi,notes in parts:
        w.writerow([item,quantity,spec,source,lo,hi,notes,labels[item],channels.get(item,'淘宝 / 立创商城'),subtotal(quantity,lo),subtotal(quantity,hi),price_basis,units.get(item,'个')])
# Labor and order handling are separate from component materials.
services=[
 ('main_smt','主板 SMT / 代焊工费','嘉立创SMT / 本地代焊',1,'次',100,300,'仅工费与常见起步/换料额度；元件另列。若最低装配量高于1套，需要重算整个订单'),
 ('adapter_solder','适配板手工 / 代焊工费','本地焊接店',1,'块',10,30,'只焊接适配板六颗贴片件与排针'),
 ('throughhole_wiring','主板通孔件焊接与线束装配','本地焊接店',1,'台',50,150,'电位器、编码器、屏幕、插座、排母及线束；若已包含在整台代焊报价中，应合并删除重复工费'),
 ('case_machining','外壳开孔 / 开槽 / RF窗口加工','壳体加工店',1,'台',60,180,'加工现成壳体，含单件起步预算；不含从零CNC加工整壳'),
 ('shipping','多平台合计运费','各商家',1,'项目',20,60,'整项目运费额度；包邮订单可据实减少'),
 ('material_moq','小料最小包装 / 采购补差','元器件供应商',1,'项目',0,80,'装入设备数量之外的袋装小料预算；裸板最低订单已另计，不重复计入')
]
with (ROOT/'hardware/service-budget.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['Id','Item_ZH','Purchase_channel','Quantity','Unit','Unit_CNY_low_estimate','Unit_CNY_high_estimate','Subtotal_CNY_low_estimate','Subtotal_CNY_high_estimate','Notes','Price_basis'])
    for identifier,label,channel,quantity,unit,lo,hi,notes in services:
        w.writerow([identifier,label,channel,quantity,unit,lo,hi,subtotal(quantity,lo),subtotal(quantity,hi),notes,price_basis])
low=round(sum(subtotal(p[1],p[4]) for p in parts),2);high=round(sum(subtotal(p[1],p[5]) for p in parts),2)
service_low=sum(subtotal(s[3],s[5]) for s in services);service_high=sum(subtotal(s[3],s[6]) for s in services)
cost={
    'currency':'CNY','buildQuantity':1,
    'basis':'engineering planning allowances for one-device purchases including minimum bare-PCB orders; not live supplier quotations',
    'partsLow':low,'partsHigh':high,'servicesLow':service_low,'servicesHigh':service_high,
    'projectLow':round(low+service_low,2),'projectHigh':round(high+service_high,2),
    'oneOffPurchasing':{
        'chargeEntireMinimumOrdersToProject':True,'shareSetupFeesAcrossDevices':False,
        'extraBareBoardsUse':'repair spares for this same device',
        'totalProjectCost':'estimated; must be revised against actual supplier quantities and complete quotations',
        'assemblyAssumption':'one installed set; if SMT MOQ requires additional assembled boards, recalculate the full order',
        'avoidDuplicateCharges':'do not repurchase factory-supplied sockets; merge labor already included in a complete assembly quote'
    },
    'included':['one minimum bare-PCB order for each board','installed parts','external 9V supply','single-device assembly and machining allowances','panel printing','shipping and material-pack allowances'],
    'excluded':['soldering/measurement tools','development labor','repairs or replacement purchases during bring-up','extra populated boards beyond the one-set assembly assumption','entire enclosure machined from raw stock'],
    'costDown':['bring up and finish the same single device','use a standard enclosure with machining','compare full one-device SMT and local assembly quotes','combine tiny printed accessories in one order','omit the MIDI population']
}
(ROOT/'hardware/cost-plan.json').write_text(json.dumps(cost,ensure_ascii=False,indent=2)+'\n')
# Generate the human purchasing view from the exact same priced rows.
def money(value):return f'{value:g}'
lines=['# 单台采购预估价','','金额为人民币工程预算预估，未获取实时店铺价格或服务商报价。采购数量为一台成品所需；裸板按一次最低订单预留费用，余板作为备件。实际最低订单、装配数量和服务费需要询价确认。','','| 项目 | 渠道 | 数量 | 单价预估（元） | 小计预估（元） |','|---|---|---:|---:|---:|']
for item,quantity,spec,source,lo,hi,notes in parts:
    lines.append(f'| {labels[item]} | {channels.get(item,"淘宝 / 立创商城")} | {quantity}{units.get(item,'个')} | {money(lo)}–{money(hi)} | {money(subtotal(quantity,lo))}–{money(subtotal(quantity,hi))} |')
lines += ['',f'材料与裸板订单预算合计：**¥{money(low)}–{money(high)}**。','','| 服务项目 | 次数 | 小计预估（元） |','|---|---:|---:|']
for identifier,label,channel,quantity,unit,lo,hi,notes in services:lines.append(f'| {label} | {quantity}{unit} | {money(subtotal(quantity,lo))}–{money(subtotal(quantity,hi))} |')
lines += ['',f'服务与采购附加预算合计：**¥{money(service_low)}–{money(service_high)}**。',f'按委托焊接、现成壳体加工路线，单台整项目预算约 **¥{money(cost["projectLow"])}–{money(cost["projectHigh"])}**。这不是供应商已确认的成交报价或价格上限。','','- 主板贴片元件费与贴装工费分开，电源座、音频插座和 Seed 排母单独列价。如果工厂报价已包含它们，从网购清单中删除对应项。','- 适配板裸板、六颗贴片件、三个排针及焊接服务分开，避免打板和小料混计。','- 若代焊报价已经包含通孔件和线束装配，合并工费；自行焊接可去掉对应人工额度，工具采购不在本预算内。','- 主板材料按一个安装套数估算；SMT平台若要求多装板，需要按其整单数量重算费用。','- 小料最小包装补差与裸板最低订单分别核算；目前没有把起步费分摊给第二台机器。','- 单台封面印刷预算为一次UV印刷或UV DTF转印，不同时计算两种工艺。','- 外壳开孔、RF窗口、模块安装尺寸及所有待确认料号仍按 [采购前复核](PROCUREMENT_READINESS.md) 关闭后下单。','','明细：[材料 CSV](../hardware/purchase-bom.csv)、[服务 CSV](../hardware/service-budget.csv)、[机器预算](../hardware/cost-plan.json)。']
lines += ['', '相对原仓库的新增与沿用费用见 [成本来源对比](COST_COMPARISON.md)。当前分项外包预算不能作为原作者造价或商家报价。']
(ROOT/'docs/PURCHASING_PRICES.md').write_text('\n'.join(lines)+'\n')
print(f'One-device materials: CNY {low:g}–{high:g}; services: {service_low:g}–{service_high:g}; project estimate: {cost["projectLow"]:g}–{cost["projectHigh"]:g}')
