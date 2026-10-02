# AI Passport 随身语音

按住设备上键，对着设备说话，松开后让文字出现在电脑输入框里。

把 **FoloToy AI Passport** 变成 Windows 电脑的无线麦克风和语音输入快捷键。
设备负责收音，电脑程序负责传音和快捷键，识别由微信输入法等软件完成，无需配置 ASR API Key。

**0.2.0-rc.1 · Windows 社区测试版**

**2026-10-02 实测更新：维护者使用本次 GitHub 完整包 `0.2.0-rc.1` 的固件和电脑程序，
确认微信输入法能正常出字，当前识别准确。** 该结果来自维护者的一套实际设备与电脑环境，
尚未测量字错误率或验证其他电脑的兼容性。详见 [验证记录](docs/VALIDATION.md)。

## 下载与开始使用

1. 到 [下载页面](https://github.com/manchunx7-bit/folo-ai-passport-voice/releases/tag/v0.2.0-rc.1) 下载 **`ai-passport-voice-0.2.0-rc.1.zip`** 并全部解压。
   GitHub 自动生成的 `Source code (zip)` 只有源码，不含现成 EXE 和固件。
2. 按 [随身语音完整教程](docs/QUICKSTART.zh-CN.md) 配好同一局域网、VB-CABLE 和微信输入法。
3. 双击解压目录内的 **`windows/start.cmd`**，进入设备“随身语音”，确认显示“按住上键说话”。
4. 在记事本中点击输入区，按住设备上键说一句话，松开，检查是否出字。

日常启动就是 `windows/start.cmd`；使用下载包不需要安装 Python。
固件安装可参考 [口袋百宝箱玩法页面](https://ai-passport.folotoy.cn/plays/438/?v=1061-7)，
注意与本版电脑端配套。本地烧录与备份见 [固件附录](docs/FIRMWARE.zh-CN.md)。

```text
AI Passport 麦克风 → 同一局域网 Wi-Fi → Windows 转发器
                                      ├─ 语音快捷键 → 微信输入法
                                      └─ VB-CABLE → 输入法麦克风 → 文字
```

## 准备什么

| 项目 | 要求 |
| --- | --- |
| 设备 | FoloToy AI Passport：ESP32-C3 / 8 MB Flash / ST7789P3 240×320 / ES8311，同款引脚及外设 |
| 固件 | 本版配套 Passport OS 整机固件，会替换设备当前玩法；不是单独安装的插件 |
| 电脑 | Windows 10/11 x64，安装微信输入法和 VB-CABLE 虚拟声卡 |
| 网络 | 设备接入 2.4 GHz Wi-Fi，电脑与设备在可互通的同一局域网；电脑可用网线或 5 GHz Wi-Fi |

USB 用于供电和烧录，本功能通过 Wi-Fi 传音；不需要蓝牙配对。
macOS、Linux 和 ARM Windows 不在当前发布包的验收范围。

## 使用说明

- [从下载到第一次出字](docs/QUICKSTART.zh-CN.md)：启动文件、配网、虚拟声卡、输入法快捷键、逐步验收。
- [故障排查](docs/TROUBLESHOOTING.zh-CN.md)：连不上、麦克风通道未就绪、没声音、出字不准。
- [固件安装与恢复](docs/FIRMWARE.zh-CN.md)：需要本地烧录时阅读。
- [源码与构建](docs/DEVELOPMENT.zh-CN.md)：固件编译、Windows 源码运行和 EXE 打包。
- [发布说明](docs/RELEASE_NOTES.zh-CN.md) / [验证记录](docs/VALIDATION.md)：本版变化和尚未完成的验证。

“电脑已连接”只说明收到了心跳；“就绪”也不代表输入法已经选对麦克风。
教程会分别验证网络、音频和实际出字。若你的环境出现错字较多，可按教程检查 CABLE Output 录音和输入法音源。

## 源码与下载包

| 路径 | 内容 |
| --- | --- |
| `windows/` | 精简转发器、启动/诊断入口、配置示例及测试；EXE 在下载包中 |
| `source/firmware/` | 本版对应的完整 Passport OS 源码，随身语音位于 `main/apps/voice/` |
| `tools/` | 固件校验、备份、烧录和测试；烧录脚本默认只预览 |
| `docs/` | 中文教程、排错、开发与验证说明 |
| `firmware/` | 下载包内含三份固件；Git 仓库保留获取说明 |
| `licenses/` | 第三方许可文本 |
| `manifest.json` | 版本、硬件型号、固件地址和哈希 |

固件和电脑端应使用同一版。旧 ASR / Agent 代码保留在 Git 历史，当前主分支只维护这里的随身语音方案。
旧版下载移入维护者草稿；请使用上方明确标注 `0.2.0-rc.1` 的下载入口。

## 网络与隐私

局域网语音协议目前没有加密或身份认证，仅用于信任的局域网，不要做公网端口映射。
不用时退出电脑转发器。输入法是否联网、是否登录以及如何处理语音，由所选输入法决定。
发布包不含个人配置、录音或设备 Flash 备份。反馈问题前请删除日志中的姓名、IP、Wi-Fi 信息和密钥。

## 开源与致谢

代码沿用 MIT 许可，保留上游版权声明，见 [LICENSE](LICENSE) 和 [第三方说明](THIRD_PARTY_NOTICES.md)。
固件基于 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 和 Passport OS，
电脑端基于 [zhaohuaxiaoy/folo-ai-passport-voice](https://github.com/zhaohuaxiaoy/folo-ai-passport-voice)。
本版本所需固件及电脑端源码均已包含在本仓库，不需要访问其他私有工程。
VB-CABLE、微信输入法不随包分发，也不属于本项目。

问题反馈请用本仓库的 [Issues](https://github.com/manchunx7-bit/folo-ai-passport-voice/issues/new/choose)，附系统版本、设备状态、诊断结果及复现步骤。
