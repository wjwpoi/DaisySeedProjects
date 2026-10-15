# ToneDock

Windows / macOS 的 NAM 模型加载与综合软件效果器。提供独立运行版、VST3，及 macOS AU。硬件方案暂缓，新软件使用原生 C++ / JUCE 和官方 NeuralAmpModelerCore，不再限制为硬件版的固定 A2 Lite 拓扑。

当前为首版工程。Linux 云端用于编译与自动测试；Windows/macOS 原生构建、DAW 扫描、声卡实测与真实 TONE3000 账号联调分别记录，不用云端编译替代平台验收。见 [验证状态](docs/VALIDATION.md)。

## 功能

- `.nam`、箱体 IR `.wav`、ZIP 模型包及文件夹导入；支持从文件管理器拖入。
- 内容 SHA-256 去重；导入时读取元数据，自动分为 Amplifier / Pedal / Cabinet IR / Unsorted，支持自定义分类、标签、收藏与本地搜索。类别推断来自文件元数据和名称，不假装通过听音分析模型。
- 模型复制到统一库目录，独立软件与同一用户的插件共享；从原下载文件夹移走文件不影响已导入模型。库索引使用跨进程锁与原子替换；从列表移除不会删除预设仍可能使用的模型文件。
- TONE3000 OAuth PKCE 登录、浏览器选取后回传、最新/热门/个人收藏/创建/下载列表，逐个捕获下载入库，保留作者、来源、许可证和标签。原生界面不提供需要商业协议的全站搜索。
- `.nam` 和 IR 拖入；TONE3000 页面链接拖入或粘贴；`tonedock://tone/42` 唤起独立软件。网页选取使用官方 OAuth 回调；任意第三方网页上的“发送到软件”需要其提供链接，或使用附带的 [发送页](packaging/send-to-tonedock.html)。不假设 TONE3000 已有 ToneDock 专用按钮。
- 最多16个内置效果，可添加、删除、旁通、拖动及上移/下移排序。Gate、Compressor、Drive、NAM、Cabinet IR、EQ、Chorus、Delay、Reverb、Gain。
- 输入/输出增益、峰值表、全局旁通、独立版音频设备/采样率/缓冲设置、预设文件与 DAW 工程状态保存。
- NAM 加载和预热、库导入及下载在后台；结构编辑准备新图后交叉淡化，旧图在非音频线程回收。模型采样率与宿主不同时采用 Lanczos 重采样，并向宿主报告额外 DSP 延迟；总演奏延迟仍由声卡、驱动、缓冲和模型计算决定。

## 构建

需要 Git、CMake 3.24+、C++20 编译器。首次配置下载固定提交的 JUCE 和 NAM Core（含其 Eigen / AudioDSPTools 子模块），无需购买软件开发框架即可按本目录 AGPL 开源方式构建。

Windows：Visual Studio 2022 的 Desktop development with C++ 和 Windows SDK。

```powershell
cmake -S ToneDock -B ToneDock/build -A x64 -DBUILD_TESTING=ON
cmake --build ToneDock/build --config Release --parallel 2
ctest --test-dir ToneDock/build -C Release --output-on-failure
```

macOS：Xcode Command Line Tools。Universal 构建同时包含 Apple Silicon 与 Intel；需要时可仅设置本机架构缩短编译。

```sh
cmake -S ToneDock -B ToneDock/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64'
cmake --build ToneDock/build --config Release --parallel 2
ctest --test-dir ToneDock/build -C Release --output-on-failure
```

产物位于 `build/ToneDockApp_artefacts/<配置>/` 和 `build/ToneDock_artefacts/<配置>/{VST3,AU}/`。未签名构建用于开发测试；正式分发的签名、公证和安装验收尚未完成。参见 [安装与网页唤起](docs/INSTALL.md)。

## 使用

1. 启动独立版，在 Audio settings 选择吉他声卡输入、输出与缓冲；新安装默认不启用麦克风输入。插件版的音频设备由 DAW 管理。
2. 拖入 `.nam` 或 IR，选择模型后点 Load model。NAM 模型加载到第一个 NAM 位置，IR 加载到第一个 Cabinet IR 位置；后续版本再支持明确指定多个同类位置。
3. 从效果选择框添加效果，拖动行或点 Up / Down 调整顺序，选中效果调节下方旋钮。
4. TONE3000 首次连接需要应用的 `t3k_pub_...` publishable key，可在 TONE3000 Settings → API Keys 创建，在窗口输入一次。它是客户端公开标识，不是账户密码或 `t3k_cs_` secret。登录令牌在 macOS Keychain / Windows DPAPI 中保存，不写入预设。
5. 在浏览器选择 tone，回到软件检查作者和许可，选择具体捕获下载。导入验证完成后出现在本地模型库，可离线使用。

库默认在 `%APPDATA%/ToneDock`（Windows）或 `~/Library/Application Support/ToneDock`（macOS）。可通过 `TONEDOCK_DATA_DIR` 指定其他位置。独立软件保存并恢复上次退出时的链路，库变更会在打开的编辑器内刷新。跨电脑转移预设时也需要转移对应的 `models/` 文件；预设引用内容 ID，不内嵌或重新分发第三方模型权重。

## 首版边界

- NAM 目前处理单输入、单输出捕获，插件支持单声道或立体声输入、立体声输出；NAM 节点将输入混成单声道，之后的 IR 和时间效果可为立体声。立体声或多条件 NAM 捕获导入会给出不支持提示。
- IR 限制为单/双声道 WAV、最长两秒。旧 NAM 缺少训练采样率时按48 kHz解释；需要核对捕获来源。
- DAW 自动化目前覆盖输入、输出和全局旁通；每个效果的旋钮保存在预设中，尚未开放为独立宿主自动化参数。
- 效果链使用内置处理器，尚不承载第三方 VST/AU 插件；没有承诺任意模型都能在最低声卡缓冲下实时运行。
- TONE3000 搜索接口的商业开放、模型许可证与应用框架许可证是不同事项，详见 [接入与依赖](docs/INTEGRATION.md)。
