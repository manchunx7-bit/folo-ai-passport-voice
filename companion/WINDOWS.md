# AI Passport Windows 使用文档

Windows 移植（仅兼容 Windows 10 与 Windows 11）三通道：

| 通道 | 适用 | 连接方式 |
|---|---|---|
| `"wifi"` | **passport-os 固件用这个** | 同一局域网 UDP 33333；PC 每秒广播 beacon，设备听到后主动连上 |
| `"ble"`（缺省） | 有蓝牙的 Win10/Win11 | 设备 BLE 广播 "AI Passport"，PC bleak winrt 后端直连 |
| `"usb"` | 任意电脑（无蓝牙的场合） | USB 线直连（设备 USB-Serial-JTAG），PC 自动扫描端口 |

通道由 `companion/config.local.json` 的 `channel` 字段手动选择，不自动判断
（`ble` / `usb` 需设备侧对应 `mode ble` / `mode usb`，见下；**`wifi` 无需设备侧切换**）。

> ⚠️ **本机实际的 `passport-os` 固件已整体移除 BLE**（`ble_audio.c` 与 NimBLE 全删，
> 见 `passport-os/docs/voice-udp-channel.md`），语音只走 `wifi`。
> 另外注意**配置有两份、互不通用**：源码运行读 `companion/config.local.json`；
> 早期 `dist/` 打包版（PyInstaller frozen）读 `%APPDATA%\AI Passport\config.local.json`。
> 换了运行方式就要在新位置重新配一次。

## 环境要求

- Windows 10 1803 及以上（建议 21H2）/ Windows 11（bleak winrt 后端要求）
- Python 3.10+；首次运行防火墙弹窗需点「允许」（专用网络）

## 安装

```bat
python -m venv companion\.venv
companion\.venv\Scripts\pip install -r companion\requirements.txt
```

`requirements.txt` 用 PEP 508 平台标记按系统自动选包：
Windows 装 `bleak[winrt]`（BLE 通道）+ `pywin32`（注入）；
`pyserial` 全平台安装（USB 通道数据收发）。

> ⚠️ **`websockets` 版本是硬要求，别用系统里已有的凑合**：`asr_client.py:229`
> 用的是 `additional_headers`，这是 websockets **15+** 的参数名（14.x 叫
> `extra_headers`），仓库锁 `websockets==17.0.1`。系统 Python 里若是 14.x，
> 链路能连上但**一按说话就抛 `TypeError`**。所以按上面装进 venv。
>
> 另：`pystray` / `ttkbootstrap` 只有向导 GUI（`fre_app.py`）要用，
> 只跑 `relay.py` 命令行的话不需要。

## 配置（config.local.json）

```json
{
  "volcano_api_key": "你的火山 API Key",
  "channel": "ble",
  "udp_port": "",
  "usb_port": "",
  "inject_focus_delay": 2.0
}
```

| 键 | 缺省 | 说明 |
|---|---|---|
| `channel` | `"ble"` | `"ble"` / `"usb"` / `"wifi"`；无效值 relay 报错退出。**passport-os 固件填 `"wifi"`** |
| `udp_port` | `""` | Wi-Fi 通道：UDP 端口；留空 = 33333（与固件 `VOICE_UDP_PORT` 一致） |
| `usb_port` | `""` | USB 通道：串口路径；留空 = 自动扫描（VID 0x303A / PID 0x1001）；多设备时填指定端口（如 `COM5`） |
| `inject_focus_delay` | 2.0 | 注入前等用户切换到目标窗口的秒数 |

密钥安全：`config.local.json` 已被 git 忽略；`volcano_api_key` 也可用环境变量
`VOLCANO_API_KEY` 提供。**绝不**写入任何配置文件。

## 使用

```bat
companion\.venv\Scripts\python companion\relay.py
```

### 通道：BLE（有蓝牙电脑）

设备开机（屏幕显示 BLE 状态）→ 运行 relay → 首次连接会弹系统配对窗口，
确认即可。其余流程与 Mac 一致：按住设备 ● 说话，设备屏幕实时预览转写，
松手后定稿文本注入当前聚焦输入框。

### 通道：USB（有线）

任何电脑都能用 USB 线直连（ESP32-C3 原生 USB-Serial-JTAG 口）。USB 供电，
此模式下**蓝牙保持开启**，数据传输走 USB 线。

首次启用（设备侧，任意模式的控制台或经 USB 的 relay `!mode usb`）：

```
mode usb                        # 切 USB 模式 = 写 NVS + 重启(约 1-2s)
```

设备重启后插 USB 线连电脑。PC 侧把 `channel` 改为 `"usb"` 后运行 relay：
自动扫描 ESP32-C3 串口（多设备时打印清单取第一个，可用 `usb_port` 指定），
握手成功后与 BLE 流程一致：按住设备 ● 说话 → 设备屏幕实时预览 → 松手
定稿注入当前输入框。

**USB 模式没有设备控制台**（REPL 与数据通道独占串口，启动时已跳过）。
状态查询经 relay 的 stdin 交互（`!<命令>` 下行 SYS 命令面，与设备 console
同一批命令）：

```
!mode ble                       # 切回 BLE 模式(立即生效,控制台重启后恢复)
!log                            # 取回设备日志环(esp_log 已重定向 RAM 环)
!st                             # 会话状态
!reboot / !factory              # 重启 / 恢复出厂
```

