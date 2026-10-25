# macOS 配置教程：AI Passport 随身语音

目标：**按住设备上键，对着设备说话，松开后 Mac 输入框出现文字；核对后短按下键回车。** 识别由微信输入法等语音输入软件完成，客户端负责传声音和快捷键，不提供 ASR 服务或 API Key。

**适用范围：Intel Mac 原生 x86_64 或 Apple 芯片 Mac 原生 arm64，Python 3.11 或更新版本。当前为源码测试客户端，没有签名的 `.app` / `.dmg`；尚未完成完整的真实 Mac 安装、CoreAudio 和微信输入法出字验收。** Apple Silicon 下通过 Rosetta 运行不支持；支持的最低 macOS 版本及输入法版本仍待实机核对。下文是配置与验收流程，不代表所有版本已通过兼容测试。

使用完整源码仓库时，可将 [Agent 配置指令](https://github.com/manchunx7-bit/folo-ai-passport-voice/blob/main/docs/AGENT_SETUP.zh-CN.md) 交给本地 Codex、Claude Code、WorkBuddy 等。独立 Mac 测试 ZIP 可能不含此文档或新增诊断工具，缺失时继续按本页操作，不猜不存在的命令。

Windows 用户请看 [Windows 完整教程](https://github.com/manchunx7-bit/folo-ai-passport-voice/blob/main/docs/QUICKSTART.zh-CN.md)。

## 配置顺序

1. [准备设备、下载完整源码](#prepare)
2. [安装并检查原生 Python](#python)
3. [安装 BlackHole 2ch](#blackhole)
4. [先单独验证输入法](#input-method)
5. [安装电脑客户端依赖](#install)
6. [配置音频方向和系统权限](#permissions)
7. [匹配快捷键和 hold/tap 模式](#mapping)
8. [诊断、启动和连接设备](#start)
9. [在文本编辑中完成首次出字验收](#first-test)
10. [日常使用、故障排查与反馈](#daily)

```text
设备麦克风 ── Wi-Fi UDP ──► Mac 客户端 ──► BlackHole 2ch ──► 输入法 ──► 文字
设备上键 ─────────────────► Mac 客户端 ──► 语音快捷键：开始 / 结束
设备下键 ─────────────────► Mac 客户端 ──► Enter
```

USB 用于供电或烧录，插上 USB 不会出现 USB 麦克风，也不需要蓝牙配对。Mac 客户端不会读取 Mac 自带麦克风，不会替你安装声卡或更改系统默认音源。输入法是否使用云端识别，由它自己的服务与设置决定。

<a id="prepare"></a>
## 1. 准备设备、下载完整源码

### 1.1 准备清单

| 项目 | 要求 / 下载入口 |
| --- | --- |
| 电脑 | 苹果菜单 → 关于本机，确认是 Apple 芯片或 Intel Mac；Apple 芯片不要通过 Rosetta 打开终端 |
| 设备 | 同款 ESP32-C3 / 8 MB / ES8311 AI Passport，已安装包含“随身语音 / AI语音”的固件 |
| Python | [Python 官方 macOS 下载页](https://www.python.org/downloads/macos/)，3.11+；Apple 芯片实际运行架构为 arm64，Intel Mac 为 x86_64 |
| 虚拟声卡 | [BlackHole 官方安装页](https://existential.audio/blackhole/)，选择 **2ch** |
| 输入法 | [微信输入法官网](https://z.weixin.qq.com/)，下载 Mac 版；需有可用的语音功能及可匹配的快捷键 |
| 网络 | 设备接入 2.4 GHz Wi-Fi；Mac 和设备在同一可互通局域网，默认使用 UDP 33333 |

Mac 可以通过网线或路由器的 5 GHz Wi-Fi 接入，只要能与设备互通。同名 Wi-Fi 不保证互通；公司、学校、酒店和访客网络可能隔离客户端，首次建议用自己的路由器。

### 1.2 下载和保留目录结构

1. 打开 [本项目 GitHub 首页](https://github.com/manchunx7-bit/folo-ai-passport-voice)。
2. 确认分支为 `main`，点击 **Code → Download ZIP**。
3. 完整解压，放到以后不会随意移动的目录。不要只下载 `mac_voice.py` 或单独拖出 `macos/`。
4. 解压后的项目根目录应同时包含 `macos/` 和 `windows/`。

```text
项目根目录/
├─ macos/
│  ├─ README.zh-CN.md
│  ├─ install.command
│  ├─ diagnose.command
│  ├─ start.command
│  ├─ mac_voice.py
│  └─ voice-config.example.json
└─ windows/
   ├─ adpcm.py
   ├─ hotkey_forwarder.py
   └─ udp_transport.py
```

Mac 会复用 `windows/` 中的协议模块，不会调用 Windows 的按键接口。现有 `ai-passport-voice-app-0.2.0.zip` 是 Windows 应用包，不能用它代替最新 Mac 源码。

如果拿到的是维护者生成的 Mac 源码测试包，根目录可能叫 `AI-Passport-Voice-Mac-test`；同样保留两个目录，并从该根目录执行下文命令。

### 1.3 设备固件和配网

已经能进入随身语音的设备，不需要为本次 Mac 适配重新烧录。未安装者从 [FoloToy 玩法页面](https://ai-passport.folotoy.cn/plays/438/) 核对硬件与包含此功能的固件；页面未提供时等待更新。**GitHub 源码 ZIP 不是固件，不能用于刷写。**

新设备按固件页面完成手机配网：连接设备屏幕显示的设置热点，按页面选择路由器的 2.4 GHz Wi-Fi。没有自动打开设置页时，可按设备说明访问 `http://192.168.4.1`。配好后让 Mac 接回同一路由器，设备设置热点不用于日常传音。已有可用 Wi-Fi 的设备可跳过。

<a id="python"></a>
## 2. 安装并检查原生 Python

1. 从 Python 官方下载页安装 **3.11 或更新版本的 macOS 安装包**。Apple 芯片可使用 universal2 安装包，Intel Mac 使用 x86_64 安装包。
2. 安装完成后重新打开“终端”。Apple 芯片如果以前给终端勾过“使用 Rosetta 打开”，取消后重开。
3. 逐条执行：

```bash
uname -m
python3 --version
python3 -c 'import platform, sys; print(sys.executable); print(platform.machine())'
```

**完成标志：** Intel Mac 上 `uname -m` 和 Python 打印 `x86_64`；Apple 芯片上两者均为 `arm64`；Python 版本不低于 3.11。系统与 Python 架构要分别检查。

如果提示 `command not found`、版本太旧、架构与 Mac 不匹配或 Rosetta 检测失败，先修好 Python 环境。装过多个版本时，用 `which -a python3` 查看命令解析到了哪一个；安装脚本会使用当前终端找到的 `python3`。不要在架构检查失败时继续安装，不要用 `sudo pip` 修改系统 Python。

<a id="blackhole"></a>
## 3. 安装 BlackHole 2ch

BlackHole 把客户端播放的设备声音变成输入法能选择的麦克风。Mac 使用 BlackHole，Windows 教程中的 VB-CABLE 名称不适用于这里。

1. 从 [BlackHole 官方安装页](https://existential.audio/blackhole/) 获取 **2ch** 安装程序，按网站流程下载。
2. 退出正在使用音频的 App，打开官方安装包，按安装器完成安装。系统要求重启时先重启。
3. 打开“音频 MIDI 设置”（可用 Spotlight 搜索），确认设备列表出现 **BlackHole 2ch**。
4. 后续在系统“声音”的输入列表中也应能选到它。

已经安装并使用 Homebrew 的用户，也可按 [BlackHole 官方说明](https://github.com/ExistentialAudio/BlackHole) 执行：

```bash
brew install blackhole-2ch
```

选择一种安装方式即可。BlackHole 是音频驱动，**不会像普通 App 一样出现在“应用程序”中**。本项目这条传音路线不需要创建“多输出设备”或“聚集设备”，也不需要降低系统启动安全性。

<a id="input-method"></a>
## 4. 先单独验证输入法

先证明输入法自己能工作，再接入设备：

1. 安装 Mac 版微信输入法，按它的引导添加并切换到该输入法。
2. 新建一个“文本编辑”文稿，点出输入光标，暂时使用 Mac 自带麦克风或已有的正常麦克风。
3. 在输入法设置中找到语音输入相关选项，确认自己的版本支持语音功能。
4. **用实体键盘**触发语音，说一句话并结束，确认文稿真实出现文字。
5. 记录实际语音快捷键，以及它是“按住说话、松开结束”，还是“按一次开始、再按一次结束”。

不同输入法版本的菜单和可选快捷键可能不同。**F9、右 Shift 均不是本教程确认的微信输入法默认键。** 如果该版本只能用客户端无法可靠模拟的 Fn / 地球键，或没有语音功能，先解决输入法兼容问题；仅修改客户端 JSON 不能给输入法增加快捷键。

首次出现麦克风授权时，允许输入法使用麦克风。此时若内置麦克风都无法出字，先检查输入法登录、权限、识别语言、网络和语音服务。

<a id="install"></a>
## 5. 安装电脑客户端依赖

下面所有命令都从**项目根目录**运行，即能同时看到 `macos`、`windows` 的那一层。

不熟悉终端路径时：打开“终端”，输入 `cd `（末尾留一个空格），把 Finder 中的项目根文件夹拖进终端，再按回车。然后检查：

```bash
pwd
ls macos/install.command windows/hotkey_forwarder.py
```

两个文件都能列出来后，运行：

```bash
bash macos/install.command
```

安装脚本会检查芯片和 Python 架构，在 `macos/.venv/` 中创建独立环境，联网安装 `sounddevice` 和 PyObjC 依赖，并在没有个人配置时创建 `macos/voice-config.json`。已有配置会保留，不会安装 BlackHole 或输入法。

**完成标志：** 终端显示“依赖安装完成”，没有上方报错；出现 `macos/.venv/bin/python3` 和 `macos/voice-config.json`。脚本末尾可能等待回车关闭。

下载依赖失败时保留第一条错误，检查当前网络，再重跑同一命令。不要用关闭证书校验来解决下载问题。若移动过项目目录且虚拟环境失效，关闭客户端，把旧 `macos/.venv` 改名留存，再运行安装脚本；保留 `voice-config.json`。

脚本也可从 Finder 打开；如果双击没有执行或提示无法打开，先确认文件来自本仓库，再用上述 `bash` 命令。不要关闭 Gatekeeper 或整套系统保护。

<a id="permissions"></a>
## 6. 配置音频方向和系统权限

### 6.1 音频选项怎么选

| 位置 | 选择 | 原因 |
| --- | --- | --- |
| 客户端 `output_substr` | `BlackHole 2ch` | 将设备声音播放进虚拟声卡 |
| 输入法的语音麦克风 | `BlackHole 2ch` | 从虚拟声卡接收设备声音 |
| 系统“声音 → 输入” | 输入法跟随系统音源时选 `BlackHole 2ch` | 只在输入法没有独立音源选择时依赖系统默认值 |
| 系统“声音 → 输出” | 平时使用的扬声器或耳机 | 保留正常系统声音 |

先看输入法有没有独立麦克风选择，有就直接选 BlackHole 2ch。如果它只跟随系统默认输入，在“系统设置 → 声音 → 输入”选择 BlackHole 2ch。**Mac 上输入与输出端都可能显示同一个 BlackHole 名称，不套用 Windows 的 CABLE Input / Output 名称。**

切换默认输入会影响其他跟随系统麦克风的 App。会议时按需要选择真正的麦克风，回来用设备时再切回。系统输出保持扬声器或耳机；改为 BlackHole 会让系统声音进入虚拟通道，也可能使你听不到声音。

### 6.2 需要哪些权限

系统版本不同，项目名称可能略有差异；按权限功能查找。

| 权限 | 授权对象与位置 | 用途 |
| --- | --- | --- |
| 辅助功能 | 系统设置 → 隐私与安全性 → 辅助功能；授权实际启动客户端的终端 / iTerm / VS Code，或系统列出的对应 Python | 向当前输入框发送语音快捷键与 Enter |
| 麦克风 | 隐私与安全性 → 麦克风；授权微信输入法，录音排查时也授权录音 App | 接收虚拟声卡或实体麦克风声音 |
| 本地网络 | 出现访问提示时允许实际客户端宿主；若系统提供“本地网络”设置项，在那里核对 | 与设备在局域网中发现和收发数据 |

辅助功能授权后，完全退出并重开启动客户端的宿主，再启动客户端。换用另一个终端 App 时可能需要重新核对；不要把权限给同名但无关的程序。

客户端只播放收到的音频，不采集 Mac 麦克风，所以不要把“给终端开麦克风权限”当成固定步骤。它也不监听键盘，不需要为了本教程无差别开启“输入监控”。诊断会检查按键权限，但不能替你授予权限。

防火墙或安全软件有拦截时，仅允许本程序所需的可信局域网通信，不关闭整套防火墙。

<a id="mapping"></a>
## 7. 匹配快捷键和 hold/tap 模式

编辑的是 **`macos/voice-config.json`**，不是 `voice-config.example.json`。先备份已有个人配置，使用代码编辑器以 UTF-8 纯文本保存，不要保存成富文本或 `.json.txt`。JSON 用英文双引号，不能写注释或末尾多余逗号。

```json
{
  "hotkey_key": "f9",
  "hotkey_mode": "hold",
  "output_substr": "BlackHole 2ch",
  "udp_port": 33333,
  "device_ip": ""
}
```

**这只是配置格式示例。只有输入法已支持并设置为 F9 按住说话，才能直接保留。** 按第 4 节的实体键盘结果修改：

| 输入法实际设置 | `hotkey_key` 示例 | `hotkey_mode` |
| --- | --- | --- |
| 右 Shift 按住开始、松开结束 | `right-shift` | `hold` |
| F9 按住开始、松开结束 | `f9` | `hold` |
| 同一个组合键按一次开始、再按一次结束 | 例如 `cmd+f9`，以输入法允许的实际键为准 | `tap` |

`hold` 时客户端按下 / 松开同一快捷键；`tap` 时在开始和结束各点一次同一快捷键。**两种模式在设备上都仍然是按住上键说话、松开结束，不是让设备上键改成点击。** 不支持用 `tap` 适配“开始和结束是不同快捷键”的输入法。

可用键名包括左右 Shift、F1–F12、字母数字及 `cmd` / `option` / `ctrl` / `shift` 组合。解析器能识别 Fn / 地球键不代表输入法实机可用，此项尚待验证。查询完整键名：

```bash
macos/.venv/bin/python3 macos/mac_voice.py --list-keys
```

默认保留 `output_substr` 与端口。`device_ip` 一般留空；只有自动发现受阻时再填**路由器里查到的设备 IPv4 地址**，不是 Mac 的地址、配网热点地址或 `127.0.0.1`。它不能绕过路由器客户端隔离。

保存后检查 JSON 语法：

```bash
macos/.venv/bin/python3 -m json.tool macos/voice-config.json
```

应打印格式化配置且无报错。随后重启客户端，核对启动窗口打印的快捷键 / 模式。自建启动命令中的 `--key`、`--mode` 等参数会覆盖 JSON；下键固定为 Enter，没有 `down_key` 配置项。

<a id="start"></a>
## 8. 诊断、启动和连接设备

先关闭自己之前打开的客户端或诊断窗口，再运行：

```bash
bash macos/diagnose.command
```

| 诊断结果 | 意味着什么 | 还需要确认什么 |
| --- | --- | --- |
| 按键权限 OK | 系统允许当前宿主发送按键 | 输入法是否匹配快捷键、焦点是否正确 |
| UDP 33333 可绑定 | 检查当时端口可用 | 本地网络权限、路由器互通、设备是否在线 |
| 可用虚拟声卡：BlackHole 2ch | 找到了支持所需格式的双向声卡 | 输入法是否读取它、真实声音是否到达 |
| FAIL | 对应检查失败 | 按提示修复，不把其他 OK 当作全部通过 |

诊断不录音、不注入按键，也不检查输入法语音设置。**全部 OK 也不等于已经可以出字。**

诊断结束后再启动：

```bash
bash macos/start.command
```

保持这个终端运行，让设备联网并进入“随身语音 / AI语音”，等待电脑连接、音频和快捷键就绪。首次出现局域网访问提示时按第 6 节处理。

只运行一个客户端。诊断和正常客户端都会尝试绑定 UDP 33333，不能同时运行。需要查看声卡时可用：

```bash
macos/.venv/bin/python3 macos/mac_voice.py --list-devices
```

列表应包含 BlackHole 2ch 的输入、输出通道。真实传音时客户端把 16 kHz 单声道复制到左右声道，由 CoreAudio 做必要转换，不用为了教程手工修改全局采样率。

<a id="first-test"></a>
## 9. 在文本编辑中完成首次出字验收

新建“文本编辑”文稿，切换到已经验证过的输入法，点出光标。先在这里测试，下键 Enter 在聊天窗口可能直接发送消息。

1. 按住设备上键，等待输入法语音界面出现，再对设备说“这是 Mac 语音测试”。
2. 松开上键，确认语音结束，文稿出现对应文字。文字出现后再操作。
3. 短按设备下键，确认只插入一次换行。
4. 重复三次，分别确认开始、结束、声音和文字，避免只凭一次打开面板判断成功。
5. 正常松键后退出设备语音应用，再重新进入，确认可以重新连接和说话。

**完成标准：真实说话能出字、松开能结束、下键只换行一次。** `audioReady` / `hotkeyReady` 仅表示客户端通道和权限就绪，不能证明识别服务或音源设置正确。

### 面板能打开，但没有字：先分清声音与识别

打开“系统设置 → 声音 → 输入”，选 BlackHole 2ch（会改变默认麦克风），观察输入电平；保持客户端运行，按住设备上键并对设备说话。电平应随设备收音变化，周围安静时不应只是 Mac 自带麦克风在动。

需要进一步确认时，用 QuickTime Player 的“新建音频录制”，在录音选项中选 BlackHole 2ch，录一小段设备测试语音并停止。保持系统输出为耳机或扬声器，再回放本地录音。录音 App 需要麦克风权限，录音可以只保留在自己电脑。

- **完全无声：** 查设备传音、客户端错误、BlackHole 音源与电平；不要先反复改识别语言。
- **断续、太小或失真：** 查设备收音距离和局域网，再核对虚拟声卡输入 / 输出音量；不要盲目加增益。
- **录音清楚但输入法不出字：** 查输入法自己的音源、语言、登录、网络服务及快捷键模式，再用实体麦克风复测。

检查后把系统输入恢复到你实际需要的音源。首次与日常使用都不要求公开上传录音。

<a id="daily"></a>
## 10. 日常使用与停止

安装只需做一次。以后接入同一局域网，确认输入法和 BlackHole 音源，运行 `bash macos/start.command`，设备进入随身语音，点选目标输入框即可。

- 上键按住说话，松开结束；下键在识别完成后发送 Enter；长按确定退出设备应用。
- 用终端 **Ctrl+C** 停止客户端。重配 JSON、改权限、换网络或重新诊断前先停止旧实例。
- 会议或视频通话改用实体麦克风后，回来使用设备时核对输入法 / 系统输入是否需要切回 BlackHole。
- 客户端不会自动恢复你手动改过的系统音源。停止后按需恢复原麦克风，正常扬声器无需改动。
- 电脑休眠、设备断网后先等链路恢复，再重新按键开始。传音中断超过约 2.5 秒会结束当前语音，不要继续对着已结束的面板说话。

正常退出和异常处理会尝试释放快捷键。若系统撤销权限、强杀进程或电脑异常中止，清理可能无法执行；松开设备键，手动按下再松开对应实体快捷键，必要时关闭输入法语音面板，再排查。

<a id="troubleshooting"></a>
## 11. 常见故障排查

| 现象 | 按顺序处理 |
| --- | --- |
| 找不到 Python / 架构错误 | 回到第 2 节，分别检查系统与 Python 架构（Intel 为 x86_64、Apple 芯片为 arm64）、版本为 3.11+，并确认没有使用 Rosetta；重开终端 |
| `No such file` / 找不到共享模块 | 回到项目根目录，确认完整保留 macos 和 windows；不要单独移动脚本 |
| 提示先运行 install.command | 在根目录运行 `bash macos/install.command`，查看首条错误；移动目录后检查虚拟环境是否失效 |
| 缺少 `sounddevice` / `Quartz` | 用安装脚本装到 macos/.venv；启动用 start.command，不要切到另一个 Python |
| 没找到 BlackHole / 音频格式不可用 | 核对官方 2ch 安装和重启，关闭占用音频的 App，重开客户端；用 --list-devices 核对双向通道，保留具体错误 |
| 辅助功能已开仍未就绪 | 核对授权的是实际启动宿主，完全退出并重开；换终端后重新核对，不靠重装声卡解决按键权限 |
| `Address already in use` / UDP 绑定失败 | 停止自己的旧客户端和诊断，只运行一个；可用下方命令查占用 |
| 设备一直等待电脑 | 同一可互通局域网、设备已进入应用、客户端在运行；检查本地网络权限、VPN 的局域网路由、访客隔离与程序防火墙 |
| 多网卡 / 广播发现失败 | 路由器中核对设备 IPv4，在 device_ip 填该地址后重启；地址变化后更新，不能绕过客户端隔离 |
| 就绪但语音面板打不开 | 文稿有光标且已切到正确输入法；实体键盘先试；核对辅助功能、实际键名、系统快捷键冲突与 Fn/F键行为 |
| 松开仍录音 / 结束后又开始 | 核对 hold/tap，两边必须一致；tap 必须是同一个快捷键开始和结束；再检查客户端错误与网络 |
| 有语音面板但不出字 | 按第 9 节查 BlackHole 电平和本地录音；区分音频未到达与输入法识别失败 |
| 改配置不生效 | 编辑的是 macos/voice-config.json，校验 JSON；检查命令行覆盖，重启自己的客户端 |
| 下键没有换行 / 发到别处 | 先松上键、等文字出现，重新点选文稿光标；Enter 的实际行为由当前获得焦点的 App 决定 |
| 设备提示检查 VB-CABLE | 现有固件部分文案针对 Windows；Mac 实际检查 BlackHole、辅助功能和输入法设置 |

只读查端口占用：

```bash
lsof -nP -iUDP:33333
```

核对进程名、PID 和对应终端，只关闭确认属于自己的客户端。不要批量结束 Python 或无关程序。

## 12. 反馈信息与验证边界

反馈到仓库 Issues 前，按事实填写：

```text
Mac 芯片 / macOS 版本：
源码提交或下载日期：
python3 --version / platform.machine()：
微信输入法名称、版本和语音音源：
实体键盘的实际快捷键 / hold 或 tap / 能否正常出字：
BlackHole 2ch 是否可见 / 输入电平是否随设备变化：
诊断失败项和脱敏后的首条错误：
设备提示 / 是否只有一个客户端：
文本编辑中：上键开始、松开结束、出字、下键换行各是否通过：
复现步骤：
```

隐去用户名、个人路径、局域网 IP、Wi-Fi 密码、账号和密钥。个人配置和录音不提交到 GitHub；不需要为了排错公开真实语音。

现有记录：2026-10-17 在 Windows 原生 Python 3.11 上通过 24 项 Mac 客户端测试与 15 项原 Windows 测试。Mac 测试使用模拟 Quartz、模拟声卡及真实回环 UDP，覆盖权限、按键清理、重复包、断线、音频超时、双声道、队列、广播和收发流程，没有采集真实录音或注入真实桌面按键。

**这些结果不代替 Intel 或 Apple 芯片 Mac 上的安装、系统权限、CoreAudio 和输入法真实出字验收。** GitHub Actions 的 Mac 运行器用于检查依赖和自动测试，结果以对应提交为准；CI 通过也不是微信输入法实机兼容认证。

开发者从项目根目录运行已有测试：

```bash
python3 -m unittest discover -s macos/tests -p 'test_*.py' -v
python3 -m unittest discover -s windows/tests -p 'test_*.py' -v
```

官方参考：[Python](https://www.python.org/downloads/macos/)、[BlackHole 安装与路由](https://github.com/ExistentialAudio/BlackHole)、[微信输入法](https://z.weixin.qq.com/)、[Apple 辅助功能权限](https://support.apple.com/guide/mac-help/allow-accessibility-apps-to-access-your-mac-mh43185/mac)、[Apple 麦克风权限](https://support.apple.com/guide/mac-help/control-access-to-the-microphone-on-mac-mchla1b1e1fe/mac)、[PyObjC](https://pyobjc.readthedocs.io/)。依赖按各自许可证单独安装，驱动和输入法不随本项目分发。
