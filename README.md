# AI Passport 随身语音

按住设备上键说话，松开结束识别，核对文字后短按下键回车。这个项目把 AI Passport 用作电脑的无线麦克风与语音快捷键，文字识别由微信输入法等软件完成。

提供 Windows 客户端与 Apple 芯片 Mac 源码测试客户端。**Mac 实机出字和输入法版本兼容尚未验收**，请先阅读对应平台的要求，再按教程完成实际说话测试。

**本仓库只开源随身语音功能，不含整机源码或可刷写固件。** 固件由维护者在 [FoloToy 口袋百宝箱玩法页面](https://ai-passport.folotoy.cn/plays/438/) 发布。该页面若暂未提供包含随身语音的版本，请等待更新；不要把本仓库 Source code ZIP 当固件。

<a id="codex-setup"></a>
## 让 AI 帮你配置

在这台电脑上的 Codex、Claude Code、WorkBuddy 等 Agent 中复制下面的指令。Agent 会先检查当前目录；如果项目尚未下载，会使用下面的公开仓库地址准备完整项目，再继续配置。需要本地文件与终端权限；系统授权和实际说话需要你参与。

```text
请帮我配置 AI Passport 随身语音。
项目地址：https://github.com/manchunx7-bit/folo-ai-passport-voice
如果当前电脑没有本项目，请先下载或克隆这个仓库，进入项目根目录后继续；不要要求我先手动下载原项目。
先读 AGENTS.md 和 docs/AGENT_SETUP.zh-CN.md，
识别我的系统、现有程序与配置，按对应教程执行检查和配置，已有设置先备份。
只询问不能检测的信息，核对输入法真实快捷键与音源；需要授权或重启时记录进度。
最后让我在空白文稿亲自说话，确认开始、松开结束、出字和下键换行。
不要自动录音发送。分别报告自动检查、用户确认和待验证项目，并告诉我日常如何启动。
```

详细流程、诊断命令、重启后继续及完成标准见 **[Agent 配置手册](docs/AGENT_SETUP.zh-CN.md)**。旧版应用包没有新手册时使用其内置 [Codex 教程](docs/CODEX_SETUP.zh-CN.md)，或从上面的仓库地址获取最新文档；不假定旧 EXE 支持新工具。

## 手动配置：选择你的电脑

| 平台 | 安装与配置教程 | 软件与启动方式 |
| --- | --- | --- |
| **macOS · Apple 芯片** | **[Mac 完整配置教程](macos/README.zh-CN.md)**：下载、Python、BlackHole、权限、快捷键、首次出字及排障 | 从本仓库 main 分支 **Code → Download ZIP** 获取完整源码；原生 arm64 Python 3.11+；`bash macos/start.command` |
| **Windows 10/11 x64** | **[Windows 完整配置教程](docs/QUICKSTART.zh-CN.md)**：应用包、VB-CABLE、输入法、映射和记事本验收 | [Releases](https://github.com/manchunx7-bit/folo-ai-passport-voice/releases) 下载 `ai-passport-voice-app-0.2.0.zip`；解压后双击 `windows/start.cmd` |

Mac 当前没有签名的 `.app` / `.dmg`，Intel Mac 和 Rosetta 下的 x86 Python 不支持。Windows 现有发布包不包含最新 Mac 客户端；Mac 请保留完整源码中 `macos/` 与 `windows/` 的相对位置。

### 两个平台共用的准备

使用同款 ESP32-C3 / 8 MB / ES8311 的 FoloToy AI Passport，安装官网玩法页提供的、包含“随身语音”的固件；设备已能打开此功能就跳过刷写。本次 Mac 适配复用现有协议，无需为它重新烧录。

设备接入 2.4 GHz Wi-Fi，电脑和设备在同一可互通局域网，使用 UDP 33333。电脑可通过同一路由器的网线或 5 GHz Wi-Fi 接入。USB 不会自动把设备变成 USB 麦克风，不需要蓝牙配对。虚拟声卡和输入法需从各自官网单独安装。

### 音频方向别选反

设备麦克风 → Wi-Fi UDP → 电脑转发器 → 虚拟声卡 → 输入法 → 当前输入框。

| 设置位置 | macOS | Windows |
| --- | --- | --- |
| 转发器音频出口 | **BlackHole 2ch** | **CABLE Input** |
| 输入法麦克风 | **BlackHole 2ch** | **CABLE Output** |
| 系统默认输入 | 输入法只跟随系统时选 BlackHole 2ch | 按教程选 CABLE Output |
| 系统默认输出 | 保持自己的扬声器 / 耳机 | 保持自己的扬声器 / 耳机 |

### 按键与首次验收

- 按住上键：开启语音；等输入法界面出现后再说话。
- 松开上键：结束语音；等文字出现并核对。
- 短按下键：发送 Enter；当前软件决定它是换行还是发送消息。先在 Mac 文本编辑或 Windows 记事本试，不要在重要聊天窗口直接测试。
- 长按确定：退出设备应用。

输入法的快捷键与对应平台 `voice-config.json` 中的 `hotkey_key`、`hotkey_mode` 必须一致。`hold` 为按住 / 松开，`tap` 为同一快捷键开始 / 结束各点击一次；两种模式在设备上都仍是按住上键说话、松开结束。默认示例不代表输入法默认键，先用实体键盘验证。电脑诊断通过不等于输入法真实识别已验收。

## 源码边界

| 目录 | 内容 |
| --- | --- |
| `windows/` | 可运行的 Windows 转发器、ADPCM 解码、UDP 通道、诊断与测试 |
| `macos/` | Apple 芯片 Mac 测试客户端、启动脚本、诊断与模拟接口测试；复用 Windows 目录的协议模块 |
| `device/main/apps/voice/` | 本功能的设备端状态机、协议、录音生命周期、传输和界面业务代码 |
| `device/tests/` | 语音状态机和界面数学的可独立运行测试 |
| `docs/` | 使用、排错、固件入口、开发与底层接口适配说明 |

不包含首页/名片、收音机、小智、游戏、秒表、整机启动器、设置页、BSP 实现、共享字体或整机历史。设备端是**功能模块源码**，不是单独可刷写的 ESP-IDF 工程；开发者需按 [适配说明](docs/PORTING.zh-CN.md) 提供宿主接口。Windows 程序与纯逻辑测试可独立运行。

## 文档

- [macOS 完整配置教程与故障排查](macos/README.zh-CN.md)
- [Windows 完整配置教程](docs/QUICKSTART.zh-CN.md)
- [按平台查找故障排查](docs/TROUBLESHOOTING.zh-CN.md)
- [Agent 自动检查与配置：Codex / Claude Code / WorkBuddy](docs/AGENT_SETUP.zh-CN.md)
- [原 Codex 平台配置说明](docs/CODEX_SETUP.zh-CN.md)
- [官网固件与刷写注意](docs/FIRMWARE.zh-CN.md)
- [源码运行与测试](docs/DEVELOPMENT.zh-CN.md)
- [验证记录及限制](docs/VALIDATION.md)
- [第三方来源](THIRD_PARTY_NOTICES.md)

个人配置、录音与输入法账号不要提交到 GitHub。本项目不提供识别账号或 ASR 服务，也不承诺所有输入法版本兼容。
