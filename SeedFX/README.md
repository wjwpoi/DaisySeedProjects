# SeedFX：NAM A2 综合效果器

本项目目标是制作 **一台自用综合效果器**；组装调试和最终使用均由同一台设备完成。基于本 fork 的 **完整可构建工程** 包含：音频固件、ESP32 无线固件、手机网页、两块 PCB 工程与制作文件、BOM、外壳 CAD、封面模板和验收说明。实机音质、端到端延迟、无线噪声、温升和器件装配公差在这同一台设备上验证；完成调试与验收后即为最终成品。

首版采用 Daisy Seed Rev7、XIAO ESP32-C3、六旋钮、一个按压编码器、两个脚踏、0.96 英寸 128×64 SPI OLED 和 125B 面板尺寸、含增高隔圈深度 42mm 的外壳。屏幕按参考主板的实际封装选型；模型名与调音读数采用大字体，详细编辑在手机页面完成。

音频链为：**输入 → 噪声门 → 压缩 → 过载 → NAM → 三段 EQ → 箱体 IR → 合唱 → 延迟 → 混响 → 输出**，另有调音器和数字旁通。音频始终在 Daisy 本机运行；Wi-Fi 用于参数、旋钮映射和预设控制。

## 采购状态

当前软件与工程文件可构建，采购前复核尚未全部关闭。完整器件装配、公差、供应商壳体与报价仍需确认；目前可先准备指定型号的两块主控采购，PCB 装配和最终壳体加工待复核后下单，见 [采购前复核](docs/PROCUREMENT_READINESS.md)。

## 从这里开始

| 内容 | 文件 |
|---|---|
| 打包内容与恢复工程 | [交付指南](docs/RELEASE.md)、[文件校验清单](releases/manifest.json) |
| 产品范围、架构和设计指标 | [产品设计](docs/PRODUCT.md) |
| 采购和生产 | [硬件与装配](docs/HARDWARE.md)、[采购 BOM](hardware/purchase-bom.csv)、[逐项预估价](docs/PURCHASING_PRICES.md)、[连接表](hardware/wiring.csv) |
| PCB 源文件 | [主板](hardware/mainboard/DaisySeedPedal125b.kicad_pcb)、[无线适配板](hardware/adapter/seedfx-adapter.kicad_pcb) |
| 原理图源文件 | [主板](hardware/mainboard/DaisySeedPedal125b.kicad_sch)、[无线适配板](hardware/adapter/seedfx-adapter.kicad_sch) |
| 外壳与封面 | [装配 STEP](mechanical/seedfx-enclosure.step)、[开孔 DXF](mechanical/front-drill.dxf)、[印刷版](mechanical/panel-print.svg)、[自定义封面模板](mechanical/panel-artwork-template.svg) |
| 编译与烧录 | [构建指南](docs/BUILD.md) |
| 串口、HTTP 与存储接口 | [协议说明](docs/PROTOCOL.md)、[GPIO 对照](hardware/io-map.csv) |
| 操作 | [用户手册](docs/USER_MANUAL.md) |
| NAM 导入边界 | [模型指南](docs/MODELS.md) |
| 已执行检查与单台成品验收 | [验证说明](docs/VALIDATION.md)、[机器报告](docs/validation.json) |
| 默认封面外观 | [面板预览](docs/images/panel-preview.png)，图中的圆圈为硬件避让位置 |
| 手机页面外观 | [手机预览](docs/images/web-mobile.png)、[桌面预览](docs/images/web-desktop.png)；预览明确使用模拟状态，CPU/音频数据未实测 |

## 已实现的关键行为

- 内置五个参考模型、两个 1024 点箱体 IR、16 个可保存预设和六旋钮自定义映射。
- 旋钮支持软接管；缓慢旋转也能累计生效。启动前一秒只等待 ADC 稳定，避免覆盖保存的参数。
- 连续参数平滑；模型、IR 和效果开关通过淡出、主循环配置、淡入完成切换。音频中断不做文件操作、网络请求、闪存擦写或动态分配。
- 新增解析稳态初始化：与原 133 块预热在五个模型、15,360 个后续样本上对比，最大绝对误差约 `8.64e-7`。实际换模耗时须在 Daisy 上测量。
- 预设双区 CRC 日志，保留上一个有效银行；保存确认在写入并读回验证后才返回。重启恢复上次显式保存的槽位。
- UART 使用 COBS、CRC32、范围检查、ACK 和启动会话握手；无线失联时网页禁用控制，本机旋钮和音频继续工作。

## 成本与边界

相对原仓库的沿用成本、新增无线与制作费用拆分见 [成本来源对比](docs/COST_COMPARISON.md)。当前未证明这条单台 DIY 路线比市售成品便宜。

当前制作方向改为自行焊接、淘宝通用铝壳或 PETG 打印壳、简单贴纸封面。用户需要从零购置工具：全手焊加通用铝壳、外包开孔，整项目预留 **¥833.9–1801.5**；全手焊加打印壳预留 **¥778.9–1636.5**。两者均含 ¥180–350 的基础工具与练习耗材、外接 9V 电源、运费及小料采购补差，详见 [手焊与通用外壳预算](docs/DIY_BUILD_OPTIONS.md)。当前外壳 CAD 仍是尺寸参考，实际铝壳内腔和打印壳承重设计待确认。

此前委托焊接、壳体加工及 UV 印刷路线 **¥848.9–2006.5** 保留为 [分项对照表](docs/PURCHASING_PRICES.md)，不能与新路线费用叠加。所有价格仍为工程预估，未经实时询价；全部起步费和最低采购量由这一个项目承担，多余裸板作为维修备件。默认选用上电标定并锁定到 5V 的通用 Mini-360 电源模块；希望使用固定输出品牌件时可换 Pololu D24V10F5。

首版为单声道、外接 9V、Wi-Fi 网页控制。采用参考主板的 TRS 接口时，正常使用 mono TS 线，DSP 模式下 Ring 输出为零信号。没有耳机驱动、电池、蓝牙控制、无线音频或 OTA 功能。

原仓库应用与原始硬件文件保持作为参考；本项目的工作集中在 `SeedFX/`。源代码与 KiCad 库的出处见 [第三方说明](THIRD_PARTY.md)。
