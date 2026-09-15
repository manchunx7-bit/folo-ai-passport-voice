# 热键+麦克风转发器(hotkey_forwarder.py)

把 AI Passport 设备变成电脑的**无线热键 + 无线麦克风**:按住设备说话,
电脑上正在使用的语音输入法直接出字。不依赖任何 ASR 密钥/账号 —— 识别由
输入法自己完成(讯飞/搜狗/Windows Win+H 均可)。

```
设备「AI语音」按住UP说话 ──UDP──► hotkey_forwarder.py
                                    ├─ 按键事件 → 注入右 Shift(调起输入法语音)
                                    └─ 麦克风音频 → 虚拟声卡 → 输入法当麦克风收音
```

## 首次安装(一次性)

1. **安装虚拟声卡**(麦克风路由必需):到 https://vb-audio.com/Cable/ 下载
   `VB-CABLE_Driver`,解压后右键管理员运行 `VBCABLE_Setup_x64.exe`,安装完重启。
2. 安装依赖(companion 目录):
   ```
   companion\.venv\Scripts\python.exe -m pip install sounddevice
   ```
3. 验证设备已识别: `companion\.venv\Scripts\python.exe hotkey_forwarder.py --list-devices`
   列表里出现 **CABLE Input**(播放)即虚拟声卡就绪。

## 输入法配置(一次性)

1. 语音输入法的快捷键设置为**右 Shift**(或用 --key 指定的其它键);
2. 输入法的**语音识别麦克风**选择 **CABLE Output (VB-Audio Virtual Cable)**;
3. 若输入法支持"按住说话"模式选 hold 转发模式,默认 tap(点击式)即可。

## 日常使用

### 使用设备麦克风无线收音（当前模式）

1. 确认 Windows 已安装 VB-Cable，输入法的麦克风选择 `CABLE Output`；
2. 双击 `companion\start_hotkey_only.cmd`。入口会把设备上传的音频写进
   `CABLE Input`，不依赖项目中已经失效的旧 `.venv`；
3. 设备:进入「AI语音」应用 → 自动连接(几秒内);
4. 按下设备 **UP** 时点击一次右 Shift，松开时再点击一次右 Shift，结束输入法
   的“正在识别”状态并提交文字；设备 **DOWN** 会注入一次 Enter，用于确认或
   发送当前输入框内容。

转发器必须保持运行；它与 `relay.py` 二选一，不能同时占用 UDP 33333。

### 改用电脑自己的麦克风

启动时添加 `--no-audio`；此时硬件只发送热键，输入法使用 Windows 当前选择的
实体麦克风。

## 命令行参数

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--key` | right-shift | 注入的热键(right-shift/left-shift/right-ctrl/right-alt/f6/f9/f10) |
| `--mode` | tap（当前配置） | tap=按下和松开时各点击一次热键;hold=设备按住期间热键保持按下 |
| `--port` | 33333 | UDP 端口(与固件契约一致,勿改) |
| `--output-substr` | CABLE Input | 虚拟声卡播放设备名匹配子串 |
| `--no-audio` | 关 | 只转发热键 |
| `--list-devices` | - | 列出本机声卡后退出 |

## 自测

不触碰真实桌面的映射单测：

```
%LocalAppData%\Programs\Python\Python311\python.exe -m unittest companion\tests\test_hotkey_forwarder_unit.py -v
```

它会验证 UP → 右 Shift 按下/抬起、DOWN → 单次 Enter，以及 `--no-audio`
收到设备音频包时不会崩溃。

## 常见问题

- **没有找到虚拟声卡**:VB-Cable 未安装/未重启;`--list-devices` 确认。
- **设备连不上(OFFLINE)**:电脑没跑本程序 / 不同网段 / 路由器 AP 隔离 /
  防火墙拦了 UDP 33333 入站(首次运行放行)。
- **输入法没反应**:确认输入法语音快捷键 = 右 Shift;麦克风选了 CABLE Output;
  tap/hold 模式与输入法的"点击开始/按住说话"设置匹配。
- **与 relay.py 冲突**:两者都占 UDP 33333,同一时间只能跑一个。
