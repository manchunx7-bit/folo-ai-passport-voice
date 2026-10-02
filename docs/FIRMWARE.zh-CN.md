# 固件安装与恢复（附录）

日常使用请阅读 [随身语音跑通教程](QUICKSTART.zh-CN.md)。本页只供需要本地烧录或恢复的用户查阅。仅适用于匹配的 ESP32-C3 / 8 MB AI Passport，首次更换系统前备份资料。

## 2. 安装烧录工具、找到 COM 端口

1. 从 [Python 官方 Windows 下载页](https://www.python.org/downloads/windows/) 安装 Python 3.11 x64，包含 Python Launcher。
2. 在解压目录打开 PowerShell：文件管理器地址栏输入 `powershell` 并回车。
3. 执行：

```powershell
py -3.11 -m venv .flash-venv
.\.flash-venv\Scripts\python.exe -m pip install esptool==5.4.0
```

4. USB 连接设备，在“设备管理器 → 端口（COM 和 LPT）”查找新增端口。
   下文用 `COM7` 举例，必须替换为你自己的端口。关闭串口监视器和其他烧录软件。

没有端口时，先换数据线/USB 口；不要随机选择电脑上其他设备的端口。
自动进入下载模式失败时，按你的硬件版本官方说明进入 Recovery/下载模式，
不要套用其他 ESP32 开发板的按键组合。官方资料：[FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)。

## 3. 首次烧录

先预览将执行的操作：

```powershell
.\.flash-venv\Scripts\python.exe tools\flash.py --port COM7 --mode first
```

确认型号一致后执行：

```powershell
.\.flash-venv\Scripts\python.exe tools\flash.py --port COM7 --mode first --execute
```

脚本会依次：校验固件哈希 → 检查 ESP32-C3 / 8 MB → 读取完整 8 MB 备份到 `backups/` →
备份成功后才写入 `0x0` 引导程序、`0x8000` 分区表、`0x10000` 应用 → 校验并重启。
中途失败会停止，勿在写入时拔线；如传输不稳定，可添加 `--baud 115200` 重试。

**不要执行 `erase-flash` / `--erase-all`，不要把应用文件写到 `0x0`。**
本包不提供全片备份式“大合并镜像”，避免填充字节连带覆盖个人配置或受保护区域。
保存好 `backups/*.bin`：它含设备身份、Wi-Fi 和个人资料，只能私下保存，不可作为开源固件上传。

### 已安装本包后的升级

```powershell
.\.flash-venv\Scripts\python.exe tools\flash.py --port COM7 --mode upgrade --execute
```

升级模式会先备份，再核对设备现有分区表与本包一致；不一致时拒绝升级，需要按首次安装路径处理。
升级仅写 `0x10000` 应用，不主动清除 Wi-Fi 或个人资料。


## 10. 恢复自己的备份

只对生成该备份的同一台设备执行；不要刷入别人的完整 Flash 备份：

```powershell
.\.flash-venv\Scripts\python.exe -m esptool --chip esp32c3 --port COM7 --baud 460800 write-flash 0x0 backups\你的备份文件.bin
```

这会恢复备份时的整个 8 MB 内容，包括当时的个人配置；备份之后的新资料会被覆盖。
不确定备份来源时不要执行。无法恢复时参考硬件官方 Recovery 文档。
