# 架构与 TONE3000 接入

## 固定依赖

| 依赖 | 提交 | 许可 |
|---|---|---|
| JUCE 8.0.12 | `29396c22c93392d6738e021b83196283d6e4d850` | AGPLv3 / JUCE 商业许可 |
| NeuralAmpModelerCore | `0b3d3c97b0859a3a8c92a8628c4dd89a25eb5842` | MIT |
| TONE3000 官方 JUCE 接入示例 | `cc37a11f62f0d3472a3e5a1c1274664bd3a78e47` | MIT；本地适配来源见 third_party/t3k/UPSTREAM.md |

AudioDSPTools、Eigen、JUCE 的内嵌依赖保留各自许可。ToneDock 新应用源码采用 AGPL-3.0-or-later；这不改变仓库中其他项目、模型文件或官方 MIT 适配器的许可。闭源商业分发需要另评估 JUCE 许可。

## 数据与音频

Library 将导入数据复制到 `models/<sha256>.nam` 或 `.wav`；索引只保存分类与来源等元数据。索引更新跨进程互斥，使用临时文件替换，避免插件实例互相覆盖；坏索引不会自动清空。相同字节去重。

效果顺序和文件引用使用 StageSpec，旋钮值通过原子变量交给实时节点。后台线程构建并预热完整图，通过原子发布和 hazard pointer 保留渲染中对象；音频线程不做网络、磁盘、模型创建或图回收。结构切换为1024个宿主样本的交叉淡化。超过CPU预算、声卡驱动问题仍可能造成掉音，需实机测量。

文件库中的 NAM 先由官方运行时读取校验；IR 使用音频解码器校验。ZIP 只导入支持的模型格式，限制文件个数、压缩/展开尺寸并拒绝越界路径。预设保存内容 ID，库目录改变后按当前数据目录解析；被从列表移除的字节仍保留，保护既有 DAW 工程。

## 官方接口

依据 [官方集成仓库](https://github.com/tone-3000/api/tree/cc37a11f62f0d3472a3e5a1c1274664bd3a78e47) 的已公开客户端实现：

- `GET /api/v1/oauth/authorize`：客户端 publishable key、随机 state、PKCE S256，`prompt=select_tone` 或 `load_tone`。
- `POST /api/v1/oauth/token`：授权码兑换、refresh_token 更新；提前60秒刷新，401只重试一次，400/401刷新失败清理会话。
- `GET /api/v1/tones/{id}`、`GET /api/v1/models?tone_id=...`：读取 tone 和分页模型。
- `GET /api/v1/tones/{latest,trending,created,favorited,downloaded}`：免费接入范围内的列表，分页端点保留翻页。
- 模型文件 `model_url` 通过官方域名带 Bearer 下载，不是直接抓网页 HTML。

架构选择明确提供 A2 与 Legacy / Custom。官方 API 省略 architecture 会排除 A2；界面不会用一次不带过滤的请求假装得到全部架构。

令牌使用 Windows DPAPI / macOS Keychain。Linux 仅为开发目标，令牌只在内存中存在，不使用明文文件回退。客户端公开 key 可保存在 settings.json，secret key 不接收、不嵌入产物、不写进项目。

HTTP 正文限制64 MiB；显式处理 HTTPS 跳转，跨域跳转不转发 Authorization。下载先写临时文件，成功后替换并由模型导入器校验。下载失败不会作为已入库模型展示。模型条目保存作者、来源 URL、许可和供应方标签；分类与收藏属于本地管理，不自动操作用户的远端收藏。

官方说明将免费非商业接入限制为 OAuth prompt flows 与有界列表；全站搜索、批量 zip 下载等商业接口不在首版 UI 中调用。开发环境没有真实账号、公钥配置和下载验证时，必须明确记录为待联调。
