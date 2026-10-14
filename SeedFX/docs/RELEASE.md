# 工程交付与重新生成

发布包为 `SeedFX-v1-engineering.zip`，旁边的 `.sha256` 用于校验压缩包。`releases/manifest.json` 逐项记录源文件、固件、制造文件和测试日志的大小/SHA256；也记录原始 NAM 运行时、权重和 IR 摘录的校验值。

- `releases/firmware/daisy/`：应用 BIN/HEX/ELF、固定 v6.4 bootloader。首次 bootloader 刷内部 Flash `0x08000000`；应用刷 `0x90040000`。
- `releases/firmware/esp32c3/`：bootloader、分区表、应用 BIN/ELF；对应地址为 `0x0`、`0x8000`、`0x10000`。
- `releases/mainboard-pcb.zip` / `adapter-pcb.zip`：Gerber，包括焊膏层，以及 Excellon 钻孔/钻孔图。
- `hardware/manufacturing/`：原理图 PDF、装配图和贴片位置；主板装配 BOM 明确标记 MIDI DNP，不能把所有贴片坐标都当作需装器件。
- `mechanical/`：壳体、盖板、2mm 增高隔圈、无线窗口、托盘及总装 STEP/STL；开孔与印刷文件；模块位置和包络检查数据。
- `releases/licenses/`：libDaisy 和 ESP-IDF 源码提供的许可/NOTICE 文件合集；模型捕获来源单独见 MODELS.md / THIRD_PARTY.md。
- `releases/verification-logs/`：实际运行的构建、浏览器、制造、CAD 与 AddressSanitizer/UndefinedBehaviorSanitizer 日志。

本包用于制作并调试唯一一台最终成品。实体制造、刷机、听音、延迟/RF/供电/温升和实际器件公差验收尚未执行，详见 VALIDATION.md。用户封面可以替换 `panel-artwork-template.svg` 的 `custom-artwork` 图层。

## 从压缩包恢复工程

源代码读取 fork 中固定文件与子模块；压缩包不包含完整 ESP-IDF/编译器。先克隆基础仓库并固定参考版本，再把压缩包的 `SeedFX/` 目录放入仓库根目录。`reference/` 是四个源文件的审阅摘录，无需覆盖基础仓库。

```bash
git clone https://github.com/wjwpoi/DaisySeedProjects.git
cd DaisySeedProjects
git checkout 8e9f468ab0d35b87fafa33b9da2f017a0e6a865b
git submodule update --init --recursive
# 将发布包 SeedFX/ 解压到当前仓库根目录。
cd SeedFX
bash tools/prepare_environment.sh
```

该云配置使用固定 `/workspace` 工具位置，其他机器请按照 BUILD.md 设置相应路径。源码、模型、几何或 UI 改动后，先执行检查再打包：

```bash
python3 tools/validate.py
python3 tools/package_release.py --output /workspace/SeedFX-v1-engineering.zip
```

确认 manifest 校验值后，将 `mainboard-pcb.zip` / `adapter-pcb.zip` 交给 PCB 厂，结合装配 BOM、贴片位置和装配图下单；壳体厂用实际采购壳体核对开孔 DXF/CSV 与 STEP。默认图案用于预览与该单台设备的 UV 印刷；收到定制封面后替换图层并重新导出印刷文件。
