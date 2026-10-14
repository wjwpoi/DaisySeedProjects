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
# Keep the outsourced reference bill for comparison. Self-build scenarios charge
# the tools this user needs to buy, with no assumed second device or free labor.
tool_rows=[
 ('solder_station','温控烙铁 / T12 焊台（含电源、烙铁头、支架）',80,150,'基础焊接；选可靠接地的设备，热风与回流设备不作为必购'),
 ('multimeter','万用表',35,70,'直流电压、通断、电阻测量；不能替代纹波/延迟验收仪器'),
 ('consumables','焊锡 / 助焊剂 / 吸锡带 / 清洗耗材',35,70,'手焊耗材；不另计锡膏和钢网'),
 ('hand_tools','镊子 / 斜口钳等小工具',20,40,'不含电钻、台钳、锉刀等壳体加工工具'),
 ('practice_board','0805 / SOIC 焊接练习板与小料',10,20,'正式主板前练习；不算第二台效果器')
]
with (ROOT/'hardware/tool-budget.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['Id','Item_ZH','CNY_low_estimate','CNY_high_estimate','Notes','Price_basis'])
    for identifier,label,lo,hi,notes in tool_rows:w.writerow([identifier,label,lo,hi,notes,price_basis])
tool_low=sum(t[2] for t in tool_rows);tool_high=sum(t[3] for t in tool_rows)
common_parts=[p for p in parts if p[0] not in {'Case and finish','Dielectric inserts and tray','Panel printing'}]
common_low=round(sum(subtotal(p[1],p[4]) for p in common_parts),2)
common_high=round(sum(subtotal(p[1],p[5]) for p in common_parts),2)
common_services=[s for s in services if s[0] in {'shipping','material_moq'}]
scenarios={}
variant_items={
 'self_stock_aluminum':('全手焊 + 通用铝壳 + 外包开孔',[
     ('stock_case','淘宝通用未涂装铝壳',25,50,'候选125B外形；完整主板装配与内腔需复核，必要时选更深壳体'),
     ('insulation','绝缘托盘 / 增高圈 / RF附件',20,60,'保留安装与天线所需预算；尚不能直接删掉已有隔圈'),
     ('panel_decal','封面贴纸 / 简单转印',10,25,'替代UV直印；用户图案后续适配'),
     ('case_machining','壳体开孔、OLED窗口、必要的天线开槽',60,180,'沿用原加工额度；未取得简化加工的真实报价')]),
 'self_printed':('全手焊 + PETG打印壳',[
     ('printed_case','PETG壳体、盖板及安装附件打印',40,100,'按一次合并打印预留；需要重做脚踏受力和打印结构，现有STL未作承重验证'),
     ('reinforcement','脚踏金属垫片 / 加强板与紧固件',10,25,'待结构设计确认；不把普通塑料壁直接视作可靠脚踏支撑'),
     ('panel_decal','封面贴纸 / 简单转印',10,25,'非金属表面图案；无需铝壳的RF开槽')])
}
scenario_rows=[]
for identifier,(label,extras) in variant_items.items():
    line_items=[dict(id=p[0],label=labels[p[0]],category='common_material',low=subtotal(p[1],p[4]),high=subtotal(p[1],p[5]),notes=p[6]) for p in common_parts]
    line_items += [dict(id=s[0],label=s[1],category='procurement',low=subtotal(s[3],s[5]),high=subtotal(s[3],s[6]),notes=s[7]) for s in common_services]
    line_items += [dict(id=i,label=l,category='enclosure',low=lo,high=hi,notes=n) for i,l,lo,hi,n in extras]
    without_low=round(sum(i['low'] for i in line_items),2);without_high=round(sum(i['high'] for i in line_items),2)
    line_items += [dict(id=i,label=l,category='tools',low=lo,high=hi,notes=n) for i,l,lo,hi,n in tool_rows]
    scenarios[identifier]=dict(label=label,toolsAlreadyOwned=False,withoutToolsLow=without_low,withoutToolsHigh=without_high,toolsLow=tool_low,toolsHigh=tool_high,projectLow=round(without_low+tool_low,2),projectHigh=round(without_high+tool_high,2),lineItems=line_items,physicalValidation=False)
    for item in line_items:scenario_rows.append([identifier,label,item['id'],item['label'],item['category'],item['low'],item['high'],item['notes'],price_basis])
with (ROOT/'hardware/diy-budget.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['Scenario','Scenario_ZH','Id','Item_ZH','Category','CNY_low_estimate','CNY_high_estimate','Notes','Price_basis']);w.writerows(scenario_rows)
cost.update(referenceScenario='outsourced_original',preferredScenario='self_stock_aluminum',
            referenceScenarioFields=['partsLow','partsHigh','servicesLow','servicesHigh','projectLow','projectHigh','included','excluded'],
            assemblyPreference='user self-soldering; tools must be purchased',
            enclosureCandidates=['generic aluminum enclosure with machining','reinforced PETG printed enclosure'],
            scenarioStatus='planning alternatives; no supplier quote or structural/complete-fit validation',
            selfBuildScenarios=scenarios)
(ROOT/'hardware/cost-plan.json').write_text(json.dumps(cost,ensure_ascii=False,indent=2)+'\n')
# Generate the human purchasing view from the exact same priced rows.
def money(value):return f'{value:g}'
lines=['# 单台采购预估价','','金额为人民币工程预算预估，未获取实时店铺价格或服务商报价。采购数量为一台成品所需；裸板按一次最低订单预留费用，余板作为备件。实际最低订单、装配数量和服务费需要询价确认。','','**当前制作方向改为自行焊接、通用铝壳或3D打印壳，并计入从零购置工具的费用。** 下表保留此前委托制作的对照报价口径；不应把全表与新路线叠加购买。新路线见 [手焊与通用外壳预算](DIY_BUILD_OPTIONS.md)。','','| 项目 | 渠道 | 数量 | 单价预估（元） | 小计预估（元） |','|---|---|---:|---:|---:|']
for item,quantity,spec,source,lo,hi,notes in parts:
    lines.append(f'| {labels[item]} | {channels.get(item,"淘宝 / 立创商城")} | {quantity}{units.get(item,'个')} | {money(lo)}–{money(hi)} | {money(subtotal(quantity,lo))}–{money(subtotal(quantity,hi))} |')
lines += ['',f'材料与裸板订单预算合计：**¥{money(low)}–{money(high)}**。','','| 服务项目 | 次数 | 小计预估（元） |','|---|---:|---:|']
for identifier,label,channel,quantity,unit,lo,hi,notes in services:lines.append(f'| {label} | {quantity}{unit} | {money(subtotal(quantity,lo))}–{money(subtotal(quantity,hi))} |')
lines += ['',f'服务与采购附加预算合计：**¥{money(service_low)}–{money(service_high)}**。',f'按委托焊接、现成壳体加工路线，单台整项目预算约 **¥{money(cost["projectLow"])}–{money(cost["projectHigh"])}**。这不是供应商已确认的成交报价或价格上限。','','- 主板贴片元件费与贴装工费分开，电源座、音频插座和 Seed 排母单独列价。如果工厂报价已包含它们，从网购清单中删除对应项。','- 适配板裸板、六颗贴片件、三个排针及焊接服务分开，避免打板和小料混计。','- 若代焊报价已经包含通孔件和线束装配，合并工费；自行焊接可去掉对应人工额度，工具采购不在本预算内。','- 主板材料按一个安装套数估算；SMT平台若要求多装板，需要按其整单数量重算费用。','- 小料最小包装补差与裸板最低订单分别核算；目前没有把起步费分摊给第二台机器。','- 单台封面印刷预算为一次UV印刷或UV DTF转印，不同时计算两种工艺。','- 外壳开孔、RF窗口、模块安装尺寸及所有待确认料号仍按 [采购前复核](PROCUREMENT_READINESS.md) 关闭后下单。','','明细：[材料 CSV](../hardware/purchase-bom.csv)、[服务 CSV](../hardware/service-budget.csv)、[机器预算](../hardware/cost-plan.json)。']
lines += ['', '相对原仓库的新增与沿用费用见 [成本来源对比](COST_COMPARISON.md)。当前分项外包预算不能作为原作者造价或商家报价。']
(ROOT/'docs/PURCHASING_PRICES.md').write_text('\n'.join(lines)+'\n')
lines=['# 自己焊接与通用外壳方案','','用户没有焊接工具，本预算将工具完整计入这一台项目。金额是工程预留，未经实时询价；保留同一套电子电路、无线、屏幕与六旋钮，不靠删除功能降低账面金额。','','## 当前选择方向','','优先评估自行焊接、淘宝通用铝壳、简单贴纸封面；PETG打印壳作为候选。铝壳适合长期踩踏，但必须核实实际内腔、装配深度与天线位置。打印壳需要完成承重结构设计后才能制作。这里只修改制作路线与预算，没有宣称外壳结构已经重新验证。','','## 两条路线的完整预算','','| 路线 | 不含新工具（元） | 新工具与耗材（元） | 从零购置合计（元） |','|---|---:|---:|---:|']
for scenario in scenarios.values():lines.append(f'| {scenario["label"]} | {money(scenario["withoutToolsLow"])}–{money(scenario["withoutToolsHigh"])} | {money(tool_low)}–{money(tool_high)} | **{money(scenario["projectLow"])}–{money(scenario["projectHigh"])}** |')
lines += ['',f'两条路线共用的电子物料、裸板订单、线束与外接9V电源：**¥{money(common_low)}–{money(common_high)}**。运费另预留¥20–60、小料最低包装补差¥0–80；这些费用已计入上表。若已有合格9V电源，按原表采购额度可减少¥30–80。','','| 替换项目 | 通用铝壳（元） | PETG打印壳（元） |','|---|---:|---:|---:|','| 壳体 | 25–50 | 40–100（含盖板、打印安装附件） |','| 绝缘 / 安装附件或脚踏金属加强件 | 20–60 | 10–25 |','| 封面贴纸 / 简单转印 | 10–25 | 10–25 |','| 开孔、屏幕窗口、必要的天线开槽 | 60–180 | 已在打印壳费用中预留 |','','铝壳预算沿用原加工额度，没有将未询价的低价打孔当作确定成交价。只有圆孔的报价无法覆盖OLED矩形窗口、当前DC-005开口与天线处理。若自行加工，可删去加工工费，但需要另算电钻、钻头、锉刀、夹具；从零购置暂预留¥130–280（工具额外购置，尚未计入上表），不能直接把开孔费全部当成净节省。','','## 焊接工具逐项预留','','| 项目 | 预估（元） |','|---|---:|']
for identifier,label,lo,hi,notes in tool_rows:lines.append(f'| {label} | {money(lo)}–{money(hi)} |')
lines += ['',f'工具与练习耗材合计：**¥{money(tool_low)}–{money(tool_high)}**。不默认购买热风枪、回流炉、钢网或示波器。示波器等电源/音频验收仪器需要借用或另行核算；返工、重复打印、替换元件与个人工时也未计入。','','## 能否全手焊','','核对当前PCB封装与装配BOM，主板实际安装的贴片件54颗：43颗0805阻容、3颗SOD二极管、2个贴片继电器、2个SOIC运放、2个SOP-4光继电器、1个SOT-23晶体管、1个SOT-223稳压器。适配板还有6颗0805/1206贴片件和3个排针。','', '这些外围封装具备烙铁手焊条件，没有需要用户自己焊接的BGA/QFN主控；Daisy、XIAO、OLED与降压模块整块购买。SOIC焊桥、器件方向、继电器焊点和小件极性仍需逐个检查，不能把模块插接等同于全部免焊。','','取消主板SMT、适配板焊接、通孔与线束代焊，可去掉原预算人工 **¥160–480**。购置工具增加¥180–350，首台的净节省取决于实际工费和工具成交价，不保证全手焊必然比局部代焊便宜。若练习后难以稳定焊好SOIC，可只询价主板贴片，自己焊外围；从对应路线预算另加实际贴片报价，不能套用包含通孔装配的旧整机工费。','','建议顺序：练习0805/SOIC → 焊主板和适配板低矮贴片件 → 焊排母与通孔件 → 断电检查短路及电位器金属脚绝缘 → 不插主控先检查供电，标定Mini-360到5V → 接入模块烧录与裸机验收 → 按实际装配位置加工/打印壳体。接线、供电和USB限制继续按 [硬件说明](HARDWARE.md) 执行。','','## 外壳与外观','','- **铝壳**：普通现成壳体与贴纸即可做完整封面，不默认烤漆、UV直印或整壳CNC。现有PCB安装位置决定孔距；通用壳体的尺寸、螺柱和壁厚必须核实，增高隔圈预算仍保留。','- **无线**：金属壳需要非金属天线窗口或在壳外保护放置天线，方式待复核；不能将天线完全封在金属盒里并假定连接正常。','- **PETG打印壳**：开口与安装柱可一体打印，省去金属壳加工；脚踏附近需要肋、厚壁或金属加强板，并使踩踏载荷传到壳体/底板，避免靠PCB承重。打印层方向、紧固方式与实际受力仍需验证。','- **屏蔽**：金属壳按原设计接地；塑料壳可评估局部导电屏蔽并接地，但需要给天线留非导电区域，新增屏蔽材料据实计价。','- **现有STL**：是当前尺寸与孔位的参考模型，没有做整壳打印承重、耐热和长期踩踏验证，不能作为最终打印下单依据。','','采购冻结仍见 [采购前复核](PROCUREMENT_READINESS.md)。新路线逐项费用见 [DIY明细](../hardware/diy-budget.csv)、[工具清单](../hardware/tool-budget.csv)、[机器预算](../hardware/cost-plan.json)；原分项委托预算保留在 [采购预估价](PURCHASING_PRICES.md)。']
(ROOT/'docs/DIY_BUILD_OPTIONS.md').write_text('\n'.join(lines)+'\n')
print(f'One-device materials: CNY {low:g}–{high:g}; services: {service_low:g}–{service_high:g}; project estimate: {cost["projectLow"]:g}–{cost["projectHigh"]:g}')
for scenario in scenarios.values():print(f'{scenario["label"]} including new tools: CNY {scenario["projectLow"]:g}–{scenario["projectHigh"]:g}')
