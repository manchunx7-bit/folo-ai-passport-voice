#!/bin/bash
set -eu
cd -- "$(dirname -- "$0")"
trap 'result=$?; if [ "$result" -ne 0 ]; then echo "安装未完成，请查看上方错误。"; fi; if [ -t 0 ]; then read -r -p "按回车关闭…" _reply || true; fi' EXIT
if [ "$(uname -s)" != Darwin ]; then
  echo "此测试包需要 Apple 芯片 Mac，请勿通过 Rosetta 启动。"
  exit 1
fi
python3 -c 'import platform, sys; assert sys.version_info >= (3, 11), "请安装 Python 3.11 或更新版本"'
if [ ! -x .venv/bin/python3 ]; then
  python3 -m venv .venv
fi
.venv/bin/python3 -m pip install -r requirements.txt
if [ ! -f voice-config.json ]; then
  cp voice-config.example.json voice-config.json
fi
echo "依赖安装完成。请按 README 设置 BlackHole 和微信输入法，再运行 diagnose.command。"
