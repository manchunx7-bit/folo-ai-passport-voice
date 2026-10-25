# 随身语音验证边界

## 2026-10-25 Intel Mac 支持

- 纳入 [社区 PR #2](https://github.com/manchunx7-bit/folo-ai-passport-voice/pull/2) 的 Intel x86_64 支持；安装脚本、客户端和 Agent doctor 共用原生架构检查，拒绝 Rosetta，并检查已有虚拟环境。
- 本地 Windows 运行 doctor 13 项、Mac 客户端和架构门禁 27 项、Windows 15 项测试；测试使用模拟后端与本地回环 UDP，不向桌面注入真实按键或采集声音。公开范围检查和 Mac 源码包构建通过。
- 贡献者 leonezhu 在 PR 中报告 Intel MacBook Pro 2017 / macOS 13.7.8 / Python 3.14 原生 x86_64 上完成诊断、BlackHole 传音与微信输入法右 Shift hold 出字。这是贡献者报告，维护者未在 Mac 上独立复测；Apple 芯片及其他版本仍需真实验收。
- 本地 WSL 启动返回 `E_ACCESSDENIED`，设备 C 主机测试与 Bash 检查未在本机运行；远端 CI 结果以对应提交记录为准。本次未改设备代码或重打包 Windows EXE。

## 2026-10-24 Agent 配置改进（未发布）

本轮新增 Agent 手册、只读 JSON 诊断和测试，Windows 源码原诊断跟随实际配置/命令参数检查；未改变设备业务代码，也未重打包旧发布 EXE。

- 本地 Windows 上运行新增诊断单测 11 项、原 Windows 单测 15 项和 Mac 客户端测试 24 项，通过。Mac 测试使用模拟原生后端及本地 UDP，不是 Apple 芯片实机验收。
- JSON doctor 在缺少 sounddevice、没有个人配置的本地运行时返回 `NEEDS_ATTENTION` 和退出码 1，未误报出字成功。测试覆盖自定义端口、占用、配置错误、缺少虚拟声卡方向、Mac 权限拒绝和不回显坏配置内容。
- 公开文件 allowlist、源码范围/敏感模式检查和 Markdown 文件链接检查通过；新增诊断测试已接入 CI。远端 CI 是否通过以推送后的实际记录为准。
- 未安装驱动、未改变本机音频设置、未刷机、未录音发送；新电脑完整安装、真实 Mac、实际输入法出字仍待用户流程验收。安装/授权后的续接依赖 Agent 维护 `.setup/progress.json`，不是客户端内置恢复命令。

## 历史拆分与设备记录

以下记录对应原拆分版本，没有改变设备业务代码或当时的 Windows 程序。独立项目不是新的整机固件版本。

在已验证的集成固件 `0.2.0-rc.6` 上，Windows EXE 与实机完成 3 轮按住上键、松开、下键测试：12 个系统注入键事件顺序正确，语音键释放 47–93 ms，Enter 16–47 ms。测试拦截键事件，未向用户当前窗口提交文字。这不是输入法识别准确率测试。

Windows 转发器 15 项单测覆盖 hold/tap、结束释放、残留状态对账、重复 Enter、音频失败回退等。设备纯状态机与界面数学通过 host 测试。新的应用目录再次独立运行这些测试；GitHub Actions 记录对应提交结果。

PC EXE 来自 Python 3.11.8、sounddevice 0.5.6、PyInstaller 6.20.0，源码与本项目 Windows 文件一致；EXE 的 `--help`、`--diagnose` 已检查。

**未验收：** 新电脑从零安装、其他输入法/系统版本、真实字错误率、长时运行、用户自定义宿主的整机编译及音质。本仓库不分发整机 BIN，不宣称这里的模块可直接刷机。
