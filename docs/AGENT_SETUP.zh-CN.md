# 让 Agent 配好随身语音

本流程适用于 Codex、Claude Code、WorkBuddy 等能访问**待配置电脑的本地文件与终端**的 Agent。产品名字不是能力保证：远端任务只能提供指导；有系统界面工具时才可协助点击设置。普通使用不用编译设备固件。

## 1. 给用户的复制指令

优先直接把下面的完整指令交给能访问这台电脑本地文件与终端的 Agent。它会先检查当前工作目录；如果项目不存在，先从公开地址下载或克隆，再进入项目根目录读取规范并继续。不要让用户为了让 Agent 配置而先手动下载源码：

`https://github.com/manchunx7-bit/folo-ai-passport-voice`

```text
请帮我配置 AI Passport 随身语音，直到实际说话出字。
项目地址：https://github.com/manchunx7-bit/folo-ai-passport-voice
如果当前电脑没有这个项目，请先下载或克隆它，进入项目根目录后继续，不要要求我先手动下载原项目。
先阅读 AGENTS.md、docs/AGENT_SETUP.zh-CN.md 和当前平台教程。
识别操作系统、安装包和已有配置，执行本地检查并修复可处理的缺项；
已有配置先备份到 .setup/，不要覆盖有效设置或重复安装可用组件。
只询问无法检测的信息，尤其是输入法实际语音快捷键与 hold/tap 模式。
按手册选择真实存在的命令，旧版 EXE 不支持的新参数不要猜。
需要安装驱动、系统授权或重启时，说明具体操作与影响，记录进度后继续。
保持系统声音输出为自己的扬声器/耳机，只启动一个转发器。
最后引导我在空白文稿里亲自说话，确认开始、松开结束、出字和下键换行。
不要自动录音、发送环境声音或在重要聊天窗口注入按键。
把进度记到本地 .setup/progress.json；区分自动检查、用户确认和未验证，
给出日常启动方式。不要上传个人配置、录音或账号信息。
```

如果 Agent 无法访问网络或没有创建目录的权限，只询问用户完成下载/授权这一项，再从项目根目录继续；不要把“项目未下载”误报成“配置失败”。Windows 普通用户优先使用 README 链接的 EXE 应用包，Mac 使用完整源码。旧包没有本手册时，读取其 `docs/CODEX_SETUP.zh-CN.md`，或先从上面的仓库地址获取最新文档，不假定旧包包含新工具。

## 2. 分支选择与准备

| 检查项 | Windows 10/11 x64 | macOS 原生 arm64/x86_64 |
| --- | --- | --- |
| 必读教程 | [Windows 完整教程](QUICKSTART.zh-CN.md) | [Mac 完整教程](../macos/README.zh-CN.md) |
| 首选程序 | 发布包内 `windows/AI-Passport-Voice.exe` | 原生 arm64 或 x86_64 Python 3.11+ 与源码 |
| 缺少程序 | 从 README 的 Releases 获取应用包，不要求普通用户安装 Python | 按教程检查 Python，再运行 `bash macos/install.command` |
| 虚拟声卡出口 | CABLE Input | BlackHole 2ch |
| 输入法麦克风 | CABLE Output | BlackHole 2ch |
| 当前验证范围 | 按 [验证记录](VALIDATION.md) 判断 | 源码测试版，真实 Mac 出字仍待验收；支持 Intel 原生 x86_64，Rosetta 不支持 |

先确认用户设备已有“随身语音/AI语音”应用，能打开则跳过刷机。设备使用 2.4 GHz Wi-Fi，电脑与设备在同一可互通局域网；USB 不是麦克风通道，不需蓝牙配对。版本关系见 [固件说明](FIRMWARE.zh-CN.md)。

Agent 先识别自己是否就在用户电脑上、能否操作系统界面。没有界面权限时给出教程中的具体路径，让用户完成对应一项后再继续，不将无法检查的设置记成通过。

## 3. 运行诊断，按结果配置

所有命令从项目根目录执行。不要并行启动两个客户端。

Windows 已有 EXE，无需 Python：

```powershell
.\windows\AI-Passport-Voice.exe --list-keys
.\windows\AI-Passport-Voice.exe --list-devices
.\windows\AI-Passport-Voice.exe --diagnose
```

旧发布 EXE 使用它自己内置的诊断，端口检查可能固定为 33333；新源码工具不会自动更新旧 EXE。保留默认端口，不能用换端口掩盖占用。源码用户已有 Python 时，可用新增的结构化检查：

```powershell
python tools/setup_doctor.py --json
```

Mac 完成依赖安装后：

```bash
macos/.venv/bin/python3 tools/setup_doctor.py --json
macos/.venv/bin/python3 macos/mac_voice.py --diagnose
```

诊断不录音、不模拟按键、不改音频设置。`setup_doctor.py` 只使用 Python 标准库启动；缺少声卡依赖时也返回 JSON，而不是开始安装。可用 `--config PATH` 检查指定配置；未提供时选择当前平台配置，Windows 兼容旧 `config.local.json`。未创建个人配置时只检查默认值，并列出需要创建配置的动作。

