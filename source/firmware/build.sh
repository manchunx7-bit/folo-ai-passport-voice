#!/usr/bin/env bash
# 在 WSL 中编译 passport-os。
#
# 用法:
#   bash build.sh          仅编译
#   bash build.sh clean    先清掉 CMake 缓存再全量编译（换 IDF 版本或路径变了才需要）
#
# 注意:ESP-IDF 装在 WSL 的 /opt/esp-idf,Windows 侧没有工具链,所以编译必须在 WSL 里跑。
# 烧录走 Windows 的 COM3,WSL 默认看不到,需要 usbipd 绑定,或直接在 Windows 上用 esptool。
set -u

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
IDF_DIR="${IDF_DIR:-/opt/esp-idf}"

cd "$PROJECT_DIR" || { echo "找不到项目目录 $PROJECT_DIR"; exit 1; }

if [ ! -f "$IDF_DIR/export.sh" ]; then
    echo "找不到 ESP-IDF: $IDF_DIR/export.sh"
    echo "如有其他位置,用 IDF_DIR=/path/to/esp-idf bash build.sh 指定"
    exit 1
fi

echo "==> 载入 ESP-IDF 环境 ($IDF_DIR)"
set +e
. "$IDF_DIR/export.sh" >/dev/null 2>&1
rc=$?
set -e
if [ $rc -ne 0 ]; then
    echo "export.sh 执行失败,请检查 IDF 安装"
    exit 1
fi

if [ "${1:-}" = "clean" ]; then
    echo "==> 清理构建目录"
    rm -rf build
fi

echo "==> 开始编译"
idf.py build