> 注意：`mode usb` 切换 = 设备重启（运行时摘除 REPL 阻塞读不安全，设计上
> 杜绝）；从 USB 切走（`!mode ble`）**立即生效无需重启**（REPL 控制台
> 仍缺席，重启后恢复）。USB 模式下设备日志不再实时上屏，进 4KB RAM 环，
> 经 `!log` 取回。

### 通道：Wi-Fi（UDP）—— passport-os 固件用这个

设备与电脑在同一局域网即可，**无需配对、无需插线**。协议契约见
`passport-os/docs/voice-udp-channel.md`。

**步骤**

1. **先确认同网段**——这是最容易卡住的一步。设备语音界面顶栏会显示它的地址，
   在电脑上 `ping` 必须通：

   ```bat
   ipconfig | findstr /i "IPv4"      :: 看本机在哪个网段
   ping 192.168.5.4                  :: 换成设备实际 IP
   ```

   电脑的联网网卡若是「媒体已断开连接」，或 IPv4 拿到 `169.254.x.x`（APIPA），
   说明它根本没进网络，先解决这个再谈连接。

2. **电脑侧配置** `companion/config.local.json`：

   ```json
   { "channel": "wifi", "udp_port": "", "volcano_api_key": "你的火山 API Key" }
   ```

3. **运行**：

   ```bat
   companion\.venv\Scripts\python companion\relay.py
   ```

   首次运行 Windows 会弹防火墙窗口，**必须点「允许」**（专用网络），放行入站 UDP 33333。

**正常日志**

```
[udp] 服务端模式: 监听 0.0.0.0:33333, 每 1s 广播 beacon
[relay] 已连接, 订阅 EVENT/AUDIO
```

设备侧对应出现 `companion beacon 来自 192.168.x.x → link up`。

**连不上的排查顺序**

| 症状 | 原因 |
|---|---|
| 设备一直「与电脑断开，正在重连」 | PC 没在跑 relay，或 `channel` 仍是 `ble` |
| relay 起得来但永远等不到设备 | 不同网段 / 路由器开了 AP 隔离 / 防火墙拦了入站 UDP |
| 同网段、防火墙也放行了仍不通 | 代理软件（Clash / Mihomo 等 **TUN 模式**）抢占默认路由，广播被吞 → 先退出它再试 |

**已知不对称（v1）**：设备侧 beacon 一恢复就自动重连；电脑侧**不自动重连** ——
设备静默 6 s 即判定断开、收束会话并退出，需**重新运行 relay.py**。
详见 `passport-os/docs/voice-udp-channel.md` §4.1。

## 注入焦点提示（Windows）

注入 = 剪贴板（CF_UNICODETEXT）+ SendInput Ctrl+V（实现为 pywin32
`keybd_event`，Win10/11 均有效）。relay 收到定稿后：
1. 先检查前台窗口——若还是 relay 自己的控制台（标题含 python/cmd/PowerShell
   等）→ **拒绝注入**并给出指引；
2. 等 `inject_focus_delay` 秒（缺省 2s），此时请点击目标输入窗口；
3. 复查前台——仍停在控制台则中止（不往控制台乱粘）。

不自动置顶/抢焦点（Windows 前台锁定使 SetForegroundWindow 不可靠）。
独立测试注入：

```bat
companion\.venv\Scripts\python companion\inject_win.py "你好世界 123"
companion\.venv\Scripts\python companion\inject_win.py "你好" --dry-run
```

## 验收清单（Windows 真机，NOT RUN）

> 以下项目需 Windows 机器实机验证，本仓库开发环境（macOS）无法执行。
> 固件侧验证同样未跑（本机无 ESP-IDF 构建环境）。

- [ ] **Win10 21H2 + Win11 各一台**：BLE 通道全流程（配对弹窗 → 录音 → 转写 → 注入）
- [ ] **无蓝牙电脑**：USB 通道可用；`st` 显示 mode: USB
- [ ] **USB 通道**：`mode usb`（重启）→ relay 自动扫描端口 → 握手 → 全流程；`!mode` / `!log` / `!st` 往返；拔线 → relay 报断连收束
- [ ] **USB 串口枚举**：Windows COM 自动识别（VID/PID 匹配）；多设备时 `usb_port` 指定生效；端口被占用给可理解报错
- [ ] **USB 射频保持**：USB 模式蓝牙广播保持；数据仍走 USB（`st` 链路 = USB）
- [ ] **USB 音频流**：32KB/s 持续流 100ms 写超时不异常掉帧（瓶颈在主机读侧）
- [ ] **模式切换**：`mode ble` ↔ `mode usb` 往返，NimBLE controller re-sync 后广播自动恢复（固件唯一硬依赖）
- [ ] **配对弹窗**：Windows 首次 BLE 配对 UX（bleak winrt 触发系统弹窗）
- [ ] **注入焦点**：真实粘贴中文/英文到记事本；焦点护栏拦截 relay 控制台；focus_delay 生效
- [ ] **30 分钟内存**：长跑 BLE/USB 通道无内存增长（relay 侧 + 固件 `st` 堆水位）
- [ ] **固件侧**：`idf.py build` 双模式构建、`esp_driver_usb_serial_jtag` REQUIRES、USB 模式堆栈/功耗实测

## 单元测试（Mac 可跑）

```bash
companion/.venv/bin/python companion/tests/test_relay.py
companion/.venv/bin/python companion/tests/test_serial_frame.py
companion/.venv/bin/python companion/tests/test_serial_transport.py
companion/.venv/bin/python companion/tests/test_serial_relay.py
companion/.venv/bin/python companion/tests/test_inject_win.py
```
