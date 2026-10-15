# 安装与浏览器唤起

## Windows

开发构建的 `ToneDock.exe` 可直接运行。将 `ToneDock.vst3` 整个目录放到 `%LOCALAPPDATA%/Programs/Common/VST3`，或宿主支持的用户 VST3 目录；全局位置为 `C:\Program Files\Common Files\VST3`，需要相应文件权限。

对独立软件执行以下脚本注册当前用户的 URL 协议，不需要改系统级注册表：

```powershell
pwsh -File ToneDock/packaging/register-protocol.ps1 -Executable 'C:\Your Folder\ToneDock.exe'
```

浏览器第一次打开 `tonedock://tone/42` 会请求是否允许调用本机软件。链接只接受正整数 tone ID，经官方 API 和账号授权取得模型；不执行链接中的命令，不将任意远端文件作为程序运行。

## macOS

从Actions下载开发构建后，先解压外层artifact ZIP，再解压其中的 `ToneDock-macOS-development.tar.gz`。内层归档保留应用可执行权限和bundle内的符号链接。

把 `ToneDock.app` 复制到 `~/Applications` 或 `/Applications`，正常启动一次使系统登记 `tonedock` 协议。AU 的整个 `.component` 放入 `~/Library/Audio/Plug-Ins/Components/`，VST3 放入 `~/Library/Audio/Plug-Ins/VST3/`。重启 DAW 并重新扫描。

开发版本未签名、公证；本工程不提供关闭 Gatekeeper 的脚本。正式安装包、开发者签名、公证和宿主验收需在 macOS 完成。

## 网页与拖拽

- 软件内 Browse 使用 TONE3000 官方 `prompt=select_tone`，浏览器 OAuth 完成后通过仅监听 `127.0.0.1` 的临时端口返回。校验 state，使用 PKCE S256 兑换令牌。
- 若复制了 tone 页面，拖入软件或粘贴到 Open link；未确认访问时通过 `prompt=load_tone` 确认。
- 任意网页可放置 `<a href="tonedock://tone/42">Open in ToneDock</a>`。附带 HTML 发送页可离线打开，粘贴 TONE3000 链接后生成唤起链接。
- 插件窗口里也可发起官方浏览器选取，并直接下载到共享模型库；系统的自定义协议由独立软件处理，打开后插件可刷新本地库。插件不会作为一个独立系统应用被浏览器唤起。

协议注册只解决应用启动，首次账号授权和缺失/私有模型的访问仍由 TONE3000 决定。
