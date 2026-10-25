#!/bin/bash
set -eu
cd -- "$(dirname -- "$0")"
trap 'result=$?; if [ "$result" -ne 0 ]; then echo "安装未完成，请查看上方错误。"; fi; if [ -t 0 ]; then read -r -p "按回车关闭…" _reply || true; fi' EXIT
if [ "$(uname -s)" != Darwin ]; then
  echo "此测试包需要 macOS。"
  exit 1
fi
translated="$(sysctl -in sysctl.proc_translated 2>/dev/null || true)"
if [ "$translated" = 1 ]; then
  echo "不支持通过 Rosetta 启动，请使用原生 arm64 或 Intel x86_64 Python。"
  exit 1
fi
if [ "$(uname -m)" != arm64 ] && [ "$(uname -m)" != x86_64 ]; then
  echo "需要原生 arm64 或 Intel x86_64 Mac。"
  exit 1
fi
python3 -c 'import platform, sys; assert sys.version_info >= (3, 11) and platform.machine() in ("arm64", "x86_64"), "请安装原生 arm64 或 x86_64 Python 3.11 或更新版本"'
if [ ! -x .venv/bin/python3 ]; then
  python3 -m venv .venv
fi
.venv/bin/python3 -m pip install -r requirements.txt
if [ ! -f voice-config.json ]; then
  cp voice-config.example.json voice-config.json
fi
echo "依赖安装完成。请按 README 设置 BlackHole 和微信输入法，再运行 diagnose.command。"
