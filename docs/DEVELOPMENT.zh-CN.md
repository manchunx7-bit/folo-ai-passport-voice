# 随身语音开发与测试

本仓库只包含本功能的代码。电脑配置分别看 [macOS 教程](../macos/README.zh-CN.md) 与 [Windows 教程](QUICKSTART.zh-CN.md)；设备固件到官网获取。本仓库不含整机入口、BSP、共享 UI 或其他应用，不提供整机 `idf.py build`。

## macOS 源码运行

Intel Mac 使用原生 x86_64 Python，Apple 芯片 Mac 使用原生 arm64 Python；两者均需 Python 3.11+，从项目根目录依次运行：

```bash
bash macos/install.command
bash macos/diagnose.command
bash macos/start.command
```

运行前按 Mac 教程完成 BlackHole 2ch、系统权限、输入法音源与快捷键设置。保留 `macos/`、`windows/` 的相对位置；诊断与客户端不能同时占用端口。此客户端仍为测试版，自动测试不代替实机出字验收。

## Windows 源码运行

在仓库根目录打开 PowerShell，准备 Python 3.11 x64：

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r windows\requirements-hotkey.txt
.\.venv\Scripts\python.exe windows\hotkey_forwarder.py --diagnose
.\.venv\Scripts\python.exe windows\hotkey_forwarder.py
```

先安装 VB-CABLE、按教程设置输入法和可信局域网。将 `windows/voice-config.example.json` 复制为 `windows/voice-config.json` 并核对路径与快捷键。不要同时运行源码版与 EXE 争用 UDP 33333。

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s windows\tests -p "test_*.py" -v
```

发布的 EXE 沿用已验证的 Python 3.11.8 / sounddevice 0.5.6 / PyInstaller 6.20.0 构建。它与这里的 Windows 源码保持一致，未加入其他整机源码。驱动和输入法不打包。

## 设备纯逻辑测试

在 Linux、WSL 或具备 Bash、C 编译器的环境中，从仓库根目录运行：

```bash
bash tools/test-host.sh
```

测试直接编译 `device/main/apps/voice/app_state.c`、`ui_pixel_math.c`，不需要 ESP-IDF、私有仓库或设备。它覆盖长按/松开、电脑就绪、下键、重复事件、超时与故障分支，不替代实机音频测试。

## 设备代码移植

设备端源码来源于已验证固件的 Voice 应用目录，算法和状态机未为这次拆分改写。应用模块依赖 ESP-IDF 5.5.3、FreeRTOS、LVGL 9.5，以及由宿主实现的硬件和 UI 接口。详细边界见 [PORTING](PORTING.zh-CN.md)。

如想在自己的固件集成，先接入纯状态机与协议，再提供录音、UDP、按键和显示生命周期。不要把缺少的整机文件从私有仓库复制进公开 PR；新增适配实现需独立审查发布范围。
