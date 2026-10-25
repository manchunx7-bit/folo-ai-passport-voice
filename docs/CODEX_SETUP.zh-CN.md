# 用 Codex 辅助配置随身 AI 语音

**统一入口已移至 [Agent 配置手册](AGENT_SETUP.zh-CN.md)**，支持 Codex、Claude Code、WorkBuddy 等本地 Agent，包含复制指令、结构化诊断、进度保存和实际验收。本页保留原平台操作细节与旧链接；新配置从统一手册开始。

Codex 可以帮助检查下载包、声卡、快捷键配置与端口，编辑转发器的 JSON，并定位连接问题。
需要使用**运行在待配置的这台 Mac 或 Windows 电脑上、能访问本地项目和终端的 Codex**。
只在远端执行的任务无法替你切换这台电脑的麦克风；系统界面操作还取决于当前可用的工具。

完整配置指令在 [GitHub 首页](../README.md#codex-setup)，按平台复制。本文分为 [Mac 流程](#macos) 和 [Windows 流程](#windows)。
不使用 Codex 也能照 [Mac 手动教程](../macos/README.zh-CN.md) 或 [Windows 手动教程](QUICKSTART.zh-CN.md) 完成配置。

<a id="macos"></a>
## Mac：让 Codex 按这个顺序处理

### 准备完整项目

从仓库 `main` 分支的 **Code → Download ZIP** 下载并完整解压。在本地 Codex 中选择同时包含 `macos/` 与 `windows/` 的根目录。现有 Windows 发布包不能代替最新 Mac 源码；Mac 没有签名 App，需原生 arm64 或 x86_64 Python 3.11+。Apple Silicon 下通过 Rosetta 运行不支持。

先让 Codex 读取 `macos/README.zh-CN.md`，核对芯片、Python 版本和架构，再检查 BlackHole 2ch 与输入法是否已安装。不要直接套用 Windows 的 EXE、VB-CABLE 或 PowerShell 指令。

### 依赖、权限和诊断

已有 `macos/voice-config.json` 先备份；依赖通过 `bash macos/install.command` 安装。这个脚本在交互终端末尾会等待回车。安装 BlackHole、系统授权和重启按教程引导用户完成；不要把驱动存在当成已经选对输入法麦克风。

下面从**项目根目录**运行，命令会结束，适合自动诊断：

```bash
uname -m
python3 --version
python3 -c 'import platform, sys; print(sys.executable); print(platform.machine())'
macos/.venv/bin/python3 macos/mac_voice.py --list-keys
macos/.venv/bin/python3 macos/mac_voice.py --list-devices
macos/.venv/bin/python3 macos/mac_voice.py --diagnose
```

按键权限未就绪时，核对实际启动宿主的辅助功能权限，授权后重开宿主和客户端。输入法需要麦克风权限；客户端只播放设备音频，不把终端麦克风或输入监控权限当成固定前提。出现局域网访问提示时核对本地网络权限，不关闭整套防火墙。

诊断会绑定端口做检查；先确认没有自己的旧客户端在运行。`UDP 可绑定`、`按键权限 OK`、`可用虚拟声卡` 分别记录，任何一项都不能证明真实识别成功。

### 配置与实际验收

配置位置为 `macos/voice-config.json`。`output_substr` 为 BlackHole 2ch；输入法音源也为 BlackHole 2ch，若它只跟随系统默认输入，再引导用户切换系统输入。系统输出保持扬声器 / 耳机，并说明默认输入切换对会议软件的影响。

先让用户用实体键盘确认输入法的**实际快捷键和开始 / 结束方式**，再填写 `hotkey_key` 与 `hotkey_mode`。F9 只是示例，Fn / 地球键尚待兼容验证；不要替用户假定可用。校验配置：

```bash
macos/.venv/bin/python3 -m json.tool macos/voice-config.json
```

只启动一个 `bash macos/start.command`。让用户在文本编辑中亲自对设备说话，分别确认“上键开始、松开结束、文稿出字、下键一次换行”。无字时先查 BlackHole 电平或本地短录音，再分辨声音与输入法服务问题。

反馈必须区分自动检查、用户确认和尚未验证。自动测试不代替真实 Mac / 微信输入法出字验收；不要上传录音、个人配置或账号。

### 安装或授权后继续的指令

```text
继续配置这台 Mac 的 AI Passport 随身语音，先阅读 macos/README.zh-CN.md。
我实际完成的安装 / 授权是：（填写 BlackHole、Python、辅助功能等实际结果）。
输入法版本、语音快捷键、hold/tap 方式和当前麦克风是：（按真实设置填写）。
请重新诊断，保留已有配置，不重复安装已经可用的组件。只关闭确认属于我的旧客户端，不结束无关进程。
最后引导我在文本编辑中对设备说话，分别记录开始、松开结束、出字、下键换行是否通过；未实测的部分写明待验证。
```

<a id="windows"></a>
## Windows：以下步骤用于 EXE 应用包

### 1. 先给 Codex 哪个文件夹

从 [Releases](https://github.com/manchunx7-bit/folo-ai-passport-voice/releases) 下载最近发布的 **ai-passport-voice-app-*.zip**，全部解压。
在 Codex 中选择**解压后的完整目录**作为本地项目，让它能访问 docs 和 windows。

```text
你选择的解压目录/
├─ docs/                         使用教程
├─ windows/
│  ├─ AI-Passport-Voice.exe       可直接运行的电脑端
│  ├─ start.cmd                  日常启动
│  ├─ diagnose.cmd               手动诊断
│  ├─ voice-config.example.json  配置示例
│  └─ voice-config.json          根据你的快捷键创建；语音应用包不含个人配置
└─ device/                       本功能设备模块源码（不是可刷写固件）
```

如果选的是源码仓库，没有 EXE，先让它获取完整发布包；不要为了普通使用建立 Python 开发环境。
指令中的路径填实际解压目录，例如 `D:\AI-Passport-Voice`，不要把示例路径直接当成自己的路径。
未下载时也可让 Codex 从本项目 Releases 准备语音应用包。

### 2. 它能帮你处理什么

| 内容 | 处理方式 | 怎样确认 |
| --- | --- | --- |
| 语音应用包和启动入口 | 检查文件、确认下载来源 | windows 里有 EXE 和 start.cmd |
| 声卡与端口 | 调用程序的只读检查入口 | 枚举 CABLE 两端、查格式及端口 |
| VB-CABLE 安装准备 | 从官网获取、解压安装程序 | x64 安装程序与驱动文件在一起 |
| Windows 麦克风设置 | 有系统界面工具时协助；没有则给具体点击路径 | 默认输入为 CABLE Output |
| 输入法语音设置 | 通过原生设置界面核对，或让你提供实际键位/模式 | 实体键盘能开始并结束语音 |
| 映射 JSON | 备份已有文件、写入并校验配置 | 转发窗口热键/模式与输入法一致 |
| 启动与排错 | 启动一个转发器，区分网络/按键/声音问题 | 设备就绪、声音确实到达 |
| 实际识别 | 引导你对设备说话、检查输入框 | 记事本出字、松开结束、下键换行 |

管理员安装确认、重启以及开口说话需要你参与。
输入法登录、识别语言、隐私选项由你按其界面要求选择；不让 Codex 直接修改输入法 MMKV 或程序文件。
没有界面工具时，Codex 仍可完成文件和终端检查，再给你需要手动点击的步骤。

### 3. 应使用哪些诊断命令

下面在**语音应用包的 windows 目录**用 PowerShell 执行：

```powershell
.\AI-Passport-Voice.exe --list-keys
.\AI-Passport-Voice.exe --list-devices
.\AI-Passport-Voice.exe --diagnose
```

这些命令会结束，不会开始录音或模拟按键。自动执行时用 EXE，不用末尾会等待按键的 diagnose.cmd。
如果已经有自己的 start.cmd 窗口，诊断端口可能显示 BUSY；先识别该进程，关闭自己的窗口再检查。
旧发布 EXE 的端口检查为 UDP 33333；当前源码诊断按配置或启动参数检查。首次设置保留默认端口，勿让 Agent 通过随意换端口规避占用。

| 输出 | 表示什么 | 还缺什么验证 |
| --- | --- | --- |
| Shortcut syntax OK | 键名和模式合法 | 输入法是否设置为相同键位/模式 |
| UDP 33333 available | 检查时端口未被占用 | 设备与电脑能否互通 |
| UDP 33333 BUSY | 已有进程占用 | 是否自己的转发器正在运行 |
| Output format supported | 匹配的播放端格式可用，检测到 CABLE 录制端 | Windows 默认输入、输入法音源和实际收音 |
| Install VB-CABLE / Audio failure | 声卡缺失、格式或枚举失败 | 安装、重启、设备启用与具体错误 |

**诊断没有读出输入法设置，也没有做真实识别测试。** 不接受仅凭 OK 就宣布“已经能用了”的结论。

### 4. 配置文件怎样算正确

配置位置是 **windows/voice-config.json**，与 EXE 同目录。已有文件先备份，再改本次需要的字段。

```json
{
  "hotkey_key": "right-shift",
  "hotkey_mode": "hold",
  "output_substr": "CABLE Input",
  "udp_port": 33333,
  "device_ip": ""
}
```

这是转发器默认方案，需要输入法也支持并设置为右 Shift 按住说话。
左 Shift 改成 left-shift；点击切换型用 tap，但必须先验证同一快捷键能开始和结束。
具体键名与限制见 [映射键教程](QUICKSTART.zh-CN.md#mapping)。

Codex 应校验 JSON，再核对启动窗口的真实热键和模式。
命令行参数比 JSON 优先，修改后要重启自己的转发窗口。
不应添加不存在的 down_key 配置，或使用关闭音频的 --no-audio 来解决麦克风未就绪。

### 5. 安装重启后，复制什么继续

回到**同一个本地 Codex 项目**，补充实际完成的情况。下面的内容按事实修改后发送：

```text
继续配置 AI Passport 随身 AI 语音。
我刚完成 VB-CABLE 安装并重启。语音应用包仍在之前的解压目录，请重新检查声卡两端，不要直接重复安装。
请继续核对 Windows 默认输入为 CABLE Output，并完成输入法与转发器的语音快捷键匹配。
我在输入法里实际设置的是：右 Shift、按住说话。（请把这句改成你自己的设置）
如果端口占用，先判断是不是我自己的转发窗口；不要结束无关进程。
最后引导我用设备在记事本说话出字、松开结束、短按下键换行，分别记录结果。
```

如果设置窗口需要你操作，按 Codex 给出的步骤完成后，告诉它**实际选了什么**，而不是只说“已经设置”。
例如：“Windows 输入现在是 CABLE Output；输入法选左 Shift 长按；实体键盘可以打开和结束语音。”

### 6. 完成标准和以后怎么启动

以下都通过，才算完成：

- Windows 默认输入是 **CABLE Output**，输入法读取同一音源。
- 电脑实体语音快捷键能开始和结束；转发器的键位/模式一致。
- 转发器只有一个窗口，音频出口为 **CABLE Input**，设备显示可以说话。
- 按住设备上键，对设备说话后**记事本出现对应文字**；松开结束。
- 等识别完成，短按下键**记事本换行**。

以后双击 **windows/start.cmd**，设备进入随身语音，再点选目标输入框即可。
换过会议麦克风后，记得选回 CABLE Output。

配置结果中应分别注明“已自动完成”“由用户确认”“尚待验证”，不上传录音、Wi-Fi 密码或个人配置。
问题继续按 [故障排查](TROUBLESHOOTING.zh-CN.md) 定位。
