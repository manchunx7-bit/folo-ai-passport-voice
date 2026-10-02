# 源码与维护

## 源码布局

- `source/firmware/`：Passport OS ESP-IDF 工程，包含其他整机应用，随身语音在 `main/apps/voice/`。
- `windows/hotkey_forwarder.py`：快捷键/音频/就绪心跳；`udp_transport.py`：局域网传输；`adpcm.py`：音频解码。
- `windows/diagnose_voice.py`：环境诊断，不发送快捷键或录音。
- `tools/flash.py`：校验/备份/烧录，默认 dry-run。

使用本仓库 `v0.2.0-rc.1` 标签或同版本完整下载包中的源码；不要用旧版本的 HEAD 构建本版。
完整下载包提供 `SHA256SUMS.txt` 校验内容，不包含 `.git` 历史、个人配置、录音或设备 Flash 导出。
GitHub 自动生成的源码 ZIP 不含预编译固件和 EXE；普通用户请下载 Release 中的完整包。

## 固件编译

使用 Linux / WSL2 和 **ESP-IDF 5.5.3**，参考
[ESP-IDF ESP32-C3 官方安装说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/get-started/index.html)。
在已激活 IDF 的终端执行：

```bash
cd source/firmware
idf.py set-target esp32c3
idf.py build
bash tools/test-host.sh
```

第一次需要联网获取组件，版本由 `dependencies.lock` 固定。
生成的三个文件是 `build/bootloader/bootloader.bin`、`build/partition_table/partition-table.bin`、`build/passport-os.bin`。
编译结果受 IDF、编译器、路径、构建时间和版本字符串影响，不承诺逐字节等同于本包二进制。
硬件引脚见 `components/bsp/include/bsp_pins.h`；字体文件是子集，增加中文文案前要检查字形覆盖。

`build.sh` 自动以脚本所在目录为工程根；可用 `IDF_DIR` 指定 IDF 安装路径。
烧录地址和保护范围以本包 `tools/flash.py` 为准；新构建不能套用旧包的 manifest 哈希。

## Windows 源码验证与 EXE 构建

```powershell
cd windows
py -3.11 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-hotkey.txt
.\.venv\Scripts\python.exe -m unittest discover -s tests -p "test_*.py" -v
.\.venv\Scripts\python.exe diagnose_voice.py
```

创建 EXE（Windows x64、Python 3.11）：

```powershell
.\.venv\Scripts\python.exe -m pip install pyinstaller==6.20.0
.\.venv\Scripts\python.exe -m PyInstaller --noconfirm --clean --onefile --console --name AI-Passport-Voice --distpath . --workpath build --specpath build hotkey_forwarder.py
```

不要使用旧的完整 GUI/ASR 打包入口发布这个精简模式。声卡驱动仍需由用户从官网安装。
发布前检查 PyInstaller TOC，不得包含 `config.local.json`、`voice-config.json`、`.env` 或个人录音。
`voice-config.example.json` 只有通用示例，可以提交；实际配置文件应加入 `.gitignore`。

## 协议与状态

- UDP 33333，设备与 PC 同网；单 PC 只启一个转发器。本版本没有多人/多设备配对隔离。
- 设备发送 `voice.start`、音频块、`voice.end`；按住上键开始，松开结束。
- 音频为 16 kHz / 单声道，100ms 一块，ADPCM 载荷带序号；重复/迟到块丢弃。
- PC 每约 1 秒发送 `pc.status`，含 `audioReady`、`hotkeyReady`；固件约 3.5 秒未收到有效更新即停止放行录音。
- 音频 ready 代表虚拟声卡输出流已打开；hotkey ready 代表映射按键可解析。两者都不能自动证明输入法当前设置正确。
- 断链/采音超时/停止会释放热键；设备不发送 Enter、不清空文字，不触发旧 Agent 事件。

## 发布检查

1. 源码、电脑端单测、固件 host 测试和实际构建通过。
2. 在干净 Windows 环境安装驱动，使用发布 EXE，而不是开发机旧进程。
3. 同版本固件实机验证 Wi-Fi、就绪状态、按住/松开、断网重连、退出释放快捷键。
4. 通过 CABLE Output 录音与设备麦克风来源对照，再验证微信输入法真实出字。
5. 核对每个固件文件地址与大小，首刷/升级脚本备份及拒绝路径通过。
6. 生成哈希/第三方许可清单；检查源码与发布 ZIP，不含任何个人文件或完整 Flash 备份。
7. 新建 GitHub Release 时，端到端未完成的版本标为 **Pre-release**，附已验证/未验证范围。

本仓库的 CI 运行电脑端单测、烧录脚本测试和固件主机测试，不自动发布旧版 ASR / Agent 程序。
Release 由维护者核对完整包、源码、哈希及验证范围后发布；主机测试不替代硬件与输入法实测。
