# 构建、重复配置与烧录

## 已验证的工具

- ARM：xPack GNU Arm Embedded GCC `10.3.1-2.3`，编译器 `10.3.1`。
- Wi-Fi：ESP-IDF `v5.4.2`，ESP32-C3 目标，官方 `riscv32-esp-elf` 工具链。
- KiCad：`9.0.2`，包括系统 Python 的 `pcbnew`。
- 机械：CadQuery `2.5.2` / OpenCascade `7.7.2`、ezdxf。
- 主机：g++、Python 3、Node.js；浏览器检查使用 Playwright 与系统 Chromium。

SDK 的可选 `dl.espressif.com` Python 约束/额外 wheel 服务被当前云策略拒绝。安装使用官方支持的主 PyPI 路径，按 `esp-python.lock` 固定实际验证过的依赖；没有关闭 TLS 或工具包 SHA 校验。对应的 Kconfig、component manager、esptool 和 Click 版本在锁文件中。

## 当前云环境

使用已有 `/workspace/DaisySeedProjects`，不新建 Git worktree。

```bash
cd /workspace/DaisySeedProjects/SeedFX
make test
make audio
IDF_TOOLS_PATH=/workspace/daisy-environment/esp-tools \
IDF_PYTHON_CHECK_CONSTRAINTS=no \
PATH=/workspace/daisy-environment/dev-python/bin:$PATH \
bash -c 'source /workspace/daisy-environment/esp-idf/export.sh && idf.py -C firmware/wireless build'
node tests/browser.cjs
```

`make test` 执行真实 NAM 推理、解析稳态对比、协议破损/恢复、参数/预设/旋钮/调音检查、JS 参数验证和模型结构拒绝测试。浏览器检查使用明确标注的遥测测试夹具，验证页面交互；不验证实物串口、Wi-Fi 或音频性能。

工具/依赖需要重建时：

```bash
bash tools/prepare_environment.sh
```

脚本不修改应用源文件或版本锁，只安装固定工具、初始化已有记录的子模块并生成构建产物。需已具备 make/g++、Node、KiCad9 与 pcbnew。其他机器可分别按以下步骤，设置实际工具位置。

## 音频目标

```bash
export GCC_PATH=/path/to/xpack-arm-none-eabi-gcc-10.3.1-2.3/bin
export PATH="$GCC_PATH:$PATH"
make -C ../Software/GuitarPedal/dependencies/libDaisy -j4
make -C firmware/audio -j4 GCC_PATH="$GCC_PATH"
```

产物在 `firmware/audio/build/seedfx.{elf,hex,bin}`。SRAM 链接脚本来自固定 libDaisy，在 D2 SRAM 单独放置 NAM 历史；QSPI 最后 64KiB 留给配置，银行位于 `0x7f0000` 和 `0x7f1000`。

## ESP32-C3 目标

```bash
source /path/to/esp-idf-v5.4.2/export.sh
cd firmware/wireless
# 新配置首次执行；已有 esp32c3 配置仅需 build。
idf.py set-target esp32c3
idf.py build
```

浏览器文件作为文本资源嵌入固件，运行时不从 CDN 或互联网加载依赖。`schema.json` 由 `tools/generate_schema.py` 从参数表生成；产品代码修改参数后需要生成、测试、重编译两端。

## 制作文件再生成

```bash
export XDG_CONFIG_HOME=/workspace/daisy-environment/kicad/config
export XDG_CACHE_HOME=/workspace/daisy-environment/kicad/cache
export XDG_DATA_HOME=/workspace/daisy-environment/kicad/data
export KICAD9_FOOTPRINT_DIR=/usr/share/kicad/footprints
export KICAD9_SYMBOL_DIR=/usr/share/kicad/symbols
/usr/bin/python3 tools/hardware.py
python3 tools/bom.py
bash tools/export_hardware.sh
/workspace/daisy-environment/dev-python/bin/python tools/mechanical.py
```

生成脚本仅写 SeedFX 工程/输出目录。自动布线数据在 `hardware.py` 中，机械尺寸在 `mechanical.py` 中；应将修改后的工程与生成参数一起保存。裸 PCB STEP 先导出，机械脚本随后加入装配并检查裸板/外壳干涉。

## 完整数字检查与打包

```bash
python3 tools/validate.py
python3 tools/package_release.py
```

校验不会连接实体硬件；重新生成报告与交付包，内容见 RELEASE.md。

## 实物烧录：尚未执行

烧录前断开外部 9V 与 UART 可拔插线束，分别用 USB 给待烧录主控供电。确认操作系统驱动和本机设备端口。

Daisy 烧录需要本机安装 `dfu-util` 并配置 USB 权限/驱动；当前云环境没有接入设备，也没有执行烧录。Daisy 首次在 ROM DFU 模式刷入固定 libDaisy 附带的 bootloader：

```bash
make -C firmware/audio program-boot
```

随后复位进入 Daisy bootloader，再刷应用：

```bash
make -C firmware/audio program-dfu-wait
```

应用地址为 `0x90040000`。不要用全片 QSPI 擦除覆盖预设银行；更新应用 BIN 保留配置区。ROM DFU 和应用 bootloader 的操作窗口/按键应按 libDaisy 原说明确认。

XIAO 通过 USB-C，首次必要时按住 BOOT 接入：

```bash
cd firmware/wireless
idf.py -p /dev/ttyACM0 flash monitor
```

或使用发布包 `bootloader.bin`（`0x0`）、`partition-table.bin`（`0x8000`）和 `seedfx_wireless.bin`（`0x10000`），以 ESP32-C3 / 4MB / DIO 配置烧录。实际串口可能不同；这条命令没有在实体设备执行。