结构化报告 `schema_version` 为 1，包含检查时间、`checks`、`pending_user_actions` 和始终为 `NOT_RUN` 的 `actual_typing`。`checks` 的每项有 `id/status/detail/next_action`。退出码 0 表示本地检查通过、可以继续用户测试，1 表示有缺项/失败；2 表示命令参数错误（由命令行解析器报告）。该报告不用于自动授予“实际出字通过”。

| 检查结果 | Agent 下一步 |
| --- | --- |
| `runtime` 失败 | 修正系统/运行架构或 Python，不继续启动不兼容程序 |
| `config` 失败 | 定位 JSON、端口、键名或字段类型问题；保留原文件再修改 |
| `udp_port` 被占用 | 查归属进程，确认是否用户自己的旧转发器；不结束无关程序 |
| `audio` 失败 | 区分依赖缺失、声卡缺失和格式不可用，按平台教程处理 |
| `keyboard_permission` 失败 | Mac 授权实际运行宿主的辅助功能，重开后复检 |
| 全部本地检查通过 | 继续输入法设置和实际出字验收，不宣称已能使用 |

进程排查：Windows 用 `Get-NetUDPEndpoint -LocalPort 33333` 查 PID，再用 `Get-Process -Id PID` 核对；Mac 用 `lsof -nP -iUDP:33333`。实际端口不同则使用配置值。先让用户关闭自己的旧窗口，再重测。

## 4. 生成配置并核对输入法

配置文件位于 `windows/voice-config.json` 或 `macos/voice-config.json`。保留未知的已有字段；不要把示例当成用户的真实快捷键。

| 字段 | 填写依据 |
| --- | --- |
| `hotkey_key` | 用户先用实体键盘验证成功的语音快捷键；通过 `--list-keys` 核对名称 |
| `hotkey_mode` | 按住/松开为 `hold`，同一键点击开始/结束为 `tap` |
| `output_substr` | Windows 为 CABLE Input；Mac 为 BlackHole 2ch |
| `udp_port` | 默认 33333；设备和电脑必须匹配，不为绕过占用随意修改 |
| `device_ip` | 通常留空自动发现；仅在已确认设备地址时填写，不猜地址 |

Windows：系统默认输入按教程选择 CABLE Output，输入法使用同一音源。Mac：输入法选择 BlackHole 2ch；若只跟随系统输入，才设置系统默认输入。说明这会影响会议软件；**系统默认输出始终保持自己的扬声器/耳机**。不直接改输入法的数据库或程序文件。

JSON 格式检查后，再运行诊断；配置改变后重启自己的客户端。命令行参数可覆盖配置，排错时同时核对启动命令。没有真实确认的输入法麦克风或快捷键必须保留待确认状态。

## 5. 启动与真实验收

Windows 双击 `windows/start.cmd`；Mac 运行 `bash macos/start.command`。保持窗口开着，设备打开随身语音。请用户打开记事本/文本编辑的空白文稿，把光标放入其中，亲自完成：

1. 按住上键，确认输入法进入语音状态，再说一句无敏感信息的测试句。
2. 松开上键，确认识别结束，文稿出现对应文字。
3. 文字稳定后短按下键，确认只换行一次。
4. 再做一次，退出设备应用再进入，确认能继续使用。

两种热键模式下，设备都是按住上键说话、松开结束。无字时按 [排错教程](TROUBLESHOOTING.zh-CN.md) 区分网络、按键、音频与输入法服务，不用 `--no-audio` 规避声卡故障。诊断不包含收音验证；可选的本地录音测试须先说明保存位置并征得用户同意。

## 6. 中断后继续与交付

Agent 在忽略的 `.setup/progress.json` 保存进度。该文件由 Agent 管理，不是客户端加载的配置，也不存在 `setup --resume` 命令。最小格式：

```json
{
  "schema_version": 1,
  "application": "voice",
  "platform": "windows",
  "updated_at": "实际检查时间（ISO 8601）",
  "steps": {
    "runtime": {"status": "PASS", "evidence": "实际系统与程序版本"},
    "input_method": {"status": "USER_ACTION", "evidence": "待实体键盘确认"},
    "typing": {"status": "NOT_RUN", "evidence": "尚未实际说话"}
  },
  "next_action": "核对输入法快捷键"
}
```

状态只能按证据更新：`PASS`、`FAIL`、`USER_ACTION`、`NOT_RUN`。记录测试是否出字，不保存测试句、音频、账号或网络密码。配置备份存 `.setup/config-backups/`，只在本机保留，勿覆盖前次备份。

用户重启后可直接说：“继续配置，读取 .setup/progress.json，重新诊断当前环境，保留有效配置，继续未完成的验收。”声卡重装、换网络、权限变动、快捷键变化后要复检对应步骤；旧进度不是当前可用性的证明。

最终交付：程序位置与启动方法、已自动检查的项目、用户确认的操作、真实出字结果、仍待验证的项目和下一步。没有真实出字就报告“电脑端检查完成，待实际说话验收”。只需给 Agent 脱敏摘要，不要求用户上传整个诊断报告。
