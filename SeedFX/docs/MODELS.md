# NAM 模型边界与导入

当前运行时是参考仓库的固定 A2 WaveNet：3 通道、23 层、1871 权重、48 点块、48kHz、LeakyReLU 斜率 0.01、16 点 head。文件名中的 “Lite” 不是兼容证明。Standard/LSTM、其他通道数、卷积结构或采样率不能直接导入。

Kernel schedule 为 `6×14, 15×2, 6×7`。Dilation schedule 为 `1,3,7,17,41,101,239,1,3,7,17,41,101,239,1,13,1,3,7,17,41,101,239`。主控一次运行一个模型，NAM 输出为单声道。

## 五个内置参考

| 名称 | 参考文件中的来源信息 | 箱体处理 |
|---|---|---|
| BE-100 | `[AMP] BE100DLX-BE TEST - DI.nam` | DI，外部箱体 IR 起点 |
| JCM800 | `[AMP] JCM800-2203-MODIFIED-HI The Sound - DI.nam` | DI，外部箱体 IR 起点 |
| Ampeg | SVT Classic，Gain10，Ultra Lo/Hi，MD421 | 名称带麦克风信息，预设关闭额外 IR |
| Mesa | Dual Rectifier 2025，Rhythm #3 | 捕获链需按原模型资料确认 |
| 1959BJA | `Marshall 1959BJA SUPER BOWL SETTINGS.nam` | 捕获链需按原模型资料确认 |

各模型 outputGain 延用参考表的手动响度系数，尚未重新实测。NAM 参数 Input 改的是送入模型的信号增益；Bass/Mid/Treble 是模型后的 EQ，不是被捕获实体音箱的原始旋钮。

## 导出流程

从仓库 `SeedFX/` 运行：

```bash
python3 tools/model_tool.py path/to/capture.nam --output user-model.hpp
```

工具要求明确的 48kHz 元数据、正确的层/通道/核/膨胀结构单层线性 3→1/16 点有偏置 head，以及恰好 1871 个有限 FP32 数值。工具采用显式 `config.head` 字段（`in_channels:3,out_channels:1,kernel_size:16,bias:true`）；其他导出 schema 必须先核对并转换元数据，缺少 head 信息会拒绝。SlimmableContainer 必须具有唯一 `max_value=0.5` 子模型，采样率可继承容器但不能冲突。它拒绝只靠权重数量判断兼容，也拒绝 NaN、错误采样率和错结构。导出目标必须不存在，避免覆盖已有文件。

当前五个模型编译进参考 `Software/GuitarPedal/Effect-Modules/Nam/model_data_nam_a2.h`，本产品从该表读取。需要更换模型时，将验证后的数组登记到模型表，并同步参数 MODEL 上限、网页 schema 和显示名，再编译与运行所有检查。首版没有无线 `.nam` 上传或运行时通用 JSON 模型加载器。

## 快速初始化的依据

该固定网络由因果 FIR、前馈残差和激活构成，没有递归状态反馈。零输入下每层入值、激活和 head 都是常量，可直接算出并填充延迟历史。`warm_start.hpp` 实现该初始化，避免重新模拟 133 块零输入。

`warm_test.cpp` 以原始完整预热作为数值参照，对五模型逐一比较后续混合正弦/噪声输入的 15,360 个样本。当前最大绝对误差约 `8.64e-7`。这个验证不说明其他模型结构兼容，也不是 MCU 耗时或音质测试。
