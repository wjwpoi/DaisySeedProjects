# 来源与许可记录

- 基础 fork：`wjwpoi/DaisySeedProjects`，参考 HEAD `8e9f468ab0d35b87fafa33b9da2f017a0e6a865b`。参考项目 MIT，Copyright 2023 Keith Shepherd；根目录 LICENSE 保留。
- 主板：参考 `Hardware/GuitarPedal125b/pcb` Rev6。铜层/焊盘保留；封装与符号从该工程缓存打包；丝印整理至 Fabrication/Dwgs 层。原理图标题中的原作者信息保留。
- libDaisy：固定子模块 `24be4afea00d71988019fe8dd9fd9fc8fc5bf72c`；许可与来源保留在子模块。本音频目标仅链接 libDaisy，未使用 Dattorro 或 DaisySP-LGPL 效果代码。
- NAM 固定运行时、五个权重数组和两个 IR：读取参考 fork 中对应文件。运行时文件注明 nadavb 的 A2 实现来源。权重捕获名称记录在模型指南；代码仓库许可不替代捕获者对模型文件的授权说明。
- CMSIS-DSP：libDaisy 固定子模块 `3a04f817a4380c00ff51f9bcdc6c4e4bb7a18b80`（v1.14.4），箱体 FIR 链接其 `arm_fir_f32` / `arm_fir_init_f32`，遵循 Apache-2.0；头文件和源文件原声明保留。
- ESP-IDF：Espressif v5.4.2，按其仓库许可使用。Python 工具依赖以实际构建后的 `tools/esp-python.lock` 锁定。
- XIAO 引脚核对：Seeed-Studio/OSHW-XIAO-Series，检查时 HEAD `810c3313d74dcf2c529c27028efa49a300e1334a`，官方 `pinout/XIAO_ESP32-C3_front_pinout.png`。本包没有复制其完整项目集合。
- 适配板使用系统 KiCad 标准器件与封装库；工程中保留库名称。CadQuery/ezdxf 用于生成机械文件。

新建控制协议、控制逻辑、效果链、手机页面及工程生成脚本沿用项目 MIT 许可。批量分发模型和成品前应根据捕获来源补齐所选模型的许可记录。
